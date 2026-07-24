#ifndef SPIRO_CLASSIFY_H
#define SPIRO_CLASSIFY_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SPIRO_NORMAL,
    SPIRO_OBSTRUCTION,
    SPIRO_RESTRICTIVE,
    SPIRO_MIXED,
    SPIRO_PRISM       /* ratio >= LLN but FEV1 < LLN */
} SpiroClass;

typedef struct {
    SpiroClass  cls;
    const char *label;   /* "OK" / "OBST" / "REST" / "MIXED" / "PRISm" */
    float       fev1_pct_pred;
    float       fvc_pct_pred;
    float       ratio_pct_pred;
} SpiroResult;

/*
 * Classify a single maneuver.
 *   sex    : 0 = Male, 1 = Female
 *   age    : years (float)
 *   height : cm
 *   fev1   : litres
 *   fvc    : litres
 */
SpiroResult spiro_classify(int sex, float age, float height,
                           float fev1, float fvc);

#ifdef __cplusplus
}
#endif

#endif /* SPIRO_CLASSIFY_H */
