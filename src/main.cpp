#include <Arduino.h>
#include <lvgl.h>

#include "Arduino_H7_Video.h"
#include "Arduino_GigaDisplayTouch.h"

#include "ui/ui.h"
#include "ui/vars.h"

LV_FONT_DECLARE(lv_font_montserrat_14);

Arduino_H7_Video Display(800, 480, GigaDisplayShield);
Arduino_GigaDisplayTouch  TouchDetector;

void setup() {
    Display.begin();
    TouchDetector.begin();

    lv_init();
    lv_tick_set_cb(millis);

    ui_init();
}

unsigned long nextTimeDisplayUpdate = 0;

constexpr int32_t RPM_TEST_STEP = 10;
unsigned long nextStartupIndicatorTestUpdate = 0;
bool indicatorReverse = false;
bool startupDone = false;

void loop() {
    if (millis() >= nextTimeDisplayUpdate)
    {
        nextTimeDisplayUpdate = lv_task_handler() + millis();
        ui_tick();
    }

    if (!startupDone)
    {
        if (millis() >= nextStartupIndicatorTestUpdate)
        {
            int32_t rpm = get_var_motor_rpm();
            if (indicatorReverse)
            {
                if (rpm > 0)
                {
                    rpm -= RPM_TEST_STEP;
                }
                else
                {
                    startupDone = false;
                    rpm = 0;
                }
            }
            else
            {
                if (rpm < 10000)
                {
                    rpm += RPM_TEST_STEP;
                }
                else
                {
                    indicatorReverse = true;
                    rpm = 10000;
                }
            }
            set_var_motor_rpm(rpm);
            nextStartupIndicatorTestUpdate = millis() + 1;
        }
    }

}
