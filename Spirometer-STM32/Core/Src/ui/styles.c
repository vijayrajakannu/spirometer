#include "styles.h"
#include "images.h"
#include "fonts.h"

#include "ui.h"
#include "screens.h"

//
// Style: style_label_primary
//

void init_style_style_label_primary_MAIN_DEFAULT(lv_style_t *style) {
    lv_style_set_text_color(style, lv_color_hex(0xff00e5a0));
    lv_style_set_text_font(style, &lv_font_montserrat_14);
};

lv_style_t *get_style_style_label_primary_MAIN_DEFAULT() {
    static lv_style_t *style;
    if (!style) {
        style = (lv_style_t *)lv_malloc(sizeof(lv_style_t));
        lv_style_init(style);
        init_style_style_label_primary_MAIN_DEFAULT(style);
    }
    return style;
};

void add_style_style_label_primary(lv_obj_t *obj) {
    (void)obj;
    lv_obj_add_style(obj, get_style_style_label_primary_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
};

void remove_style_style_label_primary(lv_obj_t *obj) {
    (void)obj;
    lv_obj_remove_style(obj, get_style_style_label_primary_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
};

//
// Style: style_label_secondary
//

void init_style_style_label_secondary_MAIN_DEFAULT(lv_style_t *style) {
    lv_style_set_text_color(style, lv_color_hex(0xff00d4ff));
    lv_style_set_text_font(style, &lv_font_montserrat_14);
};

lv_style_t *get_style_style_label_secondary_MAIN_DEFAULT() {
    static lv_style_t *style;
    if (!style) {
        style = (lv_style_t *)lv_malloc(sizeof(lv_style_t));
        lv_style_init(style);
        init_style_style_label_secondary_MAIN_DEFAULT(style);
    }
    return style;
};

void add_style_style_label_secondary(lv_obj_t *obj) {
    (void)obj;
    lv_obj_add_style(obj, get_style_style_label_secondary_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
};

void remove_style_style_label_secondary(lv_obj_t *obj) {
    (void)obj;
    lv_obj_remove_style(obj, get_style_style_label_secondary_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
};

//
// Style: style_dim_label
//

void init_style_style_dim_label_MAIN_DEFAULT(lv_style_t *style) {
    lv_style_set_text_color(style, lv_color_hex(0xff3d5070));
    lv_style_set_text_font(style, &lv_font_montserrat_14);
};

lv_style_t *get_style_style_dim_label_MAIN_DEFAULT() {
    static lv_style_t *style;
    if (!style) {
        style = (lv_style_t *)lv_malloc(sizeof(lv_style_t));
        lv_style_init(style);
        init_style_style_dim_label_MAIN_DEFAULT(style);
    }
    return style;
};

void add_style_style_dim_label(lv_obj_t *obj) {
    (void)obj;
    lv_obj_add_style(obj, get_style_style_dim_label_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
};

void remove_style_style_dim_label(lv_obj_t *obj) {
    (void)obj;
    lv_obj_remove_style(obj, get_style_style_dim_label_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
};

//
// Style: style_progress_fev1
//

void init_style_style_progress_fev1_MAIN_DEFAULT(lv_style_t *style) {
    lv_style_set_bg_color(style, lv_color_hex(0xff1a2540));
};

lv_style_t *get_style_style_progress_fev1_MAIN_DEFAULT() {
    static lv_style_t *style;
    if (!style) {
        style = (lv_style_t *)lv_malloc(sizeof(lv_style_t));
        lv_style_init(style);
        init_style_style_progress_fev1_MAIN_DEFAULT(style);
    }
    return style;
};

void init_style_style_progress_fev1_INDICATOR_DEFAULT(lv_style_t *style) {
    lv_style_set_bg_color(style, lv_color_hex(0xff00e5a0));
};

lv_style_t *get_style_style_progress_fev1_INDICATOR_DEFAULT() {
    static lv_style_t *style;
    if (!style) {
        style = (lv_style_t *)lv_malloc(sizeof(lv_style_t));
        lv_style_init(style);
        init_style_style_progress_fev1_INDICATOR_DEFAULT(style);
    }
    return style;
};

void add_style_style_progress_fev1(lv_obj_t *obj) {
    (void)obj;
    lv_obj_add_style(obj, get_style_style_progress_fev1_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_style(obj, get_style_style_progress_fev1_INDICATOR_DEFAULT(), LV_PART_INDICATOR | LV_STATE_DEFAULT);
};

void remove_style_style_progress_fev1(lv_obj_t *obj) {
    (void)obj;
    lv_obj_remove_style(obj, get_style_style_progress_fev1_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_remove_style(obj, get_style_style_progress_fev1_INDICATOR_DEFAULT(), LV_PART_INDICATOR | LV_STATE_DEFAULT);
};

//
// Style: style_surface
//

void init_style_style_surface_MAIN_DEFAULT(lv_style_t *style) {
    lv_style_set_bg_color(style, lv_color_hex(0xff111620));
    lv_style_set_radius(style, 8);
    lv_style_set_border_width(style, 1);
    lv_style_set_border_color(style, lv_color_hex(0xff1e2738));
};

lv_style_t *get_style_style_surface_MAIN_DEFAULT() {
    static lv_style_t *style;
    if (!style) {
        style = (lv_style_t *)lv_malloc(sizeof(lv_style_t));
        lv_style_init(style);
        init_style_style_surface_MAIN_DEFAULT(style);
    }
    return style;
};

void add_style_style_surface(lv_obj_t *obj) {
    (void)obj;
    lv_obj_add_style(obj, get_style_style_surface_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
};

void remove_style_style_surface(lv_obj_t *obj) {
    (void)obj;
    lv_obj_remove_style(obj, get_style_style_surface_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
};

//
// Style: style_tile
//

void init_style_style_tile_MAIN_DEFAULT(lv_style_t *style) {
    lv_style_set_bg_color(style, lv_color_hex(0xff0d1420));
    lv_style_set_radius(style, 6);
    lv_style_set_pad_top(style, 6);
    lv_style_set_pad_bottom(style, 6);
    lv_style_set_pad_left(style, 6);
    lv_style_set_pad_right(style, 6);
    lv_style_set_border_color(style, lv_color_hex(0xff1a2540));
    lv_style_set_border_width(style, 1);
};

lv_style_t *get_style_style_tile_MAIN_DEFAULT() {
    static lv_style_t *style;
    if (!style) {
        style = (lv_style_t *)lv_malloc(sizeof(lv_style_t));
        lv_style_init(style);
        init_style_style_tile_MAIN_DEFAULT(style);
    }
    return style;
};

void add_style_style_tile(lv_obj_t *obj) {
    (void)obj;
    lv_obj_add_style(obj, get_style_style_tile_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
};

void remove_style_style_tile(lv_obj_t *obj) {
    (void)obj;
    lv_obj_remove_style(obj, get_style_style_tile_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
};

//
// Style: style_progress_fvc
//

void init_style_style_progress_fvc_MAIN_DEFAULT(lv_style_t *style) {
    lv_style_set_bg_color(style, lv_color_hex(0xff1a2540));
};

lv_style_t *get_style_style_progress_fvc_MAIN_DEFAULT() {
    static lv_style_t *style;
    if (!style) {
        style = (lv_style_t *)lv_malloc(sizeof(lv_style_t));
        lv_style_init(style);
        init_style_style_progress_fvc_MAIN_DEFAULT(style);
    }
    return style;
};

void init_style_style_progress_fvc_INDICATOR_DEFAULT(lv_style_t *style) {
    lv_style_set_bg_color(style, lv_color_hex(0xff00d4ff));
};

lv_style_t *get_style_style_progress_fvc_INDICATOR_DEFAULT() {
    static lv_style_t *style;
    if (!style) {
        style = (lv_style_t *)lv_malloc(sizeof(lv_style_t));
        lv_style_init(style);
        init_style_style_progress_fvc_INDICATOR_DEFAULT(style);
    }
    return style;
};

void add_style_style_progress_fvc(lv_obj_t *obj) {
    (void)obj;
    lv_obj_add_style(obj, get_style_style_progress_fvc_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_style(obj, get_style_style_progress_fvc_INDICATOR_DEFAULT(), LV_PART_INDICATOR | LV_STATE_DEFAULT);
};

void remove_style_style_progress_fvc(lv_obj_t *obj) {
    (void)obj;
    lv_obj_remove_style(obj, get_style_style_progress_fvc_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_remove_style(obj, get_style_style_progress_fvc_INDICATOR_DEFAULT(), LV_PART_INDICATOR | LV_STATE_DEFAULT);
};

//
// Style: style_progress_ratio
//

void init_style_style_progress_ratio_INDICATOR_DEFAULT(lv_style_t *style) {
    lv_style_set_bg_color(style, lv_color_hex(0xffffb020));
};

lv_style_t *get_style_style_progress_ratio_INDICATOR_DEFAULT() {
    static lv_style_t *style;
    if (!style) {
        style = (lv_style_t *)lv_malloc(sizeof(lv_style_t));
        lv_style_init(style);
        init_style_style_progress_ratio_INDICATOR_DEFAULT(style);
    }
    return style;
};

void init_style_style_progress_ratio_MAIN_DEFAULT(lv_style_t *style) {
    lv_style_set_bg_color(style, lv_color_hex(0xff1a2540));
};

lv_style_t *get_style_style_progress_ratio_MAIN_DEFAULT() {
    static lv_style_t *style;
    if (!style) {
        style = (lv_style_t *)lv_malloc(sizeof(lv_style_t));
        lv_style_init(style);
        init_style_style_progress_ratio_MAIN_DEFAULT(style);
    }
    return style;
};

void add_style_style_progress_ratio(lv_obj_t *obj) {
    (void)obj;
    lv_obj_add_style(obj, get_style_style_progress_ratio_INDICATOR_DEFAULT(), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_add_style(obj, get_style_style_progress_ratio_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
};

void remove_style_style_progress_ratio(lv_obj_t *obj) {
    (void)obj;
    lv_obj_remove_style(obj, get_style_style_progress_ratio_INDICATOR_DEFAULT(), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_remove_style(obj, get_style_style_progress_ratio_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
};

//
// Style: style_progress_pef
//

void init_style_style_progress_pef_INDICATOR_DEFAULT(lv_style_t *style) {
    lv_style_set_bg_color(style, lv_color_hex(0xff4a7dff));
};

lv_style_t *get_style_style_progress_pef_INDICATOR_DEFAULT() {
    static lv_style_t *style;
    if (!style) {
        style = (lv_style_t *)lv_malloc(sizeof(lv_style_t));
        lv_style_init(style);
        init_style_style_progress_pef_INDICATOR_DEFAULT(style);
    }
    return style;
};

void init_style_style_progress_pef_MAIN_DEFAULT(lv_style_t *style) {
    lv_style_set_bg_color(style, lv_color_hex(0xff1a2540));
};

lv_style_t *get_style_style_progress_pef_MAIN_DEFAULT() {
    static lv_style_t *style;
    if (!style) {
        style = (lv_style_t *)lv_malloc(sizeof(lv_style_t));
        lv_style_init(style);
        init_style_style_progress_pef_MAIN_DEFAULT(style);
    }
    return style;
};

void add_style_style_progress_pef(lv_obj_t *obj) {
    (void)obj;
    lv_obj_add_style(obj, get_style_style_progress_pef_INDICATOR_DEFAULT(), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_add_style(obj, get_style_style_progress_pef_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
};

void remove_style_style_progress_pef(lv_obj_t *obj) {
    (void)obj;
    lv_obj_remove_style(obj, get_style_style_progress_pef_INDICATOR_DEFAULT(), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_remove_style(obj, get_style_style_progress_pef_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
};

//
// Style: f-v
//

void add_style_f_v(lv_obj_t *obj) {
    (void)obj;
};

void remove_style_f_v(lv_obj_t *obj) {
    (void)obj;
};

//
//
//

void add_style(lv_obj_t *obj, int32_t styleIndex) {
    typedef void (*AddStyleFunc)(lv_obj_t *obj);
    static const AddStyleFunc add_style_funcs[] = {
        add_style_style_label_primary,
        add_style_style_label_secondary,
        add_style_style_dim_label,
        add_style_style_progress_fev1,
        add_style_style_surface,
        add_style_style_tile,
        add_style_style_progress_fvc,
        add_style_style_progress_ratio,
        add_style_style_progress_pef,
        add_style_f_v,
    };
    add_style_funcs[styleIndex](obj);
}

void remove_style(lv_obj_t *obj, int32_t styleIndex) {
    typedef void (*RemoveStyleFunc)(lv_obj_t *obj);
    static const RemoveStyleFunc remove_style_funcs[] = {
        remove_style_style_label_primary,
        remove_style_style_label_secondary,
        remove_style_style_dim_label,
        remove_style_style_progress_fev1,
        remove_style_style_surface,
        remove_style_style_tile,
        remove_style_style_progress_fvc,
        remove_style_style_progress_ratio,
        remove_style_style_progress_pef,
        remove_style_f_v,
    };
    remove_style_funcs[styleIndex](obj);
}
