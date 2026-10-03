#include "app.h"
#include "../ui/ui.h"
#include "../backend/mock_audio_backend.h"
#include "lvgl.h"

static lv_timer_t* backend_timer;

static void backend_timer_cb(lv_timer_t* timer) {
    g_backend.update();
    (void)timer;
}

void app_init(void) {
    g_backend.init();
    ui_init();
    
    backend_timer = lv_timer_create(backend_timer_cb, 50, NULL);
}
