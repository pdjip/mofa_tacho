#ifndef EEZ_LVGL_UI_VARS_H
#define EEZ_LVGL_UI_VARS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// enum declarations

// Flow global variables

enum FlowGlobalVariables {
    FLOW_GLOBAL_VARIABLE_MOTOR_RPM = 0,
    FLOW_GLOBAL_VARIABLE_HIDE_RPM_WARNING = 1,
    FLOW_GLOBAL_VARIABLE_MOTOR_RPM_TEXT = 2,
    FLOW_GLOBAL_VARIABLE_VELOCITY_TEXT = 3,
    FLOW_GLOBAL_VARIABLE_TEMPERATURE_TEXT = 4,
    FLOW_GLOBAL_VARIABLE_TIME_TEXT = 5,
    FLOW_GLOBAL_VARIABLE_GPS_ONLINE = 6,
    FLOW_GLOBAL_VARIABLE_GPS_STATUS_TEXT = 7,
    FLOW_GLOBAL_VARIABLE_HIDE_GPS_OK = 8,
    FLOW_GLOBAL_VARIABLE_HIDE_GPS_BUSY = 9,
    FLOW_GLOBAL_VARIABLE_HIDE_GPS_ERROR = 10,
    FLOW_GLOBAL_VARIABLE_USB_STATUS_TEXT = 11,
    FLOW_GLOBAL_VARIABLE_HIDE_USB_OK = 12,
    FLOW_GLOBAL_VARIABLE_HIDE_USB_BUSY = 13,
    FLOW_GLOBAL_VARIABLE_HIDE_USB_ERROR = 14
};

// Native global variables

extern int32_t get_var_motor_rpm();
extern void set_var_motor_rpm(int32_t value);
extern bool get_var_hide_rpm_warning();
extern void set_var_hide_rpm_warning(bool value);
extern const char *get_var_motor_rpm_text();
extern void set_var_motor_rpm_text(const char *value);
extern const char *get_var_velocity_text();
extern void set_var_velocity_text(const char *value);
extern const char *get_var_temperature_text();
extern void set_var_temperature_text(const char *value);
extern const char *get_var_time_text();
extern void set_var_time_text(const char *value);
extern bool get_var_gps_online();
extern void set_var_gps_online(bool value);
extern const char *get_var_gps_status_text();
extern void set_var_gps_status_text(const char *value);
extern bool get_var_hide_gps_ok();
extern void set_var_hide_gps_ok(bool value);
extern bool get_var_hide_gps_busy();
extern void set_var_hide_gps_busy(bool value);
extern bool get_var_hide_gps_error();
extern void set_var_hide_gps_error(bool value);
extern const char *get_var_usb_status_text();
extern void set_var_usb_status_text(const char *value);
extern bool get_var_hide_usb_ok();
extern void set_var_hide_usb_ok(bool value);
extern bool get_var_hide_usb_busy();
extern void set_var_hide_usb_busy(bool value);
extern bool get_var_hide_usb_error();
extern void set_var_hide_usb_error(bool value);

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_VARS_H*/