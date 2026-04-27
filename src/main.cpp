#include <Arduino.h>
#include "user_config.h"
#include "lvgl_port.h"
#include "i2c_bsp.h"
#include "lcd_bl_pwm_bsp.h"
#include "lvgl.h"
#include "ui_boot.h"
#include "ui_dashboard.h"
#include "obd_globals.h"
#include "ObdManager.h"
#include "BluetoothManager.h"
#include <NimBLEDevice.h>

static TaskHandle_t obdTaskHandle = NULL;

// ─── Brightness Control ─────────────────────────────────────
// Hardware PWM: 0 = full brightness, 255 = completely dark
static volatile int currentBrightnessPct = 100; // 1-100%
static volatile bool brightnessBtnPressed = false;
static unsigned long lastBtnTime = 0;

static uint16_t percentToDuty(int pct) {
    if (pct < 1) pct = 1;
    if (pct > 100) pct = 100;
    // Active low: 0 = max brightness, 255 = completely dark
    // The previous math showed that 26% (duty 165) was the lowest working value.
    // Map 1-100% to 165-0 so 1% is precisely the dimmest visible level.
    int range = 165;
    int duty = range - ((pct - 1) * range / 99);
    return (uint16_t)duty;
}

void setSystemBrightness(int pct) {
    if (pct < 1) pct = 1;
    if (pct > 100) pct = 100;
    currentBrightnessPct = pct;
    setUpduty(percentToDuty(pct));
    
    // Save to NVS
    preferences.begin("zoeyee", false);
    preferences.putInt("bl_pct", pct);
    preferences.end();
}

static void IRAM_ATTR btnBrightnessISR() {
    brightnessBtnPressed = true;
}

void obdTask(void *pvParameters) {
  Serial.println("[OBD] Background task started on Core 0");
  const unsigned long RECONNECT_INTERVAL = 15000;
  const unsigned long CAN_RETRY_INTERVAL = 5000;
  unsigned long lastReconnect = 0;
  unsigned long lastCanRetry = 0;
  bool canInitInProgress = false;
  
  while (true) {
    bool connected = false;
    bool connecting = false;
    bool canReady = false;
    if (xSemaphoreTake(obdDataMutex, pdMS_TO_TICKS(100))) {
        connected = isBluetoothConnected;
        connecting = bleConnecting;
        canReady = obdCanConnected;
        xSemaphoreGive(obdDataMutex);
    }
    
    if (connected && !connecting) {
        if (!canReady && !canInitInProgress) {
            if (millis() - lastCanRetry > CAN_RETRY_INTERVAL) {
                lastCanRetry = millis();
                canInitInProgress = true;
                Serial.println("[OBD] Attempting CAN/OBD init...");
                bool ok = ObdManager::initOBD();
                canInitInProgress = false;
                if (ok) {
                    Serial.println("[OBD] CAN/OBD init success!");
                    if (xSemaphoreTake(obdDataMutex, portMAX_DELAY)) {
                        obdCanConnected = true;
                        xSemaphoreGive(obdDataMutex);
                    }
                } else {
                    Serial.println("[OBD] CAN/OBD init failed, will retry...");
                }
            }
        } else if (canReady) {
            if (hvacState != HVAC_IDLE) {
                ObdManager::processHvacStep();
            } else if (evcState != EVC_IDLE) {
                ObdManager::processEvcStep();
            } else if (lbcState != LBC_IDLE) {
                ObdManager::processLbcStep();
            } else {
                int page = UiDashboard::getCurrentPage();
                static int pollPhase = 0;
                
                if (page == 0) {
                    switch (pollPhase % 3) {
                        case 0: hvacState = HVAC_SWITCH_SH; break;
                        case 1: evcState = EVC_SWITCH_SH;   break;
                        case 2: lbcState = LBC_SWITCH_SH;   break;
                    }
                } else if (page == 1) {
                    evcState = EVC_SWITCH_SH;
                } else {
                    switch (pollPhase % 3) {
                        case 0: hvacState = HVAC_SWITCH_SH; break;
                        case 1: evcState = EVC_SWITCH_SH;   break;
                        case 2: lbcState = LBC_SWITCH_SH;   break;
                    }
                }
                pollPhase++;
            }
        }
    } else {
        String mac = "";
        uint8_t atype = 0;
        if (xSemaphoreTake(obdDataMutex, pdMS_TO_TICKS(100))) {
            mac = btTargetMAC;
            atype = btTargetType;
            xSemaphoreGive(obdDataMutex);
        }
        if (mac.length() > 0 && !connecting && millis() - lastReconnect > RECONNECT_INTERVAL) {
            lastReconnect = millis();
            Serial.println("[OBD] Initiating async auto-reconnect...");
            BluetoothManager::startReconnectTask(mac, atype);
        }
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

// ═══════════════════════════════════════════════════════════
//  Touch Debug Overlay
// ═══════════════════════════════════════════════════════════
#define TOUCH_DEBUG_ENABLED true
#define TOUCH_DOT_SIZE 10
#define TOUCH_DOT_LIFETIME_MS 2000

#if TOUCH_DEBUG_ENABLED
static void touchDebugIndevCb(lv_event_t *e) {
  lv_indev_t *indev = (lv_indev_t *)lv_event_get_target(e);
  if (indev == NULL) return;
  lv_point_t point;
  lv_indev_get_point(indev, &point);
  if (point.x == 0 && point.y == 0) return;
  lv_obj_t *dot = lv_obj_create(lv_layer_top());
  lv_obj_set_size(dot, TOUCH_DOT_SIZE, TOUCH_DOT_SIZE);
  lv_obj_set_pos(dot, point.x - TOUCH_DOT_SIZE / 2, point.y - TOUCH_DOT_SIZE / 2);
  lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(dot, lv_color_hex(0xFF0000), 0);
  lv_obj_set_style_bg_opa(dot, LV_OPA_80, 0);
  lv_obj_set_style_border_width(dot, 0, 0);
  lv_obj_set_style_pad_all(dot, 0, 0);
  lv_obj_clear_flag(dot, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  lv_obj_set_scrollbar_mode(dot, LV_SCROLLBAR_MODE_OFF);
  lv_obj_delete_delayed(dot, TOUCH_DOT_LIFETIME_MS);
}

static void setupTouchDebug(void) {
  lv_indev_t *indev = lv_indev_get_next(NULL);
  if (indev) {
    lv_indev_add_event_cb(indev, touchDebugIndevCb, LV_EVENT_PRESSED, NULL);
    Serial.println("[SYS] Touch debug overlay ENABLED (indev callback)");
  }
}
#endif

// Called when boot animation finishes
static void onBootComplete(void) {
  Serial.println("[SYS] Boot animation done, loading dashboard...");
  UiDashboard::init();
#if TOUCH_DEBUG_ENABLED
  setupTouchDebug();
#endif
}

void setup()
{
  Serial.begin(115200);
  delay(500);
  Serial.println("[SYS] ============================");
  Serial.println("[SYS] ZoEyee ESP32-S3 " ZOEYEE_VERSION);
  Serial.println("[SYS] ============================");

  // Hardware init
  i2c_master_Init();
  lvgl_port_init();

  // Load saved brightness from NVS
  preferences.begin("zoeyee", true);
  // Try new key first, fallback to 100
  currentBrightnessPct = preferences.getInt("bl_pct", 100);
  if (currentBrightnessPct < 1 || currentBrightnessPct > 100) currentBrightnessPct = 100;
  preferences.end();

  lcd_bl_pwm_bsp_init(percentToDuty(currentBrightnessPct));
  Serial.printf("[SYS] Brightness = %d%%\n", currentBrightnessPct);

  // BOOT button = brightness cycling
  pinMode(BTN_BRIGHTNESS_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BTN_BRIGHTNESS_PIN), btnBrightnessISR, FALLING);

  obdDataMutex = xSemaphoreCreateMutex();

  // Load saved BT MAC from NVS for auto-reconnect
  preferences.begin("zoeyee", true);
  String savedMAC = preferences.getString("bt_mac", "");
  String savedName = preferences.getString("bt_name", "");
  uint8_t savedType = preferences.getUChar("bt_type", 0);
  preferences.end();
  if (savedMAC.length() > 0) {
    Serial.printf("[SYS] Saved BT target: %s [%s] type=%d\n", savedName.c_str(), savedMAC.c_str(), savedType);
    if (xSemaphoreTake(obdDataMutex, portMAX_DELAY)) {
        btTargetMAC = savedMAC;
        btTargetName = savedName;
        btTargetType = savedType;
        xSemaphoreGive(obdDataMutex);
    }
  }

  NimBLEDevice::init("ZoEyee");
  NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_PUBLIC);

  xTaskCreatePinnedToCore(
      obdTask, "obdTask", 8192, NULL, 1, &obdTaskHandle, 0
  );

  Serial.println("[SYS] Display initialized.");

  if (example_lvgl_lock(-1)) {
    UiBoot::show(lv_screen_active(), onBootComplete);
    example_lvgl_unlock();
  }

  Serial.printf("[SYS] Free Heap: %d bytes\n", ESP.getFreeHeap());
  Serial.printf("[SYS] Free PSRAM: %d bytes\n", ESP.getFreePsram());
  Serial.printf("[SYS] PSRAM Size: %d bytes\n", ESP.getPsramSize());
  Serial.println("[SYS] Boot sequence started.");
}

void loop()
{
  // Handle brightness button press (debounced from ISR)
  if (brightnessBtnPressed) {
    unsigned long now = millis();
    if (now - lastBtnTime > 250) {
      lastBtnTime = now;
      int pct = currentBrightnessPct;
      // Cycle: 1 -> 5 -> 20 -> 65 -> 100 -> 1
      if (pct < 5) pct = 5;
      else if (pct < 20) pct = 20;
      else if (pct < 65) pct = 65;
      else if (pct < 100) pct = 100;
      else pct = 1;
      
      setSystemBrightness(pct);
      Serial.printf("[SYS] Brightness → %d%%\n", pct);
    }
    brightnessBtnPressed = false;
  }
  delay(50);
}
