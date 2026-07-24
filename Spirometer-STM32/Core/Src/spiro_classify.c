/*
 * spiro_classify.c
 *
 * On-device FEV1/FVC pattern classification.
 *
 * Coefficients derived from NHANES 2007-2012 spirometry dataset
 * (Zavorsky, 2024) using simple/segmented linear regression,
 * matching the paper's SLR approach.
 *
 * LLN = predicted - 1.645 * residual_SD  (5th percentile boundary)
 *
 * Patterns classified (ATS 2022):
 *   NORMAL      — FEV1 >= LLN, FVC >= LLN, ratio >= LLN
 *   OBSTRUCTION — ratio < LLN
 *   RESTRICTIVE — FVC < LLN, ratio >= LLN
 *   MIXED       — FVC < LLN, ratio < LLN
 *   PRISm       — ratio >= LLN, FEV1 < LLN, FVC >= LLN
 */

#include "spiro_classify.h"
#include <stdbool.h>

/* ── Coefficients from NHANES 2007-2012 linear regression ───────────────
 *   predicted = intercept + age * b_age + height_cm * b_ht
 *   LLN       = predicted - 1.645 * sd
 * ────────────────────────────────────────────────────────────────────── */
typedef struct { float intercept, b_age, b_ht, sd; } Coef;

/*                        intercept    b_age      b_ht       SD      */
static const Coef ratio_m = {  0.880645f, -0.002398f,  0.000005f, 0.071070f };
static const Coef ratio_f = {  0.953316f, -0.002123f, -0.000380f, 0.063739f };

static const Coef fev1_m  = { -5.7619f,  -0.0210f,    0.0586f,   0.5866f   };
static const Coef fev1_f  = { -4.2588f,  -0.0169f,    0.0471f,   0.4348f   };

static const Coef fvc_m   = { -7.2474f,  -0.0144f,    0.0712f,   0.6808f   };
static const Coef fvc_f   = { -5.4497f,  -0.0132f,    0.0575f,   0.5038f   };

static float predict(const Coef *c, float age, float height)
{
    return c->intercept + c->b_age * age + c->b_ht * height;
}

static float lln(const Coef *c, float age, float height)
{
    return predict(c, age, height) - 1.645f * c->sd;
}

SpiroResult spiro_classify(int sex, float age, float height,
                           float fev1, float fvc)
{
    const Coef *cr  = (sex == 0) ? &ratio_m : &ratio_f;
    const Coef *cf1 = (sex == 0) ? &fev1_m  : &fev1_f;
    const Coef *cfc = (sex == 0) ? &fvc_m   : &fvc_f;

    float ratio = (fvc > 0.01f) ? (fev1 / fvc) : 0.0f;

    float pred_ratio = predict(cr,  age, height);
    float pred_fev1  = predict(cf1, age, height);
    float pred_fvc   = predict(cfc, age, height);

    float lln_ratio  = lln(cr,  age, height);
    float lln_fev1   = lln(cf1, age, height);
    float lln_fvc    = lln(cfc, age, height);

    bool ratio_low = (ratio < lln_ratio);
    bool fev1_low  = (fev1  < lln_fev1);
    bool fvc_low   = (fvc   < lln_fvc);

    SpiroResult r;
    r.fev1_pct_pred  = (pred_fev1  > 0.01f) ? (fev1  / pred_fev1  * 100.0f) : 0.0f;
    r.fvc_pct_pred   = (pred_fvc   > 0.01f) ? (fvc   / pred_fvc   * 100.0f) : 0.0f;
    r.ratio_pct_pred = (pred_ratio > 0.01f) ? (ratio / pred_ratio * 100.0f) : 0.0f;

    if (ratio_low && fvc_low) {
        r.cls   = SPIRO_MIXED;
        r.label = "MIXED";
    } else if (ratio_low) {
        r.cls   = SPIRO_OBSTRUCTION;
        r.label = "OBST";
    } else if (fvc_low) {
        r.cls   = SPIRO_RESTRICTIVE;
        r.label = "REST";
    } else if (fev1_low) {
        r.cls   = SPIRO_PRISM;
        r.label = "PRISm";
    } else {
        r.cls   = SPIRO_NORMAL;
        r.label = "OK";
    }

    return r;
}
