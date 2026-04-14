#include <Arduino.h>
#include "user_config.h"
#include "lvgl_port.h"
#include "i2c_bsp.h"
#include "lcd_bl_pwm_bsp.h"
#include "lvgl.h"

void setup()
{
  Serial.begin(115200);
  delay(1000);
  Serial.println("[SYS] Starting ZoEyee ESP32-S3...");

  // Perifériák inicializálása
  i2c_master_Init();
  lvgl_port_init();
  lcd_bl_pwm_bsp_init(LCD_PWM_MODE_255);

  Serial.println("[SYS] Display and UI Drivers Initialized.");

  // LVGL UI építése (Mutexszel védve, amíg megrajzoljuk)
  if (example_lvgl_lock(-1)) {
    lv_obj_t * screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), LV_PART_MAIN);

    // Üdvözlő szöveg
    lv_obj_t * label = lv_label_create(screen);
    lv_label_set_text(label, "ZoEyee ESP32-S3 - Fazis 1 Kesz!");
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    // Középre igazítás némi felső eltolással
    lv_obj_align(label, LV_ALIGN_CENTER, 0, -20);
    
    // Gomb, hogy leteszteljük a touch-ot
    lv_obj_t * btn = lv_button_create(screen);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 30);
    lv_obj_t * btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "Erintsd meg!");

    example_lvgl_unlock();
  }

  Serial.println("[SYS] Boot complete. Free Heap: " + String(ESP.getFreeHeap()));
  Serial.println("[SYS] Free PSRAM: " + String(ESP.getFreePsram()));
}

void loop()
{
  // A Core 0-n (vagy LVGL task specifikus magon) már fut a UI timer.
  // Ideiglenesen a fő loop csak várakozik. 
  delay(100);
}
