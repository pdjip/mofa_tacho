/** @file rpm_display_filter.h
 * Display-only adaptive RPM smoothing. Does not affect raw logging or physical warning output; update belongs exclusively to the display thread.
 *
 * Pure float/integer policy consumed by displayTask; input raw RPM and host elapsed time, output displayed integer RPM. Instance state is single-owner with boot/task lifetime. Tuning constants approximate jitter tolerance and are not a sensor calibration.
 */
#ifndef MOFA_RPM_DISPLAY_FILTER_H
#define MOFA_RPM_DISPLAY_FILTER_H

#include <stdint.h>

// Display-thread state only. Raw RPM and the hardware warning never use this filter.
class RpmDisplayFilter {
public:
    static constexpr float deadzone(float rpm)
    {
        // Two microseconds of period uncertainty at two pulses/revolution,
        // plus a small baseline. This is a tuning assumption, not a measurement.
        return 2.0f + rpm * rpm / 15000000.0f;
    }

    static constexpr float timeConstantMs(float error, float zone)
    {
        const float relative = error / (200.0f + 4.0f * zone);
        return 30.0f + 270.0f / (1.0f + relative * relative);
    }

    /** Display-thread state update: raw RPM plus wrapping host milliseconds produce integer displayed RPM. Nonpositive/NaN input resets display state to zero. No hardware warning or logger state is changed. */
    constexpr int32_t update(float raw, uint32_t nowMs)
    {
        const uint32_t elapsed = initialized_ ? nowMs - lastMs_ : 0;
        initialized_ = true;
        lastMs_ = nowMs;
        if (!(raw > 0.0f)) {
            target_ = filtered_ = 0.0f;
            shown_ = 0;
            return shown_;
        }
        const float zone = deadzone(raw);
        const float difference = raw - target_;
        if (difference > zone || difference < -zone) target_ = raw;
        const float error = target_ - filtered_;
        const float tau = timeConstantMs(error, zone);
        const float alpha = float(elapsed) / (tau + float(elapsed));
        filtered_ += alpha * error;
        // A 0.6-RPM Schmitt band prevents chatter at rounding half-integers and
        // keeps a steady value within 0.6 of the filtered one (a 1.0 band left it
        // up to one RPM off).
        if (filtered_ > float(shown_) + 0.6f || filtered_ < float(shown_) - 0.6f)
            shown_ = static_cast<int32_t>(filtered_ + 0.5f);
        return shown_;
    }

private:
    bool initialized_ = false;
    uint32_t lastMs_ = 0;
    float target_ = 0.0f;
    float filtered_ = 0.0f;
    int32_t shown_ = 0;
};

#endif
