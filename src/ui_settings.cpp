#include "ui_settings.h"
#include "BluetoothManager.h"
#include "lvgl.h"
#include "obd_globals.h"
#include "ui_boot.h"
#include "ui_dashboard.h"
#include <cstdio>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <esp_chip_info.h>
#include <esp_flash.h>

// ─── Internal state ───────────────────────────────────────
static lv_obj_t *settingsScreen = NULL;
static lv_obj_t *btMenuScreen = NULL;
static lv_obj_t *infoMenuScreen = NULL;
static lv_obj_t *rightPanel = NULL; // reusable right side
static bool _visible = false;
static bool _touchDebug = true; // Set to false to disable touch debug dots

// ─── Layout constants ─────────────────────────────────────
#define LEFT_PANEL_W 200
#define RIGHT_PANEL_W (SCREEN_W - LEFT_PANEL_W)
#define BTN_W (LEFT_PANEL_W - 24)
#define BTN_H 40
#define MAX_SCAN_ROWS 3

// ─── Forward declarations ─────────────────────────────────
static lv_obj_t *createTopBar(lv_obj_t *parent, const char *title);
static void backBtnCb(lv_event_t *e);
static void infoCardCb(lv_event_t *e);
static void btCardCb(lv_event_t *e);
static void scanBtnCb(lv_event_t *e);
static void statusBtnCb(lv_event_t *e);
static void clearRightPanel(void);
static void showScanResults(void);
static void showDeviceDetails(int devIdx);
static void showStatusView(void);
static void deviceListItemCb(lv_event_t *e);
static void connectBtnCb(lv_event_t *e);
static void saveToggleCb(lv_event_t *e);
static void deleteCfgBtnCb(lv_event_t *e);
static void backToListBtnCb(lv_event_t *e);
static void showInfoMenu(void);
static void drawSignalBars(lv_obj_t *parent, int rssi, int x, int y);

// Get same widget dimensions as dashboard
static void getCardDimensions(int *outW, int *outH) {
  int totalGap = WIDGET_GAP * (WIDGET_COUNT - 1) + WIDGET_MARGIN * 2;
  *outW = (SCREEN_W - totalGap) / WIDGET_COUNT;
  *outH = CONTENT_H - WIDGET_MARGIN * 2;
}

// RSSI color helper
static lv_color_t rssiColor(int rssi) {
  if (rssi > -50)
    return COLOR_GREEN;
  if (rssi > -70)
    return COLOR_YELLOW;
  return COLOR_RED;
}

// Draw signal strength bars (3 bars of increasing height)
static void drawSignalBars(lv_obj_t *parent, int rssi, int x, int y) {
  int bars = (rssi > -50) ? 3 : (rssi > -70) ? 2 : 1;
  lv_color_t col = rssiColor(rssi);
  int barW = 5, gap = 2;
  int heights[] = {8, 14, 20};
  for (int i = 0; i < 3; i++) {
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, barW, heights[i]);
    lv_obj_set_pos(bar, x + i * (barW + gap), y + (20 - heights[i]));
    lv_obj_set_style_radius(bar, 1, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_set_scrollbar_mode(bar, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);
    if (i < bars) {
      lv_obj_set_style_bg_color(bar, col, 0);
      lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    } else {
      lv_obj_set_style_bg_color(bar, COLOR_WIDGET_BORDER, 0);
      lv_obj_set_style_bg_opa(bar, LV_OPA_60, 0);
    }
  }
}

// ═══════════════════════════════════════════════════════════
//  Public API
// ═══════════════════════════════════════════════════════════

bool UiSettings::isVisible(void) { return _visible; }

void UiSettings::show(void) {
  if (_visible)
    return;
  _visible = true;

  settingsScreen = lv_obj_create(lv_screen_active());
  lv_obj_set_size(settingsScreen, SCREEN_W, SCREEN_H);
  lv_obj_align(settingsScreen, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_bg_color(settingsScreen, COLOR_BG, 0);
  lv_obj_set_style_bg_opa(settingsScreen, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(settingsScreen, 0, 0);
  lv_obj_set_style_radius(settingsScreen, 0, 0);
  lv_obj_set_style_pad_all(settingsScreen, 0, 0);
  lv_obj_set_scrollbar_mode(settingsScreen, LV_SCROLLBAR_MODE_OFF);
  lv_obj_clear_flag(settingsScreen, LV_OBJ_FLAG_SCROLLABLE);

  createTopBar(settingsScreen, "Settings");

  // ── Content: same card grid as dashboard ──
  int cardW, cardH;
  getCardDimensions(&cardW, &cardH);

  // Card 1: Info
  {
    int x = WIDGET_MARGIN;
    int y = TOP_BAR_H + WIDGET_MARGIN;

    lv_obj_t *card = lv_obj_create(settingsScreen);
    lv_obj_set_size(card, cardW, cardH);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_style_bg_color(card, COLOR_WIDGET_BG, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, COLOR_WIDGET_BORDER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);

    // Big centered icon
    lv_obj_t *icon = lv_label_create(card);
    lv_label_set_text(icon, LV_SYMBOL_LIST);
    lv_obj_set_style_text_color(icon, COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_28, 0);
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, -10);

    // Label below
    lv_obj_t *lbl = lv_label_create(card);
    lv_label_set_text(lbl, "Info");
    lv_obj_set_style_text_color(lbl, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 18);
    lv_obj_add_event_cb(card, infoCardCb, LV_EVENT_CLICKED, NULL);
  }

  // Card 2: Bluetooth
  {
    int x = WIDGET_MARGIN + (cardW + WIDGET_GAP);
    int y = TOP_BAR_H + WIDGET_MARGIN;

    lv_obj_t *card = lv_obj_create(settingsScreen);
    lv_obj_set_size(card, cardW, cardH);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_style_bg_color(card, COLOR_WIDGET_BG, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, COLOR_WIDGET_BORDER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *icon = lv_label_create(card);
    lv_label_set_text(icon, LV_SYMBOL_BLUETOOTH);
    lv_obj_set_style_text_color(icon, COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_28, 0);
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, -10);

    lv_obj_t *lbl = lv_label_create(card);
    lv_label_set_text(lbl, "Bluetooth");
    lv_obj_set_style_text_color(lbl, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 18);

    lv_obj_add_event_cb(card, btCardCb, LV_EVENT_CLICKED, NULL);
  }

  // Remaining empty cards (3 & 4) with "+" style
  for (int i = 2; i < WIDGET_COUNT; i++) {
    int x = WIDGET_MARGIN + i * (cardW + WIDGET_GAP);
    int y = TOP_BAR_H + WIDGET_MARGIN;

    lv_obj_t *slot = lv_obj_create(settingsScreen);
    lv_obj_set_size(slot, cardW, cardH);
    lv_obj_set_pos(slot, x, y);
    lv_obj_set_style_bg_opa(slot, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(slot, 8, 0);
    lv_obj_set_style_border_color(slot, COLOR_EMPTY_DASH, 0);
    lv_obj_set_style_border_width(slot, 2, 0);
    lv_obj_set_style_border_opa(slot, LV_OPA_70, 0);
    lv_obj_set_style_pad_all(slot, 0, 0);
    lv_obj_set_scrollbar_mode(slot, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(slot, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *plus = lv_label_create(slot);
    lv_label_set_text(plus, LV_SYMBOL_PLUS);
    lv_obj_set_style_text_color(plus, COLOR_PLUS, 0);
    lv_obj_set_style_text_font(plus, &lv_font_montserrat_28, 0);
    lv_obj_center(plus);
  }
}

void UiSettings::hide(void) {
  if (!_visible)
    return;
  _visible = false;
  if (btMenuScreen) {
    lv_obj_delete(btMenuScreen);
    btMenuScreen = NULL;
  }
  if (infoMenuScreen) {
    lv_obj_delete(infoMenuScreen);
    infoMenuScreen = NULL;
  }
  if (settingsScreen) {
    lv_obj_delete(settingsScreen);
    settingsScreen = NULL;
  }
  rightPanel = NULL;
}

// ═══════════════════════════════════════════════════════════
//  Bluetooth Sub-Menu (Split Screen)
// ═══════════════════════════════════════════════════════════

void UiSettings::showBtMenu(void) {
  if (btMenuScreen) {
    lv_obj_delete(btMenuScreen);
    btMenuScreen = NULL;
  }
  rightPanel = NULL;

  btMenuScreen = lv_obj_create(lv_screen_active());
  lv_obj_set_size(btMenuScreen, SCREEN_W, SCREEN_H);
  lv_obj_align(btMenuScreen, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_bg_color(btMenuScreen, COLOR_BG, 0);
  lv_obj_set_style_bg_opa(btMenuScreen, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(btMenuScreen, 0, 0);
  lv_obj_set_style_radius(btMenuScreen, 0, 0);
  lv_obj_set_style_pad_all(btMenuScreen, 0, 0);
  lv_obj_set_scrollbar_mode(btMenuScreen, LV_SCROLLBAR_MODE_OFF);
  lv_obj_clear_flag(btMenuScreen, LV_OBJ_FLAG_SCROLLABLE);

  // ── Left panel ──
  lv_obj_t *leftPanel = lv_obj_create(btMenuScreen);
  lv_obj_set_size(leftPanel, LEFT_PANEL_W, SCREEN_H);
  lv_obj_align(leftPanel, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_bg_color(leftPanel, COLOR_TOP_BAR, 0);
  lv_obj_set_style_bg_opa(leftPanel, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(leftPanel, 0, 0);
  lv_obj_set_style_border_side(leftPanel, LV_BORDER_SIDE_RIGHT, 0);
  lv_obj_set_style_border_color(leftPanel, COLOR_WIDGET_BORDER, 0);
  lv_obj_set_style_border_width(leftPanel, 1, 0);
  lv_obj_set_style_radius(leftPanel, 0, 0);
  lv_obj_set_style_pad_all(leftPanel, 12, 0);
  lv_obj_set_scrollbar_mode(leftPanel, LV_SCROLLBAR_MODE_OFF);
  lv_obj_clear_flag(leftPanel, LV_OBJ_FLAG_SCROLLABLE);

  // Back arrow (bigger)
  lv_obj_t *btnBack = lv_button_create(leftPanel);
  lv_obj_set_size(btnBack, 48, 32);
  lv_obj_align(btnBack, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_bg_color(btnBack, COLOR_WIDGET_BG, 0);
  lv_obj_set_style_bg_opa(btnBack, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(btnBack, COLOR_WIDGET_BORDER, 0);
  lv_obj_set_style_border_width(btnBack, 1, 0);
  lv_obj_set_style_radius(btnBack, 4, 0);
  lv_obj_set_style_pad_all(btnBack, 0, 0);
  lv_obj_t *lblBack = lv_label_create(btnBack);
  lv_label_set_text(lblBack, LV_SYMBOL_LEFT);
  lv_obj_set_style_text_color(lblBack, COLOR_TEXT_SECONDARY, 0);
  lv_obj_set_style_text_font(lblBack, &lv_font_montserrat_20, 0);
  lv_obj_center(lblBack);
  lv_obj_add_event_cb(btnBack, backBtnCb, LV_EVENT_CLICKED, NULL);

  // "Bluetooth" title (next to back button)
  lv_obj_t *lblBtTitle = lv_label_create(leftPanel);
  lv_label_set_text(lblBtTitle, "Bluetooth");
  lv_obj_set_style_text_color(lblBtTitle, COLOR_TEXT_PRIMARY, 0);
  lv_obj_set_style_text_font(lblBtTitle, &lv_font_montserrat_16, 0);
  lv_obj_align_to(lblBtTitle, btnBack, LV_ALIGN_OUT_RIGHT_MID, 8, 0);

  // STATUS button (bottom-most)
  lv_obj_t *btnStatus = lv_button_create(leftPanel);
  lv_obj_set_size(btnStatus, BTN_W, BTN_H);
  lv_obj_align(btnStatus, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  lv_obj_set_style_bg_color(btnStatus, COLOR_WIDGET_BG, 0);
  lv_obj_set_style_bg_opa(btnStatus, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(btnStatus, COLOR_WIDGET_BORDER, 0);
  lv_obj_set_style_border_width(btnStatus, 1, 0);
  lv_obj_set_style_radius(btnStatus, 6, 0);
  lv_obj_t *lblStatus = lv_label_create(btnStatus);
  lv_label_set_text(lblStatus, LV_SYMBOL_EYE_OPEN " STATUS");
  lv_obj_set_style_text_color(lblStatus, COLOR_TEXT_PRIMARY, 0);
  lv_obj_set_style_text_font(lblStatus, &lv_font_montserrat_16, 0);
  lv_obj_center(lblStatus);
  lv_obj_add_event_cb(btnStatus, statusBtnCb, LV_EVENT_CLICKED, NULL);

  // SCAN button (above STATUS)
  lv_obj_t *btnScan = lv_button_create(leftPanel);
  lv_obj_set_size(btnScan, BTN_W, BTN_H);
  lv_obj_align(btnScan, LV_ALIGN_BOTTOM_LEFT, 0, -(BTN_H + 8));
  lv_obj_set_style_bg_color(btnScan, COLOR_ACCENT, 0);
  lv_obj_set_style_bg_opa(btnScan, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(btnScan, 6, 0);
  lv_obj_t *lblScan = lv_label_create(btnScan);
  lv_label_set_text(lblScan, LV_SYMBOL_REFRESH " SCAN");
  lv_obj_set_style_text_color(lblScan, COLOR_TEXT_PRIMARY, 0);
  lv_obj_set_style_text_font(lblScan, &lv_font_montserrat_16, 0);
  lv_obj_center(lblScan);
  lv_obj_add_event_cb(btnScan, scanBtnCb, LV_EVENT_CLICKED, NULL);

  // ── Right panel (empty, filled by SCAN or STATUS) ──
  rightPanel = lv_obj_create(btMenuScreen);
  lv_obj_set_size(rightPanel, RIGHT_PANEL_W, SCREEN_H);
  lv_obj_align(rightPanel, LV_ALIGN_TOP_LEFT, LEFT_PANEL_W, 0);
  lv_obj_set_style_bg_color(rightPanel, COLOR_BG, 0);
  lv_obj_set_style_bg_opa(rightPanel, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(rightPanel, 0, 0);
  lv_obj_set_style_radius(rightPanel, 0, 0);
  lv_obj_set_style_pad_all(rightPanel, 10, 0);
  lv_obj_set_scrollbar_mode(rightPanel, LV_SCROLLBAR_MODE_OFF);
  lv_obj_clear_flag(rightPanel, LV_OBJ_FLAG_SCROLLABLE);

  // Show a hint
  lv_obj_t *hint = lv_label_create(rightPanel);
  lv_label_set_text(hint, "Select SCAN or STATUS");
  lv_obj_set_style_text_color(hint, COLOR_TEXT_SECONDARY, 0);
  lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
  lv_obj_center(hint);
}

// ═══════════════════════════════════════════════════════════
//  Info Sub-Menu
// ═══════════════════════════════════════════════════════════

static void showInfoMenu(void) {
  if (infoMenuScreen) {
    lv_obj_delete(infoMenuScreen);
    infoMenuScreen = NULL;
  }

  infoMenuScreen = lv_obj_create(lv_screen_active());
  lv_obj_set_size(infoMenuScreen, SCREEN_W, SCREEN_H);
  lv_obj_align(infoMenuScreen, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_bg_color(infoMenuScreen, COLOR_BG, 0);
  lv_obj_set_style_bg_opa(infoMenuScreen, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(infoMenuScreen, 0, 0);
  lv_obj_set_style_pad_all(infoMenuScreen, 0, 0);
  lv_obj_clear_flag(infoMenuScreen, LV_OBJ_FLAG_SCROLLABLE);

  // Top Bar (internal)
  lv_obj_t *bar = lv_obj_create(infoMenuScreen);
  lv_obj_set_size(bar, SCREEN_W, TOP_BAR_H);
  lv_obj_set_style_bg_color(bar, COLOR_TOP_BAR, 0);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(bar, 0, 0);
  lv_obj_set_style_pad_all(bar, 0, 0);
  lv_obj_set_style_pad_left(bar, 8, 0);
  lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *btnBack = lv_button_create(bar);
  lv_obj_set_size(btnBack, 48, 32);
  lv_obj_align(btnBack, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_set_style_bg_color(btnBack, COLOR_WIDGET_BG, 0);
  lv_obj_set_style_bg_opa(btnBack, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(btnBack, COLOR_WIDGET_BORDER, 0);
  lv_obj_set_style_border_width(btnBack, 1, 0);
  lv_obj_set_style_radius(btnBack, 4, 0);
  lv_obj_t *lblBack = lv_label_create(btnBack);
  lv_label_set_text(lblBack, LV_SYMBOL_LEFT);
  lv_obj_set_style_text_font(lblBack, &lv_font_montserrat_20, 0);
  lv_obj_center(lblBack);
  lv_obj_add_event_cb(btnBack, backBtnCb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *lblTitle = lv_label_create(bar);
  lv_label_set_text(lblTitle, "System Info");
  lv_obj_set_style_text_color(lblTitle, COLOR_CYAN, 0);
  lv_obj_set_style_text_font(lblTitle, &lv_font_montserrat_20, 0);
  lv_obj_align(lblTitle, LV_ALIGN_CENTER, 0, 0);

  // Content
  lv_obj_t *cont = lv_obj_create(infoMenuScreen);
  lv_obj_set_size(cont, SCREEN_W, SCREEN_H - TOP_BAR_H);
  lv_obj_align(cont, LV_ALIGN_TOP_LEFT, 0, TOP_BAR_H);
  lv_obj_set_style_bg_opa(cont, 0, 0);
  lv_obj_set_style_border_width(cont, 0, 0);
  lv_obj_set_style_pad_all(cont, 15, 0);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(cont, 8, 0);

  // Metrics gathering
  esp_chip_info_t chip_info;
  esp_chip_info(&chip_info);
  uint32_t flash_size = 0;
  esp_flash_get_size(NULL, &flash_size);

  char buf[256];
  
  // Row: Version
  {
    lv_obj_t *r = lv_label_create(cont);
    snprintf(buf, sizeof(buf), "Software Version: #00ffff %s#", ZOEYEE_VERSION);
    lv_label_set_text(r, buf);
    lv_label_set_recolor(r, true);
    lv_obj_set_style_text_font(r, &lv_font_montserrat_16, 0);
  }

  // Row: CPU
  {
    lv_obj_t *r = lv_label_create(cont);
    snprintf(buf, sizeof(buf), "CPU: ESP32-S3 (%d cores) @ %d MHz", chip_info.cores, getCpuFrequencyMhz());
    lv_label_set_text(r, buf);
    lv_obj_set_style_text_font(r, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(r, COLOR_TEXT_SECONDARY, 0);
  }

  // Row: Heap
  {
    lv_obj_t *r = lv_label_create(cont);
    uint32_t free_heap = esp_get_free_heap_size();
    uint32_t min_heap = esp_get_minimum_free_heap_size();
    snprintf(buf, sizeof(buf), "Free Heap: %u KB (min %u KB)", free_heap / 1024, min_heap / 1024);
    lv_label_set_text(r, buf);
    lv_obj_set_style_text_font(r, &lv_font_montserrat_14, 0);
  }

  // Row: PSRAM
  if (psramFound()) {
    lv_obj_t *r = lv_label_create(cont);
    uint32_t total_psram = ESP.getPsramSize();
    uint32_t free_psram = ESP.getFreePsram();
    snprintf(buf, sizeof(buf), "PSRAM: %u / %u KB free", free_psram / 1024, total_psram / 1024);
    lv_label_set_text(r, buf);
    lv_obj_set_style_text_font(r, &lv_font_montserrat_14, 0);
  }

  // Row: Flash
  {
    lv_obj_t *r = lv_label_create(cont);
    snprintf(buf, sizeof(buf), "Flash Size: %u MB", flash_size / (1024 * 1024));
    lv_label_set_text(r, buf);
    lv_obj_set_style_text_font(r, &lv_font_montserrat_14, 0);
  }

  // Row: Uptime
  {
    lv_obj_t *r = lv_label_create(cont);
    uint32_t sec = millis() / 1000;
    snprintf(buf, sizeof(buf), "Uptime: %u min", sec / 60);
    lv_label_set_text(r, buf);
    lv_obj_set_style_text_font(r, &lv_font_montserrat_14, 0);
  }
}

// ═══════════════════════════════════════════════════════════
//  Right Panel: Clear
// ═══════════════════════════════════════════════════════════

static void clearRightPanel(void) {
  if (rightPanel) {
    lv_obj_delete(rightPanel);
    rightPanel = NULL;
  }
  rightPanel = lv_obj_create(btMenuScreen);
  lv_obj_set_size(rightPanel, RIGHT_PANEL_W, SCREEN_H);
  lv_obj_align(rightPanel, LV_ALIGN_TOP_LEFT, LEFT_PANEL_W, 0);
  lv_obj_set_style_bg_color(rightPanel, COLOR_BG, 0);
  lv_obj_set_style_bg_opa(rightPanel, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(rightPanel, 0, 0);
  lv_obj_set_style_radius(rightPanel, 0, 0);
  lv_obj_set_style_pad_all(rightPanel, 10, 0);
  lv_obj_set_scrollbar_mode(rightPanel, LV_SCROLLBAR_MODE_OFF);
  lv_obj_clear_flag(rightPanel, LV_OBJ_FLAG_SCROLLABLE);
}

// ═══════════════════════════════════════════════════════════
//  Right Panel: Scan Results (device list)
// ═══════════════════════════════════════════════════════════

static void showScanResults(void) {
  clearRightPanel();

  int devCount = 0;
  if (xSemaphoreTake(obdDataMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
    devCount = btTotalDevices;
    xSemaphoreGive(obdDataMutex);
  }

  if (devCount == 0) {
    lv_obj_t *lbl = lv_label_create(rightPanel);
    lv_label_set_text(lbl, "No devices found");
    lv_obj_set_style_text_color(lbl, COLOR_RED, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl);
    return;
  }

  // Make right panel scrollable vertically for overflow
  lv_obj_set_scroll_dir(rightPanel, LV_DIR_VER);
  lv_obj_add_flag(rightPanel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(rightPanel, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(rightPanel, 6, 0);

  int rowH =
      (SCREEN_H - 20) / MAX_SCAN_ROWS; // Divide available height into 3 rows
  if (rowH > 50)
    rowH = 50;

  for (int i = 0; i < devCount; i++) {
    String devName = "";
    int devRssi = -99;
    if (xSemaphoreTake(obdDataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      devName = btDevices[i].name;
      devRssi = btDevices[i].rssi;
      xSemaphoreGive(obdDataMutex);
    }
    if (devName.length() == 0)
      devName = "(unknown)";

    lv_obj_t *row = lv_obj_create(rightPanel);
    lv_obj_set_size(row, RIGHT_PANEL_W - 24, rowH);
    lv_obj_set_style_bg_color(row, COLOR_WIDGET_BG, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(row, COLOR_WIDGET_BORDER, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_radius(row, 6, 0);
    lv_obj_set_style_pad_left(row, 10, 0);
    lv_obj_set_style_pad_right(row, 10, 0);
    lv_obj_set_style_pad_top(row, 0, 0);
    lv_obj_set_style_pad_bottom(row, 0, 0);
    lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);

    // Signal bars (left side)
    drawSignalBars(row, devRssi, 0, (rowH - 20) / 2);

    // Device name (bigger font)
    lv_obj_t *lblName = lv_label_create(row);
    lv_label_set_text(lblName, devName.c_str());
    lv_obj_set_style_text_color(lblName, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(lblName, &lv_font_montserrat_16, 0);
    lv_obj_set_width(lblName, RIGHT_PANEL_W - 80);
    lv_label_set_long_mode(lblName, LV_LABEL_LONG_CLIP);
    lv_obj_align(lblName, LV_ALIGN_LEFT_MID, 30, 0);

    lv_obj_set_user_data(row, (void *)(intptr_t)i);
    lv_obj_add_event_cb(row, deviceListItemCb, LV_EVENT_CLICKED, NULL);
  }
}

// ═══════════════════════════════════════════════════════════
//  Right Panel: Device Details (after selecting from list)
// ═══════════════════════════════════════════════════════════

static void showDeviceDetails(int devIdx) {
  clearRightPanel();

  String devName = "";
  String devAddr = "";
  int devRssi = -99;
  if (xSemaphoreTake(obdDataMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
    if (devIdx >= 0 && devIdx < btTotalDevices) {
      devName = btDevices[devIdx].name;
      devAddr = btDevices[devIdx].address;
      devRssi = btDevices[devIdx].rssi;
    }
    xSemaphoreGive(obdDataMutex);
  }
  if (devName.length() == 0)
    devName = "(unknown)";

  // Row 1: Device name
  lv_obj_t *lblName = lv_label_create(rightPanel);
  lv_label_set_text(lblName, devName.c_str());
  lv_obj_set_style_text_color(lblName, COLOR_TEXT_PRIMARY, 0);
  lv_obj_set_style_text_font(lblName, &lv_font_montserrat_20, 0);
  lv_obj_align(lblName, LV_ALIGN_TOP_LEFT, 0, 6);

  // Row 2: MAC address
  lv_obj_t *lblMAC = lv_label_create(rightPanel);
  char macBuf[32];
  snprintf(macBuf, sizeof(macBuf), "MAC: %s", devAddr.c_str());
  lv_label_set_text(lblMAC, macBuf);
  lv_obj_set_style_text_color(lblMAC, COLOR_TEXT_SECONDARY, 0);
  lv_obj_set_style_text_font(lblMAC, &lv_font_montserrat_16, 0);
  lv_obj_align(lblMAC, LV_ALIGN_TOP_LEFT, 0, 34);

  // Row 3: Signal bars + dBm value
  drawSignalBars(rightPanel, devRssi, 0, 60);

  lv_obj_t *lblRssiVal = lv_label_create(rightPanel);
  char rssiBuf[24];
  snprintf(rssiBuf, sizeof(rssiBuf), "%d dBm", devRssi);
  lv_label_set_text(lblRssiVal, rssiBuf);
  lv_obj_set_style_text_color(lblRssiVal, rssiColor(devRssi), 0);
  lv_obj_set_style_text_font(lblRssiVal, &lv_font_montserrat_14, 0);
  lv_obj_align(lblRssiVal, LV_ALIGN_TOP_LEFT, 30, 62);

  // Bottom row: proportional buttons
  int btnGap = 10;
  int totalW = RIGHT_PANEL_W - 20; // panel padding
  int btnW = (totalW - btnGap) / 2;

  // Back button (left)
  lv_obj_t *btnBackList = lv_button_create(rightPanel);
  lv_obj_set_size(btnBackList, btnW, BTN_H);
  lv_obj_align(btnBackList, LV_ALIGN_BOTTOM_LEFT, 0, -4);
  lv_obj_set_style_bg_color(btnBackList, COLOR_WIDGET_BG, 0);
  lv_obj_set_style_bg_opa(btnBackList, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(btnBackList, COLOR_WIDGET_BORDER, 0);
  lv_obj_set_style_border_width(btnBackList, 1, 0);
  lv_obj_set_style_radius(btnBackList, 8, 0);
  lv_obj_t *lblBackList = lv_label_create(btnBackList);
  lv_label_set_text(lblBackList, LV_SYMBOL_LEFT " Back");
  lv_obj_set_style_text_color(lblBackList, COLOR_TEXT_PRIMARY, 0);
  lv_obj_set_style_text_font(lblBackList, &lv_font_montserrat_16, 0);
  lv_obj_center(lblBackList);
  lv_obj_add_event_cb(btnBackList, backToListBtnCb, LV_EVENT_CLICKED, NULL);

  // Connect button (right)
  lv_obj_t *btnConnect = lv_button_create(rightPanel);
  lv_obj_set_size(btnConnect, btnW, BTN_H);
  lv_obj_align(btnConnect, LV_ALIGN_BOTTOM_RIGHT, 0, -4);
  lv_obj_set_style_bg_color(btnConnect, COLOR_GREEN, 0);
  lv_obj_set_style_bg_opa(btnConnect, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(btnConnect, 8, 0);
  lv_obj_t *lblBtn = lv_label_create(btnConnect);
  lv_label_set_text(lblBtn, LV_SYMBOL_BLUETOOTH " Connect");
  lv_obj_set_style_text_color(lblBtn, COLOR_TEXT_PRIMARY, 0);
  lv_obj_set_style_text_font(lblBtn, &lv_font_montserrat_16, 0);
  lv_obj_center(lblBtn);

  lv_obj_set_user_data(btnConnect, (void *)(intptr_t)devIdx);
  lv_obj_add_event_cb(btnConnect, connectBtnCb, LV_EVENT_CLICKED, NULL);
}

// ═══════════════════════════════════════════════════════════
//  Right Panel: Status View
// ═══════════════════════════════════════════════════════════

static void showStatusView(void) {
  clearRightPanel();

  bool connected = false;
  String curMAC = "";
  String curName = "";
  if (xSemaphoreTake(obdDataMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
    connected = isBluetoothConnected;
    curMAC = btTargetMAC;
    curName = btTargetName;
    xSemaphoreGive(obdDataMutex);
  }

  // Check saved config early (used by both delete button and auto-reconnect toggle)
  preferences.begin("zoeyee", true);
  String savedMAC = preferences.getString("bt_mac", "");
  preferences.end();
  bool hasSaved = (savedMAC.length() > 0);

  int yOffset = 4;
  
  // Status label
  lv_obj_t *lblStatus = lv_label_create(rightPanel);
  lv_label_set_text(lblStatus, connected ? "Connected" : "Not connected");
  lv_obj_set_style_text_color(lblStatus, connected ? COLOR_GREEN : COLOR_RED,
                              0);
  lv_obj_set_style_text_font(lblStatus, &lv_font_montserrat_20, 0);
  lv_obj_align(lblStatus, LV_ALIGN_TOP_LEFT, 0, yOffset);
  yOffset += 28;

  if (curName.length() > 0) {
    lv_obj_t *lblDev = lv_label_create(rightPanel);
    char buf[64];
    snprintf(buf, sizeof(buf), "%s\n%s", curName.c_str(), curMAC.c_str());
    lv_label_set_text(lblDev, buf);
    lv_obj_set_style_text_color(lblDev, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(lblDev, &lv_font_montserrat_16, 0);
    lv_obj_align(lblDev, LV_ALIGN_TOP_LEFT, 0, yOffset);
    yOffset += 44; // 2 lines of text
    
    if (connected) {
      int rssi = BluetoothManager::getConnectedRSSI();
      drawSignalBars(rightPanel, rssi, 0, yOffset);
      
      lv_obj_t *lblRssi = lv_label_create(rightPanel);
      char rssiBuf[32];
      snprintf(rssiBuf, sizeof(rssiBuf), "%d dBm", rssi);
      lv_label_set_text(lblRssi, rssiBuf);
      lv_obj_set_style_text_color(lblRssi, rssiColor(rssi), 0);
      lv_obj_set_style_text_font(lblRssi, &lv_font_montserrat_16, 0);
      lv_obj_align(lblRssi, LV_ALIGN_TOP_LEFT, 30, yOffset + 2);
      
      // Delete button on the right third of the RSSI row
      if (hasSaved) {
        lv_obj_t *btnDel = lv_button_create(rightPanel);
        lv_obj_set_size(btnDel, 120, 28);
        lv_obj_align(btnDel, LV_ALIGN_TOP_RIGHT, 0, yOffset);
        lv_obj_set_style_bg_color(btnDel, COLOR_RED, 0);
        lv_obj_set_style_bg_opa(btnDel, LV_OPA_80, 0);
        lv_obj_set_style_radius(btnDel, 6, 0);

        lv_obj_t *lblDel = lv_label_create(btnDel);
        lv_label_set_text(lblDel, LV_SYMBOL_TRASH " Delete");
        lv_obj_set_style_text_color(lblDel, COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_style_text_font(lblDel, &lv_font_montserrat_14, 0);
        lv_obj_center(lblDel);
        lv_obj_add_event_cb(btnDel, deleteCfgBtnCb, LV_EVENT_CLICKED, NULL);
      }
      
      yOffset += 28;
    }
  } else {
    yOffset += 10;
  }

  // Save toggle (switch)
  lv_obj_t *lblSave = lv_label_create(rightPanel);
  lv_label_set_text(lblSave, "Auto-reconnect:");
  lv_obj_set_style_text_color(lblSave, COLOR_TEXT_PRIMARY, 0);
  lv_obj_set_style_text_font(lblSave, &lv_font_montserrat_16, 0);
  lv_obj_align(lblSave, LV_ALIGN_TOP_LEFT, 0, yOffset + 4);

  lv_obj_t *sw = lv_switch_create(rightPanel);
  lv_obj_set_size(sw, 44, 22);
  lv_obj_align(sw, LV_ALIGN_TOP_LEFT, 150, yOffset + 2);
  if (hasSaved)
    lv_obj_add_state(sw, LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(sw, COLOR_WIDGET_BORDER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(sw, COLOR_GREEN,
                            (lv_style_selector_t)LV_PART_INDICATOR | (lv_style_selector_t)LV_STATE_CHECKED);
  lv_obj_add_event_cb(sw, saveToggleCb, LV_EVENT_VALUE_CHANGED, NULL);

  // If not connected but has saved config, show delete button at the bottom
  if (!connected && hasSaved) {
    lv_obj_t *btnDel = lv_button_create(rightPanel);
    lv_obj_set_size(btnDel, 140, 32);
    lv_obj_align(btnDel, LV_ALIGN_BOTTOM_LEFT, 0, -4);
    lv_obj_set_style_bg_color(btnDel, COLOR_RED, 0);
    lv_obj_set_style_bg_opa(btnDel, LV_OPA_80, 0);
    lv_obj_set_style_radius(btnDel, 6, 0);

    lv_obj_t *lblDel = lv_label_create(btnDel);
    lv_label_set_text(lblDel, LV_SYMBOL_TRASH " Delete");
    lv_obj_set_style_text_color(lblDel, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(lblDel, &lv_font_montserrat_14, 0);
    lv_obj_center(lblDel);
    lv_obj_add_event_cb(btnDel, deleteCfgBtnCb, LV_EVENT_CLICKED, NULL);
  }
}

// ═══════════════════════════════════════════════════════════
//  Internal Helpers
// ═══════════════════════════════════════════════════════════

static lv_obj_t *createTopBar(lv_obj_t *parent, const char *title) {
  lv_obj_t *bar = lv_obj_create(parent);
  lv_obj_set_size(bar, SCREEN_W, TOP_BAR_H);
  lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_bg_color(bar, COLOR_TOP_BAR, 0);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(bar, 0, 0);
  lv_obj_set_style_radius(bar, 0, 0);
  lv_obj_set_style_pad_all(bar, 0, 0);
  lv_obj_set_style_pad_left(bar, 8, 0);
  lv_obj_set_scrollbar_mode(bar, LV_SCROLLBAR_MODE_OFF);
  lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

  // Back button
  lv_obj_t *btnBack = lv_button_create(bar);
  lv_obj_set_size(btnBack, 48, 26);
  lv_obj_align(btnBack, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_set_style_bg_color(btnBack, COLOR_WIDGET_BG, 0);
  lv_obj_set_style_bg_opa(btnBack, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(btnBack, COLOR_WIDGET_BORDER, 0);
  lv_obj_set_style_border_width(btnBack, 1, 0);
  lv_obj_set_style_radius(btnBack, 4, 0);
  lv_obj_set_style_pad_all(btnBack, 0, 0);
  lv_obj_t *lblBack = lv_label_create(btnBack);
  lv_label_set_text(lblBack, LV_SYMBOL_LEFT);
  lv_obj_set_style_text_color(lblBack, COLOR_TEXT_SECONDARY, 0);
  lv_obj_set_style_text_font(lblBack, &lv_font_montserrat_16, 0);
  lv_obj_center(lblBack);
  lv_obj_add_event_cb(btnBack, backBtnCb, LV_EVENT_CLICKED, NULL);

  // Title
  lv_obj_t *lblTitle = lv_label_create(bar);
  lv_label_set_text(lblTitle, title);
  lv_obj_set_style_text_color(lblTitle, COLOR_CYAN, 0);
  lv_obj_set_style_text_font(lblTitle, &lv_font_montserrat_16, 0);
  lv_obj_align(lblTitle, LV_ALIGN_CENTER, 0, 0);

  return bar;
}

// ═══════════════════════════════════════════════════════════
//  Callbacks
// ═══════════════════════════════════════════════════════════

static void backBtnCb(lv_event_t *e) {
  if (btMenuScreen) {
    lv_obj_delete(btMenuScreen);
    btMenuScreen = NULL;
    rightPanel = NULL;
    return;
  }
  if (infoMenuScreen) {
    lv_obj_delete(infoMenuScreen);
    infoMenuScreen = NULL;
    return;
  }
  UiSettings::hide();
}

static void infoCardCb(lv_event_t *e) { showInfoMenu(); }

static void btCardCb(lv_event_t *e) { UiSettings::showBtMenu(); }

static void scanBtnCb(lv_event_t *e) {
  clearRightPanel();

  // Refresh icon + "Scanning..." text
  lv_obj_t *lblScanIcon = lv_label_create(rightPanel);
  lv_label_set_text(lblScanIcon, LV_SYMBOL_REFRESH);
  lv_obj_set_style_text_color(lblScanIcon, COLOR_YELLOW, 0);
  lv_obj_set_style_text_font(lblScanIcon, &lv_font_montserrat_20, 0);
  lv_obj_align(lblScanIcon, LV_ALIGN_CENTER, -60, 0);

  lv_obj_t *lblScan = lv_label_create(rightPanel);
  lv_label_set_text(lblScan, "Scanning...");
  lv_obj_set_style_text_color(lblScan, COLOR_YELLOW, 0);
  lv_obj_set_style_text_font(lblScan, &lv_font_montserrat_20, 0);
  lv_obj_align(lblScan, LV_ALIGN_CENTER, 10, 0);
  lv_refr_now(NULL);

  // Run BLE scan (~5s blocking)
  BluetoothManager::runBLEScan();

  // Show results
  showScanResults();
}

static void statusBtnCb(lv_event_t *e) { showStatusView(); }

static void deviceListItemCb(lv_event_t *e) {
  lv_obj_t *row = (lv_obj_t *)lv_event_get_target(e);
  int devIdx = (int)(intptr_t)lv_obj_get_user_data(row);
  showDeviceDetails(devIdx);
}

static void backToListBtnCb(lv_event_t *e) { showScanResults(); }

static void connectBtnCb(lv_event_t *e) {
  lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
  int devIdx = (int)(intptr_t)lv_obj_get_user_data(btn);

  String mac = "";
  String name = "";
  if (xSemaphoreTake(obdDataMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
    if (devIdx >= 0 && devIdx < btTotalDevices) {
      mac = btDevices[devIdx].address;
      name = btDevices[devIdx].name;
      btTargetMAC = mac;
      btTargetName = name;
    }
    xSemaphoreGive(obdDataMutex);
  }
  if (mac.length() == 0)
    return;

  // Save to NVS
  preferences.begin("zoeyee", false);
  preferences.putString("bt_mac", mac);
  preferences.putString("bt_name", name);
  preferences.end();

  // Update button UI
  lv_obj_t *lblBtn = lv_obj_get_child(btn, 0);
  if (lblBtn) {
    lv_label_set_text(lblBtn, "Connecting...");
    lv_obj_set_style_text_color(lblBtn, COLOR_YELLOW, 0);
  }
  lv_obj_set_style_bg_color(btn, COLOR_WIDGET_BG, 0);
  lv_refr_now(NULL);

  Serial.printf("[Settings] Connecting to %s [%s]\n", name.c_str(),
                mac.c_str());
  bool ok = BluetoothManager::connectByMAC(mac);

  // Also check actual connection state (onConnect callback may have set it)
  if (!ok) {
    if (xSemaphoreTake(obdDataMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
      ok = isBluetoothConnected;
      xSemaphoreGive(obdDataMutex);
    }
  }

  if (ok) {
    Serial.println("[Settings] BT Connected!");
    if (lblBtn) {
      lv_label_set_text(lblBtn, LV_SYMBOL_OK " Connected");
      lv_obj_set_style_text_color(lblBtn, COLOR_GREEN, 0);
    }
    lv_obj_set_style_bg_color(btn, COLOR_WIDGET_BG, 0);
  } else {
    if (lblBtn) {
      lv_label_set_text(lblBtn, LV_SYMBOL_CLOSE " Failed");
      lv_obj_set_style_text_color(lblBtn, COLOR_RED, 0);
    }
  }
}

static void saveToggleCb(lv_event_t *e) {
  lv_obj_t *sw = (lv_obj_t *)lv_event_get_target(e);
  bool checked = lv_obj_has_state(sw, LV_STATE_CHECKED);

  if (checked) {
    // Save current target
    String mac = "";
    String name = "";
    if (xSemaphoreTake(obdDataMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
      mac = btTargetMAC;
      name = btTargetName;
      xSemaphoreGive(obdDataMutex);
    }
    if (mac.length() > 0) {
      preferences.begin("zoeyee", false);
      preferences.putString("bt_mac", mac);
      preferences.putString("bt_name", name);
      preferences.end();
      Serial.printf("[Settings] Saved config: %s [%s]\n", name.c_str(),
                    mac.c_str());
    }
  } else {
    // Clear saved
    preferences.begin("zoeyee", false);
    preferences.remove("bt_mac");
    preferences.remove("bt_name");
    preferences.end();
    Serial.println("[Settings] Cleared saved BT config");
  }
}

static void deleteCfgBtnCb(lv_event_t *e) {
  preferences.begin("zoeyee", false);
  preferences.remove("bt_mac");
  preferences.remove("bt_name");
  preferences.end();

  if (xSemaphoreTake(obdDataMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
    btTargetMAC = "";
    btTargetName = "";
    xSemaphoreGive(obdDataMutex);
  }

  Serial.println("[Settings] Deleted saved BT config + cleared target");
  // Refresh the status view
  showStatusView();
}
