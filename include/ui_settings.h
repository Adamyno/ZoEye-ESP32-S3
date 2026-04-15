#pragma once

#include "lvgl.h"
#include "ui_dashboard.h"

namespace UiSettings {
    void show(void);          // Show settings (card grid like dashboard)
    void hide(void);          // Return to dashboard
    void showBtMenu(void);    // Bluetooth sub-menu (split screen)
    bool isVisible(void);
}
