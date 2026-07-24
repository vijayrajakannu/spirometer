/*
 * spirometry.h
 *
 * Self-contained spirometry engine:
 *   - ADC flow acquisition (DMA ring, called from main loop)
 *   - Real-time parameter computation (FEV1, FVC, ratio, PEF, Te, TPEF,
 *     FEF25-75, FEF50, saturation flag, validity)
 *   - LVGL GUI + graph update helpers
 *
 * Usage
 * -----
 *   1. Call spiro_init() once after ui_init().
 *   2. Call spiro_process() inside the main while(1) loop (after lv_timer_handler).
 *   3. Nothing else is needed — spiro_process() drives everything.
 */

#ifndef SPIROMETRY_H
#define SPIROMETRY_H

#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Tuneable constants ─────────────────────────────────────────────────── */

/* ADC / hardware
 *
 * Sensor: Siargo FS1015 MEMS mass flow sensor
 *   Analog output:  0.5 V  =   0 SLPM
 *                   4.5 V  = 100 SLPM  (linear)
 *   Power supply:   5 Vdc  |  Response time: 8 ms max
 *
 * STM32F401CC ADC: 12-bit, Vref = 3.3 V
 *
 * HARDWARE NOTE — voltage divider required:
 *   The sensor output spans 0.5-4.5 V, which exceeds the 3.3 V ADC rail.
 *   Recommended: R1 = 4.7 kOhm (top), R2 = 13 kOhm (bottom)
 *     Vout = 4.5 * 13 / 17.7 = 3.31 V (within 3.3 V rail)
 *
 * With divider scaling 0.5-4.5 V to 0.367-3.3 V:
 *   ADC count at   0 SLPM (0.367 V) = 455
 *   ADC count at 100 SLPM (3.300 V) = 4095
 *
 * Conversion:
 *   flow_SLPM = (adc - 455) * 100 / (4095 - 455)
 *   flow_L/s  = (adc - 455) / 2184.0
 *
 * RAM BUDGET (STM32F401CC = 64 KB total)
 *   200 Hz is 25x the sensor 8 ms response time - no accuracy loss.
 *   ATS standard max maneuver = 6 s; hard stop at 8 s.
 *   BUF_MAX = 200 * 8 + 50 = 1650 samples  (8 s hard stop, see below)
 *   s_raw[]  uint16_t * 1650 = 3.3 KB  (raw ADC; flow reconstructed on use)
 *   s_vol[]  uint16_t * 1650 = 3.3 KB  (integrated volume, stored in mL)
 *   Total ~6.6 KB.  Storing volume as uint16_t mL (rather than float) halves
 *   the volume array from 6.6 KB to 3.3 KB with no clinically relevant loss
 *   (1 mL resolution vs the 0.01 L = 10 mL ISO display step).
 */
#define SPIRO_ADC_FS_HZ        200u      /* samples/s -- 25x sensor response time */
#define SPIRO_ADC_MAX          4095u     /* 12-bit ADC full scale                 */

/* FS1015 zero-flow ADC count (0.5 V after R-divider scaled to 0.367 V) */
#define SPIRO_ZERO_COUNTS      455u

/* ADC counts per L/s: span 3640 counts over 100 SLPM = 1.6667 L/s */
#define SPIRO_COUNTS_PER_LPS   2184.0f

/* Maneuver detection */
#define SPIRO_BLOW_THRESH_LPS  0.05f    /* flow > this starts a maneuver (100 SLPM max -> low thresh ok) */
#define SPIRO_END_THRESH_LPS   0.02f    /* flow < this for END_QUIET_MS -> done  */
#define SPIRO_END_QUIET_MS     300u     /* quiet time before maneuver ends        */
#define SPIRO_MIN_DURATION_MS  500u     /* reject blows shorter than this         */
#define SPIRO_MAX_DURATION_MS  8000u    /* hard stop (ATS: <=6 s typical; 8 s safe limit) */

/* Sample buffer
 * Sized for 200 Hz x 6 s + margin = 1232 samples.
 * Raw ADC stored as uint16_t (2 bytes each) to minimise RAM.
 * Integrated volume stored as float (4 bytes each).
 * Total static BSS: 1232*(2+4) = 7392 bytes (~7.2 KB).             */
#define SPIRO_BUF_MAX_SAMPLES  1650u

/* FVL graph canvas dimensions (must match screens.c fvl_chart geometry) */
#define SPIRO_FVL_W   180
#define SPIRO_FVL_H   100
/* Axis ranges for the FV plot */
#define SPIRO_FVL_XMAX_L    7.0f    /* X axis: volume 0 → 7 L   */
#define SPIRO_FVL_YMAX_LPS  12.0f   /* Y axis: flow   0 → 12 L/s*/

/* VT graph canvas dimensions (must match screens.c vt_chart geometry) */
#define SPIRO_VT_W   180
#define SPIRO_VT_H   100
/* Axis ranges for the VT plot */
#define SPIRO_VT_XMAX_S   8.0f     /* X axis: time   0 → 8 s   */
#define SPIRO_VT_YMAX_L   7.0f     /* Y axis: volume 0 → 7 L   */

/* ── Public types ───────────────────────────────────────────────────────── */

typedef enum {
    SPIRO_STATE_IDLE,        /* waiting for blow                  */
    SPIRO_STATE_ACQUIRING,   /* actively sampling a maneuver      */
    SPIRO_STATE_COMPUTING,   /* post-processing (one tick)        */
    SPIRO_STATE_DISPLAYING,  /* results shown, waiting for reset  */
} spiro_state_t;

typedef struct {
    /* Primary spirometry parameters */
    float fev1;          /* L   */
    float fvc;           /* L   */
    float ratio;         /* %   (FEV1/FVC × 100) */
    float pef;           /* L/s */

    /* Extended parameters */
    float te;            /* s   — total expiratory time               */
    float tpef;          /* s   — time to peak expiratory flow        */
    float fef2575;       /* L/s — forced expiratory flow 25-75 %      */
    float fef50;         /* L/s — forced expiratory flow at 50 % FVC  */

    /* Quality / status flags */
    bool  saturated;     /* ADC rail hit during maneuver              */
    bool  valid;         /* duration and volume checks passed         */
    uint32_t n_samples;  /* total samples captured                    */
    uint32_t duration_ms;

    /* Raw sample and integrated volume arrays for graphing
       (pointers into internal static buffers — valid until next blow) */
    const uint16_t *raw_buf;    /* raw ADC counts, length = n_samples       */
    const uint16_t *vol_buf;    /* integrated volume in MILLILITRES (mL),
                                   length = n_samples. Divide by 1000 for L.
                                   Stored as uint16_t (not float) to halve RAM;
                                   1 mL resolution exceeds the 0.01 L display
                                   step required by ISO 26782 §5.1.            */
} spiro_result_t;

/* ── Public API ─────────────────────────────────────────────────────────── */

/**
 * @brief  Initialise the spirometry engine.
 *         Call once after ui_init().
 */
void spiro_init(void);

/**
 * @brief  Main processing tick — call every loop iteration.
 *         Reads the ADC DMA buffer, drives the state machine, updates the GUI.
 */
void spiro_process(void);

/**
 * @brief  Feed a raw ADC sample into the engine.
 *         Call this from your ADC DMA half/full-complete ISR, or from
 *         spiro_process() if you poll the ADC inside the main loop.
 *         Returns true if the sample was accepted (buffer not full).
 */
bool spiro_push_sample(uint16_t adc_raw);

/**
 * @brief  Force-reset to IDLE (e.g. on button press or screen change).
 */
void spiro_reset(void);

/**
 * @brief  Return a pointer to the most recent result (NULL if no result yet).
 */
const spiro_result_t *spiro_get_result(void);

/**
 * @brief  Current engine state.
 */
spiro_state_t spiro_get_state(void);

#ifdef __cplusplus
}
#endif

#endif /* SPIROMETRY_H */
