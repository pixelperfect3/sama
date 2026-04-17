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

    // Configurable
    int32_t sampleRateUs_ = 16667;  // ~60Hz default
};

}  // namespace engine::platform
