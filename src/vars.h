//
// Created by luna on 09.07.25.
//

#ifndef VARS_H
#define VARS_H

#include "gnss_service.h"
#include "ui_self_test.h"


void set_ui_velocity(int32_t value);
void set_ui_temperature(float_t value);
void set_ui_time(int64_t utcSeconds); // UTC seconds since epoch; shown as CET/CEST wall-clock time
void set_ui_time_unavailable();
void set_ui_rpm_warning(bool active);
void set_ui_gps_status(const GnssSnapshot &gps);

// What the logger can tell about the USB stick; derived by the caller.
enum class UsbState : uint8_t { NoStick, Mounting, Logging, Error };
void set_ui_usb_status(UsbState state);
void set_ui_self_test(const ui_self_test::Frame &frame);

#endif //VARS_H
