/*
 * screens.c  —  SpiroFlow UI, LVGL 9, 240×320 portrait
 *
 * ── APPLIED FIXES ───────────────────────────────────────────────────────────
 *
 * FIX-A  Boot screen tick calls loadScreen(SCREEN_ID_DASHBOARD) once.
 * FIX-B  countdown_start() interval 900 ms → 1000 ms.
 * FIX-C  loadScreen() animation direction based on screen ID order.
 * FIX-D  FVL/VT chart y=24→34 (27px status bar + 7px gap).
 * FIX-E  (superseded by FIX-NAV2 below)
 * FIX-F  Legacy progress bars relocated to y=308.
 * FIX-G  Dashboard recent-test card height 120→136 px.
 * FIX-H  CANCEL button calls spiro_reset().
 * FIX-I  (superseded by FIX-NAV2 below)
 * FIX-J  LVGL heap exhaustion fix; NULL-pointer assertions.
 *
 * FIX-NAV2  *** ROOT-CAUSE FIX FOR "Flow-Vol > tap does nothing" ***
 *
 *   Root cause: make_swipe_footer() used lv_label with LV_OBJ_FLAG_CLICKABLE.
 *   In LVGL 9, lv_obj_set_ext_click_area() is clipped to the parent's content
 *   rect.  The footer container was only 20 px tall; any ext_click_area beyond
 *   those 20 px was silently discarded, leaving a true hit-target of ~14 px.
 *   Additionally, the page-dot row at y=308 overlapped the footer region and
 *   could intercept pointer events.
 *
 *   Fix:
 *     1. Replace lv_label+CLICKABLE with lv_button objects (100×36 px each).
 *        lv_button has a reliable hit rect independent of ext_click_area.
 *     2. Raise footer container: y=288 h=20  →  y=276 h=40.
 *     3. Move page dots: y=308 h=12  →  y=317 h=3.
 *        Dots are now lv_obj rect indicators, NOT lv_label (avoids any
 *        residual CLICKABLE flag from font glyph objects).
 *     4. Adjust results table card h=182→170 (bottom=272 < footer y=276).
 *
 * FIX-WHITE  Results screen text: C_DIM → C_WHITE for all table labels
 *     (parameter names, Predicted, %Pred columns, column headers, quality
 *     card "QUALITY GRADE"/"Start"/"End" labels, res_start/end values).
 *     Nav footer labels: C_NAV → C_WHITE so buttons are clearly readable.
 *
 * FIX-NAV3  Right nav button label: LV_ALIGN_RIGHT_MID → LV_ALIGN_CENTER.
 *     Root cause of "Flow-Vol button invisible / untappable on Results":
 *     LV_ALIGN_RIGHT_MID anchors the label's RIGHT edge to the button's
 *     right edge.  With LV_SIZE_CONTENT width the text content overflows
 *     LEFT outside the button's visual bounds and off-screen on narrow
 *     buttons, making it appear absent.  The hit rect of the button itself
 *     was fine, but users couldn't see it so never tapped it.
 *     Fix: LV_ALIGN_CENTER + LV_TEXT_ALIGN_CENTER keeps text squarely
 *     inside the button.  Both buttons widened 100 → 110 px to accommodate
 *     longer labels ("Flow-Vol →", "Vol-Time →") without clipping.
 *
 * FIX-COLOUR  Palette redesigned for better readability on ILI9341 RGB565:
 *     • C_DIM raised 0x3D5070→0x4A6080 (legible at arm's length).
 *     • C_GREEN desaturated 0x00E5A0→0x00D68A (less neon on cold-WP panel).
 *     • C_CYAN warmed 0x00D4FF→0x22C5EE.
 *     • C_AMBER warmed 0xFFB020→0xF5A623 (more clinical caution).
 *     • C_RED desaturated 0xFF4040→0xE8394A.
 *     • C_BORDER raised 0x1E2A40→0x243550 (card borders now visible).
 *     • C_SURFACE raised 0x111620→0x141B26 (more visible card depth).
 *     • C_NAV added for footer nav labels (distinct from data labels).
 *     • C_GRADE_CARD added for quality/ready card backgrounds.
 *     • Status bar title text: C_DIM→C_WHITE for clear hierarchy.
 *     • Interpretation label: C_DIM→C_AMBER for semantic meaning.
 *     • Status bar gains a 1px separator line at y=26 for depth.
 *
 * Color palette
 *   Background  #0A0D12   surface #141B26   tile #0E1520
 *   Green       #00D68A   cyan    #22C5EE   amber  #F5A623
 *   Blue        #3D74F5   red     #E8394A   dim    #4A6080
 *   Border      #243550   nav     #6A88B0   white  #DDE5F0
 */

#include <string.h>
#include <stdio.h>

#include "screens.h"
#include "images.h"
#include "fonts.h"
#include "actions.h"
#include "vars.h"
#include "styles.h"
#include "ui.h"
#include "spirometry.h"   /* FIX-H: spiro_reset() */

/* Forward declaration — defined in Core/Src/main.c.
 * Avoids pulling in main.h (and the full HAL chain) just for this one symbol.
 * FIX-J: used by the OOM guards in create_screens(). */
extern void Error_Handler(void);

objects_t objects;
lv_obj_t *tick_value_change_obj;

/* ── Palette constants ── REDESIGN v2 ────────────────────────────────────
 *
 * Rationale (all constraints: STM32F401, 240×320, arm-length readability):
 *
 * Backgrounds: slightly warmer/lighter (0x0D1117 → 0x0A0D12 surface raised
 *   to 0x141B26) so the dark field has more visible depth.  The old
 *   C_SURFACE was almost indistinguishable from C_BG in dim lighting.
 *
 * C_GREEN  → 0x00D68A  (teal-green, softer on ILI9341 — the old 0x00E5A0
 *   read neon-green on RGB565 panels because the display clips high-chroma
 *   greens to LED saturation.  This value renders as a clean mid-teal.)
 *
 * C_CYAN   → 0x22C5EE  (slightly warmer, avoids the "too-electric" look of
 *   pure 00D4FF on this panel's cold white point.)
 *
 * C_AMBER  → 0xF5A623  (one stop warmer, reads more clinical/caution than
 *   the old chip-yellow 0xFFB020.)
 *
 * C_BLUE   → 0x3D74F5  (shifted slightly cooler so it contrasts with AMBER
 *   and doesn't bleed into CYAN on RGB565.)
 *
 * C_RED    → 0xE8394A  (desaturated one notch — 0xFF4040 was poster-red;
 *   this reads as a clinical alert red.)
 *
 * C_DIM    → 0x4A6080  (raised from 0x3D5070 — the old value was barely
 *   legible at arm's length under overhead fluorescent light.)
 *
 * C_WHITE  → 0xDDE5F0  (cooler blue-white matches the panel's native WP.)
 *
 * C_NAV    → footer / nav-label colour — dedicated so it reads clearly
 *   without being confused with data labels.  Slightly brighter than C_DIM.
 *
 * C_BORDER_CARD → card borders raised from 0x1E2A40 to 0x243550 so cards
 *   are actually visible against C_BG.
 *
 * ──────────────────────────────────────────────────────────────────────── */
#define C_BG          0x0A0D12u   /* deepest background                     */
#define C_SURFACE     0x141B26u   /* card / status-bar surface              */
#define C_TILE        0x0E1520u   /* graph canvas background                */
#define C_BORDER      0x243550u   /* card outline border (raised visibility) */
#define C_GREEN       0x00D68Au   /* primary value / success / FEV1         */
#define C_CYAN        0x22C5EEu   /* FVC / axis labels / highlighted text   */
#define C_AMBER       0xF5A623u   /* ratio / caution                        */
#define C_BLUE        0x3D74F5u   /* PEF / info                             */
#define C_RED         0xE8394Au   /* error / out-of-range                   */
#define C_DIM         0x4A6080u   /* secondary labels (raised for legibility)*/
#define C_WHITE       0xDDE5F0u   /* primary labels                         */
#define C_NAV         0x6A88B0u   /* footer nav labels — distinct from data */
#define C_GRADE_CARD  0x182030u   /* quality-card bg (slightly distinct)    */

/* ── Forward declarations for helpers used before their definition ─────────
 * gesture_consume_cb and make_inert_container are defined further below
 * (after the screen-loaded callbacks) but are called by make_screen() and
 * make_chart_obj() which appear earlier in the file.  C89/C99 requires a
 * visible declaration before use; without these the compiler sees an
 * implicit int-return declaration for make_inert_container and flags
 * gesture_consume_cb as undeclared. */
static void gesture_consume_cb(lv_event_t *e);
static inline void make_inert_container(lv_obj_t *o);

/* ── Helper: bare screen object ─────────────────────────────────────────── */
static lv_obj_t *make_screen(void)
{
    lv_obj_t *s = lv_obj_create(0);
    lv_obj_set_pos(s, 0, 0);
    lv_obj_set_size(s, 240, 320);
    lv_obj_set_style_bg_color(s, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s, 0, 0);
    lv_obj_set_style_border_width(s, 0, 0);
    lv_obj_set_style_radius(s, 0, 0);
    lv_obj_set_style_shadow_width(s, 0, 0);
    lv_obj_remove_flag(s, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s,  LV_OBJ_FLAG_GESTURE_BUBBLE);
    /* Consume every gesture event that reaches the screen root so LVGL's
     * built-in indev gesture handler cannot trigger screen transitions.
     * Child cards also have GESTURE_BUBBLE cleared (via make_inert_container)
     * so gestures originating on cards are stopped there too; this handler
     * is a belt-and-suspenders backstop for any child that slips through. */
    lv_obj_add_event_cb(s, gesture_consume_cb, LV_EVENT_GESTURE, NULL);
    return s;
}

/* ── Helper: chart canvas ────────────────────────────────────────────────── */
static lv_obj_t *make_chart_obj(lv_obj_t *parent,
                                int32_t x, int32_t y,
                                int32_t w, int32_t h)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    make_inert_container(o);
    lv_obj_set_style_bg_color(o,     lv_color_hex(C_TILE), 0);
    lv_obj_set_style_bg_opa(o,       LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(o, lv_color_hex(C_BORDER), 0);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_radius(o,       0, 0);
    lv_obj_set_style_pad_all(o,      0, 0);
    lv_obj_set_style_shadow_width(o, 0, 0);
    lv_obj_set_style_opa(o, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_opa(o, LV_OPA_TRANSP, LV_PART_SCROLLBAR);
    return o;
}

/* ── Helper: label ───────────────────────────────────────────────────────── */
static lv_obj_t *make_label(lv_obj_t *parent,
                             int32_t x, int32_t y,
                             uint32_t color,
                             const lv_font_t *font,
                             const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_size(l, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_bg_opa(l, 0, 0);
    lv_label_set_text(l, text);
    return l;
}

/* ── Helper: status bar ──────────────────────────────────────────────────── */
static void make_status_bar(lv_obj_t *parent, const char *left, const char *right)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_set_pos(o, 0, 0);
    lv_obj_set_size(o, 240, 26);
    lv_obj_set_style_pad_left(o,   10, 0); lv_obj_set_style_pad_right(o,  10, 0);
    lv_obj_set_style_pad_top(o,     0, 0); lv_obj_set_style_pad_bottom(o,  0, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_shadow_width(o, 0, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(C_SURFACE), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_layout(o, LV_LAYOUT_FLEX, 0);
    lv_obj_set_style_flex_flow(o, LV_FLEX_FLOW_ROW, 0);
    lv_obj_set_style_flex_main_place(o, LV_FLEX_ALIGN_SPACE_BETWEEN, 0);
    lv_obj_set_style_flex_cross_place(o, LV_FLEX_ALIGN_CENTER, 0);
    make_inert_container(o);
    /* Screen title in C_WHITE for clear hierarchy; right label in C_DIM */
    make_label(o, 0, 0, C_WHITE, &lv_font_montserrat_14, left);
    make_label(o, 0, 0, C_DIM,   &lv_font_montserrat_14, right);

    /* 1 px separator line under the status bar — gives depth at zero cost */
    lv_obj_t *sep = lv_obj_create(parent);
    lv_obj_set_pos(sep, 0, 26);
    lv_obj_set_size(sep, 240, 1);
    lv_obj_set_style_bg_color(sep, lv_color_hex(C_BORDER), 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    lv_obj_set_style_shadow_width(sep, 0, 0);
    lv_obj_clear_flag(sep, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
}

/* ── Helper: carousel page dots ─────────────────────────────────────────── */
static void make_page_dots(lv_obj_t *parent, int active, lv_obj_t **out_lbl)
{
    /* FIX-NAV2: dots moved to y=318 — below the 40px footer (276+40=316).
     * Previously at y=308 the dots overlapped the footer's ext_click_area,
     * intercepting touch events intended for the nav buttons. */
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_set_pos(o, 0, 317);
    lv_obj_set_size(o, 240, 3);
    lv_obj_set_style_bg_opa(o, 0, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_shadow_width(o, 0, 0);
    lv_obj_set_style_layout(o, LV_LAYOUT_FLEX, 0);
    lv_obj_set_style_flex_flow(o, LV_FLEX_FLOW_ROW, 0);
    lv_obj_set_style_flex_main_place(o, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_cross_place(o, LV_FLEX_ALIGN_CENTER, 0);
    make_inert_container(o);

    for (int i = 0; i < 3; i++) {
        /* Use solid rect dots instead of label "●" — more reliable rendering
         * and does NOT have a clickable flag by default */
        lv_obj_t *dot = lv_obj_create(o);
        lv_obj_set_size(dot, (i == active) ? 14 : 6, 3);
        lv_obj_set_style_bg_color(dot,
            lv_color_hex(i == active ? C_CYAN : C_DIM), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_set_style_shadow_width(dot, 0, 0);
        lv_obj_set_style_radius(dot, 2, 0);
        lv_obj_set_style_pad_all(dot, 0, 0);
        lv_obj_set_style_margin_left(dot,  3, 0);
        lv_obj_set_style_margin_right(dot, 3, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }
    (void)out_lbl;
}

/* ── Helper: swipe footer — FIX-NAV2 ────────────────────────────────────────
 *
 * ROOT CAUSE of "Flow-Vol > tap does nothing":
 *
 * Using lv_label_create() + LV_OBJ_FLAG_CLICKABLE has a known reliability
 * problem in LVGL 9 when the label is inside a FLEX container.  The flex
 * layout engine recalculates the label's bounding box for layout purposes
 * but the hit-test rect for pointer events can lag behind or be overridden by
 * the parent's scroll/click propagation.  Specifically:
 *
 *   1. The footer container (y=288, h=20) is only 20 px tall.  A clickable
 *      label 14 px tall with 12 px ext_click_area should give 38 px of hit
 *      area — BUT lv_obj_set_ext_click_area() is *clipped* to the parent's
 *      content rect in LVGL 9.  The parent is exactly 20 px tall so any
 *      extension beyond those 20 px is silently discarded, leaving the true
 *      tap target at only 14 px (well below medical-device 44 px minimum).
 *
 *   2. The page-dot row (y=308, h=12) and the footer (y=288, h=20) together
 *      place interactive objects within the bottom 44 px of the screen.  The
 *      LVGL indev propagates the event to the *topmost* clickable child; the
 *      dots row may intercept taps intended for the footer.
 *
 * FIX: Replace lv_label with lv_button (a proper interactive widget) and set
 *   the button's size explicitly to 80×40 px.  lv_button:
 *     • Has a reliable built-in hit rect that does NOT depend on ext_click_area
 *     • Does NOT propagate LV_EVENT_CLICKED to the parent by default
 *     • Is designed to be the target of pointer events
 *   The footer container height is raised to 40 px (y=276) so the buttons fit
 *   without clipping, and the page dots are moved to y=318 (bottom 2 px).
 *
 * COLOUR: Nav labels use C_NAV (0x6A88B0), distinct from data labels.
 *   Pressed state uses C_CYAN for visual feedback with no extra RAM cost.
 * ─────────────────────────────────────────────────────────────────────────── */
/* ── make_swipe_footer ────────────────────────────────────────────────────────
 *
 * FINAL NAV FIX: buttons are placed DIRECTLY on the screen (parent) at
 * absolute positions — no intermediate flex container.
 *
 * Root cause of "have to press above the button":
 *   All previous versions wrapped buttons in a flex container `o` and then
 *   called make_inert_container(o) which cleared LV_OBJ_FLAG_CLICKABLE on `o`.
 *   In LVGL 9.5, lv_obj_create() sets up the hit-test so that a non-clickable
 *   object still participates in child hit resolution — but the FLEX layout
 *   engine recalculates child positions at layout time, and the reported
 *   hit-test coordinates for the children are relative to the container's
 *   content rect AFTER padding is applied.  With pad_top=0, pad_left=8, the
 *   content rect top-left shifts, causing the hit area to be displaced upward
 *   from the visually rendered position.
 *
 *   Removing the container entirely and using lv_obj_set_pos() directly on
 *   the buttons (which are children of the screen root) eliminates the layout
 *   indirection.  The screen root is the LVGL coordinate origin; absolute
 *   pixel positions are exact and never shifted by a container's padding.
 *
 * Layout:
 *   Left button:  x=8,   y=280, w=108, h=36  (left side)
 *   Right button: x=124, y=280, w=108, h=36  (right side)
 *   Both clear of the parameter table bottom at y=272.
 * ─────────────────────────────────────────────────────────────────────────── */
static void make_swipe_footer(lv_obj_t *parent,
                               const char *left_txt,
                               const char *right_txt,
                               lv_event_cb_t left_cb,
                               lv_event_cb_t right_cb)
{
    printf("[footer] left='%s' right='%s' lcb=%p rcb=%p\r\n",
           left_txt  ? left_txt  : "(null)",
           right_txt ? right_txt : "(null)",
           (void*)left_cb, (void*)right_cb);

    /* ── Left button — direct child of screen at absolute position ── */
    if (left_txt && left_cb) {
        lv_obj_t *btn = lv_button_create(parent);
        lv_obj_set_pos(btn, 8, 280);
        lv_obj_set_size(btn, 108, 36);
        lv_obj_set_style_bg_opa(btn,     LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(btn,     LV_OPA_20,     LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(btn,   lv_color_hex(C_CYAN), LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(btn,     4,  LV_PART_MAIN);
        lv_obj_set_style_pad_all(btn,    0,  LV_PART_MAIN);
        lv_obj_add_event_cb(btn, left_cb, LV_EVENT_CLICKED, NULL);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_obj_set_align(lbl, LV_ALIGN_CENTER);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(C_WHITE), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(lbl, lv_color_hex(C_CYAN),  LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_bg_opa(lbl, 0, 0);
        lv_label_set_text(lbl, left_txt);
    }

    /* ── Right button — direct child of screen at absolute position ── */
    if (right_txt && right_cb) {
        lv_obj_t *btn = lv_button_create(parent);
        lv_obj_set_pos(btn, 124, 280);
        lv_obj_set_size(btn, 108, 36);
        lv_obj_set_style_bg_opa(btn,     LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(btn,     LV_OPA_20,     LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(btn,   lv_color_hex(C_CYAN), LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_border_color(btn, lv_color_hex(C_CYAN), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(btn,     4,  LV_PART_MAIN);
        lv_obj_set_style_pad_all(btn,    0,  LV_PART_MAIN);
        lv_obj_add_event_cb(btn, right_cb, LV_EVENT_CLICKED, NULL);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_obj_set_align(lbl, LV_ALIGN_CENTER);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(C_WHITE), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(lbl, lv_color_hex(C_CYAN),  LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_bg_opa(lbl, 0, 0);
        lv_label_set_text(lbl, right_txt);
    }
}

/* ── gesture_consume_cb ──────────────────────────────────────────────────────
 *
 * ROOT-CAUSE FIX for "touching upper half of Results navigates to Flow-Vol
 * without pressing the button".
 *
 * WHY the previous fix (lv_obj_clear_flag on screen root) was not enough:
 *
 *   lv_obj_clear_flag(screen, LV_OBJ_FLAG_GESTURE_BUBBLE) only prevents the
 *   screen itself from forwarding a gesture event FURTHER UP the tree.  It
 *   does NOT prevent CHILD objects — cards, containers created with
 *   lv_obj_create() — from receiving the gesture event themselves.
 *
 *   In LVGL 9, lv_obj_create() sets LV_OBJ_FLAG_GESTURE_BUBBLE by default on
 *   every new object.  A drag on the quality card (y=30, h=68) or the parameter
 *   table card (y=102, h=170) fires LV_EVENT_GESTURE on that card, which then
 *   bubbles UP to its parent (the screen root).  The screen root receives the
 *   event even though its own bubble flag is cleared — because the event
 *   delivery walks UP from child TO parent, and the "bubble" flag controls
 *   whether a given object *sends* the event upward, not whether it *receives*
 *   events from below.
 *
 *   The LVGL indev gesture recogniser, on seeing a left-swipe LV_EVENT_GESTURE
 *   delivered to the screen object, triggers lv_scr_load_anim() internally,
 *   bypassing all button callbacks.
 *
 * THE FIX — two parts applied together:
 *
 *   1. gesture_consume_cb is registered on every carousel screen root for
 *      LV_EVENT_GESTURE.  Its only job is to call lv_event_stop_bubbling(e),
 *      which tells LVGL "this event was handled here — do not propagate it to
 *      the display/indev layer for built-in gesture processing".  This is the
 *      equivalent of event.stopPropagation() in browser JS.
 *
 *   2. make_inert_container() centralises the flag clearing that MUST be
 *      applied to every non-interactive lv_obj_create() container:
 *        • LV_OBJ_FLAG_SCROLLABLE  — already cleared everywhere, kept here
 *        • LV_OBJ_FLAG_CLICKABLE   — default ON; a clickable container can
 *                                    still originate gesture events even if no
 *                                    explicit gesture handler is registered
 *        • LV_OBJ_FLAG_GESTURE_BUBBLE — default ON; clearing this stops the
 *                                    card from forwarding drags upward
 *      Applied to quality card, parameter table card, dashboard cards, all
 *      chart containers, and the footer container.
 *
 * ─────────────────────────────────────────────────────────────────────────── */
static void gesture_consume_cb(lv_event_t *e)
{
    /*
     * lv_event_stop_processing(e) — LVGL 9.5 API (confirmed by compiler).
     * Marks the event as handled and stops all remaining callbacks in the
     * dispatch chain for this object, including LVGL's default screen-switch
     * handler.  This prevents swipe gestures from triggering screen transitions.
     *
     * Note: lv_event_stop_propagation() does NOT exist in LVGL 9.5.
     *       lv_event_stop_bubbling()    stops upward propagation only — wrong.
     *       lv_event_stop_processing()  stops all further handling — correct.
     */
    lv_event_stop_processing(e);
}

/* Convenience wrapper: apply all three "make this container truly inert" flags */
static inline void make_inert_container(lv_obj_t *o)
{
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(o,  LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
}

/* ── FIX-NAV: removed gesture callbacks (LV_EVENT_GESTURE was firing for long
 * presses / noisy drags on the footer, causing spurious multi-hop navigation).
 * Navigation is now button-only via the swipe footer.
 *
 * FIX-GRAPH: screen-loaded callbacks invalidate the chart canvases so the
 * draw-post callbacks fire with current result data when the user navigates
 * to the FVL / VT screen from the Results carousel. */
static void fvl_screen_loaded_cb(lv_event_t *e)
{
    (void)e;
    if (objects.fvl_chart) lv_obj_invalidate(objects.fvl_chart);
}

static void vt_screen_loaded_cb(lv_event_t *e)
{
    (void)e;
    if (objects.vt_chart) lv_obj_invalidate(objects.vt_chart);
}

/* ── FIX-D: axis-label helpers — adjusted for new chart y=32 ────────────── */
static void make_axis_labels_y(lv_obj_t *parent,
                                int32_t x, int32_t y0, int32_t step,
                                lv_obj_t *arr[4],
                                const char *unit)
{
    lv_obj_t *u = lv_label_create(parent);
    lv_obj_set_pos(u, x, y0 - 14);
    lv_obj_set_size(u, 22, 12);
    lv_obj_set_style_text_align(u, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(u, lv_color_hex(C_CYAN), 0);
    lv_obj_set_style_text_font(u, &lv_font_montserrat_10, 0);
    lv_obj_set_style_bg_opa(u, 0, 0);
    lv_label_set_text(u, unit);

    const char *init[4] = { "--", "--", "--", " 0" };
    for (int i = 0; i < 4; i++) {
        lv_obj_t *l = lv_label_create(parent);
        arr[i] = l;
        lv_obj_set_pos(l, x, y0 + i * step);
        lv_obj_set_size(l, 22, 12);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(C_DIM), 0);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_10, 0);
        lv_obj_set_style_bg_opa(l, 0, 0);
        lv_label_set_text(l, init[i]);
    }
}

static void make_axis_labels_x(lv_obj_t *parent,
                                int32_t y, int32_t x0, int32_t step,
                                lv_obj_t *arr[4],
                                const char *unit)
{
    const char *init[4] = { "0", "-", "-", "-" };
    for (int i = 0; i < 4; i++) {
        lv_obj_t *l = lv_label_create(parent);
        arr[i] = l;
        lv_obj_set_pos(l, x0 + i * step, y);
        lv_obj_set_size(l, 28, 12);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(C_DIM), 0);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_10, 0);
        lv_obj_set_style_bg_opa(l, 0, 0);
        lv_label_set_text(l, init[i]);
    }
    lv_obj_t *u = lv_label_create(parent);
    lv_obj_set_pos(u, 0, y + 12);
    lv_obj_set_size(u, 240, 12);
    lv_obj_set_style_text_align(u, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(u, lv_color_hex(C_CYAN), 0);
    lv_obj_set_style_text_font(u, &lv_font_montserrat_10, 0);
    lv_obj_set_style_bg_opa(u, 0, 0);
    lv_label_set_text(u, unit);
}

/* ══════════════════════════════════════════════════════════════════════════
 *  SCREEN 1 — BOOT / SELF-TEST
 * ══════════════════════════════════════════════════════════════════════════ */
void create_screen_boot(void)
{
    lv_obj_t *s = make_screen();
    objects.boot = s;

    lv_obj_t *logo = lv_label_create(s);
    lv_obj_set_pos(logo, 0, 60);
    lv_obj_set_size(logo, 240, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(logo, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(logo, lv_color_hex(C_GREEN), 0);
    lv_obj_set_style_text_font(logo, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_opa(logo, 0, 0);
    lv_label_set_text(logo, "SPIROFLOW");

    lv_obj_t *sub = lv_label_create(s);
    lv_obj_set_pos(sub, 0, 90);
    lv_obj_set_size(sub, 240, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(sub, lv_color_hex(C_DIM), 0);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_opa(sub, 0, 0);
    lv_label_set_text(sub, "Handheld Spirometer");

    lv_obj_t *fw = lv_label_create(s);
    objects.boot_fw_lbl = fw;
    lv_obj_set_pos(fw, 0, 108);
    lv_obj_set_size(fw, 240, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(fw, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(fw, lv_color_hex(C_DIM), 0);
    lv_obj_set_style_text_font(fw, &lv_font_montserrat_10, 0);
    lv_obj_set_style_bg_opa(fw, 0, 0);
    lv_label_set_text(fw, "fw v1.0.0");

    lv_obj_t *bar = lv_bar_create(s);
    objects.boot_bar = bar;
    lv_obj_set_pos(bar, 20, 250);
    lv_obj_set_size(bar, 200, 6);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, lv_color_hex(C_TILE), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(C_GREEN), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 3, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 3, LV_PART_INDICATOR);
    lv_obj_set_style_shadow_width(bar, 0, 0);

    lv_obj_t *sl = lv_label_create(s);
    objects.boot_status_lbl = sl;
    lv_obj_set_pos(sl, 0, 262);
    lv_obj_set_size(sl, 240, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(sl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(sl, lv_color_hex(C_DIM), 0);
    lv_obj_set_style_text_font(sl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_opa(sl, 0, 0);
    lv_label_set_text(sl, "Starting...");

    tick_screen_boot();
}

/* ══════════════════════════════════════════════════════════════════════════
 *  SCREEN 2 — DASHBOARD
 * ══════════════════════════════════════════════════════════════════════════ */
void create_screen_dashboard(void)
{
    lv_obj_t *s = make_screen();
    objects.scr_home = s;
    objects.history  = s;
    objects.patient  = s;
    objects.settings = s;

    /* Status bar */
    {
        lv_obj_t *o = lv_obj_create(s);
        objects.obj0 = o;
        lv_obj_set_pos(o, 0, 0);
        lv_obj_set_size(o, 240, 24);
        lv_obj_set_style_pad_left(o,   10, 0); lv_obj_set_style_pad_right(o,  10, 0);
        lv_obj_set_style_pad_top(o,     0, 0); lv_obj_set_style_pad_bottom(o,  0, 0);
        lv_obj_set_style_border_width(o, 0, 0);
        lv_obj_set_style_radius(o, 0, 0);
        lv_obj_set_style_shadow_width(o, 0, 0);
        lv_obj_set_style_bg_color(o, lv_color_hex(C_SURFACE), 0);
        lv_obj_set_style_layout(o, LV_LAYOUT_FLEX, 0);
        lv_obj_set_style_flex_flow(o, LV_FLEX_FLOW_ROW, 0);
        lv_obj_set_style_flex_main_place(o, LV_FLEX_ALIGN_SPACE_BETWEEN, 0);
        lv_obj_set_style_flex_cross_place(o, LV_FLEX_ALIGN_CENTER, 0);
        make_inert_container(o);

        lv_obj_t *tl = lv_label_create(o);
        objects.dash_time_lbl = tl;
        lv_obj_set_size(tl, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_text_color(tl, lv_color_hex(C_WHITE), 0);
        lv_obj_set_style_text_font(tl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_bg_opa(tl, 0, 0);
        lv_label_set_text(tl, "SpiroFlow");
    }

    /* READY card */
    {
        lv_obj_t *card = lv_obj_create(s);
        lv_obj_set_pos(card, 12, 34);
        lv_obj_set_size(card, 216, 70);
        lv_obj_set_style_bg_color(card, lv_color_hex(C_GRADE_CARD), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(card, lv_color_hex(C_GREEN), 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_radius(card, 8, 0);
        lv_obj_set_style_shadow_width(card, 0, 0);
        lv_obj_set_style_pad_all(card, 0, 0);
        make_inert_container(card);

        lv_obj_t *rl = lv_label_create(card);
        lv_obj_set_pos(rl, 0, 10);
        lv_obj_set_size(rl, 216, LV_SIZE_CONTENT);
        lv_obj_set_style_text_align(rl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(rl, lv_color_hex(C_GREEN), 0);
        lv_obj_set_style_text_font(rl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_bg_opa(rl, 0, 0);
        lv_label_set_text(rl, "READY");

        lv_obj_t *il = lv_label_create(card);
        lv_obj_set_pos(il, 0, 44);
        lv_obj_set_size(il, 216, LV_SIZE_CONTENT);
        lv_obj_set_style_text_align(il, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(il, lv_color_hex(C_DIM), 0);
        lv_obj_set_style_text_font(il, &lv_font_montserrat_14, 0);
        lv_obj_set_style_bg_opa(il, 0, 0);
        lv_label_set_text(il, "Place lips on mouthpiece");
    }

    /* START TEST button */
    {
        lv_obj_t *btn = lv_button_create(s);
        objects.dash_start_btn = btn;
        lv_obj_set_pos(btn, 20, 114);
        lv_obj_set_size(btn, 200, 56);
        lv_obj_set_style_bg_color(btn, lv_color_hex(C_GREEN), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x00B880), LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(btn, 10, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);
        lv_obj_add_event_cb(btn, action_start_test, LV_EVENT_CLICKED, NULL);

        lv_obj_t *bl = lv_label_create(btn);
        lv_obj_set_align(bl, LV_ALIGN_CENTER);
        lv_obj_set_style_text_color(bl, lv_color_hex(0x0A1A0A), 0);
        lv_obj_set_style_text_font(bl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_bg_opa(bl, 0, 0);
        lv_label_set_text(bl, LV_SYMBOL_PLAY "  START TEST");
    }

    /* Recent test card — FIX-G: height 120→136 px */
    {
        lv_obj_t *card = lv_obj_create(s);
        lv_obj_set_pos(card, 12, 182);
        lv_obj_set_size(card, 216, 136);  /* FIX-G */
        lv_obj_set_style_bg_color(card, lv_color_hex(C_SURFACE), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(card, lv_color_hex(C_BORDER), 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_radius(card, 8, 0);
        lv_obj_set_style_shadow_width(card, 0, 0);
        lv_obj_set_style_pad_all(card, 10, 0);
        make_inert_container(card);

        make_label(card,  0,  0, C_DIM,   &lv_font_montserrat_14, "Last Test");
        make_label(card,  0, 20, C_DIM,   &lv_font_montserrat_14, "FEV1");
        lv_obj_t *f1 = make_label(card, 50, 20, C_GREEN, &lv_font_montserrat_14, "--");
        objects.dash_last_fev1 = f1;
        make_label(card,  0, 40, C_DIM,   &lv_font_montserrat_14, "FVC ");
        lv_obj_t *fv = make_label(card, 50, 40, C_CYAN,  &lv_font_montserrat_14, "--");
        objects.dash_last_fvc = fv;
        lv_obj_t *grd = make_label(card, 0, 64, C_GREEN, &lv_font_montserrat_14, "--");
        objects.dash_last_grade = grd;
        lv_obj_t *dt  = make_label(card, 0, 88, C_DIM,   &lv_font_montserrat_10, "--");
        objects.dash_last_date = dt;
    }

    tick_screen_dashboard();
}

/* ══════════════════════════════════════════════════════════════════════════
 *  SCREEN 3 — RESULTS
 *
 *  FIX-FONT: upgraded results table from montserrat_10 → montserrat_14.
 *  FIX-ROWS: removed FEF25/FEF50/FEF75/FEF25-75 rows (saves ~1 400 B heap).
 *            Only FVC, FEV1, FEV6, FEV1/FVC, PEF are shown — the five
 *            clinically primary parameters.
 *  Column layout (card inner = 212 px):
 *    Parameter name :   x=0,  w=74  (right edge 74)
 *    Actual value   :  x=74,  w=54  right-aligned (right edge 128)
 *    Predicted value: x=130,  w=42  right-aligned (right edge 172)
 *    % Predicted    : x=174,  w=38  right-aligned (right edge 212)
 *  Row spacing: 26 px (comfortable for 14-pt glyphs ≈18 px cap-height).
 * ══════════════════════════════════════════════════════════════════════════ */
static void results_table_row(lv_obj_t *parent,
                               int32_t y,
                               const char *param_name,
                               uint32_t val_color,
                               lv_obj_t **act_out,
                               lv_obj_t **pred_out,
                               lv_obj_t **pct_out)
{
    /* Parameter label — font_14 for legibility; C_WHITE for readability */
    make_label(parent, 0, y, C_WHITE, &lv_font_montserrat_14, param_name);

    /* Actual value */
    lv_obj_t *a = lv_label_create(parent);
    lv_obj_set_pos(a, 74, y);
    lv_obj_set_size(a, 54, 18);
    lv_obj_set_style_text_align(a, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(a, lv_color_hex(val_color), 0);
    lv_obj_set_style_text_font(a, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_opa(a, 0, 0);
    lv_label_set_text(a, "--");
    if (act_out) *act_out = a;

    /* Predicted value (optional) */
    if (pred_out) {
        lv_obj_t *p = lv_label_create(parent);
        lv_obj_set_pos(p, 130, y);
        lv_obj_set_size(p, 42, 18);
        lv_obj_set_style_text_align(p, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_style_text_color(p, lv_color_hex(C_WHITE), 0);
        lv_obj_set_style_text_font(p, &lv_font_montserrat_14, 0);
        lv_obj_set_style_bg_opa(p, 0, 0);
        lv_label_set_text(p, "--");
        *pred_out = p;
    }

    /* % Predicted (optional) */
    if (pct_out) {
        lv_obj_t *pp = lv_label_create(parent);
        lv_obj_set_pos(pp, 174, y);
        lv_obj_set_size(pp, 38, 18);
        lv_obj_set_style_text_align(pp, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_style_text_color(pp, lv_color_hex(C_WHITE), 0);
        lv_obj_set_style_text_font(pp, &lv_font_montserrat_14, 0);
        lv_obj_set_style_bg_opa(pp, 0, 0);
        lv_label_set_text(pp, "");
        *pct_out = pp;
    }
}

void create_screen_results(void)
{
    lv_obj_t *s = make_screen();
    objects.results = s;

    make_status_bar(s, "Results", "");

    /* Quality card — FIX-COLOUR: C_GRADE_CARD bg for subtle depth */
    {
        lv_obj_t *card = lv_obj_create(s);
        lv_obj_set_pos(card, 8, 30);
        lv_obj_set_size(card, 224, 68);
        lv_obj_set_style_bg_color(card, lv_color_hex(C_GRADE_CARD), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(card, lv_color_hex(C_GREEN), 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_radius(card, 8, 0);
        lv_obj_set_style_shadow_width(card, 0, 0);
        lv_obj_set_style_pad_all(card, 8, 0);
        make_inert_container(card);

        make_label(card, 0, 0, C_WHITE, &lv_font_montserrat_10, "QUALITY GRADE");
        lv_obj_t *gl = lv_label_create(card);
        objects.res_grade_lbl = gl;
        lv_obj_set_pos(gl, 90, 0);
        lv_obj_set_size(gl, 30, LV_SIZE_CONTENT);
        lv_obj_set_style_text_align(gl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(gl, lv_color_hex(C_GREEN), 0);
        lv_obj_set_style_text_font(gl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_bg_opa(gl, 0, 0);
        lv_label_set_text(gl, "-");

        make_label(card, 0, 30, C_WHITE, &lv_font_montserrat_10, "Start");
        lv_obj_t *stl = lv_label_create(card);
        objects.res_start_lbl = stl;
        lv_obj_set_pos(stl, 36, 28);
        lv_obj_set_size(stl, 20, 14);
        lv_obj_set_style_text_color(stl, lv_color_hex(C_WHITE), 0);
        lv_obj_set_style_text_font(stl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_bg_opa(stl, 0, 0);
        lv_label_set_text(stl, "-");

        make_label(card, 68, 30, C_WHITE, &lv_font_montserrat_10, "End");
        lv_obj_t *etl = lv_label_create(card);
        objects.res_end_lbl = etl;
        lv_obj_set_pos(etl, 98, 28);
        lv_obj_set_size(etl, 20, 14);
        lv_obj_set_style_text_color(etl, lv_color_hex(C_WHITE), 0);
        lv_obj_set_style_text_font(etl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_bg_opa(etl, 0, 0);
        lv_label_set_text(etl, "-");

        lv_obj_t *il = lv_label_create(card);
        objects.res_interp_lbl = il;
        lv_obj_set_pos(il, 130, 28);
        lv_obj_set_size(il, 78, LV_SIZE_CONTENT);
        lv_obj_set_style_text_align(il, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_style_text_color(il, lv_color_hex(C_AMBER), 0);
        lv_obj_set_style_text_font(il, &lv_font_montserrat_10, 0);
        lv_obj_set_style_bg_opa(il, 0, 0);
        lv_label_set_text(il, "--");
    }

    /* ── Parameter table — 5 primary parameters, font_14 values ──────────
     * FIX-FONT: montserrat_10 → montserrat_14 for legibility.
     * FIX-ROWS: FEF25/FEF50/FEF75/FEF25-75 removed (saves ~1 400 B heap).
     * FIX-NAV2: card height reduced 182→170 (bottom=272) so it clears the
     *   new footer at y=276 with a 4 px gap.  Row pitch 26→24 px to fit.
     * Column layout (card inner = 212 px after 6 px L+R padding):
     *   Param name  x=0   w=74
     *   Actual      x=74  w=54  right-aligned
     *   Predicted  x=130  w=42  right-aligned
     *   % Pred     x=174  w=38  right-aligned
     * ─────────────────────────────────────────────────────────────────── */
    {
        lv_obj_t *card = lv_obj_create(s);
        lv_obj_set_pos(card, 8, 102);
        lv_obj_set_size(card, 224, 170);
        lv_obj_set_style_bg_color(card, lv_color_hex(C_SURFACE), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(card, lv_color_hex(C_BORDER), 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_radius(card, 6, 0);
        lv_obj_set_style_shadow_width(card, 0, 0);
        lv_obj_set_style_pad_all(card, 6, 0);
        make_inert_container(card);

        /* Column header labels (font_10, white for readability) */
        make_label(card,   0, 0, C_WHITE, &lv_font_montserrat_10, "Parameter");
        make_label(card,  74, 0, C_WHITE, &lv_font_montserrat_10, "Actual");
        make_label(card, 130, 0, C_WHITE, &lv_font_montserrat_10, "Pred");
        make_label(card, 174, 0, C_WHITE, &lv_font_montserrat_10, "%Pred");

        /* Separator under header */
        lv_obj_t *hr = lv_obj_create(card);
        lv_obj_set_pos(hr, 0, 14);
        lv_obj_set_size(hr, 212, 1);
        lv_obj_set_style_bg_color(hr, lv_color_hex(C_BORDER), 0);
        lv_obj_set_style_bg_opa(hr, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(hr, 0, 0);
        lv_obj_set_style_shadow_width(hr, 0, 0);
        make_inert_container(hr);

        /* Five primary parameters at 30 px pitch (was 26, now 30 to fill h=170) */
        int32_t y = 18;
        results_table_row(card, y, "FVC",
                          C_CYAN,  &objects.res_fvc_act,   &objects.res_fvc_pred,   &objects.res_fvc_pct);   y += 30;
        results_table_row(card, y, "FEV1",
                          C_GREEN, &objects.res_fev1_act,  &objects.res_fev1_pred,  &objects.res_fev1_pct);  y += 30;
        results_table_row(card, y, "FEV6",
                          C_GREEN, &objects.res_fev6_act,  &objects.res_fev6_pred,  &objects.res_fev6_pct);  y += 30;
        results_table_row(card, y, "FEV1/FVC",
                          C_AMBER, &objects.res_ratio_act, &objects.res_ratio_pred, &objects.res_ratio_pct); y += 30;
        results_table_row(card, y, "PEF",
                          C_BLUE,  &objects.res_pef_act,   &objects.res_pef_pred,   &objects.res_pef_pct);

        /* Legacy aliases for spirometry.c write paths */
        objects.fev1_val = objects.res_fev1_act;
        objects.obj4     = objects.res_fvc_act;
        objects.obj6     = objects.res_ratio_act;
        objects.obj8     = objects.res_pef_act;
    }

    /* FIX-ROWS: FEF widget pointers set to NULL — spirometry.c null-guards
     * skip all writes to them silently.                                    */
    objects.res_fef25_act   = NULL;
    objects.res_fef50_act   = NULL;
    objects.res_fef75_act   = NULL;
    objects.res_fef2575_act = NULL;
    objects.fef2575_val     = NULL;
    objects.fef50_val       = NULL;

    /* FIX-J: legacy progress bars + labels — always NULL */
    objects.obj2          = NULL;
    objects.obj3          = NULL;
    objects.obj5          = NULL;
    objects.obj7          = NULL;
    objects.obj9          = NULL;
    objects.te_val        = NULL;
    objects.tpef_val      = NULL;
    objects.sat_label     = NULL;
    objects.validity_label = NULL;

    /* Footer + page dots.
     * Left slot = Home (back to dashboard) so the results carousel is not a
     * dead-end; right slot advances to the Flow-Volume page. */
    make_swipe_footer(s, LV_SYMBOL_HOME " Home", "Flow-Vol " LV_SYMBOL_RIGHT,
                      action_go_to_dashboard, action_go_to_fvl);
    make_page_dots(s, 0, &objects.res_page_lbl);

    /* FIX-NAV: gesture callback removed — footer buttons handle navigation */

    tick_screen_results();
}

/* ══════════════════════════════════════════════════════════════════════════
 *  SCREEN 6 — FLOW-VOLUME LOOP
 *  FIX-D: chart moved from y=24 to y=32; y-axis unit label adjusted.
 * ══════════════════════════════════════════════════════════════════════════ */
void create_screen_fvl(void)
{
    lv_obj_t *s = make_screen();
    objects.fvl_screen = s;

    make_status_bar(s, "Flow-Volume", "");

    /* Y-axis labels: start at y=36 to align with chart top at y=34 */
    /* y0=28, step=75 → 4 ticks span the full 224px canvas (y 34→258):
     * top tick at y_max, bottom tick at 0 aligns with the data baseline. */
    make_axis_labels_y(s, 0, 28, 75, objects.fvl_ylabel, "L/s");

    /* Chart at y=34 (27px status bar + 7px gap) */
    objects.fvl_chart = make_chart_obj(s, 24, 34, 210, 224);

    {
        lv_obj_t *l = lv_label_create(s);
        objects.fvl_wait_lbl = l;
        lv_obj_set_pos(l, 24, 142);
        lv_obj_set_size(l, 210, 14);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(C_BORDER), 0);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
        lv_obj_set_style_bg_opa(l, 0, 0);
        lv_label_set_text(l, "Blow to see graph");
    }

    /* X-axis: placed at y=262 (34 + 224 + 4) */
    /* x0=10, step=70 → tick centres at data x = 24,94,164,234 (chart x 24→234) */
    make_axis_labels_x(s, 262, 10, 70, objects.fvl_xlabel, "Volume (L)");

    make_swipe_footer(s, LV_SYMBOL_LEFT " Results", "Vol-Time " LV_SYMBOL_RIGHT,
                      action_go_to_results, action_go_to_vt);
    make_page_dots(s, 1, &objects.fvl_page_lbl);

    /* FIX-NAV: gesture removed; FIX-GRAPH: re-invalidate chart on screen load */
    lv_obj_add_event_cb(s, fvl_screen_loaded_cb, LV_EVENT_SCREEN_LOADED, NULL);

    tick_screen_fvl();
}

/* ══════════════════════════════════════════════════════════════════════════
 *  SCREEN 7 — VOLUME-TIME GRAPH
 *  FIX-D: chart moved from y=24 to y=32.
 * ══════════════════════════════════════════════════════════════════════════ */
void create_screen_vt(void)
{
    lv_obj_t *s = make_screen();
    objects.vt_screen = s;

    make_status_bar(s, "Volume-Time", "");

    /* y0=28, step=75 → 4 ticks span the full 224px canvas (see fvl screen) */
    make_axis_labels_y(s, 0, 28, 75, objects.vt_ylabel, "L");

    /* Chart at y=34 */
    objects.vt_chart = make_chart_obj(s, 24, 34, 210, 224);

    {
        lv_obj_t *l = lv_label_create(s);
        objects.vt_wait_lbl = l;
        lv_obj_set_pos(l, 24, 142);
        lv_obj_set_size(l, 210, 14);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(C_BORDER), 0);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
        lv_obj_set_style_bg_opa(l, 0, 0);
        lv_label_set_text(l, "Blow to see graph");
    }

    /* x0=10, step=70 → tick centres at data x = 24,94,164,234 (see fvl screen) */
    make_axis_labels_x(s, 262, 10, 70, objects.vt_xlabel, "Time (s)");

    make_swipe_footer(s, LV_SYMBOL_LEFT " Flow-Volume", "Results " LV_SYMBOL_RIGHT,
                      action_go_to_fvl, action_go_to_results);
    make_page_dots(s, 2, &objects.vt_page_lbl);

    /* FIX-NAV: gesture removed; FIX-GRAPH: re-invalidate chart on screen load */
    lv_obj_add_event_cb(s, vt_screen_loaded_cb, LV_EVENT_SCREEN_LOADED, NULL);

    tick_screen_vt();
}

/* ── Tick functions ──────────────────────────────────────────────────────── */

/* FIX-A: boot tick now transitions to dashboard when bar reaches 100.
 * FIX-BOOT5: rate-limited to 100 ms per step (2 units × 50 steps = 5 s). */
static bool     s_boot_done    = false;
static uint32_t s_boot_last_ms = 0;
void tick_screen_boot(void)
{
    if (!objects.boot_bar) return;
    if (s_boot_done) return;

    /* Throttle updates to one per 100 ms → 50 updates × 100 ms = 5 s */
    uint32_t now = lv_tick_get();
    if (now - s_boot_last_ms < 100u) return;
    s_boot_last_ms = now;

    int32_t v = lv_bar_get_value(objects.boot_bar);
    if (v < 100) {
        lv_bar_set_value(objects.boot_bar, v + 2, LV_ANIM_OFF);
        if (v >= 98 && objects.boot_status_lbl)
            lv_label_set_text(objects.boot_status_lbl, "Ready");
    } else {
        s_boot_done = true;
        loadScreen(SCREEN_ID_DASHBOARD);  /* FIX-A */
    }
}

void tick_screen_dashboard(void) {}
void tick_screen_results(void)   {}
void tick_screen_fvl(void)       {}   /* draw is triggered by lv_obj_invalidate in on_screen_enter */
void tick_screen_vt(void)        {}   /* same — no per-tick invalidate needed */

/* ── Dispatch table ─────────────────────────────────────────────────────── */
typedef void (*tick_fn_t)(void);
static tick_fn_t tick_fns[] = {
    tick_screen_boot,
    tick_screen_dashboard,
    tick_screen_results,
    tick_screen_fvl,
    tick_screen_vt,
};

void tick_screen(int i)                      { if (i >= 0 && i < 5) tick_fns[i](); }
void tick_screen_by_id(enum ScreensEnum id)  { tick_screen(id - 1); }

/* ── Font table ─────────────────────────────────────────────────────────── */
ext_font_desc_t fonts[] = {
#if LV_FONT_MONTSERRAT_14
    { "MONTSERRAT_14", &lv_font_montserrat_14 },
#endif
};
uint32_t active_theme_index = 0;

/* ── create_screens ─────────────────────────────────────────────────────── */
void create_screens(void)
{
    lv_display_t *disp = lv_display_get_default();
    lv_theme_t *theme  = lv_theme_default_init(disp,
        lv_palette_main(LV_PALETTE_BLUE),
        lv_palette_main(LV_PALETTE_RED),
        false, LV_FONT_DEFAULT);
    lv_display_set_theme(disp, theme);

    s_boot_done    = false;  /* FIX-A: reset on every full UI rebuild */
    s_boot_last_ms = 0;      /* FIX-BOOT5: reset rate-limiter too    */

    /* Per-screen trace + heap snapshot.  The [mem] line AFTER a screen shows
     * how much heap remains; if it shrinks to near zero the build is RAM-bound
     * and the next screen's lv_obj_create() will fail.
     *
     * FIX-J: NULL-pointer checks after each create call.  lv_obj_create()
     * returns NULL on OOM; the subsequent style writes then hard-fault with
     * no diagnostics.  Checking here converts a silent crash into a logged
     * message + halt via Error_Handler(), making the failure self-documenting.
     * If any check fires, increase LV_MEM_SIZE in lv_conf.h (max safe value
     * on the STM32F401CCU6 is 40 KB — see FIX-J note in file header). */
    lv_mem_monitor_t mon;

    printf("[screens] boot...\r\n");
    create_screen_boot();
    lv_mem_monitor(&mon);
    printf("[mem] boot      free=%u big=%u used=%u%%\r\n",
           (unsigned)mon.free_size, (unsigned)mon.free_biggest_size, (unsigned)mon.used_pct);
    if (!objects.boot) { printf("[FATAL] OOM after boot screen — increase LV_MEM_SIZE\r\n"); Error_Handler(); }

    printf("[screens] dashboard...\r\n");
    create_screen_dashboard();
    lv_mem_monitor(&mon);
    printf("[mem] dashboard free=%u big=%u used=%u%%\r\n",
           (unsigned)mon.free_size, (unsigned)mon.free_biggest_size, (unsigned)mon.used_pct);
    if (!objects.scr_home) { printf("[FATAL] OOM after dashboard screen — increase LV_MEM_SIZE\r\n"); Error_Handler(); }

    printf("[screens] results...\r\n");
    create_screen_results();
    lv_mem_monitor(&mon);
    printf("[mem] results   free=%u big=%u used=%u%%\r\n",
           (unsigned)mon.free_size, (unsigned)mon.free_biggest_size, (unsigned)mon.used_pct);
    if (!objects.results) { printf("[FATAL] OOM after results screen — increase LV_MEM_SIZE\r\n"); Error_Handler(); }

    printf("[screens] fvl...\r\n");
    create_screen_fvl();
    lv_mem_monitor(&mon);
    printf("[mem] fvl       free=%u big=%u used=%u%%\r\n",
           (unsigned)mon.free_size, (unsigned)mon.free_biggest_size, (unsigned)mon.used_pct);
    if (!objects.fvl_screen) { printf("[FATAL] OOM after fvl screen — increase LV_MEM_SIZE\r\n"); Error_Handler(); }

    printf("[screens] vt...\r\n");
    create_screen_vt();
    lv_mem_monitor(&mon);
    printf("[mem] vt        free=%u big=%u used=%u%%\r\n",
           (unsigned)mon.free_size, (unsigned)mon.free_biggest_size, (unsigned)mon.used_pct);
    if (!objects.vt_screen) { printf("[FATAL] OOM after vt screen — increase LV_MEM_SIZE\r\n"); Error_Handler(); }

    printf("[screens] all created\r\n");
}

/* ── Live test helpers ─────────────────────────────────────────────────────
 * The live/countdown screens were removed; these remain because
 * spirometry.c calls them during acquisition.  Their target objects are
 * never created (NULL), so every body below is a guarded no-op. */
void live_update_flow(float flow_lps, float vol_l, uint32_t elapsed_ms)
{
    char buf[12];
    if (objects.live_flow_lbl) {
        int v = (int)(flow_lps * 100.0f + 0.5f);
        if (v < 0) v = 0;
        snprintf(buf, sizeof(buf), "%d.%02d", v / 100, v % 100);
        lv_label_set_text(objects.live_flow_lbl, buf);
    }
    if (objects.live_vol_lbl) {
        int v = (int)(vol_l * 100.0f + 0.5f);
        if (v < 0) v = 0;
        snprintf(buf, sizeof(buf), "%d.%02d", v / 100, v % 100);
        lv_label_set_text(objects.live_vol_lbl, buf);
    }
    if (objects.live_time_lbl) {
        snprintf(buf, sizeof(buf), "%lus", (unsigned long)(elapsed_ms / 1000));
        lv_label_set_text(objects.live_time_lbl, buf);
    }
}

void live_set_coaching(const char *msg)
{
    if (objects.live_coaching_lbl)
        lv_label_set_text(objects.live_coaching_lbl, msg);
}

void live_push_sample(float flow_lps)
{
    (void)flow_lps;
    if (objects.live_chart)
        lv_obj_invalidate(objects.live_chart);
}
