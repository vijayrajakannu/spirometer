#ifndef EEZ_LVGL_UI_STYLES_H
#define EEZ_LVGL_UI_STYLES_H

#include <lvgl/lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

// Style: style_label_primary
lv_style_t *get_style_style_label_primary_MAIN_DEFAULT();
void add_style_style_label_primary(lv_obj_t *obj);
void remove_style_style_label_primary(lv_obj_t *obj);

// Style: style_label_secondary
lv_style_t *get_style_style_label_secondary_MAIN_DEFAULT();
void add_style_style_label_secondary(lv_obj_t *obj);
void remove_style_style_label_secondary(lv_obj_t *obj);

// Style: style_dim_label
lv_style_t *get_style_style_dim_label_MAIN_DEFAULT();
void add_style_style_dim_label(lv_obj_t *obj);
void remove_style_style_dim_label(lv_obj_t *obj);

// Style: style_progress_fev1
lv_style_t *get_style_style_progress_fev1_MAIN_DEFAULT();
lv_style_t *get_style_style_progress_fev1_INDICATOR_DEFAULT();
void add_style_style_progress_fev1(lv_obj_t *obj);
void remove_style_style_progress_fev1(lv_obj_t *obj);

// Style: style_surface
lv_style_t *get_style_style_surface_MAIN_DEFAULT();
void add_style_style_surface(lv_obj_t *obj);
void remove_style_style_surface(lv_obj_t *obj);

// Style: style_tile
lv_style_t *get_style_style_tile_MAIN_DEFAULT();
void add_style_style_tile(lv_obj_t *obj);
void remove_style_style_tile(lv_obj_t *obj);

// Style: style_progress_fvc
lv_style_t *get_style_style_progress_fvc_MAIN_DEFAULT();
lv_style_t *get_style_style_progress_fvc_INDICATOR_DEFAULT();
void add_style_style_progress_fvc(lv_obj_t *obj);
void remove_style_style_progress_fvc(lv_obj_t *obj);

// Style: style_progress_ratio
lv_style_t *get_style_style_progress_ratio_INDICATOR_DEFAULT();
lv_style_t *get_style_style_progress_ratio_MAIN_DEFAULT();
void add_style_style_progress_ratio(lv_obj_t *obj);
void remove_style_style_progress_ratio(lv_obj_t *obj);

// Style: style_progress_pef
lv_style_t *get_style_style_progress_pef_INDICATOR_DEFAULT();
lv_style_t *get_style_style_progress_pef_MAIN_DEFAULT();
void add_style_style_progress_pef(lv_obj_t *obj);
void remove_style_style_progress_pef(lv_obj_t *obj);

// Style: f-v
void add_style_f_v(lv_obj_t *obj);
void remove_style_f_v(lv_obj_t *obj);

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_STYLES_H*/