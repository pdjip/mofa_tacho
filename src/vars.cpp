#include <Arduino.h>
#include "ui/vars.h"
#include "vars.h"
#include "gnss_service.h"
#include "ui_self_test.h"

#include <stdio.h>

int32_t motor_rpm = 0;
char motor_rpm_text[20] = "00000";
bool hide_rpm_warning = true;
char velocity_text[20] = "0";
char temperature_text[20] = "0.0";
char time_text[20] = "99:99:99";
bool gps_online = false;
char gps_status_text[24] = "GPS: searching";
bool hide_gps_ok = true;
bool hide_gps_busy = false;
bool hide_gps_error = true;

extern "C" void set_var_motor_rpm(const int32_t value)
{
    motor_rpm = value;

    if (motor_rpm > 999999)
    {
        motor_rpm = 999999;
    }
    sprintf(motor_rpm_text, "%05ld", motor_rpm);
}

void set_ui_rpm_warning(bool active)
{
    hide_rpm_warning = !active;
}

void set_ui_velocity(const int32_t value)
{
    if (value < 0)
        velocity_text[0] = '\0'; // The generated speed font contains digits only.
    else
        snprintf(velocity_text, sizeof(velocity_text), "%ld", value < 10000 ? value : 9999);
}

void set_ui_temperature(const float_t value)
{
    if (value > -120.0) {
        sprintf(temperature_text, "%.1f", value);
    } else {
        sprintf(temperature_text, "%s", "000");
    }
}

void set_ui_time(const uint_fast8_t hours, const uint_fast8_t minutes, const uint_fast8_t seconds)
{
    sprintf(time_text, "%02i:%02i:%02i", (hours + 2) % 24, minutes, seconds);
}

void set_ui_gps_status(const GnssSnapshot &gps)
{
    enum class Level { Busy, Ok, Error };
    const char *text = "GPS: searching";
    Level level = Level::Busy;

    switch (gps.connection)
    {
    case GnssConnection::Searching:
        text = "GPS: searching";
        break;
    case GnssConnection::Configuring:
        text = "GPS: starting";
        break;
    case GnssConnection::Receiving:
        if (gps.positionValid)
        {
            text = gps.altitudeValid ? "GPS: 3D fix" : "GPS: 2D fix";
            level = Level::Ok;
        }
        else
            text = "GPS: no fix";
        break;
    case GnssConnection::Stale:
        // Also set deliberately once UTC becomes valid (planned new session),
        // so it is a reconnect in progress, not a fault.
        text = "GPS: reconnecting";
        break;
    case GnssConnection::RetryWait:
        text = "GPS: retrying";
        break;
    case GnssConnection::ConfigError:
        text = "GPS: config error";
        level = Level::Error;
        break;
    case GnssConnection::RateError:
        text = "GPS: rate error";
        level = Level::Error;
        break;
    case GnssConnection::ThreadError:
        text = "GPS: thread error";
        level = Level::Error;
        break;
    }

    snprintf(gps_status_text, sizeof(gps_status_text), "%s", text);
    hide_gps_ok = level != Level::Ok;
    hide_gps_busy = level != Level::Busy;
    hide_gps_error = level != Level::Error;
}

void set_ui_self_test(const ui_self_test::Frame &frame)
{
    using ui_self_test::GpsStep;

    set_var_motor_rpm(frame.rpm);
    set_ui_rpm_warning(frame.rpmWarning);
    set_ui_velocity(frame.speedKph);
    set_ui_temperature(frame.temperatureC);
    snprintf(time_text, sizeof(time_text), "88:88:88"); // Lights every clock segment.
    set_var_gps_online(true);                           // Satellite icon visible.

    GnssSnapshot gps;
    switch (frame.gps)
    {
    case GpsStep::Searching:   gps.connection = GnssConnection::Searching; break;
    case GpsStep::Configuring: gps.connection = GnssConnection::Configuring; break;
    case GpsStep::NoFix:       gps.connection = GnssConnection::Receiving; break;
    case GpsStep::Fix2D:
        gps.connection = GnssConnection::Receiving;
        gps.positionValid = true;
        break;
    case GpsStep::Fix3D:
        gps.connection = GnssConnection::Receiving;
        gps.positionValid = true;
        gps.altitudeValid = true;
        break;
    case GpsStep::Stale:       gps.connection = GnssConnection::Stale; break;
    case GpsStep::RetryWait:   gps.connection = GnssConnection::RetryWait; break;
    case GpsStep::ConfigError: gps.connection = GnssConnection::ConfigError; break;
    case GpsStep::RateError:   gps.connection = GnssConnection::RateError; break;
    case GpsStep::ThreadError: gps.connection = GnssConnection::ThreadError; break;
    }
    set_ui_gps_status(gps);
}

void set_ui_time_unavailable()
{
    // Dots are available in the generated clock font; hyphens are not.
    snprintf(time_text, sizeof(time_text), "...");
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

extern "C" const char *get_var_gps_status_text()
{
    return gps_status_text;
}

extern "C" bool get_var_hide_gps_ok()
{
    return hide_gps_ok;
}

extern "C" bool get_var_hide_gps_busy()
{
    return hide_gps_busy;
}

extern "C" bool get_var_hide_gps_error()
{
    return hide_gps_error;
}

extern "C" void set_var_gps_status_text(const char *value)
{
}

extern "C" void set_var_hide_gps_ok(bool value)
{
}

extern "C" void set_var_hide_gps_busy(bool value)
{
}

extern "C" void set_var_hide_gps_error(bool value)
{
}
