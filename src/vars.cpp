#include <Arduino.h>
#include "ui/vars.h"
#include "vars.h"

#include <stdio.h>

const int32_t RPM_RED_ZONE_BEGIN = 15000;

int32_t motor_rpm = 0;
char motor_rpm_text[7] = { 0 };
bool hide_rpm_warning;
char velocity_text[5] = { 0 };
char temperature_text[5] = { 0 };
char time_text[10] = "12:25:05";

extern "C" void set_var_motor_rpm(int32_t value) {
    if (value >= RPM_RED_ZONE_BEGIN)
    {
        hide_rpm_warning = false;
    }
    else
    {
        hide_rpm_warning = true;
    }

    motor_rpm = value;

    if (value > 999999)
    {
        value = 999999;
    }
    sprintf(motor_rpm_text, "%05ld", value);
}

void set_ui_velocity(int32_t value) {
    // prevent overflow
    if (value > 9999)
    {
        value = 9999;
    }

    sprintf(velocity_text, "%ld", value);
}

void set_ui_temperature(float_t value)
{
    sprintf(temperature_text, "%.1f", value);
}

void set_ui_time(uint_fast8_t hours, uint_fast8_t minutes, uint_fast8_t seconds)
{
    sprintf(time_text, "%i:%i:%i", hours, minutes, seconds);
}

extern "C" int32_t get_var_motor_rpm() {
    return motor_rpm;
}

extern "C" bool get_var_hide_rpm_warning() {
    return hide_rpm_warning;
}

extern "C" const char *get_var_motor_rpm_text() {
    return motor_rpm_text;
}

extern "C" const char *get_var_velocity_text() {
    return velocity_text;
}

extern "C" const char *get_var_temperature_text() {
    return temperature_text;
}

extern "C" const char *get_var_time_text() {
    return time_text;
}

extern "C" void set_var_time_text(const char *value) {}

extern "C" void set_var_temperature_text(const char *value) {}

extern "C" void set_var_hide_rpm_warning(bool value) {}

extern "C" void set_var_motor_rpm_text(const char *value) {}

extern "C" void set_var_velocity_text(const char *value) {}
