#include "ui_boot.h"
#include "ui_dashboard.h"
#include "zoe_logo.h"
#include "lvgl.h"
#include <cstring>

// 256x106 XBM → stride = 256/8 = 32 bytes per row
#define ZOE_W     256
#define ZOE_H     106
#define ZOE_STRIDE 32

static lv_obj_t * bootScreen   = NULL;
static lv_obj_t * carCanvas    = NULL;
static lv_obj_t * lineAccent   = NULL;
static lv_obj_t * lblLogo      = NULL;
static lv_obj_t * lblVersion   = NULL;
static void (*completeCb)(void) = NULL;

// Draw XBM bitmap onto LVGL canvas
static void drawXbmOnCanvas(lv_obj_t * canvas, const unsigned char * xbm,
                             int w, int h, int stride, lv_color_t color) {
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int byteIdx = y * stride + (x / 8);
            int bitIdx  = x % 8;
            if (xbm[byteIdx] & (1 << bitIdx)) {
                lv_canvas_set_px(canvas, x, y, color, LV_OPA_COVER);
            }
        }
    }
}

// Final fade-out callback
static void fadeOutCompleteCb(lv_anim_t * a) {
    if (bootScreen) {
        lv_obj_delete(bootScreen);
        bootScreen = NULL;
    }
    if (completeCb) completeCb();
}

// Timer: after 2s hold, fade out
static void holdTimerCb(lv_timer_t * timer) {
    // Hide canvas first — it doesn't support parent opacity inheritance
    if (carCanvas) lv_obj_add_flag(carCanvas, LV_OBJ_FLAG_HIDDEN);

    lv_anim_t fadeOut;
    lv_anim_init(&fadeOut);
    lv_anim_set_var(&fadeOut, bootScreen);
    lv_anim_set_values(&fadeOut, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_duration(&fadeOut, 400);
    lv_anim_set_exec_cb(&fadeOut, [](void * obj, int32_t val) {
        lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)val, 0);
    });
    lv_anim_set_completed_cb(&fadeOut, fadeOutCompleteCb);
    lv_anim_start(&fadeOut);
}

// Car arrived → show line, logo, version, then hold 2s
static void carArrivedCb(lv_anim_t * a) {
    // 1) Accent line grows on the right side
    lv_anim_t lineAnim;
    lv_anim_init(&lineAnim);
    lv_anim_set_var(&lineAnim, lineAccent);
    lv_anim_set_values(&lineAnim, 0, 180);
    lv_anim_set_duration(&lineAnim, 500);
    lv_anim_set_delay(&lineAnim, 100);
    lv_anim_set_path_cb(&lineAnim, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&lineAnim, [](void * obj, int32_t val) {
        lv_obj_set_width((lv_obj_t *)obj, val);
    });
    lv_anim_start(&lineAnim);

    // 2) ZoEyee text fades in above line
    lv_anim_t logoAnim;
    lv_anim_init(&logoAnim);
    lv_anim_set_var(&logoAnim, lblLogo);
    lv_anim_set_values(&logoAnim, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&logoAnim, 400);
    lv_anim_set_delay(&logoAnim, 400);
    lv_anim_set_exec_cb(&logoAnim, [](void * obj, int32_t val) {
        lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)val, 0);
    });
    lv_anim_start(&logoAnim);

    // 3) Version fades in below line
    lv_anim_t verAnim;
    lv_anim_init(&verAnim);
    lv_anim_set_var(&verAnim, lblVersion);
    lv_anim_set_values(&verAnim, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&verAnim, 400);
    lv_anim_set_delay(&verAnim, 700);
    lv_anim_set_exec_cb(&verAnim, [](void * obj, int32_t val) {
        lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)val, 0);
    });
    lv_anim_start(&verAnim);

    // 4) Hold for 2 seconds then fade out
    lv_timer_t * t = lv_timer_create(holdTimerCb, 2800, NULL);
    lv_timer_set_repeat_count(t, 1);
}

void UiBoot::show(lv_obj_t * screen, void (*onComplete)(void)) {
    completeCb = onComplete;

    // ── Full-screen boot container ──
    bootScreen = lv_obj_create(screen);
    lv_obj_set_size(bootScreen, 640, 172);
    lv_obj_align(bootScreen, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(bootScreen, lv_color_hex(0x0D1117), 0);
    lv_obj_set_style_bg_opa(bootScreen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bootScreen, 0, 0);
    lv_obj_set_style_radius(bootScreen, 0, 0);
    lv_obj_set_style_pad_all(bootScreen, 0, 0);
    lv_obj_set_scrollbar_mode(bootScreen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(bootScreen, LV_OBJ_FLAG_SCROLLABLE);

    // ── ZOE Car canvas (256x106) – left side ──
    carCanvas = lv_canvas_create(bootScreen);
    // Use PSRAM for the large canvas buffer
    static lv_color_t * cbuf = NULL;
    if (!cbuf) {
        cbuf = (lv_color_t *)heap_caps_malloc(ZOE_W * ZOE_H * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    }
    lv_canvas_set_buffer(carCanvas, cbuf, ZOE_W, ZOE_H, LV_COLOR_FORMAT_NATIVE);
    lv_canvas_fill_bg(carCanvas, lv_color_hex(0x0D1117), LV_OPA_COVER);
    drawXbmOnCanvas(carCanvas, zoe256_bits, ZOE_W, ZOE_H, ZOE_STRIDE, lv_color_hex(0x00E5FF));
    // Start off-screen left, vertically centered
    int carY = (172 - ZOE_H) / 2;
    lv_obj_set_pos(carCanvas, -ZOE_W - 10, carY);

    // ── Right side elements ──
    // Center X of the right half: (640/2 + 640) / 2 = ~480
    int rightCenterX = 480;

    // Accent line (right side, centered)
    lineAccent = lv_obj_create(bootScreen);
    lv_obj_set_size(lineAccent, 0, 2);
    lv_obj_set_pos(lineAccent, rightCenterX - 90, 172 / 2);
    lv_obj_set_style_bg_color(lineAccent, lv_color_hex(0x00E5FF), 0);
    lv_obj_set_style_bg_opa(lineAccent, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(lineAccent, 0, 0);
    lv_obj_set_style_radius(lineAccent, 1, 0);
    lv_obj_set_scrollbar_mode(lineAccent, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(lineAccent, LV_OBJ_FLAG_SCROLLABLE);

    // "ZoEyee" text – above the line
    lblLogo = lv_label_create(bootScreen);
    lv_label_set_text(lblLogo, "ZoEyee");
    lv_obj_set_style_text_color(lblLogo, lv_color_hex(0x00E5FF), 0);
    lv_obj_set_style_text_font(lblLogo, &lv_font_montserrat_24, 0);
    lv_obj_set_style_opa(lblLogo, LV_OPA_TRANSP, 0);
    lv_obj_set_pos(lblLogo, rightCenterX - 48, 172 / 2 - 32);

    // Version text – below the line
    lblVersion = lv_label_create(bootScreen);
    lv_label_set_text(lblVersion, ZOEYEE_VERSION);
    lv_obj_set_style_text_color(lblVersion, lv_color_hex(0x8B949E), 0);
    lv_obj_set_style_text_font(lblVersion, &lv_font_montserrat_12, 0);
    lv_obj_set_style_opa(lblVersion, LV_OPA_TRANSP, 0);
    lv_obj_set_pos(lblVersion, rightCenterX - 22, 172 / 2 + 10);

    // ═══════════════════════════════════════════════════════
    //  Animation: Car slides in from left → parks on left side
    // ═══════════════════════════════════════════════════════
    int carTargetX = 30; // Park on the left with some margin

    lv_anim_t carAnim;
    lv_anim_init(&carAnim);
    lv_anim_set_var(&carAnim, carCanvas);
    lv_anim_set_values(&carAnim, -ZOE_W - 10, carTargetX);
    lv_anim_set_duration(&carAnim, 1200);
    lv_anim_set_delay(&carAnim, 300);
    lv_anim_set_path_cb(&carAnim, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&carAnim, [](void * obj, int32_t val) {
        lv_obj_set_x((lv_obj_t *)obj, val);
    });
    lv_anim_set_completed_cb(&carAnim, carArrivedCb);
    lv_anim_start(&carAnim);
}
