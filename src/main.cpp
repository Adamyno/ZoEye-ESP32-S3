#include <Arduino.h>
#include "user_config.h"
#include "lvgl_port.h"
#include "i2c_bsp.h"
#include "lcd_bl_pwm_bsp.h"
#include "lvgl.h"
#include "ui_boot.h"
#include "ui_dashboard.h"

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
