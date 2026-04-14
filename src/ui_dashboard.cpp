#include "ui_dashboard.h"
#include "lvgl.h"
#include <cstdio>

// ─── Internal state ───────────────────────────────────────
static lv_obj_t * topBar       = NULL;
static lv_obj_t * lblTitle     = NULL;
static lv_obj_t * lblBt        = NULL;
static lv_obj_t * lblWifi      = NULL;
static lv_obj_t * lblCan       = NULL;
static lv_obj_t * btnMenu      = NULL;
static lv_obj_t * tileview     = NULL;
static lv_obj_t * pageDots     = NULL;
static lv_obj_t * dotIndicators[MAX_PAGES] = {};

static int numPages   = 1;
static int curPage    = 0;

// Widget slots per page (WIDGET_COUNT per page)
static lv_obj_t * widgetCards[MAX_PAGES][WIDGET_COUNT] = {};

// ─── Forward declarations ─────────────────────────────────
static void createTopBar(lv_obj_t * parent);
static void createTileview(lv_obj_t * parent);
static void createPageDots(lv_obj_t * parent);
static void createWidgetSlots(lv_obj_t * tile, int pageIdx);
static lv_obj_t * createEmptySlot(lv_obj_t * parent, int w, int h);
static void tileviewEventCb(lv_event_t * e);
static void updatePageDots(int activePage);

// ═══════════════════════════════════════════════════════════
//  Public API
// ═══════════════════════════════════════════════════════════

void UiDashboard::init(void) {
    lv_obj_t * screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, COLOR_BG, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);

    createTopBar(screen);
    createTileview(screen);
    createPageDots(screen);
    updatePageDots(0);
}

void UiDashboard::setPage(int page) {
    if (page < 0 || page >= numPages) return;
    curPage = page;
    lv_tileview_set_tile_by_index(tileview, page, 0, LV_ANIM_ON);
    updatePageDots(page);
}

int UiDashboard::getPageCount(void)  { return numPages; }
int UiDashboard::getCurrentPage(void){ return curPage;  }

void UiDashboard::setBluetoothStatus(bool connected) {
    lv_label_set_text(lblBt, connected ? LV_SYMBOL_BLUETOOTH : " ");
    lv_obj_set_style_text_color(lblBt, connected ? COLOR_ACCENT : COLOR_TEXT_SECONDARY, 0);
}

void UiDashboard::setWifiStatus(bool connected, int rssi) {
    lv_label_set_text(lblWifi, connected ? LV_SYMBOL_WIFI : " ");
    lv_color_t c = COLOR_TEXT_SECONDARY;
    if (connected) {
        if (rssi > -50)      c = COLOR_GREEN;
        else if (rssi > -70) c = COLOR_YELLOW;
        else                 c = COLOR_RED;
    }
    lv_obj_set_style_text_color(lblWifi, c, 0);
}

void UiDashboard::setCanStatus(bool active) {
    lv_label_set_text(lblCan, active ? LV_SYMBOL_CHARGE : " ");
    lv_obj_set_style_text_color(lblCan, active ? COLOR_GREEN : COLOR_TEXT_SECONDARY, 0);
}

// ═══════════════════════════════════════════════════════════
//  Top Bar  (28px height, full width)
// ═══════════════════════════════════════════════════════════

static void createTopBar(lv_obj_t * parent) {
    topBar = lv_obj_create(parent);
    lv_obj_set_size(topBar, SCREEN_W, TOP_BAR_H);
    lv_obj_align(topBar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(topBar, COLOR_TOP_BAR, 0);
    lv_obj_set_style_bg_opa(topBar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(topBar, 0, 0);
    lv_obj_set_style_radius(topBar, 0, 0);
    lv_obj_set_style_pad_all(topBar, 0, 0);
    lv_obj_set_style_pad_left(topBar, 8, 0);
    lv_obj_set_style_pad_right(topBar, 8, 0);
    lv_obj_set_scrollbar_mode(topBar, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(topBar, LV_OBJ_FLAG_SCROLLABLE);

    // ── Left: Status icons ──
    // Bluetooth icon
    lblBt = lv_label_create(topBar);
    lv_label_set_text(lblBt, " ");
    lv_obj_set_style_text_color(lblBt, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(lblBt, &lv_font_montserrat_14, 0);
    lv_obj_align(lblBt, LV_ALIGN_LEFT_MID, 0, 0);

    // WiFi icon
    lblWifi = lv_label_create(topBar);
    lv_label_set_text(lblWifi, " ");
    lv_obj_set_style_text_color(lblWifi, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(lblWifi, &lv_font_montserrat_14, 0);
    lv_obj_align(lblWifi, LV_ALIGN_LEFT_MID, 22, 0);

    // CAN activity icon
    lblCan = lv_label_create(topBar);
    lv_label_set_text(lblCan, " ");
    lv_obj_set_style_text_color(lblCan, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(lblCan, &lv_font_montserrat_14, 0);
    lv_obj_align(lblCan, LV_ALIGN_LEFT_MID, 44, 0);

    // ── Center: Title ──
    lblTitle = lv_label_create(topBar);
    lv_label_set_text(lblTitle, "ZoEyee");
    lv_obj_set_style_text_color(lblTitle, COLOR_CYAN, 0);
    lv_obj_set_style_text_font(lblTitle, &lv_font_montserrat_16, 0);
    lv_obj_align(lblTitle, LV_ALIGN_CENTER, 0, 0);

    // ── Right: Menu button ──
    btnMenu = lv_button_create(topBar);
    lv_obj_set_size(btnMenu, 36, 22);
    lv_obj_align(btnMenu, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(btnMenu, COLOR_WIDGET_BG, 0);
    lv_obj_set_style_bg_opa(btnMenu, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(btnMenu, COLOR_WIDGET_BORDER, 0);
    lv_obj_set_style_border_width(btnMenu, 1, 0);
    lv_obj_set_style_radius(btnMenu, 4, 0);
    lv_obj_set_style_pad_all(btnMenu, 0, 0);
    
    lv_obj_t * lblMenu = lv_label_create(btnMenu);
    lv_label_set_text(lblMenu, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_color(lblMenu, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(lblMenu, &lv_font_montserrat_14, 0);
    lv_obj_center(lblMenu);
}

// ═══════════════════════════════════════════════════════════
//  Tileview (horizontally scrollable pages)
// ═══════════════════════════════════════════════════════════

static void createTileview(lv_obj_t * parent) {
    tileview = lv_tileview_create(parent);
    lv_obj_set_size(tileview, SCREEN_W, CONTENT_H - 14); // Leave space for dots
    lv_obj_align(tileview, LV_ALIGN_TOP_LEFT, 0, TOP_BAR_H);
    lv_obj_set_style_bg_color(tileview, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(tileview, LV_OPA_TRANSP, 0);
    lv_obj_set_scrollbar_mode(tileview, LV_SCROLLBAR_MODE_OFF);

    // Create one page (with empty widget slots)
    lv_obj_t * tile1 = lv_tileview_add_tile(tileview, 0, 0, LV_DIR_RIGHT);
    createWidgetSlots(tile1, 0);

    // Register scroll event for page dot updates
    lv_obj_add_event_cb(tileview, tileviewEventCb, LV_EVENT_VALUE_CHANGED, NULL);
}

// ═══════════════════════════════════════════════════════════
//  Widget Slots (4 per page)
// ═══════════════════════════════════════════════════════════

static void createWidgetSlots(lv_obj_t * tile, int pageIdx) {
    // Calculate widget dimensions
    int totalGap = WIDGET_GAP * (WIDGET_COUNT - 1) + WIDGET_MARGIN * 2;
    int widgetW  = (SCREEN_W - totalGap) / WIDGET_COUNT;
    int widgetH  = CONTENT_H - 14 - WIDGET_MARGIN * 2; // Minus dots area and margins

    for (int i = 0; i < WIDGET_COUNT; i++) {
        int x = WIDGET_MARGIN + i * (widgetW + WIDGET_GAP);
        int y = WIDGET_MARGIN;

        lv_obj_t * slot = createEmptySlot(tile, widgetW, widgetH);
        lv_obj_set_pos(slot, x, y);
        widgetCards[pageIdx][i] = slot;
    }
}

// ═══════════════════════════════════════════════════════════
//  Empty Slot (dashed border + "+" icon)
// ═══════════════════════════════════════════════════════════

static void emptySlotDrawCb(lv_event_t * e) {
    lv_obj_t * obj = (lv_obj_t *)lv_event_get_target(e);
    lv_layer_t * layer = lv_event_get_layer(e);

    // Get object coordinates
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);

    int32_t x1 = coords.x1;
    int32_t y1 = coords.y1;
    int32_t x2 = coords.x2;
    int32_t y2 = coords.y2;
    int32_t r  = 8; // corner radius

    // Draw dashed border manually
    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.color = COLOR_EMPTY_DASH;
    line_dsc.width = 2;
    line_dsc.dash_width = 6;
    line_dsc.dash_gap = 4;
    line_dsc.round_start = 1;
    line_dsc.round_end = 1;

    // Top edge
    lv_area_t line_area;
    line_area.x1 = x1 + r;  line_area.y1 = y1;
    line_area.x2 = x2 - r;  line_area.y2 = y1;
    lv_draw_line(layer, &line_dsc);

    // Bottom edge
    line_area.x1 = x1 + r;  line_area.y1 = y2;
    line_area.x2 = x2 - r;  line_area.y2 = y2;
    lv_draw_line(layer, &line_dsc);

    // Left edge
    line_area.x1 = x1;  line_area.y1 = y1 + r;
    line_area.x2 = x1;  line_area.y2 = y2 - r;
    lv_draw_line(layer, &line_dsc);

    // Right edge
    line_area.x1 = x2;  line_area.y1 = y1 + r;
    line_area.x2 = x2;  line_area.y2 = y2 - r;
    lv_draw_line(layer, &line_dsc);
}

static lv_obj_t * createEmptySlot(lv_obj_t * parent, int w, int h) {
    lv_obj_t * slot = lv_obj_create(parent);
    lv_obj_set_size(slot, w, h);
    lv_obj_set_style_bg_opa(slot, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(slot, 0, 0);
    lv_obj_set_style_radius(slot, 8, 0);
    lv_obj_set_style_pad_all(slot, 0, 0);
    lv_obj_set_scrollbar_mode(slot, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(slot, LV_OBJ_FLAG_SCROLLABLE);

    // Dashed border via custom draw callback  
    lv_obj_set_style_border_color(slot, COLOR_EMPTY_DASH, 0);
    lv_obj_set_style_border_width(slot, 2, 0);
    lv_obj_set_style_border_opa(slot, LV_OPA_70, 0);
    lv_obj_set_style_border_side(slot, LV_BORDER_SIDE_FULL, 0);

    // "+" label in center
    lv_obj_t * lblPlus = lv_label_create(slot);
    lv_label_set_text(lblPlus, LV_SYMBOL_PLUS);
    lv_obj_set_style_text_color(lblPlus, COLOR_PLUS, 0);
    lv_obj_set_style_text_font(lblPlus, &lv_font_montserrat_28, 0);
    lv_obj_center(lblPlus);

    return slot;
}

// ═══════════════════════════════════════════════════════════
//  Page Dots (indicator at bottom)
// ═══════════════════════════════════════════════════════════

static void createPageDots(lv_obj_t * parent) {
    pageDots = lv_obj_create(parent);
    lv_obj_set_size(pageDots, SCREEN_W, 14);
    lv_obj_align(pageDots, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(pageDots, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pageDots, 0, 0);
    lv_obj_set_style_pad_all(pageDots, 0, 0);
    lv_obj_set_scrollbar_mode(pageDots, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(pageDots, LV_OBJ_FLAG_SCROLLABLE);

    // Create dot indicators
    int dotSize = 6;
    int dotGap  = 8;
    int totalW  = numPages * dotSize + (numPages - 1) * dotGap;
    int startX  = (SCREEN_W - totalW) / 2;

    for (int i = 0; i < numPages; i++) {
        lv_obj_t * dot = lv_obj_create(pageDots);
        lv_obj_set_size(dot, dotSize, dotSize);
        lv_obj_set_pos(dot, startX + i * (dotSize + dotGap), 4);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_scrollbar_mode(dot, LV_SCROLLBAR_MODE_OFF);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        dotIndicators[i] = dot;
    }
}

static void updatePageDots(int activePage) {
    for (int i = 0; i < numPages; i++) {
        if (dotIndicators[i] == NULL) continue;
        if (i == activePage) {
            lv_obj_set_style_bg_color(dotIndicators[i], COLOR_ACCENT, 0);
            lv_obj_set_size(dotIndicators[i], 8, 6);  // Active dot wider
        } else {
            lv_obj_set_style_bg_color(dotIndicators[i], COLOR_WIDGET_BORDER, 0);
            lv_obj_set_size(dotIndicators[i], 6, 6);
        }
    }
}

// ═══════════════════════════════════════════════════════════
//  Event callbacks
// ═══════════════════════════════════════════════════════════

static void tileviewEventCb(lv_event_t * e) {
    lv_obj_t * tv = (lv_obj_t *)lv_event_get_target(e);
    lv_obj_t * tile = lv_tileview_get_tile_active(tv);
    
    // Find which page index this tile is
    int32_t col = lv_obj_get_x(tile) / SCREEN_W;
    curPage = col;
    updatePageDots(curPage);
}
