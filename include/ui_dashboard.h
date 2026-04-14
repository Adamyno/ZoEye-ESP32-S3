#pragma once

#include "lvgl.h"

// Screen dimensions in landscape mode
#define SCREEN_W 640
#define SCREEN_H 172

// Layout constants
#define TOP_BAR_H      28
#define CONTENT_H      (SCREEN_H - TOP_BAR_H)
#define WIDGET_COUNT   4
#define MAX_PAGES      6
#define WIDGET_GAP     6
#define WIDGET_MARGIN  6

// Colors
#define COLOR_BG          lv_color_hex(0x0D1117)  // Very dark blue-gray
#define COLOR_TOP_BAR     lv_color_hex(0x161B22)  // Slightly lighter
#define COLOR_WIDGET_BG   lv_color_hex(0x1C2128)  // Widget card background
#define COLOR_WIDGET_BORDER lv_color_hex(0x30363D) // Widget card border
#define COLOR_EMPTY_DASH  lv_color_hex(0x30363D)  // Dashed empty border
#define COLOR_PLUS        lv_color_hex(0x484F58)  // Plus icon color
#define COLOR_ACCENT      lv_color_hex(0x58A6FF)  // Blue accent
#define COLOR_TEXT_PRIMARY lv_color_hex(0xF0F6FC)  // White text
#define COLOR_TEXT_SECONDARY lv_color_hex(0x8B949E) // Gray text
#define COLOR_GREEN       lv_color_hex(0x3FB950)  // Status green
#define COLOR_RED         lv_color_hex(0xF85149)  // Status red
#define COLOR_YELLOW      lv_color_hex(0xD29922)  // Warning yellow
#define COLOR_CYAN        lv_color_hex(0x00E5FF)  // ZoEyee cyan

namespace UiDashboard {
    void init(void);
    void setPage(int page);
    int  getPageCount(void);
    int  getCurrentPage(void);
    
    // Top bar updates
    void setBluetoothStatus(bool connected);
    void setWifiStatus(bool connected, int rssi);
    void setCanStatus(bool active);
}
