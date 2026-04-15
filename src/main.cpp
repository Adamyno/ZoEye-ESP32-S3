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
        // BT is connected - handle CAN layer
        if (!canReady && !canInitInProgress) {
            // CAN not ready yet - try to init (with 5s retry interval)
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
            // Both BT and CAN ready - normal polling
            if (hvacState == HVAC_IDLE && lbcState == LBC_IDLE) {
                hvacState = HVAC_SWITCH_SH; 
            }
            
            if (hvacState != HVAC_IDLE) {
                ObdManager::processHvacStep();
            } else if (lbcState != LBC_IDLE) {
                ObdManager::processLbcStep();
            }
        }
    } else {
        // BT not connected - try reconnect
        String mac = "";
        if (xSemaphoreTake(obdDataMutex, pdMS_TO_TICKS(100))) {
            mac = btTargetMAC;
            xSemaphoreGive(obdDataMutex);
        }
        if (mac.length() > 0 && !connecting && millis() - lastReconnect > RECONNECT_INTERVAL) {
            lastReconnect = millis();
            Serial.println("[OBD] Initiating async auto-reconnect...");
            BluetoothManager::startReconnectTask(mac);
        }
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

// Called when boot animation finishes
static void onBootComplete(void) {
  Serial.println("[SYS] Boot animation done, loading dashboard...");
  UiDashboard::init();
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
  lcd_bl_pwm_bsp_init(LCD_PWM_MODE_255);

  obdDataMutex = xSemaphoreCreateMutex();

  // Load saved BT MAC from NVS for auto-reconnect
  preferences.begin("zoeyee", true); // read-only
  String savedMAC = preferences.getString("bt_mac", "");
  String savedName = preferences.getString("bt_name", "");
  preferences.end();
  if (savedMAC.length() > 0) {
    Serial.printf("[SYS] Saved BT target: %s [%s]\n", savedName.c_str(), savedMAC.c_str());
    if (xSemaphoreTake(obdDataMutex, portMAX_DELAY)) {
        btTargetMAC = savedMAC;
        btTargetName = savedName;
        xSemaphoreGive(obdDataMutex);
    }
  }

  // Init NimBLE stack
  NimBLEDevice::init("ZoEyee");
  NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_PUBLIC);

  xTaskCreatePinnedToCore(
      obdTask,
      "obdTask",
      8192,
      NULL,
      1,
      &obdTaskHandle,
      0
  );

  Serial.println("[SYS] Display initialized.");

  // Show boot splash (mutex-protected)
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
  delay(100);
}
