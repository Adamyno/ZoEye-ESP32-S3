#include "ui_dashboard.h"
#include "lvgl.h"
#include <cstdio>
#include "obd_globals.h"
#include "ui_settings.h"

// ─── Cell Voltage widget label pointers ──────────────────
static lv_obj_t * lblCellDelta  = NULL;
static lv_obj_t * lblCellMin    = NULL;
static lv_obj_t * lblCellMax    = NULL;

// ─── Internal state ───────────────────────────────────────
static lv_obj_t * topBar       = NULL;
static lv_obj_t * lblTitle     = NULL;
static lv_obj_t * lblBt        = NULL;
static lv_obj_t * lblWifi      = NULL;
static lv_obj_t * lblCan       = NULL;
static lv_obj_t * btnMenu      = NULL;
static lv_obj_t * tileview     = NULL;
static lv_obj_t * dotIndicators[MAX_PAGES] = {};

static int numPages   = 2;
static int curPage    = 0;

// Widget slots per page (WIDGET_COUNT per page)
static lv_obj_t * widgetCards[MAX_PAGES][WIDGET_COUNT] = {};

// ─── Forward declarations ─────────────────────────────────
static void createTopBar(lv_obj_t * parent);
static void createTileview(lv_obj_t * parent);
static void createWidgetSlots(lv_obj_t * tile, int pageIdx);
static void createDemoWidgets(lv_obj_t * tile, int pageIdx);
static void createPage2Widgets(lv_obj_t * tile, int pageIdx);
static lv_obj_t * createCellVoltageWidget(lv_obj_t * parent, int w, int h);
static lv_obj_t * createEmptySlot(lv_obj_t * parent, int w, int h);
static lv_obj_t * createFilledWidget(lv_obj_t * parent, int w, int h,
                        const char * name, const char * value, const char * unit,
                        lv_color_t accentColor, int barPercent);
static void tileviewEventCb(lv_event_t * e);
static void updatePageDots(int activePage);
static void getWidgetDimensions(int * outW, int * outH);
static void obd_gui_update_timer_cb(lv_timer_t * timer);
static void menuBtnClickCb(lv_event_t * e);

static lv_timer_t * obdUpdateTimer = NULL;

// ═══════════════════════════════════════════════════════════
//  Public API
// ═══════════════════════════════════════════════════════════

void UiDashboard::init(void) {
    lv_obj_t * screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, COLOR_BG, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);

    createTopBar(screen);
    createTileview(screen);
    updatePageDots(0);

    obdUpdateTimer = lv_timer_create(obd_gui_update_timer_cb, 500, NULL);
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
    lv_obj_set_style_text_font(lblBt, &lv_font_montserrat_16, 0);
    lv_obj_align(lblBt, LV_ALIGN_LEFT_MID, 0, 0);

    // WiFi icon
    lblWifi = lv_label_create(topBar);
    lv_label_set_text(lblWifi, " ");
    lv_obj_set_style_text_color(lblWifi, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(lblWifi, &lv_font_montserrat_16, 0);
    lv_obj_align(lblWifi, LV_ALIGN_LEFT_MID, 24, 0);

    // CAN activity icon
    lblCan = lv_label_create(topBar);
    lv_label_set_text(lblCan, " ");
    lv_obj_set_style_text_color(lblCan, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(lblCan, &lv_font_montserrat_16, 0);
    lv_obj_align(lblCan, LV_ALIGN_LEFT_MID, 48, 0);

    // ── Center: Title ──
    lblTitle = lv_label_create(topBar);
    lv_label_set_text(lblTitle, "ZoEyee");
    lv_obj_set_style_text_color(lblTitle, COLOR_CYAN, 0);
    lv_obj_set_style_text_font(lblTitle, &lv_font_montserrat_16, 0);
    lv_obj_align(lblTitle, LV_ALIGN_CENTER, 0, 0);

    // ── Right: Page dots + Menu button ──
    // Menu button (rightmost)
    btnMenu = lv_button_create(topBar);
    lv_obj_set_size(btnMenu, 48, 26);
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
    lv_obj_set_style_text_font(lblMenu, &lv_font_montserrat_16, 0);
    lv_obj_center(lblMenu);
    lv_obj_add_event_cb(btnMenu, menuBtnClickCb, LV_EVENT_CLICKED, NULL);

    // Page dot indicators (left of menu button)
    int dotSize = 6;
    int dotGap  = 6;
    int totalDotsW = numPages * dotSize + (numPages - 1) * dotGap;
    int dotsStartX = -(48 + 10 + totalDotsW); // 48=btn width, 10=gap

    for (int i = 0; i < numPages; i++) {
        lv_obj_t * dot = lv_obj_create(topBar);
        lv_obj_set_size(dot, dotSize, dotSize);
        lv_obj_align(dot, LV_ALIGN_RIGHT_MID, dotsStartX + i * (dotSize + dotGap), 0);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(dot, 0, 0);
        lv_obj_set_scrollbar_mode(dot, LV_SCROLLBAR_MODE_OFF);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        dotIndicators[i] = dot;
    }
}

// ═══════════════════════════════════════════════════════════
//  Tileview (horizontally scrollable pages)
// ═══════════════════════════════════════════════════════════

static void createTileview(lv_obj_t * parent) {
    tileview = lv_tileview_create(parent);
    lv_obj_set_size(tileview, SCREEN_W, CONTENT_H); // Full content area
    lv_obj_align(tileview, LV_ALIGN_TOP_LEFT, 0, TOP_BAR_H);
    lv_obj_set_style_bg_color(tileview, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(tileview, LV_OPA_TRANSP, 0);
    lv_obj_set_scrollbar_mode(tileview, LV_SCROLLBAR_MODE_OFF);

    // Page 1: Demo filled widgets
    lv_obj_t * tile1 = lv_tileview_add_tile(tileview, 0, 0, LV_DIR_RIGHT);
    createDemoWidgets(tile1, 0);

    // Page 2: HV Battery + empty slots
    lv_obj_t * tile2 = lv_tileview_add_tile(tileview, 1, 0, LV_DIR_LEFT);
    createPage2Widgets(tile2, 1);

    // Register scroll event for page dot updates
    lv_obj_add_event_cb(tileview, tileviewEventCb, LV_EVENT_VALUE_CHANGED, NULL);
}

// ═══════════════════════════════════════════════════════════
//  Widget Slots (4 per page)
// ═══════════════════════════════════════════════════════════

static void getWidgetDimensions(int * outW, int * outH) {
    int totalGap = WIDGET_GAP * (WIDGET_COUNT - 1) + WIDGET_MARGIN * 2;
    *outW = (SCREEN_W - totalGap) / WIDGET_COUNT;
    *outH = CONTENT_H - WIDGET_MARGIN * 2; // Full height, no dots reservation
}

static void createWidgetSlots(lv_obj_t * tile, int pageIdx) {
    int widgetW, widgetH;
    getWidgetDimensions(&widgetW, &widgetH);

    for (int i = 0; i < WIDGET_COUNT; i++) {
        int x = WIDGET_MARGIN + i * (widgetW + WIDGET_GAP);
        int y = WIDGET_MARGIN;

        lv_obj_t * slot = createEmptySlot(tile, widgetW, widgetH);
        lv_obj_set_pos(slot, x, y);
        widgetCards[pageIdx][i] = slot;
    }
}

// ═══════════════════════════════════════════════════════════
//  Demo Widgets (Page 1 - mock data)
// ═══════════════════════════════════════════════════════════

static void createDemoWidgets(lv_obj_t * tile, int pageIdx) {
    int widgetW, widgetH;
    getWidgetDimensions(&widgetW, &widgetH);

    struct DemoData {
        const char * name;
        const char * value;
        const char * unit;
        lv_color_t color;
        int barPct;
    };

    DemoData demos[4] = {
        { "SOC",       "--",    "%",   COLOR_GREEN,  0 },
        { NULL, NULL, NULL, {}, 0 },  // Slot 1: Cell Voltage widget (custom)
        { "Cabin",     "--",    "\xC2\xB0" "C", COLOR_YELLOW, 0 },
        { "Ext Temp",  "--",    "\xC2\xB0" "C", COLOR_CYAN,   0 },
    };

    for (int i = 0; i < WIDGET_COUNT; i++) {
        int x = WIDGET_MARGIN + i * (widgetW + WIDGET_GAP);
        int y = WIDGET_MARGIN;

        if (i == 1) {
            // Custom cell voltage widget
            lv_obj_t * w = createCellVoltageWidget(tile, widgetW, widgetH);
            lv_obj_set_pos(w, x, y);
            widgetCards[pageIdx][i] = w;
        } else {
            lv_obj_t * w = createFilledWidget(tile, widgetW, widgetH,
                demos[i].name, demos[i].value, demos[i].unit,
                demos[i].color, demos[i].barPct);
            lv_obj_set_pos(w, x, y);
            widgetCards[pageIdx][i] = w;
        }
    }
}

// ═══════════════════════════════════════════════════════════
//  Page 2 Widgets (EVC data)
// ═══════════════════════════════════════════════════════════

static void createPage2Widgets(lv_obj_t * tile, int pageIdx) {
    int widgetW, widgetH;
    getWidgetDimensions(&widgetW, &widgetH);

    for (int i = 0; i < WIDGET_COUNT; i++) {
        int x = WIDGET_MARGIN + i * (widgetW + WIDGET_GAP);
        int y = WIDGET_MARGIN;

        if (i == 0) {
            // HV Battery Voltage widget
            lv_obj_t * w = createFilledWidget(tile, widgetW, widgetH,
                "HV Battery", "--", "V", COLOR_ACCENT, 0);
            lv_obj_set_pos(w, x, y);
            widgetCards[pageIdx][i] = w;
        } else {
            // Empty slots for future widgets
            lv_obj_t * slot = createEmptySlot(tile, widgetW, widgetH);
            lv_obj_set_pos(slot, x, y);
            widgetCards[pageIdx][i] = slot;
        }
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
    lv_obj_set_style_radius(slot, 8, 0);
    lv_obj_set_style_pad_all(slot, 0, 0);
    lv_obj_set_scrollbar_mode(slot, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(slot, LV_OBJ_FLAG_SCROLLABLE);

    // Subtle dashed-style border
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
//  Filled Widget Card
// ═══════════════════════════════════════════════════════════

static lv_obj_t * createFilledWidget(lv_obj_t * parent, int w, int h,
                        const char * name, const char * value, const char * unit,
                        lv_color_t accentColor, int barPercent) {
    // ── Card container ──
    lv_obj_t * card = lv_obj_create(parent);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_bg_color(card, COLOR_WIDGET_BG, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, COLOR_WIDGET_BORDER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_pad_top(card, 6, 0);
    lv_obj_set_style_pad_bottom(card, 6, 0);
    lv_obj_set_style_pad_left(card, 8, 0);
    lv_obj_set_style_pad_right(card, 8, 0);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    // ── Parameter name (top) ──
    lv_obj_t * lblName = lv_label_create(card);
    lv_label_set_text(lblName, name);
    lv_obj_set_style_text_color(lblName, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(lblName, &lv_font_montserrat_14, 0);
    lv_obj_align(lblName, LV_ALIGN_TOP_LEFT, 0, 0);

    // ── Big value + unit (center) ──
    lv_obj_t * lblValue = lv_label_create(card);
    char buf[32];
    snprintf(buf, sizeof(buf), "%s", value);
    lv_label_set_text(lblValue, buf);
    lv_obj_set_style_text_color(lblValue, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(lblValue, &lv_font_montserrat_28, 0);
    lv_obj_align(lblValue, LV_ALIGN_LEFT_MID, 0, -2);

    lv_obj_t * lblUnit = lv_label_create(card);
    lv_label_set_text(lblUnit, unit);
    lv_obj_set_style_text_color(lblUnit, accentColor, 0);
    lv_obj_set_style_text_font(lblUnit, &lv_font_montserrat_14, 0);
    lv_obj_align_to(lblUnit, lblValue, LV_ALIGN_OUT_RIGHT_BOTTOM, 3, -2);

    // ── Progress bar (bottom) ──
    lv_obj_t * bar = lv_bar_create(card);
    lv_obj_set_size(bar, w - 20, 6);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, barPercent, LV_ANIM_ON);

    // Bar background
    lv_obj_set_style_bg_color(bar, COLOR_WIDGET_BORDER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 3, LV_PART_MAIN);

    // Bar indicator (filled part)
    lv_obj_set_style_bg_color(bar, accentColor, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 3, LV_PART_INDICATOR);

    return card;
}

// ═══════════════════════════════════════════════════════════
//  Cell Voltage Widget (Delta + Min/Max)
// ═══════════════════════════════════════════════════════════

static lv_obj_t * createCellVoltageWidget(lv_obj_t * parent, int w, int h) {
    // ── Card container ──
    lv_obj_t * card = lv_obj_create(parent);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_bg_color(card, COLOR_WIDGET_BG, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, COLOR_WIDGET_BORDER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_pad_top(card, 4, 0);
    lv_obj_set_style_pad_bottom(card, 4, 0);
    lv_obj_set_style_pad_left(card, 6, 0);
    lv_obj_set_style_pad_right(card, 6, 0);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    int innerW = w - 14; // 6+6 padding + 2 border

    // ── Title "Delta V" ──
    lv_obj_t * lblTitle = lv_label_create(card);
    lv_label_set_text(lblTitle, "Delta V");
    lv_obj_set_style_text_color(lblTitle, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(lblTitle, &lv_font_montserrat_14, 0);
    lv_obj_align(lblTitle, LV_ALIGN_TOP_MID, 0, 0);

    // ── Delta value (big, prominent) ──
    lblCellDelta = lv_label_create(card);
    lv_label_set_text(lblCellDelta, "-- mV");
    lv_obj_set_style_text_color(lblCellDelta, COLOR_GREEN, 0);
    lv_obj_set_style_text_font(lblCellDelta, &lv_font_montserrat_24, 0);
    lv_obj_align(lblCellDelta, LV_ALIGN_TOP_MID, 0, 18);

    // ── Separator line ──
    lv_obj_t * line = lv_obj_create(card);
    lv_obj_set_size(line, innerW, 1);
    lv_obj_set_style_bg_color(line, COLOR_WIDGET_BORDER, 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_set_style_radius(line, 0, 0);
    lv_obj_set_style_pad_all(line, 0, 0);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(line, LV_ALIGN_TOP_MID, 0, 54);

    // ── Bottom row: Min and Max side by side (right below separator) ──
    int halfW = (innerW - 4) / 2; // 4px gap between
    int boxH = h - 64; // remaining height after title+delta+line

    // --- Min V box (left) ---
    lv_obj_t * boxMin = lv_obj_create(card);
    lv_obj_set_size(boxMin, halfW, boxH);
    lv_obj_align(boxMin, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(boxMin, lv_color_hex(0x161B22), 0);
    lv_obj_set_style_bg_opa(boxMin, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(boxMin, COLOR_WIDGET_BORDER, 0);
    lv_obj_set_style_border_width(boxMin, 1, 0);
    lv_obj_set_style_radius(boxMin, 6, 0);
    lv_obj_set_style_pad_all(boxMin, 2, 0);
    lv_obj_set_scrollbar_mode(boxMin, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(boxMin, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * lblMinLabel = lv_label_create(boxMin);
    lv_label_set_text(lblMinLabel, "Min V");
    lv_obj_set_style_text_color(lblMinLabel, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(lblMinLabel, &lv_font_montserrat_10, 0);
    lv_obj_align(lblMinLabel, LV_ALIGN_TOP_MID, 0, 0);

    lblCellMin = lv_label_create(boxMin);
    lv_label_set_text(lblCellMin, "-.--");
    lv_obj_set_style_text_color(lblCellMin, COLOR_CYAN, 0);
    lv_obj_set_style_text_font(lblCellMin, &lv_font_montserrat_16, 0);
    lv_obj_align(lblCellMin, LV_ALIGN_BOTTOM_MID, 0, 0);

    // --- Max V box (right) ---
    lv_obj_t * boxMax = lv_obj_create(card);
    lv_obj_set_size(boxMax, halfW, boxH);
    lv_obj_align(boxMax, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(boxMax, lv_color_hex(0x161B22), 0);
    lv_obj_set_style_bg_opa(boxMax, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(boxMax, COLOR_WIDGET_BORDER, 0);
    lv_obj_set_style_border_width(boxMax, 1, 0);
    lv_obj_set_style_radius(boxMax, 6, 0);
    lv_obj_set_style_pad_all(boxMax, 2, 0);
    lv_obj_set_scrollbar_mode(boxMax, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(boxMax, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * lblMaxLabel = lv_label_create(boxMax);
    lv_label_set_text(lblMaxLabel, "Max V");
    lv_obj_set_style_text_color(lblMaxLabel, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(lblMaxLabel, &lv_font_montserrat_10, 0);
    lv_obj_align(lblMaxLabel, LV_ALIGN_TOP_MID, 0, 0);

    lblCellMax = lv_label_create(boxMax);
    lv_label_set_text(lblCellMax, "-.--");
    lv_obj_set_style_text_color(lblCellMax, COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(lblCellMax, &lv_font_montserrat_16, 0);
    lv_obj_align(lblCellMax, LV_ALIGN_BOTTOM_MID, 0, 0);

    return card;
}

// ═══════════════════════════════════════════════════════════
//  Page Dots (in top bar)
// ═══════════════════════════════════════════════════════════

static void updatePageDots(int activePage) {
    for (int i = 0; i < numPages; i++) {
        if (dotIndicators[i] == NULL) continue;
        if (i == activePage) {
            lv_obj_set_style_bg_color(dotIndicators[i], COLOR_ACCENT, 0);
        } else {
            lv_obj_set_style_bg_color(dotIndicators[i], COLOR_WIDGET_BORDER, 0);
        }
    }
}

// ═══════════════════════════════════════════════════════════
//  Event callbacks and LVGL Timers
// ═══════════════════════════════════════════════════════════

static void tileviewEventCb(lv_event_t * e) {
    lv_obj_t * tv = (lv_obj_t *)lv_event_get_target(e);
    lv_obj_t * tile = lv_tileview_get_tile_active(tv);
    
    // Find which page index this tile is
    int32_t col = lv_obj_get_x(tile) / SCREEN_W;
    curPage = col;
    updatePageDots(curPage);
}

static void obd_gui_update_timer_cb(lv_timer_t * timer) {
    if (xSemaphoreTake(obdDataMutex, 0) == pdTRUE) {

        // ── BT Status Icon ──
        static bool btBlinkState = false;
        btBlinkState = !btBlinkState;
        
        if (bleConnecting) {
            // Blink BT icon yellow during connecting
            lv_label_set_text(lblBt, btBlinkState ? LV_SYMBOL_BLUETOOTH : " ");
            lv_obj_set_style_text_color(lblBt, COLOR_YELLOW, 0);
        } else if (isBluetoothConnected) {
            // Solid BT icon blue when connected
            lv_label_set_text(lblBt, LV_SYMBOL_BLUETOOTH);
            lv_obj_set_style_text_color(lblBt, COLOR_ACCENT, 0);
        } else {
            lv_label_set_text(lblBt, " ");
        }

        // ── CAN Status Icon ──
        static bool canBlinkState = false;
        canBlinkState = !canBlinkState;
        
        if (isBluetoothConnected && !obdCanConnected) {
            // BT connected but CAN not ready - blink yellow
            lv_label_set_text(lblCan, canBlinkState ? LV_SYMBOL_CHARGE : " ");
            lv_obj_set_style_text_color(lblCan, COLOR_YELLOW, 0);
        } else if (obdCanConnected) {
            // CAN connected - show solid or blink on data exchange
            bool recentData = (millis() - lastOBDRxTime < 1000);
            static bool dataBlinkState = false;
            if (recentData) dataBlinkState = !dataBlinkState;
            else dataBlinkState = true; // solid when idle
            lv_label_set_text(lblCan, LV_SYMBOL_CHARGE);
            lv_obj_set_style_text_color(lblCan, 
                (recentData && !dataBlinkState) ? COLOR_BG : COLOR_GREEN, 0);
        } else {
            lv_label_set_text(lblCan, " ");
        }

        // ── Widget data updates ──
        if (widgetCards[0][0] != NULL && obdSOC >= 0) {
            lv_obj_t * lblValue = lv_obj_get_child(widgetCards[0][0], 1);
            lv_obj_t * bar = lv_obj_get_child(widgetCards[0][0], 3);
            if (lblValue && bar) {
                char buf[16];
                snprintf(buf, sizeof(buf), "%.0f", obdSOC);
                lv_label_set_text(lblValue, buf);
                lv_bar_set_value(bar, (int)obdSOC, LV_ANIM_ON);
            }
        }
        
        if (widgetCards[0][1] != NULL && obdCellVoltageMax > 0 && obdCellVoltageMin > 0) {
            // Cell Voltage widget update via dedicated labels
            if (lblCellDelta) {
                float deltaV = (obdCellVoltageMax - obdCellVoltageMin) * 1000.0f; // mV
                char buf[16];
                snprintf(buf, sizeof(buf), "%.0f mV", deltaV);
                lv_label_set_text(lblCellDelta, buf);
                // Color: green < 20mV, yellow < 50mV, red >= 50mV
                if (deltaV < 20.0f)
                    lv_obj_set_style_text_color(lblCellDelta, COLOR_GREEN, 0);
                else if (deltaV < 50.0f)
                    lv_obj_set_style_text_color(lblCellDelta, COLOR_YELLOW, 0);
                else
                    lv_obj_set_style_text_color(lblCellDelta, COLOR_RED, 0);
            }
            if (lblCellMin) {
                char buf[16];
                snprintf(buf, sizeof(buf), "%.3f", obdCellVoltageMin);
                lv_label_set_text(lblCellMin, buf);
            }
            if (lblCellMax) {
                char buf[16];
                snprintf(buf, sizeof(buf), "%.3f", obdCellVoltageMax);
                lv_label_set_text(lblCellMax, buf);
            }
        }
        
        if (widgetCards[0][2] != NULL && obdCabinTemp > -50) {
            lv_obj_t * lblValue = lv_obj_get_child(widgetCards[0][2], 1);
            if (lblValue) {
                char buf[16];
                snprintf(buf, sizeof(buf), "%.1f", obdCabinTemp);
                lv_label_set_text(lblValue, buf);
            }
        }

        if (widgetCards[0][3] != NULL && obdExtTemp > -50) {
            lv_obj_t * lblValue = lv_obj_get_child(widgetCards[0][3], 1);
            if (lblValue) {
                char buf[16];
                snprintf(buf, sizeof(buf), "%.1f", obdExtTemp);
                lv_label_set_text(lblValue, buf);
            }
        }

        // ── Page 2: HV Battery Voltage ──
        if (widgetCards[1][0] != NULL && obdHVBatVoltage > 0) {
            lv_obj_t * lblValue = lv_obj_get_child(widgetCards[1][0], 1);
            lv_obj_t * bar = lv_obj_get_child(widgetCards[1][0], 3);
            if (lblValue) {
                char buf[16];
                snprintf(buf, sizeof(buf), "%.0f", obdHVBatVoltage);
                lv_label_set_text(lblValue, buf);
            }
            if (bar) {
                int pct = (int)((obdHVBatVoltage - 300) / (400 - 300) * 100);
                if (pct < 0) pct = 0; if (pct > 100) pct = 100;
                lv_bar_set_value(bar, pct, LV_ANIM_ON);
            }
        }

        xSemaphoreGive(obdDataMutex);
    }
}

static void menuBtnClickCb(lv_event_t * e) {
    if (!UiSettings::isVisible()) {
        UiSettings::show();
    }
}
