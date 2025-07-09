#include "vars.h"

static int32_t motor_rpm = 0;

int32_t get_var_motor_rpm() {
    return motor_rpm;
}

void set_var_motor_rpm(int32_t value) {
    motor_rpm = value;
}