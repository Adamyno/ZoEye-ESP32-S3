#pragma once

#include "lvgl.h"

#define ZOEYEE_VERSION "v0.2.0"

namespace UiBoot {
    // Show boot splash, calls onComplete when animation finishes
    void show(lv_obj_t * screen, void (*onComplete)(void));
}
