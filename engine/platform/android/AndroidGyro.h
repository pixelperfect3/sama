#pragma once

#include <cstdint>

struct ASensorManager;
struct ASensorEventQueue;
struct ASensor;
struct ALooper;

namespace engine::input
{
class InputState;
}

namespace engine::platform
{

class AndroidGyro
{
public:
    // Initialize gyroscope sensor. Returns false if not available.
    bool init(ALooper* looper);
    void shutdown();

    // Poll sensor events and update InputState::gyro_
    void update(engine::input::InputState& state);

    // Enable/disable sensor (saves battery when not needed)
    void setEnabled(bool enabled);
    bool isEnabled() const
    {
        return enabled_;
    }

private:
    ASensorManager* sensorManager_ = nullptr;
    ASensorEventQueue* eventQueue_ = nullptr;
    const ASensor* gyroscope_ = nullptr;
    const ASensor* accelerometer_ = nullptr;
    bool enabled_ = false;
    bool gyroAvailable_ = false;
    bool accelAvailable_ = false;

    // Sensor sample period in microseconds.  30 Hz default — game-style tilt
    // / parallax responses don't perceptibly improve above ~30 Hz, and the
    // sensor itself burns measurably less power when its hardware fusion
    // loop runs at half-rate (Pixel 6 measurements showed ~5-10 mW
    // continuous savings vs 60 Hz, which is meaningful for low-tier phones
    // with 3000 mAh batteries).  Games that need higher sample rates
    // (e.g. for first-person look) can crank this up before init().  See
    // docs/PERF_AUDIT_2026-05-25.md item #P1.
    int32_t sampleRateUs_ = 33333;  // ~30 Hz default (was 60 Hz)
};

}  // namespace engine::platform
