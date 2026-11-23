/** @file vars.h
 * Display-thread UI model interface. Setup initializes it before threads start; generated-binding strings have static storage and are not caller-owned.
 *
 * Dependencies: GNSS status and pure self-test frames. APIs translate measurement/status inputs to the handwritten model consumed by EEZ getters. No internal lock or heap allocation is provided; callers must respect setup/display-thread ownership.
 */
//
// Created by luna on 09.07.25.
//

#ifndef VARS_H
#define VARS_H

#include "gnss_service.h"
#include "ui_self_test.h"


/** Set displayed speed in km/h; negative hides digits for unavailable data. */
void set_ui_velocity(int32_t value);
/** Set Celsius; sensor fault sentinel is shown as an unavailable marker. */
void set_ui_temperature(float_t value);
/** Convert UTC seconds to local display time; never changes RTC or log timestamps. */
void set_ui_time(int64_t utcSeconds); // UTC seconds since epoch; shown as CET/CEST wall-clock time
void set_ui_time_unavailable();
void set_ui_rpm_warning(bool active);
/** Map connection and fix validity to mutually exclusive status indicators. */
void set_ui_gps_status(const GnssSnapshot &gps);

// What the logger can tell about the USB stick; derived by the caller.
enum class UsbState : uint8_t { NoStick, Mounting, Logging, Error };
/** Map attachment/mount/fault observations to the UI. */
void set_ui_usb_status(UsbState state);
/** Apply a transient startup frame on the display thread. */
void set_ui_self_test(const ui_self_test::Frame &frame);

#endif //VARS_H
