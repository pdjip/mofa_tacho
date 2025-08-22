#include <Arduino.h>
#include "ui/vars.h"
#include "vars.h"

#include <stdio.h>

const int32_t RPM_RED_ZONE_BEGIN = 15000;

int32_t motor_rpm = 0;
char motor_rpm_text[7] = "00000";
bool hide_rpm_warning;
char velocity_text[5] = "0";
char temperature_text[5] = "0.0";
char time_text[10] = "99:99:99";
bool gps_online = false;

extern "C" void set_var_motor_rpm(const int32_t value)
{
    if (value >= RPM_RED_ZONE_BEGIN)
    {
        hide_rpm_warning = false;
    }
    else
    {
        hide_rpm_warning = true;
    }

    motor_rpm = value;

    if (motor_rpm > 999999)
    {
        motor_rpm = 999999;
    }
    sprintf(motor_rpm_text, "%05ld", motor_rpm);
}

void set_ui_velocity(const int32_t value)
{
    sprintf(velocity_text, "%ld", value < 10000 ? value : 9999);
}

void set_ui_temperature(const float_t value)
{
    sprintf(temperature_text, "%.1f", value);
}

void set_ui_time(const uint_fast8_t hours, const uint_fast8_t minutes, const uint_fast8_t seconds)
{
    sprintf(time_text, "%02i:%02i:%02i", (hours + 2) % 24, minutes, seconds);
}

extern "C" int32_t get_var_motor_rpm()
{
    return motor_rpm;
}

extern "C" bool get_var_hide_rpm_warning()
{
    return hide_rpm_warning;
}

extern "C" const char *get_var_motor_rpm_text()
{
    return motor_rpm_text;
}

extern "C" const char *get_var_velocity_text()
{
    return velocity_text;
}

extern "C" const char *get_var_temperature_text()
{
    return temperature_text;
}

extern "C" const char *get_var_time_text()
{
    return time_text;
}

extern "C" bool get_var_gps_online()
{
    return gps_online;
}

extern "C" void set_var_time_text(const char *value)
{
}

extern "C" void set_var_temperature_text(const char *value)
{
}


extern "C" void set_var_hide_rpm_warning(bool value)
{
}


extern "C" void set_var_motor_rpm_text(const char *value)
{
}


extern "C" void set_var_velocity_text(const char *value)
{
}


extern "C" void set_var_gps_online(const bool value)
{
    gps_online = !value;
}
