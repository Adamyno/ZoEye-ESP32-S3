#include "ui_dashboard.h"
#include "lvgl.h"
#include "lvgl_port.h"
#include <cstdio>
#include <cmath>
#include "obd_globals.h"
#include "ui_settings.h"

// ─── Cell Voltage widget label pointers ──────────────────
static lv_obj_t * lblCellDelta  = NULL;
static lv_obj_t * lblCellMin    = NULL;
static lv_obj_t * lblCellMax    = NULL;

// ─── Battery Status card (SOH + SOC + BatTemp) ────────────
// Heart icon scanlines for SOH
#define HEART_MAX_ROWS 120
static int16_t heartLO[HEART_MAX_ROWS];
static int16_t heartLI[HEART_MAX_ROWS];
static int16_t heartRI[HEART_MAX_ROWS];
static int16_t heartRO[HEART_MAX_ROWS];
static int heartRows = 0;
static bool heartReady = false;
static lv_obj_t * sohHeartObj   = NULL;
static lv_obj_t * lblSohPct     = NULL;
static lv_obj_t * lblSohLabel   = NULL;

// Battery SOC icon
static lv_obj_t * socBattObj    = NULL;
static lv_obj_t * lblSocPct     = NULL;
static lv_obj_t * lblSocLabel   = NULL;

// Available Energy icon (lightning bolt)
static lv_obj_t * availEnergyObj   = NULL;
static lv_obj_t * lblAvailEnergy   = NULL;
static lv_obj_t * lblAvailEnergyLabel = NULL;

// ─── Cabin Widget (CabinTemp + Humidity + Climate Mode) ────────
static lv_obj_t * cabinThermoObj   = NULL;
static lv_obj_t * lblCabinTemp     = NULL;
static lv_obj_t * lblCabinTempLabel = NULL;

static lv_obj_t * cabinDropsObj    = NULL;
static lv_obj_t * lblHumidity      = NULL;
static lv_obj_t * lblHumidityLabel = NULL;

static lv_obj_t * climateIconObj   = NULL;
static lv_obj_t * lblClimateMode   = NULL;

// ─── Thermal Widget (Ext Temp + Bat Temp + Max Charge Power) ───
static lv_obj_t * extThermoObj     = NULL;
static lv_obj_t * lblExtTemp       = NULL;
static lv_obj_t * batThermoIconObj = NULL;
static lv_obj_t * lblBatTemp       = NULL;
static lv_obj_t * maxChargeLightObj = NULL;
static lv_obj_t * lblMaxCharge     = NULL;

static void initHeartScanlines(int w, int h) {
    heartRows = h;
    if (heartRows > HEART_MAX_ROWS) heartRows = HEART_MAX_ROWS;
    for (int i = 0; i < heartRows; i++) {
        heartLO[i] =  9999; heartLI[i] = -9999;
        heartRI[i] =  9999; heartRO[i] = -9999;
    }
    float scaleX = w / 34.0f;
    float scaleY = h / 30.0f;
    float offsetY = 13.0f;
    for (int i = 0; i <= 3000; i++) {
        float t = i * 6.28318530718f / 3000.0f;
        float s = sinf(t);
        float hx = 16.0f * s * s * s;
        float hy = -(13.0f * cosf(t) - 5.0f * cosf(2*t) - 2.0f * cosf(3*t) - cosf(4*t));
        int px = (int)(hx * scaleX);
        int py = (int)((hy + offsetY) * scaleY);
        if (py >= 0 && py < heartRows) {
            if (px <= 0) {
                if (px < heartLO[py]) heartLO[py] = px;
                if (px > heartLI[py]) heartLI[py] = px;
            }
            if (px >= 0) {
                if (px < heartRI[py]) heartRI[py] = px;
                if (px > heartRO[py]) heartRO[py] = px;
            }
        }
    }
    heartReady = true;
}

// ─── SOH Heart draw callback (fills proportionally to SOH%) ─────
static void sohHeartDrawCb(lv_event_t * e) {
    if (!heartReady) return;
    lv_obj_t * obj = (lv_obj_t *)lv_event_get_target(e);
    lv_layer_t * layer = lv_event_get_layer(e);
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);
    int cx = (coords.x1 + coords.x2) / 2;
    int top = coords.y1;

    lv_draw_rect_dsc_t rdsc;
    lv_draw_rect_dsc_init(&rdsc);
    rdsc.radius = 0;
    rdsc.border_width = 0;
    rdsc.bg_opa = LV_OPA_COVER;
    rdsc.bg_color = lv_color_hex(0xF85149); // Always red

    int py_cleft = (int)(8.0f * heartRows / 30.0f);

    for (int row = 0; row < heartRows; row++) {
        bool hasLeft  = (heartLO[row] < 9999 && heartLI[row] > -9999);
        bool hasRight = (heartRI[row] < 9999 && heartRO[row] > -9999);
        if (!hasLeft && !hasRight) continue;

        if (row > py_cleft) {
            lv_area_t a;
            a.y1 = top + row; a.y2 = top + row;
            if (hasLeft && hasRight) {
                a.x1 = cx + heartLO[row]; a.x2 = cx + heartRO[row];
            } else if (hasLeft) {
                a.x1 = cx + heartLO[row]; a.x2 = cx + heartLI[row];
            } else {
                a.x1 = cx + heartRI[row]; a.x2 = cx + heartRO[row];
            }
            lv_draw_rect(layer, &rdsc, &a);
        } else {
            if (hasLeft) {
                lv_area_t a;
                a.x1 = cx + heartLO[row]; a.x2 = cx + heartLI[row];
                a.y1 = top + row; a.y2 = top + row;
                lv_draw_rect(layer, &rdsc, &a);
            }
            if (hasRight) {
                lv_area_t a;
                a.x1 = cx + heartRI[row]; a.x2 = cx + heartRO[row];
                a.y1 = top + row; a.y2 = top + row;
                lv_draw_rect(layer, &rdsc, &a);
            }
        }
    }
}

// ─── SOC Battery draw callback (fills proportionally, color changes by level) ───
static lv_color_t getSocColor(float soc) {
    // 80-100%: blue (0x58A6FF)
    // 35-80%: green (0x3FB950)
    // 20-35%: blend green → orange
    // 0-20%: red (0xF85149)
    if (soc >= 80.0f) return lv_color_hex(0x58A6FF);
    if (soc >= 35.0f) return lv_color_hex(0x3FB950);
    if (soc >= 20.0f) {
        // Blend green → orange (0xD29922)
        float t = (soc - 20.0f) / 15.0f; // 0=orange, 1=green
        int r1 = 0xD2, g1 = 0x99, b1 = 0x22;
        int r2 = 0x3F, g2 = 0xB9, b2 = 0x50;
        int r = r1 + (int)((r2 - r1) * t);
        int g = g1 + (int)((g2 - g1) * t);
        int b = b1 + (int)((b2 - b1) * t);
        return lv_color_make(r, g, b);
    }
    return lv_color_hex(0xF85149);
}

static void socBattDrawCb(lv_event_t * e) {
    lv_obj_t * obj = (lv_obj_t *)lv_event_get_target(e);
    lv_layer_t * layer = lv_event_get_layer(e);
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);

    int objW = coords.x2 - coords.x1;
    int objH = coords.y2 - coords.y1;
    int cx = (coords.x1 + coords.x2) / 2;
    int cy = (coords.y1 + coords.y2) / 2;

    // Battery body: narrow portrait rectangle (~55% width of height)
    int bh = objH - 6;
    int bw = (int)(bh * 0.70f);
    if (bw > objW - 4) bw = objW - 4;
    int bx = cx - bw / 2;
    int by = cy - bh / 2 + 2;

    lv_draw_rect_dsc_t rdsc;
    lv_draw_rect_dsc_init(&rdsc);
    rdsc.bg_opa = LV_OPA_COVER;
    rdsc.border_width = 0;

    // Battery terminal cap (top center)
    int capW = bw / 3;
    if (capW < 4) capW = 4;
    lv_area_t capA;
    capA.x1 = cx - capW / 2; capA.x2 = cx + capW / 2;
    capA.y1 = by - 3; capA.y2 = by;
    rdsc.bg_color = lv_color_hex(0x8B949E);
    rdsc.radius = 2;
    lv_draw_rect(layer, &rdsc, &capA);

    // Battery outline (grey border)
    lv_area_t outerA;
    outerA.x1 = bx; outerA.x2 = bx + bw;
    outerA.y1 = by; outerA.y2 = by + bh;
    rdsc.bg_color = lv_color_hex(0x30363D);
    rdsc.radius = 3;
    lv_draw_rect(layer, &rdsc, &outerA);

    // Battery inner fill area
    int pad = 2;
    int innerX1 = bx + pad;
    int innerX2 = bx + bw - pad;
    int innerY1 = by + pad;
    int innerY2 = by + bh - pad;
    int innerH = innerY2 - innerY1;

    float soc = obdSOC;
    if (soc < 0) soc = 0;
    if (soc > 100) soc = 100;

    int fillH = (int)(soc / 100.0f * innerH);
    if (fillH > innerH) fillH = innerH;

    if (fillH > 0) {
        lv_area_t fillA;
        fillA.x1 = innerX1; fillA.x2 = innerX2;
        fillA.y1 = innerY2 - fillH; fillA.y2 = innerY2;
        rdsc.bg_color = getSocColor(soc);
        rdsc.radius = 1;
        lv_draw_rect(layer, &rdsc, &fillA);
    }
}

// ─── Available Energy draw callback (battery icon, same as SOC) ───
static void availEnergyDrawCb(lv_event_t * e) {
    lv_obj_t * obj = (lv_obj_t *)lv_event_get_target(e);
    lv_layer_t * layer = lv_event_get_layer(e);
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);

    int objW = coords.x2 - coords.x1;
    int objH = coords.y2 - coords.y1;
    int cx = (coords.x1 + coords.x2) / 2;
    int cy = (coords.y1 + coords.y2) / 2;

    // Battery body: narrow portrait rectangle (~55% width of height)
    int bh = objH - 6;
    int bw = (int)(bh * 0.70f);
    if (bw > objW - 4) bw = objW - 4;
    int bx = cx - bw / 2;
    int by = cy - bh / 2 + 2;

    lv_draw_rect_dsc_t rdsc;
    lv_draw_rect_dsc_init(&rdsc);
    rdsc.bg_opa = LV_OPA_COVER;
    rdsc.border_width = 0;

    // Battery terminal cap
    int capW = bw / 3;
    if (capW < 4) capW = 4;
    lv_area_t capA;
    capA.x1 = cx - capW / 2; capA.x2 = cx + capW / 2;
    capA.y1 = by - 3; capA.y2 = by;
    rdsc.bg_color = lv_color_hex(0x8B949E);
    rdsc.radius = 2;
    lv_draw_rect(layer, &rdsc, &capA);

    // Battery outline
    lv_area_t outerA;
    outerA.x1 = bx; outerA.x2 = bx + bw;
    outerA.y1 = by; outerA.y2 = by + bh;
    rdsc.bg_color = lv_color_hex(0x30363D);
    rdsc.radius = 3;
    lv_draw_rect(layer, &rdsc, &outerA);

    // Battery inner fill (mapped from kWh: 0-22 kWh = 0-100%)
    int pad = 2;
    int innerX1 = bx + pad;
    int innerX2 = bx + bw - pad;
    int innerY1 = by + pad;
    int innerY2 = by + bh - pad;
    int innerH = innerY2 - innerY1;

    float energy = obdAvailEnergy;
    float pct = (energy >= 0) ? (energy / 22.0f * 100.0f) : 0;
    if (pct > 100) pct = 100;
    if (pct < 0) pct = 0;

    int fillH = (int)(pct / 100.0f * innerH);
    if (fillH > innerH) fillH = innerH;

    if (fillH > 0) {
        lv_area_t fillA;
        fillA.x1 = innerX1; fillA.x2 = innerX2;
        fillA.y1 = innerY2 - fillH; fillA.y2 = innerY2;
        rdsc.bg_color = getSocColor(pct);
        rdsc.radius = 1;
        lv_draw_rect(layer, &rdsc, &fillA);
    }
}

// ─── Cabin Widget draw callbacks ───────────────────────────────

// Standalone thermometer (red mercury, fixed fill)
static void cabinThermoDrawCb(lv_event_t * e) {
    lv_obj_t * obj = (lv_obj_t *)lv_event_get_target(e);
    lv_layer_t * layer = lv_event_get_layer(e);
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);

    int cx = (coords.x1 + coords.x2) / 2;
    int cy = (coords.y1 + coords.y2) / 2;
    int objH = coords.y2 - coords.y1;

    lv_draw_rect_dsc_t rdsc;
    lv_draw_rect_dsc_init(&rdsc);
    rdsc.bg_opa = LV_OPA_COVER;
    rdsc.border_width = 0;

    // Thermometer stem (vertical bar)
    int stemW = 4;
    int stemH = objH - 14;
    int stemTop = cy - objH / 2 + 2;
    int stemBot = stemTop + stemH;

    // Stem outline (light grey)
    lv_area_t stemA;
    stemA.x1 = cx - stemW / 2 - 1; stemA.x2 = cx + stemW / 2 + 1;
    stemA.y1 = stemTop; stemA.y2 = stemBot;
    rdsc.bg_color = lv_color_hex(0x8B949E);
    rdsc.radius = stemW / 2;
    lv_draw_rect(layer, &rdsc, &stemA);

    // Bulb at bottom
    int bulbR = stemW + 2;
    lv_area_t bulbA;
    bulbA.x1 = cx - bulbR; bulbA.x2 = cx + bulbR;
    bulbA.y1 = stemBot - 2; bulbA.y2 = stemBot + bulbR * 2 - 2;
    rdsc.bg_color = lv_color_hex(0x8B949E);
    rdsc.radius = bulbR;
    lv_draw_rect(layer, &rdsc, &bulbA);

    // Mercury fill in bulb (always red)
    lv_color_t mercColor = lv_color_hex(0xF85149);
    lv_area_t bulbFill;
    bulbFill.x1 = cx - bulbR + 1; bulbFill.x2 = cx + bulbR - 1;
    bulbFill.y1 = stemBot - 1; bulbFill.y2 = stemBot + bulbR * 2 - 3;
    rdsc.bg_color = mercColor;
    rdsc.radius = bulbR - 1;
    lv_draw_rect(layer, &rdsc, &bulbFill);

    // Mercury fill in stem (~65% filled, always red)
    int mercFillH = (int)(stemH * 0.65f);
    lv_area_t mercStem;
    mercStem.x1 = cx - stemW / 2; mercStem.x2 = cx + stemW / 2;
    mercStem.y1 = stemBot - mercFillH; mercStem.y2 = stemBot;
    rdsc.bg_color = mercColor;
    rdsc.radius = 0;
    lv_draw_rect(layer, &rdsc, &mercStem);

    // Tick marks
    rdsc.bg_color = lv_color_hex(0x484F58);
    for (int i = 0; i < 3; i++) {
        int ty = stemTop + (int)(stemH * (i + 1) / 4.0f);
        lv_area_t tick;
        tick.x1 = cx + stemW / 2 + 3; tick.x2 = cx + stemW / 2 + 6;
        tick.y1 = ty; tick.y2 = ty;
        lv_draw_rect(layer, &rdsc, &tick);
    }
}

// Single water droplet icon (for humidity) - C6 style
static void cabinDropsDrawCb(lv_event_t * e) {
    lv_obj_t * obj = (lv_obj_t *)lv_event_get_target(e);
    lv_layer_t * layer = lv_event_get_layer(e);
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);

    int cx = (coords.x1 + coords.x2) / 2;
    int cy = (coords.y1 + coords.y2) / 2;

    lv_draw_rect_dsc_t rdsc;
    lv_draw_rect_dsc_init(&rdsc);
    rdsc.bg_opa = LV_OPA_COVER;
    rdsc.border_width = 0;

    lv_color_t dropColor = lv_color_hex(0x03DFFF); // Cyan-blue (matching C6)

    // Single large teardrop (C6 replica)
    // Lower rounded part (center at cx, cy + 2, radius 10)
    int r = 10;
    lv_area_t circle;
    circle.x1 = cx - r; circle.x2 = cx + r;
    circle.y1 = cy + 2 - r; circle.y2 = cy + 2 + r;
    rdsc.bg_color = dropColor;
    rdsc.radius = r;
    lv_draw_rect(layer, &rdsc, &circle);

    // Upper triangular part (point at cy - 20, base at cy)
    rdsc.radius = 0;
    for (int y = cy; y >= cy - 20; y--) {
        int w = 10 * (y - (cy - 20)) / 20;
        if (w < 1 && y > cy - 20) w = 1;
        if (w <= 0 && y != cy) continue;
        lv_area_t row;
        row.x1 = cx - w; row.x2 = cx + w;
        row.y1 = y; row.y2 = y;
        rdsc.bg_color = dropColor;
        lv_draw_rect(layer, &rdsc, &row);
    }

    // White highlight (small reflection circle)
    // C6: g->fillCircle(cx - 3, cy - 4, 3, WHITE);
    lv_area_t hl;
    hl.x1 = cx - 3 - 3; hl.x2 = cx - 3 + 3;
    hl.y1 = cy - 4 - 3; hl.y2 = cy - 4 + 3;
    rdsc.bg_color = lv_color_hex(0xFFFFFF);
    rdsc.bg_opa = LV_OPA_COVER;
    rdsc.radius = 3;
    lv_draw_rect(layer, &rdsc, &hl);
}

// ─── Thermal Widget draw callbacks ─────────────────────────────

// Outdoor thermometer (blue mercury)
static void extThermoDrawCb(lv_event_t * e) {
    lv_obj_t * obj = (lv_obj_t *)lv_event_get_target(e);
    lv_layer_t * layer = lv_event_get_layer(e);
    lv_area_t coords; lv_obj_get_coords(obj, &coords);
    int cx = (coords.x1 + coords.x2) / 2;
    int cy = (coords.y1 + coords.y2) / 2;
    int objH = coords.y2 - coords.y1;
    lv_draw_rect_dsc_t rdsc; lv_draw_rect_dsc_init(&rdsc);
    rdsc.bg_opa = LV_OPA_COVER; rdsc.border_width = 0;
    int stemW = 4, stemH = objH - 14;
    int stemTop = cy - objH / 2 + 2, stemBot = stemTop + stemH;
    // Stem outline
    lv_area_t a;
    a.x1 = cx-stemW/2-1; a.x2 = cx+stemW/2+1; a.y1 = stemTop; a.y2 = stemBot;
    rdsc.bg_color = lv_color_hex(0x8B949E); rdsc.radius = stemW/2;
    lv_draw_rect(layer, &rdsc, &a);
    // Bulb
    int bulbR = stemW + 2;
    a.x1 = cx-bulbR; a.x2 = cx+bulbR; a.y1 = stemBot-2; a.y2 = stemBot+bulbR*2-2;
    rdsc.bg_color = lv_color_hex(0x8B949E); rdsc.radius = bulbR;
    lv_draw_rect(layer, &rdsc, &a);
    // Blue mercury - bulb fill
    a.x1 = cx-bulbR+1; a.x2 = cx+bulbR-1; a.y1 = stemBot-1; a.y2 = stemBot+bulbR*2-3;
    rdsc.bg_color = lv_color_hex(0x58A6FF); rdsc.radius = bulbR-1;
    lv_draw_rect(layer, &rdsc, &a);
    // Blue mercury - stem fill (50%)
    int fillH = (int)(stemH * 0.50f);
    a.x1 = cx-stemW/2; a.x2 = cx+stemW/2; a.y1 = stemBot-fillH; a.y2 = stemBot;
    rdsc.bg_color = lv_color_hex(0x58A6FF); rdsc.radius = 0;
    lv_draw_rect(layer, &rdsc, &a);
    // Tick marks
    rdsc.bg_color = lv_color_hex(0x484F58);
    for (int i = 0; i < 3; i++) {
        int ty = stemTop + (int)(stemH * (i+1) / 4.0f);
        a.x1 = cx+stemW/2+3; a.x2 = cx+stemW/2+6; a.y1 = ty; a.y2 = ty;
        lv_draw_rect(layer, &rdsc, &a);
    }
}

// Battery temp color helper
static lv_color_t getBatTempColor(float t) {
    if (t <= 0.0f) return lv_color_hex(0xBC8CFF); // Purple (Lila)
    if (t >= 35.0f) return lv_color_hex(0xF85149); // Red
    if (t >= 28.0f) {
        float ratio = (t - 28.0f) / 7.0f;
        return lv_color_mix(lv_color_hex(0xF85149), lv_color_hex(0xD29922), (int)(ratio * 255));
    }
    if (t >= 24.0f) {
        float ratio = (t - 24.0f) / 4.0f;
        return lv_color_mix(lv_color_hex(0xD29922), lv_color_hex(0x3FB950), (int)(ratio * 255));
    }
    if (t <= 1.0f) return lv_color_hex(0x58A6FF); // Blue
    
    // 1 to 24: Blue to Green
    float ratio = (t - 1.0f) / 23.0f;
    return lv_color_mix(lv_color_hex(0x3FB950), lv_color_hex(0x58A6FF), (int)(ratio * 255));
}

// Battery with dynamic temperature fill + thermometer overlay
static void batThermoDrawCb(lv_event_t * e) {
    lv_obj_t * obj = (lv_obj_t *)lv_event_get_target(e);
    lv_layer_t * layer = lv_event_get_layer(e);
    lv_area_t coords; lv_obj_get_coords(obj, &coords);
    int cx = (coords.x1 + coords.x2) / 2;
    int cy = (coords.y1 + coords.y2) / 2;
    int objW = coords.x2 - coords.x1, objH = coords.y2 - coords.y1;
    lv_draw_rect_dsc_t rdsc; lv_draw_rect_dsc_init(&rdsc);
    rdsc.bg_opa = LV_OPA_COVER; rdsc.border_width = 0;

    // ── Battery body (static grey) ──
    int bh = objH - 6, bw = (int)(bh * 0.70f);
    if (bw > objW - 4) bw = objW - 4;
    int bx = cx - bw/2, by = cy - bh/2 + 2;

    // Terminal cap
    int capW = bw/3; if (capW < 4) capW = 4;
    lv_area_t a;
    a.x1 = cx-capW/2; a.x2 = cx+capW/2; a.y1 = by-3; a.y2 = by;
    rdsc.bg_color = lv_color_hex(0x8B949E); rdsc.radius = 2;
    lv_draw_rect(layer, &rdsc, &a);

    // Body Outline
    a.x1 = bx; a.x2 = bx+bw; a.y1 = by; a.y2 = by+bh;
    rdsc.bg_color = lv_color_hex(0x30363D); rdsc.radius = 3;
    lv_draw_rect(layer, &rdsc, &a);

    // ── Thermometer overlay ──
    int thCx = cx, thBot = by+bh-4, thStemH = (int)(bh * 0.75f);
    int thTop = thBot - thStemH;
    float temp = obdHVBatTemp;
    lv_color_t tCol = getBatTempColor(temp);

    // 1. White frame for thermometer (bulb + stem)
    // Bulb frame
    a.x1 = thCx-5; a.x2 = thCx+5; a.y1 = thBot-9; a.y2 = thBot+1;
    rdsc.bg_color = lv_color_hex(0xFFFFFF); rdsc.radius = 5;
    lv_draw_rect(layer, &rdsc, &a);
    // Stem frame
    a.x1 = thCx-2; a.x2 = thCx+2; a.y1 = thTop-1; a.y2 = thBot-6;
    rdsc.bg_color = lv_color_hex(0xFFFFFF); rdsc.radius = 2;
    lv_draw_rect(layer, &rdsc, &a);

    // 2. Colored bulb fill (always filled)
    a.x1 = thCx-4; a.x2 = thCx+4; a.y1 = thBot-8; a.y2 = thBot;
    rdsc.bg_color = tCol; rdsc.radius = 4;
    lv_draw_rect(layer, &rdsc, &a);

    // 3. Colored mercury fill (proportional height)
    // 0°C or below: no mercury. 35°C: full.
    if (temp > 0.0f) {
        float pct = temp / 35.0f;
        if (pct > 1.0f) pct = 1.0f;
        int maxH = (thBot - 6) - thTop;
        int fillH = (int)(pct * maxH);
        if (fillH > 0) {
            a.x1 = thCx-1; a.x2 = thCx+1; a.y1 = (thBot-6) - fillH; a.y2 = thBot-6;
            rdsc.bg_color = tCol; rdsc.radius = 1;
            lv_draw_rect(layer, &rdsc, &a);
        }
    }

    // 4. Scale marks (white)
    lv_draw_line_dsc_t ldsc; lv_draw_line_dsc_init(&ldsc);
    ldsc.color = lv_color_hex(0xFFFFFF); ldsc.width = 1; ldsc.opa = LV_OPA_COVER;
    for (int i = 0; i < 3; i++) {
        int ly = thTop + 2 + i * 4;
        ldsc.p1 = {(lv_value_precise_t)(thCx + 2), (lv_value_precise_t)ly};
        ldsc.p2 = {(lv_value_precise_t)(thCx + 5), (lv_value_precise_t)ly};
        lv_draw_line(layer, &ldsc);
    }
}

// Lightning bolt icon for Max Charge Power (yellow, static)
static void maxChargeLightningDrawCb(lv_event_t * e) {
    lv_obj_t * obj = (lv_obj_t *)lv_event_get_target(e);
    lv_layer_t * layer = lv_event_get_layer(e);
    lv_area_t coords; lv_obj_get_coords(obj, &coords);
    int cx = (coords.x1 + coords.x2) / 2;
    int cy = (coords.y1 + coords.y2) / 2;
    
    lv_draw_triangle_dsc_t tridsc;
    lv_draw_triangle_dsc_init(&tridsc);
    tridsc.color = lv_color_hex(0xD29922); // Yellow-gold
    tridsc.opa = LV_OPA_COVER;
    
    // Upper part
    tridsc.p[0].x = cx + 5; tridsc.p[0].y = cy - 14;
    tridsc.p[1].x = cx - 2; tridsc.p[1].y = cy - 14;
    tridsc.p[2].x = cx - 7; tridsc.p[2].y = cy;
    lv_draw_triangle(layer, &tridsc);

    tridsc.p[0].x = cx + 5; tridsc.p[0].y = cy - 14;
    tridsc.p[1].x = cx - 7; tridsc.p[1].y = cy;
    tridsc.p[2].x = cx + 2; tridsc.p[2].y = cy;
    lv_draw_triangle(layer, &tridsc);

    // Lower part
    tridsc.p[0].x = cx - 2; tridsc.p[0].y = cy + 1;
    tridsc.p[1].x = cx + 7; tridsc.p[1].y = cy + 1;
    tridsc.p[2].x = cx - 5; tridsc.p[2].y = cy + 19;
    lv_draw_triangle(layer, &tridsc);

    // Fill the connection area between upper and lower
    lv_draw_rect_dsc_t rdsc;
    lv_draw_rect_dsc_init(&rdsc);
    rdsc.bg_color = lv_color_hex(0xD29922);
    rdsc.bg_opa = LV_OPA_COVER;
    lv_area_t a;
    a.x1 = cx - 5; a.x2 = cx - 5 + 10;
    a.y1 = cy - 2; a.y2 = cy - 2 + 5;
    lv_draw_rect(layer, &rdsc, &a);
}

// ─── AC Widget draw callbacks ───────────────────────────────────

// AC Widget static pointers
static lv_obj_t * acRpmIconObj   = NULL;
static lv_obj_t * lblAcRpm       = NULL;
static lv_obj_t * acPressIconObj = NULL;
static lv_obj_t * lblAcPressure  = NULL;
static lv_obj_t * fanIconObj     = NULL;
static lv_obj_t * lblFanSpeed    = NULL;

// Dynamic color helpers (translated from C6 color stops)
static lv_color_t getAcRpmColor(float rpm) {
    if (rpm <= 3200.0f) return lv_color_hex(0x3FB950); // Green
    if (rpm >= 7000.0f) return lv_color_hex(0x8B5CF6); // Purple
    if (rpm < 5000.0f) {
        float ratio = (rpm - 3200.0f) / 1800.0f;
        return lv_color_mix(lv_color_hex(0xD29922), lv_color_hex(0x3FB950), (int)(ratio * 255));
    }
    if (rpm < 6500.0f) {
        float ratio = (rpm - 5000.0f) / 1500.0f;
        return lv_color_mix(lv_color_hex(0xF85149), lv_color_hex(0xD29922), (int)(ratio * 255));
    }
    // 6500 to 7000: Red to Purple
    float ratio = (rpm - 6500.0f) / 500.0f;
    return lv_color_mix(lv_color_hex(0x8B5CF6), lv_color_hex(0xF85149), (int)(ratio * 255));
}

static lv_color_t getAcPressureColor(float bar) {
    if (bar <= 8.0f) return lv_color_hex(0xF85149); // Red
    if (bar >= 22.0f) return lv_color_hex(0xF85149); // Red
    if (bar < 9.0f) {
        float ratio = (bar - 8.0f) / 1.0f;
        return lv_color_mix(lv_color_hex(0xD29922), lv_color_hex(0xF85149), (int)(ratio * 255));
    }
    if (bar < 10.0f) {
        float ratio = (bar - 9.0f) / 1.0f;
        return lv_color_mix(lv_color_hex(0x3FB950), lv_color_hex(0xD29922), (int)(ratio * 255));
    }
    if (bar < 15.0f) {
        float ratio = (bar - 10.0f) / 5.0f;
        return lv_color_mix(lv_color_hex(0xD29922), lv_color_hex(0x3FB950), (int)(ratio * 255));
    }
    float ratio = (bar - 15.0f) / 7.0f;
    return lv_color_mix(lv_color_hex(0xF85149), lv_color_hex(0xD29922), (int)(ratio * 255));
}

// Snowflake icon (dynamic color for AC RPM)
static void acSnowflakeDrawCb(lv_event_t * e) {
    lv_obj_t * obj = (lv_obj_t *)lv_event_get_target(e);
    lv_layer_t * layer = lv_event_get_layer(e);
    lv_area_t coords; lv_obj_get_coords(obj, &coords);
    int cx = (coords.x1 + coords.x2) / 2;
    int cy = (coords.y1 + coords.y2) / 2;
    lv_color_t col = getAcRpmColor(obdACRpm);

    lv_draw_line_dsc_t ldsc; lv_draw_line_dsc_init(&ldsc);
    ldsc.color = col; ldsc.width = 2; ldsc.opa = LV_OPA_COVER;

    int R = 11; // spoke radius
    for (int a = 0; a < 6; a++) {
        float angle = a * 3.14159f / 3.0f;
        lv_point_precise_t p0 = {(float)cx, (float)cy};
        lv_point_precise_t p1 = {cx + R * cosf(angle), cy + R * sinf(angle)};
        ldsc.p1 = p0; ldsc.p2 = p1;
        lv_draw_line(layer, &ldsc);
        // Crystal branches at 2/3 distance
        int mx = cx + (int)(8 * cosf(angle));
        int my = cy + (int)(8 * sinf(angle));
        lv_point_precise_t pm = {(float)mx, (float)my};
        float bA1 = angle + 0.55f, bA2 = angle - 0.55f;
        lv_point_precise_t b1 = {mx + 4 * cosf(bA1), my + 4 * sinf(bA1)};
        lv_point_precise_t b2 = {mx + 4 * cosf(bA2), my + 4 * sinf(bA2)};
        ldsc.p1 = pm; ldsc.p2 = b1; lv_draw_line(layer, &ldsc);
        ldsc.p1 = pm; ldsc.p2 = b2; lv_draw_line(layer, &ldsc);
    }
    // Center dot
    lv_draw_rect_dsc_t rdsc; lv_draw_rect_dsc_init(&rdsc);
    rdsc.bg_color = col; rdsc.bg_opa = LV_OPA_COVER; rdsc.radius = 3;
    lv_area_t ca = {cx-2, cy-2, cx+2, cy+2};
    lv_draw_rect(layer, &rdsc, &ca);
}

// Pressure gauge icon (dynamic color for AC pressure)
static void acGaugeDrawCb(lv_event_t * e) {
    lv_obj_t * obj = (lv_obj_t *)lv_event_get_target(e);
    lv_layer_t * layer = lv_event_get_layer(e);
    lv_area_t coords; lv_obj_get_coords(obj, &coords);
    int cx = (coords.x1 + coords.x2) / 2;
    int cy = (coords.y1 + coords.y2) / 2;
    lv_color_t col = getAcPressureColor(obdACPressure);

    lv_draw_line_dsc_t ldsc; lv_draw_line_dsc_init(&ldsc);
    ldsc.color = col; ldsc.width = 2; ldsc.opa = LV_OPA_COVER;

    // Half-circle arc (top half, 180° to 360°)
    for (int a = 180; a < 360; a += 8) {
        float r1 = a * 3.14159f / 180.0f, r2 = (a + 8) * 3.14159f / 180.0f;
        ldsc.p1 = {(lv_value_precise_t)(cx + 12 * cosf(r1)), (lv_value_precise_t)(cy + 12 * sinf(r1))};
        ldsc.p2 = {(lv_value_precise_t)(cx + 12 * cosf(r2)), (lv_value_precise_t)(cy + 12 * sinf(r2))};
        lv_draw_line(layer, &ldsc);
    }
    // Base line
    ldsc.p1 = {(lv_value_precise_t)(cx - 12), (lv_value_precise_t)(cy + 1)};
    ldsc.p2 = {(lv_value_precise_t)(cx + 12), (lv_value_precise_t)(cy + 1)};
    lv_draw_line(layer, &ldsc);

    // Dynamic Needle
    float p = obdACPressure;
    if (p < 0) p = 0;
    if (p > 22) p = 22;
    float angle = 180.0f + (p / 22.0f) * 180.0f; // 180 to 360
    float rad = angle * 3.14159f / 180.0f;
    int nx = cx + (int)(10 * cosf(rad));
    int ny = cy + (int)(10 * sinf(rad));

    lv_draw_line_dsc_t wdsc; lv_draw_line_dsc_init(&wdsc);
    wdsc.color = lv_color_hex(0xFFFFFF); // White needle
    wdsc.width = 2; wdsc.opa = LV_OPA_COVER;
    wdsc.p1 = {(lv_value_precise_t)cx, (lv_value_precise_t)cy};
    wdsc.p2 = {(lv_value_precise_t)nx, (lv_value_precise_t)ny};
    lv_draw_line(layer, &wdsc);

    // Center pivot
    lv_draw_rect_dsc_t rdsc; lv_draw_rect_dsc_init(&rdsc);
    rdsc.bg_color = lv_color_hex(0xFFFFFF); rdsc.bg_opa = LV_OPA_COVER; rdsc.radius = 3;
    lv_area_t ca = {cx-2, cy-2, cx+2, cy+2};
    lv_draw_rect(layer, &rdsc, &ca);
}

// Fan icon (cyan, static shape)
static void fanDrawCb(lv_event_t * e) {
    lv_obj_t * obj = (lv_obj_t *)lv_event_get_target(e);
    lv_layer_t * layer = lv_event_get_layer(e);
    lv_area_t coords; lv_obj_get_coords(obj, &coords);
    int cx = (coords.x1 + coords.x2) / 2;
    int cy = (coords.y1 + coords.y2) / 2;
    lv_color_t col = lv_color_hex(0x00BFFF); // cyan-blue

    float angle_offset = 0;
    if (obdFanSpeed > 0.5f) {
        uint32_t t = lv_tick_get();
        float p = 1000.0f;
        if (obdFanSpeed >= 100.0f) p = 250.0f;
        else if (obdFanSpeed > 10.0f) {
            p = 1000.0f - (obdFanSpeed - 10.0f) * (750.0f / 90.0f);
        }
        angle_offset = 360.0f * (float)(t % (uint32_t)p) / p;
    }

    lv_draw_triangle_dsc_t tridsc; lv_draw_triangle_dsc_init(&tridsc);
    tridsc.color = col; tridsc.opa = LV_OPA_COVER;
    // 3 fan blades, each as a triangle from center to rim
    for (int i = 0; i < 3; i++) {
        float a  = (i * 120 + 30 + angle_offset)  * 3.14159f / 180.0f;
        float a2 = (i * 120 + 90 + angle_offset)  * 3.14159f / 180.0f;
        tridsc.p[0].x = cx + (int)(3 * cosf(a));  tridsc.p[0].y = cy + (int)(3 * sinf(a));
        tridsc.p[1].x = cx + (int)(12 * cosf(a)); tridsc.p[1].y = cy + (int)(12 * sinf(a));
        tridsc.p[2].x = cx + (int)(12 * cosf(a2));tridsc.p[2].y = cy + (int)(12 * sinf(a2));
        lv_draw_triangle(layer, &tridsc);
    }
    // Outer ring
    lv_draw_line_dsc_t ldsc; lv_draw_line_dsc_init(&ldsc);
    ldsc.color = col; ldsc.width = 2; ldsc.opa = LV_OPA_COVER;
    for (int a = 0; a < 360; a += 10) {
        float r1 = a * 3.14159f / 180.0f, r2 = (a + 10) * 3.14159f / 180.0f;
        ldsc.p1.x = (lv_value_precise_t)(cx + 13 * cosf(r1));
        ldsc.p1.y = (lv_value_precise_t)(cy + 13 * sinf(r1));
        ldsc.p2.x = (lv_value_precise_t)(cx + 13 * cosf(r2));
        ldsc.p2.y = (lv_value_precise_t)(cy + 13 * sinf(r2));
        lv_draw_line(layer, &ldsc);
    }
    // Center hub
    lv_draw_rect_dsc_t rdsc; lv_draw_rect_dsc_init(&rdsc);
    rdsc.bg_color = col; rdsc.bg_opa = LV_OPA_COVER; rdsc.radius = 3;
    lv_area_t ca = {cx - 3, cy - 3, cx + 3, cy + 3};
    lv_draw_rect(layer, &rdsc, &ca);
}

// Climate mode icon (snowflake or sun, drawn procedurally)
static void climateIconDrawCb(lv_event_t * e) {
    lv_obj_t * obj = (lv_obj_t *)lv_event_get_target(e);
    lv_layer_t * layer = lv_event_get_layer(e);
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);

    int cx = (coords.x1 + coords.x2) / 2;
    int cy = (coords.y1 + coords.y2) / 2;

    lv_draw_rect_dsc_t rdsc;
    lv_draw_rect_dsc_init(&rdsc);
    rdsc.bg_opa = LV_OPA_COVER;
    rdsc.border_width = 0;
    rdsc.radius = 0;

    int mode = obdClimateLoopMode;

    if (mode == 4) {
        // ── SUN icon (yellow) ──
        lv_color_t sunColor = lv_color_hex(0xD29922);

        // Sun center circle
        lv_area_t center;
        center.x1 = cx - 5; center.x2 = cx + 5;
        center.y1 = cy - 5; center.y2 = cy + 5;
        rdsc.bg_color = sunColor;
        rdsc.radius = 5;
        lv_draw_rect(layer, &rdsc, &center);

        // Sun rays (8 rays)
        rdsc.radius = 0;
        for (int a = 0; a < 8; a++) {
            float angle = a * 3.14159f / 4.0f;
            float cosA = cosf(angle);
            float sinA = sinf(angle);
            // Each ray: small rect from r=8 to r=12
            for (int r = 8; r <= 12; r++) {
                int rx = cx + (int)(r * cosA);
                int ry = cy + (int)(r * sinA);
                lv_area_t ray;
                ray.x1 = rx - 1; ray.x2 = rx + 1;
                ray.y1 = ry - 1; ray.y2 = ry + 1;
                rdsc.bg_color = sunColor;
                lv_draw_rect(layer, &rdsc, &ray);
            }
        }
    } else {
        // ── SNOWFLAKE icon ──
        lv_color_t flakeColor;
        if (mode == 1 || mode == 2) {
            flakeColor = lv_color_hex(0x58A6FF); // Blue for COOL
        } else {
            flakeColor = lv_color_hex(0x484F58); // Grey for NONE/Idle
        }

        rdsc.bg_color = flakeColor;

        // 6 branches
        for (int a = 0; a < 6; a++) {
            float angle = a * 3.14159f / 3.0f;
            float cosA = cosf(angle);
            float sinA = sinf(angle);
            // Main branch
            for (int r = 0; r <= 11; r++) {
                int rx = cx + (int)(r * cosA);
                int ry = cy + (int)(r * sinA);
                lv_area_t pt;
                pt.x1 = rx; pt.x2 = rx;
                pt.y1 = ry; pt.y2 = ry;
                lv_draw_rect(layer, &rdsc, &pt);
            }
            // Crystal branches at midpoint
            int mx = cx + (int)(6 * cosA);
            int my = cy + (int)(6 * sinA);
            float bA1 = angle + 0.6f;
            float bA2 = angle - 0.6f;
            for (int r = 0; r <= 4; r++) {
                int bx1 = mx + (int)(r * cosf(bA1));
                int by1 = my + (int)(r * sinf(bA1));
                lv_area_t bp1;
                bp1.x1 = bx1; bp1.x2 = bx1;
                bp1.y1 = by1; bp1.y2 = by1;
                lv_draw_rect(layer, &rdsc, &bp1);

                int bx2 = mx + (int)(r * cosf(bA2));
                int by2 = my + (int)(r * sinf(bA2));
                lv_area_t bp2;
                bp2.x1 = bx2; bp2.x2 = bx2;
                bp2.y1 = by2; bp2.y2 = by2;
                lv_draw_rect(layer, &rdsc, &bp2);
            }
        }
    }
}

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
// Widget slots per page (WIDGET_COUNT per page)
static lv_obj_t * widgetCards[MAX_PAGES][WIDGET_COUNT] = {};

// ─── Widget Type System (for dynamic polling) ──────────────
enum WidgetType {
    WIDGET_EMPTY = 0,
    WIDGET_BATTERY_STATUS,
    WIDGET_CELL_VOLTAGE,
    WIDGET_CABIN,
    WIDGET_THERMAL,
    WIDGET_HV,
    WIDGET_AC,
    WIDGET_GRID,
    WIDGET_12V,
};

// Track which widget type is in each slot
static WidgetType pageWidgetTypes[MAX_PAGES][WIDGET_COUNT] = {};

static uint32_t getWidgetPollFlags(WidgetType t) {
    switch (t) {
        case WIDGET_BATTERY_STATUS: return POLL_SOH | POLL_SOC | POLL_AVAIL_ENERGY;
        case WIDGET_CELL_VOLTAGE:   return POLL_LBC;
        case WIDGET_CABIN:          return POLL_HVAC;
        case WIDGET_THERMAL:        return POLL_HVAC | POLL_BAT_TEMP | POLL_MAX_CHARGE;
        case WIDGET_HV:             return POLL_HV_VOLT | POLL_HV_CURRENT;
        case WIDGET_AC:             return POLL_AC_COMP | POLL_FAN_SPEED;
        case WIDGET_GRID:           return POLL_INSULATION | POLL_AC_PHASE;
        case WIDGET_12V:            return POLL_AT_RV | POLL_12V_CURRENT | POLL_DCDC_LOAD;
        default: return 0;
    }
}

static void rebuildPollFlags(int pageIndex) {
    activePollFlags = 0;
    for (int i = 0; i < WIDGET_COUNT; i++) {
        activePollFlags |= getWidgetPollFlags(pageWidgetTypes[pageIndex][i]);
    }
    // Reset EVC state machine to re-evaluate with new flags
    evcState = EVC_IDLE;
    evcCmdSentTime = 0;
    Serial.printf("[POLL] Page %d flags: 0x%04X\n", pageIndex, activePollFlags);
}

// ─── Forward declarations ─────────────────────────────────
static void createTopBar(lv_obj_t * parent);
static void createTileview(lv_obj_t * parent);
static void createWidgetSlots(lv_obj_t * tile, int pageIdx);
static void createDemoWidgets(lv_obj_t * tile, int pageIdx);
static void createPage2Widgets(lv_obj_t * tile, int pageIdx);
static lv_obj_t * createAcWidget(lv_obj_t * parent, int w, int h);
static lv_obj_t * createHvWidget(lv_obj_t * parent, int w, int h);
static lv_obj_t * createGridWidget(lv_obj_t * parent, int w, int h);
static lv_obj_t * create12VWidget(lv_obj_t * parent, int w, int h);
static lv_obj_t * createBatteryStatusWidget(lv_obj_t * parent, int w, int h);
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
//  Cabin Widget (CabinTemp + Humidity + Climate Mode)
// ═══════════════════════════════════════════════════════════

static lv_obj_t * createCabinWidget(lv_obj_t * parent, int w, int h) {
    lv_obj_t * card = lv_obj_create(parent);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_bg_color(card, COLOR_WIDGET_BG, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, COLOR_WIDGET_BORDER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_pad_all(card, 2, 0);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    // Layout: 3 rows
    int innerH = h - 6;
    int innerW = w - 6;
    int rowH = innerH / 3;
    int iconSize = rowH - 4;
    if (iconSize > 36) iconSize = 36;

    int iconColW = iconSize;
    int iconCx = iconColW / 2;
    int textAreaX1 = iconColW + 2;
    int textAreaW = innerW - textAreaX1;

    // ═══════════════════════════════════════
    //  Row 1: Cabin Temperature (Thermometer icon)
    // ═══════════════════════════════════════
    int row1Y = 0;

    cabinThermoObj = lv_obj_create(card);
    lv_obj_set_size(cabinThermoObj, iconSize, iconSize);
    lv_obj_set_pos(cabinThermoObj, iconCx - iconSize / 2, row1Y + (rowH - iconSize) / 2);
    lv_obj_set_style_bg_opa(cabinThermoObj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cabinThermoObj, 0, 0);
    lv_obj_set_style_pad_all(cabinThermoObj, 0, 0);
    lv_obj_set_scrollbar_mode(cabinThermoObj, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(cabinThermoObj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(cabinThermoObj, cabinThermoDrawCb, LV_EVENT_DRAW_MAIN, NULL);

    lblCabinTemp = lv_label_create(card);
    lv_label_set_text(lblCabinTemp, "--");
    lv_obj_set_style_text_color(lblCabinTemp, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(lblCabinTemp, &lv_font_montserrat_24, 0);
    lv_obj_set_size(lblCabinTemp, textAreaW, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(lblCabinTemp, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(lblCabinTemp, textAreaX1, row1Y + rowH / 2 - 16);

    lblCabinTempLabel = lv_label_create(card);
    lv_label_set_text(lblCabinTempLabel, "\xC2\xB0" "C");
    lv_obj_set_style_text_color(lblCabinTempLabel, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(lblCabinTempLabel, &lv_font_montserrat_10, 0);
    lv_obj_set_pos(lblCabinTempLabel, innerW - 16, row1Y + rowH / 2 - 9);

    // ═══════════════════════════════════════
    //  Row 2: Humidity (Water drops icon)
    // ═══════════════════════════════════════
    int row2Y = rowH;

    lv_obj_t * sep1 = lv_obj_create(card);
    lv_obj_set_size(sep1, innerW - 4, 1);
    lv_obj_set_pos(sep1, 2, row2Y);
    lv_obj_set_style_bg_color(sep1, COLOR_WIDGET_BORDER, 0);
    lv_obj_set_style_bg_opa(sep1, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sep1, 0, 0);
    lv_obj_set_style_radius(sep1, 0, 0);
    lv_obj_set_style_pad_all(sep1, 0, 0);
    lv_obj_clear_flag(sep1, LV_OBJ_FLAG_SCROLLABLE);

    cabinDropsObj = lv_obj_create(card);
    lv_obj_set_size(cabinDropsObj, iconSize, iconSize);
    lv_obj_set_pos(cabinDropsObj, iconCx - iconSize / 2, row2Y + (rowH - iconSize) / 2 + 4);
    lv_obj_set_style_bg_opa(cabinDropsObj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cabinDropsObj, 0, 0);
    lv_obj_set_style_pad_all(cabinDropsObj, 0, 0);
    lv_obj_set_scrollbar_mode(cabinDropsObj, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(cabinDropsObj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(cabinDropsObj, cabinDropsDrawCb, LV_EVENT_DRAW_MAIN, NULL);

    lblHumidity = lv_label_create(card);
    lv_label_set_text(lblHumidity, "--");
    lv_obj_set_style_text_color(lblHumidity, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(lblHumidity, &lv_font_montserrat_24, 0);
    lv_obj_set_size(lblHumidity, textAreaW, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(lblHumidity, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(lblHumidity, textAreaX1, row2Y + rowH / 2 - 16);

    lblHumidityLabel = lv_label_create(card);
    lv_label_set_text(lblHumidityLabel, "%RH");
    lv_obj_set_style_text_color(lblHumidityLabel, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(lblHumidityLabel, &lv_font_montserrat_10, 0);
    lv_obj_set_pos(lblHumidityLabel, innerW - 24, row2Y + rowH / 2 - 9);

    // ═══════════════════════════════════════
    //  Row 3: Climate Mode (Snowflake/Sun icon)
    // ═══════════════════════════════════════
    int row3Y = rowH * 2;

    lv_obj_t * sep2 = lv_obj_create(card);
    lv_obj_set_size(sep2, innerW - 4, 1);
    lv_obj_set_pos(sep2, 2, row3Y);
    lv_obj_set_style_bg_color(sep2, COLOR_WIDGET_BORDER, 0);
    lv_obj_set_style_bg_opa(sep2, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sep2, 0, 0);
    lv_obj_set_style_radius(sep2, 0, 0);
    lv_obj_set_style_pad_all(sep2, 0, 0);
    lv_obj_clear_flag(sep2, LV_OBJ_FLAG_SCROLLABLE);

    climateIconObj = lv_obj_create(card);
    lv_obj_set_size(climateIconObj, iconSize, iconSize);
    lv_obj_set_pos(climateIconObj, iconCx - iconSize / 2, row3Y + (rowH - iconSize) / 2);
    lv_obj_set_style_bg_opa(climateIconObj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(climateIconObj, 0, 0);
    lv_obj_set_style_pad_all(climateIconObj, 0, 0);
    lv_obj_set_scrollbar_mode(climateIconObj, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(climateIconObj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(climateIconObj, climateIconDrawCb, LV_EVENT_DRAW_MAIN, NULL);

    lblClimateMode = lv_label_create(card);
    lv_label_set_text(lblClimateMode, "Idle");
    lv_obj_set_style_text_color(lblClimateMode, lv_color_hex(0x484F58), 0);
    lv_obj_set_style_text_font(lblClimateMode, &lv_font_montserrat_24, 0);
    lv_obj_set_size(lblClimateMode, textAreaW, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(lblClimateMode, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(lblClimateMode, textAreaX1, row3Y + rowH / 2 - 12);

    return card;
}

// ═══════════════════════════════════════════════════════════
//  Thermal Widget (Ext Temp + Bat Temp + Max Charge Power)
// ═══════════════════════════════════════════════════════════

static lv_obj_t * createThermalWidget(lv_obj_t * parent, int w, int h) {
    lv_obj_t * card = lv_obj_create(parent);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_bg_color(card, COLOR_WIDGET_BG, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, COLOR_WIDGET_BORDER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_pad_all(card, 2, 0);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    int innerH = h - 6, innerW = w - 6;
    int rowH = innerH / 3;
    int iconSize = rowH - 4; if (iconSize > 36) iconSize = 36;
    int iconColW = iconSize;
    int iconCx = iconColW / 2;
    int textAreaX1 = iconColW + 2;
    int textAreaW = innerW - textAreaX1;

    // ── Row 1: External Temperature (blue thermometer + OUT label) ──
    int row1Y = 0;

    extThermoObj = lv_obj_create(card);
    lv_obj_set_size(extThermoObj, iconSize, iconSize);
    lv_obj_set_pos(extThermoObj, iconCx - iconSize/2, row1Y + (rowH - iconSize)/2);
    lv_obj_set_style_bg_opa(extThermoObj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(extThermoObj, 0, 0);
    lv_obj_set_style_pad_all(extThermoObj, 0, 0);
    lv_obj_set_scrollbar_mode(extThermoObj, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(extThermoObj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(extThermoObj, extThermoDrawCb, LV_EVENT_DRAW_MAIN, NULL);

    lblExtTemp = lv_label_create(card);
    lv_label_set_text(lblExtTemp, "--");
    lv_obj_set_style_text_color(lblExtTemp, lv_color_hex(0x58A6FF), 0);
    lv_obj_set_style_text_font(lblExtTemp, &lv_font_montserrat_24, 0);
    lv_obj_set_size(lblExtTemp, textAreaW, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(lblExtTemp, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(lblExtTemp, textAreaX1, row1Y + rowH/2 - 16);



    // Unit label °C
    lv_obj_t * lblExtUnit = lv_label_create(card);
    lv_label_set_text(lblExtUnit, "\xC2\xB0" "C");
    lv_obj_set_style_text_color(lblExtUnit, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(lblExtUnit, &lv_font_montserrat_10, 0);
    lv_obj_set_pos(lblExtUnit, innerW - 20, row1Y + rowH/2 - 9);

    // ── Separator 1 ──
    int row2Y = rowH;
    lv_obj_t * sep1 = lv_obj_create(card);
    lv_obj_set_size(sep1, innerW - 4, 1);
    lv_obj_set_pos(sep1, 2, row2Y);
    lv_obj_set_style_bg_color(sep1, COLOR_WIDGET_BORDER, 0);
    lv_obj_set_style_bg_opa(sep1, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sep1, 0, 0);
    lv_obj_set_style_radius(sep1, 0, 0);
    lv_obj_set_style_pad_all(sep1, 0, 0);
    lv_obj_clear_flag(sep1, LV_OBJ_FLAG_SCROLLABLE);

    // ── Row 2: Battery Temperature (dynamic battery+thermo icon) ──
    batThermoIconObj = lv_obj_create(card);
    lv_obj_set_size(batThermoIconObj, iconSize, iconSize);
    lv_obj_set_pos(batThermoIconObj, iconCx - iconSize/2, row2Y + (rowH - iconSize)/2);
    lv_obj_set_style_bg_opa(batThermoIconObj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(batThermoIconObj, 0, 0);
    lv_obj_set_style_pad_all(batThermoIconObj, 0, 0);
    lv_obj_set_scrollbar_mode(batThermoIconObj, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(batThermoIconObj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(batThermoIconObj, batThermoDrawCb, LV_EVENT_DRAW_MAIN, NULL);

    lblBatTemp = lv_label_create(card);
    lv_label_set_text(lblBatTemp, "--");
    lv_obj_set_style_text_color(lblBatTemp, lv_color_hex(0x87CEEB), 0);
    lv_obj_set_style_text_font(lblBatTemp, &lv_font_montserrat_24, 0);
    lv_obj_set_size(lblBatTemp, textAreaW, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(lblBatTemp, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(lblBatTemp, textAreaX1, row2Y + rowH/2 - 16);

    lv_obj_t * lblBatUnit = lv_label_create(card);
    lv_label_set_text(lblBatUnit, "\xC2\xB0" "C");
    lv_obj_set_style_text_color(lblBatUnit, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(lblBatUnit, &lv_font_montserrat_10, 0);
    lv_obj_set_pos(lblBatUnit, innerW - 16, row2Y + rowH/2 - 9);

    // ── Separator 2 ──
    int row3Y = rowH * 2;
    lv_obj_t * sep2 = lv_obj_create(card);
    lv_obj_set_size(sep2, innerW - 4, 1);
    lv_obj_set_pos(sep2, 2, row3Y);
    lv_obj_set_style_bg_color(sep2, COLOR_WIDGET_BORDER, 0);
    lv_obj_set_style_bg_opa(sep2, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sep2, 0, 0);
    lv_obj_set_style_radius(sep2, 0, 0);
    lv_obj_set_style_pad_all(sep2, 0, 0);
    lv_obj_clear_flag(sep2, LV_OBJ_FLAG_SCROLLABLE);

    // ── Row 3: Max Charge Power (lightning bolt + MAX label) ──
    maxChargeLightObj = lv_obj_create(card);
    lv_obj_set_size(maxChargeLightObj, iconSize, iconSize);
    lv_obj_set_pos(maxChargeLightObj, iconCx - iconSize/2, row3Y + (rowH - iconSize)/2);
    lv_obj_set_style_bg_opa(maxChargeLightObj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(maxChargeLightObj, 0, 0);
    lv_obj_set_style_pad_all(maxChargeLightObj, 0, 0);
    lv_obj_set_scrollbar_mode(maxChargeLightObj, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(maxChargeLightObj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(maxChargeLightObj, maxChargeLightningDrawCb, LV_EVENT_DRAW_MAIN, NULL);

    lblMaxCharge = lv_label_create(card);
    lv_label_set_text(lblMaxCharge, "--");
    lv_obj_set_style_text_color(lblMaxCharge, lv_color_hex(0xD29922), 0);
    lv_obj_set_style_text_font(lblMaxCharge, &lv_font_montserrat_24, 0);
    lv_obj_set_size(lblMaxCharge, textAreaW, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(lblMaxCharge, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(lblMaxCharge, textAreaX1, row3Y + rowH/2 - 16);



    lv_obj_t * lblMaxUnit = lv_label_create(card);
    lv_label_set_text(lblMaxUnit, "kW");
    lv_obj_set_style_text_color(lblMaxUnit, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(lblMaxUnit, &lv_font_montserrat_10, 0);
    lv_obj_set_pos(lblMaxUnit, innerW - 20, row3Y + rowH/2 - 9);

    return card;
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
        { NULL, NULL, NULL, {}, 0 },  // Slot 0: Battery Status widget (SOH+SOC+AvailEnergy)
        { NULL, NULL, NULL, {}, 0 },  // Slot 1: Cell Voltage widget (custom)
        { NULL, NULL, NULL, {}, 0 },  // Slot 2: Cabin widget (custom)
        { NULL, NULL, NULL, {}, 0 },  // Slot 3: Thermal widget (custom)
    };

    for (int i = 0; i < WIDGET_COUNT; i++) {
        int x = WIDGET_MARGIN + i * (widgetW + WIDGET_GAP);
        int y = WIDGET_MARGIN;

        if (i == 0) {
            // Custom Battery Status widget (SOH + SOC + AvailEnergy)
            lv_obj_t * w = createBatteryStatusWidget(tile, widgetW, widgetH);
            lv_obj_set_pos(w, x, y);
            widgetCards[pageIdx][i] = w;
            pageWidgetTypes[pageIdx][i] = WIDGET_BATTERY_STATUS;
        } else if (i == 1) {
            // Custom cell voltage widget
            lv_obj_t * w = createCellVoltageWidget(tile, widgetW, widgetH);
            lv_obj_set_pos(w, x, y);
            widgetCards[pageIdx][i] = w;
            pageWidgetTypes[pageIdx][i] = WIDGET_CELL_VOLTAGE;
        } else if (i == 2) {
            // Custom cabin widget (Temp + Humidity + Climate Mode)
            lv_obj_t * w = createCabinWidget(tile, widgetW, widgetH);
            lv_obj_set_pos(w, x, y);
            widgetCards[pageIdx][i] = w;
            pageWidgetTypes[pageIdx][i] = WIDGET_CABIN;
        } else if (i == 3) {
            // Custom Thermal widget (Ext Temp + Bat Temp + Max Charge)
            lv_obj_t * w = createThermalWidget(tile, widgetW, widgetH);
            lv_obj_set_pos(w, x, y);
            widgetCards[pageIdx][i] = w;
            pageWidgetTypes[pageIdx][i] = WIDGET_THERMAL;
        }
    }
    rebuildPollFlags(pageIdx);
}

// ═══════════════════════════════════════════════════════════
//  HV Widget (HV Voltage + DC Power)
// ═══════════════════════════════════════════════════════════

static lv_obj_t * hvBatIconObj = NULL;
static lv_obj_t * lblHvVolt = NULL;
static lv_obj_t * dcArrowObj = NULL;
static lv_obj_t * lblDcPower = NULL;

static lv_color_t getHvVoltColor(float v) {
    if (v <= 330.0f) return lv_color_hex(0xF85149); // Red
    if (v >= 380.0f) return lv_color_hex(0x58A6FF); // Blue
    if (v < 345.0f) {
        float ratio = (v - 330.0f) / 15.0f;
        return lv_color_mix(lv_color_hex(0xD29922), lv_color_hex(0xF85149), (int)(ratio * 255));
    }
    if (v < 360.0f) {
        float ratio = (v - 345.0f) / 15.0f;
        return lv_color_mix(lv_color_hex(0x3FB950), lv_color_hex(0xD29922), (int)(ratio * 255));
    }
    float ratio = (v - 360.0f) / 20.0f;
    return lv_color_mix(lv_color_hex(0x58A6FF), lv_color_hex(0x3FB950), (int)(ratio * 255));
}

// Battery with lightning bolt inside
static void hvBatBoltDrawCb(lv_event_t * e) {
    lv_obj_t * obj = (lv_obj_t *)lv_event_get_target(e);
    lv_layer_t * layer = lv_event_get_layer(e);
    lv_area_t coords; lv_obj_get_coords(obj, &coords);
    int cx = (coords.x1+coords.x2)/2, cy = (coords.y1+coords.y2)/2;
    int objH = coords.y2-coords.y1;
    lv_draw_rect_dsc_t rd; lv_draw_rect_dsc_init(&rd);
    rd.bg_opa = LV_OPA_COVER; rd.border_width = 0;
    int bh = objH-6, bw = (int)(bh*0.70f);
    int bx = cx-bw/2, by = cy-bh/2+2;
    int capW = bw/3; if(capW<4) capW=4;
    lv_area_t a;
    a.x1=cx-capW/2; a.x2=cx+capW/2; a.y1=by-3; a.y2=by;
    rd.bg_color=lv_color_hex(0x8B949E); rd.radius=2; lv_draw_rect(layer,&rd,&a);
    a.x1=bx; a.x2=bx+bw; a.y1=by; a.y2=by+bh;
    rd.bg_color=lv_color_hex(0x30363D); rd.radius=3; lv_draw_rect(layer,&rd,&a);
    // Yellow LV_SYMBOL_CHARGE inside
    lv_draw_label_dsc_t ldsc;
    lv_draw_label_dsc_init(&ldsc);
    ldsc.color = lv_color_hex(0xD29922);
    ldsc.font = &lv_font_montserrat_20;
    ldsc.text = LV_SYMBOL_CHARGE;
    ldsc.text_local = true;
    ldsc.align = LV_TEXT_ALIGN_CENTER;
    lv_area_t la = {bx, cy - 8, bx + bw, cy + 12}; // Moved down 2px
    lv_draw_label(layer, &ldsc, &la);
}

// DC Power arrow (green down=charging, red up=discharging)
static void dcArrowDrawCb(lv_event_t * e) {
    lv_obj_t * obj = (lv_obj_t *)lv_event_get_target(e);
    lv_layer_t * layer = lv_event_get_layer(e);
    lv_area_t coords; lv_obj_get_coords(obj, &coords);
    int cx = (coords.x1+coords.x2)/2, cy = (coords.y1+coords.y2)/2;
    int objH = coords.y2-coords.y1;
    bool charging = (obdDCPower > 0);
    lv_color_t col = charging ? lv_color_hex(0x3FB950) : lv_color_hex(0xF85149);
    lv_draw_rect_dsc_t rd; lv_draw_rect_dsc_init(&rd);
    rd.bg_opa=LV_OPA_COVER; rd.border_width=0; rd.radius=0; rd.bg_color=col;
    lv_draw_triangle_dsc_t td; lv_draw_triangle_dsc_init(&td);
    td.color=col; td.opa=LV_OPA_COVER;
    int stemW=6, headW=14, headH=10;
    int top=cy-objH/2+4, bot=cy+objH/2-4;
    if (charging) { // down arrow
        lv_area_t a={cx-stemW/2, top, cx+stemW/2, bot-headH};
        lv_draw_rect(layer,&rd,&a);
        td.p[0].x=cx; td.p[0].y=bot; td.p[1].x=cx-headW/2; td.p[1].y=bot-headH; td.p[2].x=cx+headW/2; td.p[2].y=bot-headH;
        lv_draw_triangle(layer,&td);
    } else { // up arrow
        lv_area_t a={cx-stemW/2, top+headH, cx+stemW/2, bot};
        lv_draw_rect(layer,&rd,&a);
        td.p[0].x=cx; td.p[0].y=top; td.p[1].x=cx-headW/2; td.p[1].y=top+headH; td.p[2].x=cx+headW/2; td.p[2].y=top+headH;
        lv_draw_triangle(layer,&td);
    }
}

static lv_obj_t * createHvWidget(lv_obj_t * parent, int w, int h) {
    lv_obj_t * card = lv_obj_create(parent);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_bg_color(card, COLOR_WIDGET_BG, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, COLOR_WIDGET_BORDER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_pad_all(card, 2, 0);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    int innerH=h-6, innerW=w-6, rowH=innerH/2;
    int iconSize=rowH-4; if(iconSize>36) iconSize=36;
    int iconCx=iconSize/2, textX=iconSize+2;
    int unitW = 44;
    int valueW = innerW - textX - unitW;

    // Row 1: HV Voltage
    hvBatIconObj = lv_obj_create(card);
    lv_obj_set_size(hvBatIconObj, iconSize, iconSize);
    lv_obj_set_pos(hvBatIconObj, iconCx-iconSize/2, (rowH-iconSize)/2);
    lv_obj_set_style_bg_opa(hvBatIconObj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hvBatIconObj, 0, 0);
    lv_obj_set_style_pad_all(hvBatIconObj, 0, 0);
    lv_obj_set_scrollbar_mode(hvBatIconObj, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(hvBatIconObj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(hvBatIconObj, hvBatBoltDrawCb, LV_EVENT_DRAW_MAIN, NULL);
    lblHvVolt = lv_label_create(card);
    lv_label_set_text(lblHvVolt, "--");
    lv_obj_set_style_text_color(lblHvVolt, lv_color_hex(0xD29922), 0);
    lv_obj_set_style_text_font(lblHvVolt, &lv_font_montserrat_24, 0);
    lv_obj_set_size(lblHvVolt, valueW, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(lblHvVolt, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(lblHvVolt, textX, rowH / 2 - 16);
    lv_obj_t * u1 = lv_label_create(card);
    lv_label_set_text(u1, "V");
    lv_obj_set_style_text_color(u1, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(u1, &lv_font_montserrat_16, 0);
    lv_obj_set_size(u1, unitW, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(u1, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(u1, innerW - unitW, rowH / 2 - 12);

    // Separator
    lv_obj_t * sep=lv_obj_create(card); lv_obj_set_size(sep, innerW-4, 1); lv_obj_set_pos(sep, 2, rowH);
    lv_obj_set_style_bg_color(sep, COLOR_WIDGET_BORDER, 0); lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sep, 0, 0); lv_obj_set_style_radius(sep, 0, 0);
    lv_obj_set_style_pad_all(sep, 0, 0); lv_obj_clear_flag(sep, LV_OBJ_FLAG_SCROLLABLE);

    // Row 2: DC Power
    dcArrowObj = lv_obj_create(card);
    lv_obj_set_size(dcArrowObj, iconSize, iconSize);
    lv_obj_set_pos(dcArrowObj, iconCx-iconSize/2, rowH+(rowH-iconSize)/2);
    lv_obj_set_style_bg_opa(dcArrowObj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(dcArrowObj, 0, 0);
    lv_obj_set_style_pad_all(dcArrowObj, 0, 0);
    lv_obj_set_scrollbar_mode(dcArrowObj, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(dcArrowObj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(dcArrowObj, dcArrowDrawCb, LV_EVENT_DRAW_MAIN, NULL);
    lblDcPower = lv_label_create(card);
    lv_label_set_text(lblDcPower, "--");
    lv_obj_set_style_text_color(lblDcPower, lv_color_hex(0x3FB950), 0);
    lv_obj_set_style_text_font(lblDcPower, &lv_font_montserrat_24, 0);
    lv_obj_set_size(lblDcPower, valueW, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(lblDcPower, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(lblDcPower, textX, rowH + rowH / 2 - 16);
    lv_obj_t * u2 = lv_label_create(card);
    lv_label_set_text(u2, "kW");
    lv_obj_set_style_text_color(u2, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(u2, &lv_font_montserrat_16, 0);
    lv_obj_set_size(u2, unitW, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(u2, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(u2, innerW - unitW, rowH + rowH / 2 - 12);
    return card;
}

// ═══════════════════════════════════════════════════════════
//  AC Widget (AC RPM + AC Pressure + Fan Speed)
// ═══════════════════════════════════════════════════════════

// Animation callback to trigger fan redraw
static void fan_anim_cb(void * var, int32_t v) {
    if (obdFanSpeed > 0.5f) lv_obj_invalidate((lv_obj_t *)var);
}

static lv_obj_t * createAcWidget(lv_obj_t * parent, int w, int h) {
    lv_obj_t * card = lv_obj_create(parent);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_bg_color(card, COLOR_WIDGET_BG, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, COLOR_WIDGET_BORDER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_pad_all(card, 2, 0);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    int innerH = h - 6, innerW = w - 6;
    int rowH = innerH / 3;
    int iconSize = rowH - 4; if (iconSize > 36) iconSize = 36;
    int iconColW = iconSize;
    int iconCx = iconColW / 2;
    int textAreaX1 = iconColW + 2;
    int unitW = 44;
    int valueW = innerW - textAreaX1 - unitW;
    int textAreaW = valueW;

    // Helper lambda-like macro for separator
    auto makeSep = [&](int yPos) {
        lv_obj_t * sep = lv_obj_create(card);
        lv_obj_set_size(sep, innerW - 4, 1);
        lv_obj_set_pos(sep, 2, yPos);
        lv_obj_set_style_bg_color(sep, COLOR_WIDGET_BORDER, 0);
        lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(sep, 0, 0);
        lv_obj_set_style_radius(sep, 0, 0);
        lv_obj_set_style_pad_all(sep, 0, 0);
        lv_obj_clear_flag(sep, LV_OBJ_FLAG_SCROLLABLE);
    };

    // ── Row 1: AC Compressor RPM (snowflake, dynamic color) ──
    int row1Y = 0;
    acRpmIconObj = lv_obj_create(card);
    lv_obj_set_size(acRpmIconObj, iconSize, iconSize);
    lv_obj_set_pos(acRpmIconObj, iconCx - iconSize/2, row1Y + (rowH - iconSize)/2);
    lv_obj_set_style_bg_opa(acRpmIconObj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(acRpmIconObj, 0, 0);
    lv_obj_set_style_pad_all(acRpmIconObj, 0, 0);
    lv_obj_set_scrollbar_mode(acRpmIconObj, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(acRpmIconObj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(acRpmIconObj, acSnowflakeDrawCb, LV_EVENT_DRAW_MAIN, NULL);

    lblAcRpm = lv_label_create(card);
    lv_label_set_text(lblAcRpm, "--");
    lv_obj_set_style_text_color(lblAcRpm, lv_color_hex(0x3FB950), 0);
    lv_obj_set_style_text_font(lblAcRpm, &lv_font_montserrat_24, 0);
    lv_obj_set_size(lblAcRpm, valueW, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(lblAcRpm, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(lblAcRpm, textAreaX1, row1Y + rowH / 2 - 16);

    lv_obj_t * lblRpmUnit = lv_label_create(card);
    lv_label_set_text(lblRpmUnit, "rpm");
    lv_obj_set_style_text_color(lblRpmUnit, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(lblRpmUnit, &lv_font_montserrat_16, 0);
    lv_obj_set_size(lblRpmUnit, unitW, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(lblRpmUnit, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(lblRpmUnit, innerW - unitW, row1Y + rowH / 2 - 12);

    // ── Separator 1 ──
    int row2Y = rowH;
    makeSep(row2Y);

    // ── Row 2: AC Pressure (gauge, dynamic color) ──
    acPressIconObj = lv_obj_create(card);
    lv_obj_set_size(acPressIconObj, iconSize, iconSize);
    lv_obj_set_pos(acPressIconObj, iconCx - iconSize / 2, row2Y + (rowH - iconSize) / 2 + 3);
    lv_obj_set_style_bg_opa(acPressIconObj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(acPressIconObj, 0, 0);
    lv_obj_set_style_pad_all(acPressIconObj, 0, 0);
    lv_obj_set_scrollbar_mode(acPressIconObj, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(acPressIconObj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(acPressIconObj, acGaugeDrawCb, LV_EVENT_DRAW_MAIN, NULL);

    lblAcPressure = lv_label_create(card);
    lv_label_set_text(lblAcPressure, "--");
    lv_obj_set_style_text_color(lblAcPressure, lv_color_hex(0xF85149), 0);
    lv_obj_set_style_text_font(lblAcPressure, &lv_font_montserrat_24, 0);
    lv_obj_set_size(lblAcPressure, valueW, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(lblAcPressure, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(lblAcPressure, textAreaX1, row2Y + rowH / 2 - 16);

    lv_obj_t * lblBarUnit = lv_label_create(card);
    lv_label_set_text(lblBarUnit, "bar");
    lv_obj_set_style_text_color(lblBarUnit, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(lblBarUnit, &lv_font_montserrat_16, 0);
    lv_obj_set_size(lblBarUnit, unitW, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(lblBarUnit, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(lblBarUnit, innerW - unitW, row2Y + rowH / 2 - 12);

    // ── Separator 2 ──
    int row3Y = rowH * 2;
    makeSep(row3Y);

    // ── Row 3: Motor Fan Speed (fan icon, cyan, static) ──
    fanIconObj = lv_obj_create(card);
    lv_obj_set_size(fanIconObj, iconSize, iconSize);
    lv_obj_set_pos(fanIconObj, iconCx - iconSize/2, row3Y + (rowH - iconSize)/2);
    lv_obj_set_style_bg_opa(fanIconObj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(fanIconObj, 0, 0);
    lv_obj_set_style_pad_all(fanIconObj, 0, 0);
    lv_obj_set_scrollbar_mode(fanIconObj, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(fanIconObj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(fanIconObj, fanDrawCb, LV_EVENT_DRAW_MAIN, NULL);

    lblFanSpeed = lv_label_create(card);
    lv_label_set_text(lblFanSpeed, "--");
    lv_obj_set_style_text_color(lblFanSpeed, lv_color_hex(0x00BFFF), 0);
    lv_obj_set_style_text_font(lblFanSpeed, &lv_font_montserrat_24, 0);
    lv_obj_set_size(lblFanSpeed, valueW, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(lblFanSpeed, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(lblFanSpeed, textAreaX1, row3Y + rowH / 2 - 16);

    lv_obj_t * lblFanUnit = lv_label_create(card);
    lv_label_set_text(lblFanUnit, "%");
    lv_obj_set_style_text_color(lblFanUnit, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(lblFanUnit, &lv_font_montserrat_16, 0);
    lv_obj_set_size(lblFanUnit, unitW, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(lblFanUnit, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(lblFanUnit, innerW - unitW, row3Y + rowH / 2 - 12);

    // Start fan animation
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, fanIconObj);
    lv_anim_set_values(&a, 0, 100);
    lv_anim_set_duration(&a, 1000);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&a, fan_anim_cb);
    lv_anim_start(&a);

    return card;
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
            lv_obj_t * w = createHvWidget(tile, widgetW, widgetH);
            lv_obj_set_pos(w, x, y);
            widgetCards[pageIdx][i] = w;
            pageWidgetTypes[pageIdx][i] = WIDGET_HV;
        } else if (i == 1) {
            lv_obj_t * w = createAcWidget(tile, widgetW, widgetH);
            lv_obj_set_pos(w, x, y);
            widgetCards[pageIdx][i] = w;
            pageWidgetTypes[pageIdx][i] = WIDGET_AC;
        } else if (i == 2) {
            lv_obj_t * w = createGridWidget(tile, widgetW, widgetH);
            lv_obj_set_pos(w, x, y);
            widgetCards[pageIdx][i] = w;
            pageWidgetTypes[pageIdx][i] = WIDGET_GRID;
        } else if (i == 3) {
            lv_obj_t * w = create12VWidget(tile, widgetW, widgetH);
            lv_obj_set_pos(w, x, y);
            widgetCards[pageIdx][i] = w;
            pageWidgetTypes[pageIdx][i] = WIDGET_12V;
        }
    }
}

// ═══════════════════════════════════════════════════════════
//  Grid Widget (Insulation Resistance + AC Phase)
// ═══════════════════════════════════════════════════════════

static lv_obj_t * groundIconObj = NULL;
static lv_obj_t * lblInsulation = NULL;
static lv_obj_t * plugIconObj = NULL;
static lv_obj_t * lblAcPhase = NULL;

// Ground/Earth symbol
static void groundDrawCb(lv_event_t * e) {
    lv_obj_t * obj = (lv_obj_t *)lv_event_get_target(e);
    lv_layer_t * layer = lv_event_get_layer(e);
    lv_area_t coords; lv_obj_get_coords(obj, &coords);
    int cx=(coords.x1+coords.x2)/2, cy=(coords.y1+coords.y2)/2;
    // Dynamic color based on insulation value
    lv_color_t col;
    if (obdInsulationRes < 0) col = lv_color_hex(0x8B949E);
    else if (obdInsulationRes >= 100000) col = lv_color_hex(0x8B949E); // infinity
    else if (obdInsulationRes < 50000) col = lv_color_hex(0x3FB950);
    else if (obdInsulationRes < 95000) col = lv_color_hex(0xD29922);
    else col = lv_color_hex(0xF85149);

    lv_draw_line_dsc_t ld; lv_draw_line_dsc_init(&ld);
    ld.color=col; ld.width=2; ld.opa=LV_OPA_COVER;
    // Vertical stem
    ld.p1={(lv_value_precise_t)cx,(lv_value_precise_t)(cy-10)};
    ld.p2={(lv_value_precise_t)cx,(lv_value_precise_t)(cy-2)};
    lv_draw_line(layer,&ld);
    // 3 horizontal lines (getting shorter)
    ld.p1={(lv_value_precise_t)(cx-10),(lv_value_precise_t)(cy-2)};
    ld.p2={(lv_value_precise_t)(cx+10),(lv_value_precise_t)(cy-2)};
    lv_draw_line(layer,&ld);
    ld.p1={(lv_value_precise_t)(cx-7),(lv_value_precise_t)(cy+3)};
    ld.p2={(lv_value_precise_t)(cx+7),(lv_value_precise_t)(cy+3)};
    lv_draw_line(layer,&ld);
    ld.p1={(lv_value_precise_t)(cx-4),(lv_value_precise_t)(cy+8)};
    ld.p2={(lv_value_precise_t)(cx+4),(lv_value_precise_t)(cy+8)};
    lv_draw_line(layer,&ld);
}

// Simple plug icon
static void plugDrawCb(lv_event_t * e) {
    lv_obj_t * obj = (lv_obj_t *)lv_event_get_target(e);
    lv_layer_t * layer = lv_event_get_layer(e);
    lv_area_t coords; lv_obj_get_coords(obj, &coords);
    int cx=(coords.x1+coords.x2)/2, cy=(coords.y1+coords.y2)/2;
    lv_color_t col = lv_color_hex(0x58A6FF);
    lv_draw_rect_dsc_t rd; lv_draw_rect_dsc_init(&rd);
    rd.bg_color=col; rd.bg_opa=LV_OPA_COVER; rd.border_width=0; rd.radius=3;
    // Plug body
    lv_area_t a={cx-6, cy-4, cx+6, cy+8}; lv_draw_rect(layer,&rd,&a);
    // Two prongs
    rd.radius=1;
    lv_area_t p1={cx-4, cy-10, cx-2, cy-4}; lv_draw_rect(layer,&rd,&p1);
    lv_area_t p2={cx+2, cy-10, cx+4, cy-4}; lv_draw_rect(layer,&rd,&p2);
    // Cable
    lv_draw_line_dsc_t ld; lv_draw_line_dsc_init(&ld);
    ld.color=col; ld.width=3; ld.opa=LV_OPA_COVER;
    ld.p1={(lv_value_precise_t)cx,(lv_value_precise_t)(cy+8)};
    ld.p2={(lv_value_precise_t)cx,(lv_value_precise_t)(cy+13)};
    lv_draw_line(layer,&ld);
}

static lv_obj_t * createGridWidget(lv_obj_t * parent, int w, int h) {
    lv_obj_t * card = lv_obj_create(parent);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_bg_color(card, COLOR_WIDGET_BG, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, COLOR_WIDGET_BORDER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_pad_all(card, 2, 0);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    int innerH=h-6, innerW=w-6, rowH=innerH/2;
    int iconSize=rowH-4; if(iconSize>36) iconSize=36;
    int iconCx=iconSize/2, textX=iconSize+2, textW=innerW-textX;

    // Row 1: Insulation
    groundIconObj = lv_obj_create(card);
    lv_obj_set_size(groundIconObj, iconSize, iconSize);
    lv_obj_set_pos(groundIconObj, iconCx-iconSize/2, (rowH-iconSize)/2);
    lv_obj_set_style_bg_opa(groundIconObj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(groundIconObj, 0, 0);
    lv_obj_set_style_pad_all(groundIconObj, 0, 0);
    lv_obj_set_scrollbar_mode(groundIconObj, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(groundIconObj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(groundIconObj, groundDrawCb, LV_EVENT_DRAW_MAIN, NULL);
    lblInsulation = lv_label_create(card);
    lv_label_set_text(lblInsulation, "--");
    lv_obj_set_style_text_color(lblInsulation, lv_color_hex(0x8B949E), 0);
    lv_obj_set_style_text_font(lblInsulation, &lv_font_montserrat_20, 0);
    lv_obj_set_size(lblInsulation, textW, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(lblInsulation, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(lblInsulation, textX, rowH/2-14);
    lv_obj_t * u1=lv_label_create(card); lv_label_set_text(u1,LV_SYMBOL_HOME "");
    lv_obj_set_style_text_color(u1, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(u1, &lv_font_montserrat_10, 0);
    lv_obj_set_pos(u1, innerW-18, rowH/2-9);

    // Separator
    lv_obj_t * sep=lv_obj_create(card); lv_obj_set_size(sep, innerW-4, 1); lv_obj_set_pos(sep, 2, rowH);
    lv_obj_set_style_bg_color(sep, COLOR_WIDGET_BORDER, 0); lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sep, 0, 0); lv_obj_set_style_radius(sep, 0, 0);
    lv_obj_set_style_pad_all(sep, 0, 0); lv_obj_clear_flag(sep, LV_OBJ_FLAG_SCROLLABLE);

    // Row 2: AC Phase
    plugIconObj = lv_obj_create(card);
    lv_obj_set_size(plugIconObj, iconSize, iconSize);
    lv_obj_set_pos(plugIconObj, iconCx-iconSize/2, rowH+(rowH-iconSize)/2);
    lv_obj_set_style_bg_opa(plugIconObj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(plugIconObj, 0, 0);
    lv_obj_set_style_pad_all(plugIconObj, 0, 0);
    lv_obj_set_scrollbar_mode(plugIconObj, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(plugIconObj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(plugIconObj, plugDrawCb, LV_EVENT_DRAW_MAIN, NULL);
    lblAcPhase = lv_label_create(card);
    lv_label_set_text(lblAcPhase, "--");
    lv_obj_set_style_text_color(lblAcPhase, lv_color_hex(0x58A6FF), 0);
    lv_obj_set_style_text_font(lblAcPhase, &lv_font_montserrat_20, 0);
    lv_obj_set_size(lblAcPhase, textW, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(lblAcPhase, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(lblAcPhase, textX, rowH+rowH/2-14);
    return card;
}

// ═══════════════════════════════════════════════════════════
//  12V Widget (12V Voltage + 12V Current + DCDC Load)
// ═══════════════════════════════════════════════════════════

static lv_obj_t * bat12vIconObj = NULL;
static lv_obj_t * lbl12VVolt = NULL;
static lv_obj_t * lbl12VCurrent = NULL;
static lv_obj_t * lblDCDCLoad = NULL;

// 12V battery icon (from C6)
static void bat12VDrawCb(lv_event_t * e) {
    lv_obj_t * obj = (lv_obj_t *)lv_event_get_target(e);
    lv_layer_t * layer = lv_event_get_layer(e);
    lv_area_t coords; lv_obj_get_coords(obj, &coords);
    int cx=(coords.x1+coords.x2)/2, cy=(coords.y1+coords.y2)/2;
    lv_color_t col = lv_color_hex(0xFD8C20); // orange
    lv_draw_rect_dsc_t rd; lv_draw_rect_dsc_init(&rd);
    rd.bg_opa=LV_OPA_TRANSP; rd.border_width=2; rd.border_color=lv_color_hex(0xFFFFFF);
    // Body
    int bw=22, bh=14, bx=cx-bw/2, by=cy-bh/2+2;
    lv_area_t a={bx, by, bx+bw, by+bh}; rd.radius=2; lv_draw_rect(layer,&rd,&a);
    // Terminal posts
    rd.bg_opa=LV_OPA_COVER; rd.bg_color=col; rd.border_width=0; rd.radius=1;
    lv_area_t t1={cx-7, by-4, cx-4, by}; lv_draw_rect(layer,&rd,&t1);
    lv_area_t t2={cx+4, by-4, cx+7, by}; lv_draw_rect(layer,&rd,&t2);
    // "12V" text (small labels)
    lv_draw_label_dsc_t ldsc; lv_draw_label_dsc_init(&ldsc);
    ldsc.color = lv_color_hex(0xFFFFFF); ldsc.font = &lv_font_montserrat_10;
    ldsc.align = LV_TEXT_ALIGN_CENTER;
    lv_area_t ta = {bx, by+1, bx+bw, by+bh};
    ldsc.text = "12V"; ldsc.text_local = true;
    lv_draw_label(layer, &ldsc, &ta);
}

static lv_obj_t * create12VWidget(lv_obj_t * parent, int w, int h) {
    lv_obj_t * card = lv_obj_create(parent);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_bg_color(card, COLOR_WIDGET_BG, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, COLOR_WIDGET_BORDER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_pad_all(card, 2, 0);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    int innerH=h-6, innerW=w-6, rowH=innerH/3;
    int iconSize=rowH-4; if(iconSize>36) iconSize=36;
    int iconCx=iconSize/2, textX=iconSize+2, textW=innerW-textX;

    auto makeSep = [&](int yPos) {
        lv_obj_t * s=lv_obj_create(card); lv_obj_set_size(s, innerW-4, 1); lv_obj_set_pos(s, 2, yPos);
        lv_obj_set_style_bg_color(s, COLOR_WIDGET_BORDER, 0); lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s, 0, 0); lv_obj_set_style_radius(s, 0, 0);
        lv_obj_set_style_pad_all(s, 0, 0); lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);
    };

    // Row 1: 12V Voltage
    bat12vIconObj = lv_obj_create(card);
    lv_obj_set_size(bat12vIconObj, iconSize, iconSize);
    lv_obj_set_pos(bat12vIconObj, iconCx-iconSize/2, (rowH-iconSize)/2);
    lv_obj_set_style_bg_opa(bat12vIconObj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bat12vIconObj, 0, 0);
    lv_obj_set_style_pad_all(bat12vIconObj, 0, 0);
    lv_obj_set_scrollbar_mode(bat12vIconObj, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(bat12vIconObj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(bat12vIconObj, bat12VDrawCb, LV_EVENT_DRAW_MAIN, NULL);
    lbl12VVolt = lv_label_create(card);
    lv_label_set_text(lbl12VVolt, "--");
    lv_obj_set_style_text_color(lbl12VVolt, lv_color_hex(0xFD8C20), 0);
    lv_obj_set_style_text_font(lbl12VVolt, &lv_font_montserrat_24, 0);
    lv_obj_set_size(lbl12VVolt, textW, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(lbl12VVolt, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(lbl12VVolt, textX, rowH/2-16);
    lv_obj_t * u1=lv_label_create(card); lv_label_set_text(u1,"V");
    lv_obj_set_style_text_color(u1, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(u1, &lv_font_montserrat_10, 0);
    lv_obj_set_pos(u1, innerW-12, rowH/2-9);

    makeSep(rowH);

    // Row 2: 12V Current
    lv_obj_t * ampIcon = lv_label_create(card);
    lv_label_set_text(ampIcon, LV_SYMBOL_CHARGE);
    lv_obj_set_style_text_color(ampIcon, lv_color_hex(0x58A6FF), 0);
    lv_obj_set_style_text_font(ampIcon, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(ampIcon, iconCx-10, rowH+(rowH-20)/2);
    lbl12VCurrent = lv_label_create(card);
    lv_label_set_text(lbl12VCurrent, "--");
    lv_obj_set_style_text_color(lbl12VCurrent, lv_color_hex(0x58A6FF), 0);
    lv_obj_set_style_text_font(lbl12VCurrent, &lv_font_montserrat_24, 0);
    lv_obj_set_size(lbl12VCurrent, textW, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(lbl12VCurrent, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(lbl12VCurrent, textX, rowH+rowH/2-16);
    lv_obj_t * u2=lv_label_create(card); lv_label_set_text(u2,"A");
    lv_obj_set_style_text_color(u2, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(u2, &lv_font_montserrat_10, 0);
    lv_obj_set_pos(u2, innerW-12, rowH+rowH/2-9);

    makeSep(rowH*2);

    // Row 3: DCDC Load
    lv_obj_t * dcdcIcon = lv_label_create(card);
    lv_label_set_text(dcdcIcon, LV_SYMBOL_LOOP);
    lv_obj_set_style_text_color(dcdcIcon, lv_color_hex(0xBC8CFF), 0);
    lv_obj_set_style_text_font(dcdcIcon, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(dcdcIcon, iconCx-10, rowH*2+(rowH-20)/2);
    lblDCDCLoad = lv_label_create(card);
    lv_label_set_text(lblDCDCLoad, "--");
    lv_obj_set_style_text_color(lblDCDCLoad, lv_color_hex(0xBC8CFF), 0);
    lv_obj_set_style_text_font(lblDCDCLoad, &lv_font_montserrat_24, 0);
    lv_obj_set_size(lblDCDCLoad, textW, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(lblDCDCLoad, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(lblDCDCLoad, textX, rowH*2+rowH/2-16);
    lv_obj_t * u3=lv_label_create(card); lv_label_set_text(u3,"%");
    lv_obj_set_style_text_color(u3, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(u3, &lv_font_montserrat_10, 0);
    lv_obj_set_pos(u3, innerW-14, rowH*2+rowH/2-9);
    return card;
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
//  Battery Status Widget (SOH + SOC + Available Energy)
//  3 rows: each has [icon] [value] [label]
// ═══════════════════════════════════════════════════════════

static lv_obj_t * createBatteryStatusWidget(lv_obj_t * parent, int w, int h) {
    // ── Card container ──
    lv_obj_t * card = lv_obj_create(parent);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_bg_color(card, COLOR_WIDGET_BG, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, COLOR_WIDGET_BORDER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_pad_top(card, 2, 0);
    lv_obj_set_style_pad_bottom(card, 2, 0);
    lv_obj_set_style_pad_left(card, 2, 0);
    lv_obj_set_style_pad_right(card, 2, 0);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    // Layout: 3 rows filling the card height
    int innerH = h - 6; // 2+2 padding + 2 border
    int innerW = w - 6;
    int rowH = innerH / 3;
    int iconSize = rowH - 4; // icon area height (square draw area)
    if (iconSize > 36) iconSize = 36;

    // Icon column: centered on heart's visual center
    // Heart is the widest icon, battery icons are narrower and drawn within same area
    int iconColW = iconSize; // column width for all icons
    int iconCx = iconColW / 2; // center X of icon column
    // Text area: from icon column right edge to card right edge
    int textAreaX1 = iconColW + 2;
    int textAreaW = innerW - textAreaX1;

    // ═══════════════════════════════════════
    //  Row 1: SOH (Heart icon)
    // ═══════════════════════════════════════
    int row1Y = 0;

    // Heart icon drawing area (reduced by 15% for better visual balance)
    int heartH = (int)(iconSize * 0.85f);
    int heartW = (int)(heartH * 32.0f / 29.0f);
    if (heartW > iconSize) heartW = iconSize;
    initHeartScanlines(heartW, heartH);

    sohHeartObj = lv_obj_create(card);
    lv_obj_set_size(sohHeartObj, heartW, heartH);
    lv_obj_set_pos(sohHeartObj, iconCx - heartW / 2, row1Y + (rowH - heartH) / 2);
    lv_obj_set_style_bg_opa(sohHeartObj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sohHeartObj, 0, 0);
    lv_obj_set_style_pad_all(sohHeartObj, 0, 0);
    lv_obj_set_scrollbar_mode(sohHeartObj, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(sohHeartObj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(sohHeartObj, sohHeartDrawCb, LV_EVENT_DRAW_MAIN, NULL);

    // SOH value (centered in text area)
    lblSohPct = lv_label_create(card);
    lv_label_set_text(lblSohPct, "--%");
    lv_obj_set_style_text_color(lblSohPct, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(lblSohPct, &lv_font_montserrat_24, 0);
    lv_obj_set_size(lblSohPct, textAreaW, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(lblSohPct, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(lblSohPct, textAreaX1, row1Y + rowH / 2 - 16);

    // SOH label (right-aligned in card, moved down 5px)
    lblSohLabel = lv_label_create(card);
    lv_label_set_text(lblSohLabel, "SOH");
    lv_obj_set_style_text_color(lblSohLabel, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(lblSohLabel, &lv_font_montserrat_10, 0);
    lv_obj_set_pos(lblSohLabel, innerW - 24, row1Y + rowH / 2 - 9);

    // ═══════════════════════════════════════
    //  Row 2: SOC (Battery icon)
    // ═══════════════════════════════════════
    int row2Y = rowH;

    // Separator line
    lv_obj_t * sep1 = lv_obj_create(card);
    lv_obj_set_size(sep1, innerW - 4, 1);
    lv_obj_set_pos(sep1, 2, row2Y);
    lv_obj_set_style_bg_color(sep1, COLOR_WIDGET_BORDER, 0);
    lv_obj_set_style_bg_opa(sep1, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sep1, 0, 0);
    lv_obj_set_style_radius(sep1, 0, 0);
    lv_obj_set_style_pad_all(sep1, 0, 0);
    lv_obj_clear_flag(sep1, LV_OBJ_FLAG_SCROLLABLE);

    // Battery icon, centered on same axis as heart
    socBattObj = lv_obj_create(card);
    lv_obj_set_size(socBattObj, iconSize, iconSize);
    lv_obj_set_pos(socBattObj, iconCx - iconSize / 2, row2Y + (rowH - iconSize) / 2);
    lv_obj_set_style_bg_opa(socBattObj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(socBattObj, 0, 0);
    lv_obj_set_style_pad_all(socBattObj, 0, 0);
    lv_obj_set_scrollbar_mode(socBattObj, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(socBattObj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(socBattObj, socBattDrawCb, LV_EVENT_DRAW_MAIN, NULL);

    // SOC value (centered in text area)
    lblSocPct = lv_label_create(card);
    lv_label_set_text(lblSocPct, "--%");
    lv_obj_set_style_text_color(lblSocPct, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(lblSocPct, &lv_font_montserrat_24, 0);
    lv_obj_set_size(lblSocPct, textAreaW, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(lblSocPct, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(lblSocPct, textAreaX1, row2Y + rowH / 2 - 16);

    // SOC label (right-aligned in card, moved down 5px)
    lblSocLabel = lv_label_create(card);
    lv_label_set_text(lblSocLabel, "SOC");
    lv_obj_set_style_text_color(lblSocLabel, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(lblSocLabel, &lv_font_montserrat_10, 0);
    lv_obj_set_pos(lblSocLabel, innerW - 24, row2Y + rowH / 2 - 9);

    // ═══════════════════════════════════════
    //  Row 3: Available Energy (Battery icon, same as SOC)
    // ═══════════════════════════════════════
    int row3Y = rowH * 2;

    // Separator line
    lv_obj_t * sep2 = lv_obj_create(card);
    lv_obj_set_size(sep2, innerW - 4, 1);
    lv_obj_set_pos(sep2, 2, row3Y);
    lv_obj_set_style_bg_color(sep2, COLOR_WIDGET_BORDER, 0);
    lv_obj_set_style_bg_opa(sep2, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sep2, 0, 0);
    lv_obj_set_style_radius(sep2, 0, 0);
    lv_obj_set_style_pad_all(sep2, 0, 0);
    lv_obj_clear_flag(sep2, LV_OBJ_FLAG_SCROLLABLE);

    // Battery icon for energy, centered on same axis as heart
    availEnergyObj = lv_obj_create(card);
    lv_obj_set_size(availEnergyObj, iconSize, iconSize);
    lv_obj_set_pos(availEnergyObj, iconCx - iconSize / 2, row3Y + (rowH - iconSize) / 2);
    lv_obj_set_style_bg_opa(availEnergyObj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(availEnergyObj, 0, 0);
    lv_obj_set_style_pad_all(availEnergyObj, 0, 0);
    lv_obj_set_scrollbar_mode(availEnergyObj, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(availEnergyObj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(availEnergyObj, availEnergyDrawCb, LV_EVENT_DRAW_MAIN, NULL);

    // Available Energy value (centered in text area)
    lblAvailEnergy = lv_label_create(card);
    lv_label_set_text(lblAvailEnergy, "--");
    lv_obj_set_style_text_color(lblAvailEnergy, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(lblAvailEnergy, &lv_font_montserrat_24, 0);
    lv_obj_set_size(lblAvailEnergy, textAreaW, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(lblAvailEnergy, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(lblAvailEnergy, textAreaX1, row3Y + rowH / 2 - 16);

    // kWh label (right-aligned in card, moved down 5px)
    lblAvailEnergyLabel = lv_label_create(card);
    lv_label_set_text(lblAvailEnergyLabel, "kWh");
    lv_obj_set_style_text_color(lblAvailEnergyLabel, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(lblAvailEnergyLabel, &lv_font_montserrat_10, 0);
    lv_obj_set_pos(lblAvailEnergyLabel, innerW - 24, row3Y + rowH / 2 - 9);

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

    // (no separator line)

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
    lv_obj_set_style_text_font(lblMinLabel, &lv_font_montserrat_16, 0);
    lv_obj_align(lblMinLabel, LV_ALIGN_TOP_MID, 0, 0);

    lblCellMin = lv_label_create(boxMin);
    lv_label_set_text(lblCellMin, "-.--");
    lv_obj_set_style_text_color(lblCellMin, COLOR_CYAN, 0);
    lv_obj_set_style_text_font(lblCellMin, &lv_font_montserrat_20, 0);
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
    lv_obj_set_style_text_font(lblMaxLabel, &lv_font_montserrat_16, 0);
    lv_obj_align(lblMaxLabel, LV_ALIGN_TOP_MID, 0, 0);

    lblCellMax = lv_label_create(boxMax);
    lv_label_set_text(lblCellMax, "-.--");
    lv_obj_set_style_text_color(lblCellMax, COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(lblCellMax, &lv_font_montserrat_20, 0);
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
    
    int32_t col = lv_obj_get_x(tile) / SCREEN_W;
    curPage = col;
    currentDashPage = col;
    updatePageDots(curPage);
    rebuildPollFlags(curPage);
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
        // ── Battery Status card: SOH + SOC + BatTemp ──
        // SOH Heart
        if (lblSohPct != NULL) {
            if (obdSOH >= 0) {
                char buf[16];
                snprintf(buf, sizeof(buf), "%d%%", obdSOH);
                lv_label_set_text(lblSohPct, buf);
            } else {
                lv_label_set_text(lblSohPct, "--%");
            }
            if (sohHeartObj) lv_obj_invalidate(sohHeartObj);
        }
        // SOC Battery
        if (lblSocPct != NULL) {
            if (obdSOC >= 0) {
                char buf[16];
                int socInt = (int)(obdSOC + 0.5f);
                snprintf(buf, sizeof(buf), "%d%%", socInt);
                lv_label_set_text(lblSocPct, buf);
                // Update text color to match SOC level
                lv_obj_set_style_text_color(lblSocPct, getSocColor(obdSOC), 0);
            } else {
                lv_label_set_text(lblSocPct, "--%");
            }
            if (socBattObj) lv_obj_invalidate(socBattObj);
        }
        // Available Energy
        if (lblAvailEnergy != NULL) {
            if (obdAvailEnergy >= 0) {
                char buf[16];
                snprintf(buf, sizeof(buf), "%.1f", obdAvailEnergy);
                lv_label_set_text(lblAvailEnergy, buf);
                // Color matches SOC style (0-22 kWh mapped to 0-100%)
                float pct = obdAvailEnergy / 22.0f * 100.0f;
                if (pct > 100) pct = 100;
                lv_obj_set_style_text_color(lblAvailEnergy, getSocColor(pct), 0);
            } else {
                lv_label_set_text(lblAvailEnergy, "--");
            }
            if (availEnergyObj) lv_obj_invalidate(availEnergyObj);
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
        
        // ── Cabin widget updates ──
        // Cabin Temperature
        if (lblCabinTemp != NULL) {
            if (obdCabinTemp > -50) {
                char buf[16];
                snprintf(buf, sizeof(buf), "%.1f", obdCabinTemp);
                lv_label_set_text(lblCabinTemp, buf);
            } else {
                lv_label_set_text(lblCabinTemp, "--");
            }
        }
        // Humidity
        if (lblHumidity != NULL) {
            if (obdHumidity >= 0) {
                char buf[16];
                snprintf(buf, sizeof(buf), "%.0f", obdHumidity);
                lv_label_set_text(lblHumidity, buf);
            } else {
                lv_label_set_text(lblHumidity, "--");
            }
        }
        // Climate Mode
        if (lblClimateMode != NULL) {
            int mode = obdClimateLoopMode;
            if (mode == 1 || mode == 2) {
                lv_label_set_text(lblClimateMode, "Cool");
                lv_obj_set_style_text_color(lblClimateMode, lv_color_hex(0x58A6FF), 0);
            } else if (mode == 4) {
                lv_label_set_text(lblClimateMode, "Heat");
                lv_obj_set_style_text_color(lblClimateMode, lv_color_hex(0xD29922), 0);
            } else {
                lv_label_set_text(lblClimateMode, "Idle");
                lv_obj_set_style_text_color(lblClimateMode, lv_color_hex(0x484F58), 0);
            }
            if (climateIconObj) lv_obj_invalidate(climateIconObj);
        }

        // ── Thermal Widget updates ──
        // External Temperature
        if (lblExtTemp != NULL) {
            if (obdExtTemp > -50) {
                char buf[16];
                snprintf(buf, sizeof(buf), "%.1f", obdExtTemp);
                lv_label_set_text(lblExtTemp, buf);
                lv_obj_set_style_text_color(lblExtTemp, lv_color_hex(0x58A6FF), 0);
            } else {
                lv_label_set_text(lblExtTemp, "--");
            }
        }
        // Battery Temperature
        if (lblBatTemp != NULL) {
            if (obdHVBatTemp > -99) {
                char buf[16];
                snprintf(buf, sizeof(buf), "%.0f", obdHVBatTemp);
                lv_label_set_text(lblBatTemp, buf);
                lv_obj_set_style_text_color(lblBatTemp, getBatTempColor(obdHVBatTemp), 0);
                if (batThermoIconObj) lv_obj_invalidate(batThermoIconObj);
            } else {
                lv_label_set_text(lblBatTemp, "--");
            }
        }
        // Max Charge Power
        if (lblMaxCharge != NULL) {
            if (obdMaxChargePower >= 0) {
                char buf[16];
                snprintf(buf, sizeof(buf), "%.1f", obdMaxChargePower);
                lv_label_set_text(lblMaxCharge, buf);
            } else {
                lv_label_set_text(lblMaxCharge, "--");
            }
        }

        // ── Page 2: HV Widget ──
        if (lblHvVolt != NULL) {
            char buf[16];
            if (obdHVBatVoltage > 0) {
                snprintf(buf, sizeof(buf), "%.0f", obdHVBatVoltage);
                lv_label_set_text(lblHvVolt, buf);
                lv_obj_set_style_text_color(lblHvVolt, getHvVoltColor(obdHVBatVoltage), 0);
            } else { lv_label_set_text(lblHvVolt, "--"); }
        }
        if (lblDcPower != NULL) {
            char buf[16];
            if (obdDCPower > -900) {
                float absP = (obdDCPower < 0) ? -obdDCPower : obdDCPower;
                snprintf(buf, sizeof(buf), "%.1f", absP);
                lv_label_set_text(lblDcPower, buf);
                bool charging = (obdDCPower > 0);
                lv_obj_set_style_text_color(lblDcPower,
                    charging ? lv_color_hex(0x3FB950) : lv_color_hex(0xF85149), 0);
                if (dcArrowObj) lv_obj_invalidate(dcArrowObj);
            } else { lv_label_set_text(lblDcPower, "--"); }
        }

        // ── Page 2: AC Widget (RPM, Pressure, Fan) ──
        if (lblAcRpm != NULL) {
            char buf[16];
            if (obdACRpm >= 0) {
                snprintf(buf, sizeof(buf), "%.0f", obdACRpm);
                lv_label_set_text(lblAcRpm, buf);
                lv_obj_set_style_text_color(lblAcRpm, getAcRpmColor(obdACRpm), 0);
                if (acRpmIconObj) lv_obj_invalidate(acRpmIconObj);
            } else { lv_label_set_text(lblAcRpm, "--"); }
        }
        if (lblAcPressure != NULL) {
            char buf[16];
            if (obdACPressure >= 0) {
                snprintf(buf, sizeof(buf), "%.1f", obdACPressure);
                lv_label_set_text(lblAcPressure, buf);
                lv_obj_set_style_text_color(lblAcPressure, getAcPressureColor(obdACPressure), 0);
                if (acPressIconObj) lv_obj_invalidate(acPressIconObj);
            } else { lv_label_set_text(lblAcPressure, "--"); }
        }
        if (lblFanSpeed != NULL) {
            char buf[16];
            if (obdFanSpeed >= 0) {
                snprintf(buf, sizeof(buf), "%.0f", obdFanSpeed);
                lv_label_set_text(lblFanSpeed, buf);
            } else { lv_label_set_text(lblFanSpeed, "--"); }
        }

        // ── Page 2: Grid Widget (Insulation, AC Phase) ──
        if (lblInsulation != NULL) {
            if (obdInsulationRes >= 0) {
                char buf[16];
                if (obdInsulationRes >= 100000) {
                    lv_label_set_text(lblInsulation, LV_SYMBOL_SHUFFLE); // infinity-like
                } else {
                    float kOhm = obdInsulationRes / 1000.0f;
                    snprintf(buf, sizeof(buf), "%.0f", kOhm);
                    lv_label_set_text(lblInsulation, buf);
                }
                if (groundIconObj) lv_obj_invalidate(groundIconObj);
            } else { lv_label_set_text(lblInsulation, "--"); }
        }
        if (lblAcPhase != NULL) {
            if (obdACPhase >= 0) {
                if (obdACPhase == 0) lv_label_set_text(lblAcPhase, "-");
                else if (obdACPhase == 1) lv_label_set_text(lblAcPhase, "1P");
                else lv_label_set_text(lblAcPhase, "3P");
            } else { lv_label_set_text(lblAcPhase, "--"); }
        }

        // ── Page 2: 12V Widget ──
        if (lbl12VVolt != NULL) {
            char buf[16];
            if (obd12VBatVoltage > 0) {
                snprintf(buf, sizeof(buf), "%.1f", obd12VBatVoltage);
                lv_label_set_text(lbl12VVolt, buf);
            } else { lv_label_set_text(lbl12VVolt, "--"); }
        }
        if (lbl12VCurrent != NULL) {
            char buf[16];
            if (obd12VCurrent >= 0) {
                snprintf(buf, sizeof(buf), "%.0f", obd12VCurrent);
                lv_label_set_text(lbl12VCurrent, buf);
            } else { lv_label_set_text(lbl12VCurrent, "--"); }
        }
        if (lblDCDCLoad != NULL) {
            char buf[16];
            if (obdDCDCLoad >= 0) {
                snprintf(buf, sizeof(buf), "%.0f", obdDCDCLoad);
                lv_label_set_text(lblDCDCLoad, buf);
            } else { lv_label_set_text(lblDCDCLoad, "--"); }
        }

        xSemaphoreGive(obdDataMutex);
    }
}

static void menuBtnClickCb(lv_event_t * e) {
    if (!UiSettings::isVisible()) {
        UiSettings::show();
    }
}
