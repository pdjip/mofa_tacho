//
// Created by luna on 09.07.25.
//

#ifndef VARS_H
#define VARS_H


void set_ui_velocity(int32_t value);
void set_ui_temperature(float_t value);
void set_ui_time(uint_fast8_t hours, uint_fast8_t minutes, uint_fast8_t seconds);
void set_ui_time_unavailable();
void set_ui_rpm_warning(bool active);

#endif //VARS_H
