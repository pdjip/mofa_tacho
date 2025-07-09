#include <Arduino.h>
#include <lvgl.h>

#include "Arduino_H7_Video.h"
#include "Arduino_GigaDisplayTouch.h"
#include "vars.h"

#include "ui/ui.h"
#include "ui/vars.h"

Arduino_H7_Video Display(800, 480, GigaDisplayShield);
Arduino_GigaDisplayTouch  TouchDetector;

void setup() {
    Display.begin();
    TouchDetector.begin();

    lv_init();
    lv_tick_set_cb(millis);

    ui_init();
}

unsigned long next_time_display_update = 0;

constexpr int32_t RPM_TEST_STEP = 500;
constexpr int32_t RPM_MAX = 20000;

unsigned long next_startup_indicator_test_update = 0;
bool indicator_reverse = false;
bool startup_done = false;

void loop() {
    if (millis() >= next_time_display_update)
    {
        next_time_display_update = lv_task_handler() + millis();
        ui_tick();
    }

    if (!startup_done)
    {
        if (millis() >= next_startup_indicator_test_update)
        {
            int32_t rpm = get_var_motor_rpm();
            if (indicator_reverse)
            {
                if (rpm > 0)
                {
                    rpm -= RPM_TEST_STEP;
                }
                else
                {
                    startup_done = true;
                    rpm = 0;
                    set_ui_time(22,55,34);
                    set_ui_velocity(32);
                    set_ui_temperature(60.54);
                }
            }
            else
            {
                if (rpm < RPM_MAX)
                {
                    rpm += RPM_TEST_STEP;
                }
                else
                {
                    indicator_reverse = true;
                    rpm = RPM_MAX;
                }
            }

            set_var_motor_rpm(rpm);
            next_startup_indicator_test_update = millis() + 30;
        }
    }

}
