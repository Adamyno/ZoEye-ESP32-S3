#pragma once

#include "lvgl.h"
#include "obd_globals.h"  // ZOEYE_VERSION is defined there

namespace UiBoot {
    // Show boot splash, calls onComplete when animation finishes
    void show(lv_obj_t * screen, void (*onComplete)(void));
}
