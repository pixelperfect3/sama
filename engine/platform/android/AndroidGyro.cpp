#ifdef __ANDROID__

#include "engine/platform/android/AndroidGyro.h"

#include <android/looper.h>
#include <android/sensor.h>

#include "engine/input/InputState.h"

namespace engine::platform
{

bool AndroidGyro::init(ALooper* looper)
{
    sensorManager_ = ASensorManager_getInstance();
    if (!sensorManager_)
        return false;

    gyroscope_ = ASensorManager_getDefaultSensor(sensorManager_, ASENSOR_TYPE_GYROSCOPE);
    accelerometer_ = ASensorManager_getDefaultSensor(sensorManager_, ASENSOR_TYPE_ACCELEROMETER);

    gyroAvailable_ = (gyroscope_ != nullptr);
    accelAvailable_ = (accelerometer_ != nullptr);

    if (!gyroAvailable_ && !accelAvailable_)
        return false;

    // Create event queue on the provided looper
    eventQueue_ = ASensorManager_createEventQueue(sensorManager_, looper, ALOOPER_POLL_CALLBACK,
                                                  nullptr, nullptr);
    return (eventQueue_ != nullptr);
}

void AndroidGyro::shutdown()
{
    if (eventQueue_)
    {
        setEnabled(false);
        ASensorManager_destroyEventQueue(sensorManager_, eventQueue_);
        eventQueue_ = nullptr;
    }
}

void AndroidGyro::setEnabled(bool enabled)
{
    if (enabled == enabled_)
        return;
    enabled_ = enabled;

    if (enabled)
    {
        if (gyroscope_)
        {
            ASensorEventQueue_enableSensor(eventQueue_, gyroscope_);
            ASensorEventQueue_setEventRate(eventQueue_, gyroscope_, sampleRateUs_);
        }
        if (accelerometer_)
        {
            ASensorEventQueue_enableSensor(eventQueue_, accelerometer_);
            ASensorEventQueue_setEventRate(eventQueue_, accelerometer_, sampleRateUs_);
        }
    }
    else
    {
        if (gyroscope_)
            ASensorEventQueue_disableSensor(eventQueue_, gyroscope_);
        if (accelerometer_)
            ASensorEventQueue_disableSensor(eventQueue_, accelerometer_);
    }
}

void AndroidGyro::update(engine::input::InputState& state)
{
    state.gyro_.available = gyroAvailable_;

    if (!enabled_ || !eventQueue_)
        return;

    ASensorEvent event;
    while (ASensorEventQueue_getEvents(eventQueue_, &event, 1) > 0)
    {
        if (event.type == ASENSOR_TYPE_GYROSCOPE)
        {
            state.gyro_.pitchRate = event.gyro.x;
            state.gyro_.yawRate = event.gyro.y;
            state.gyro_.rollRate = event.gyro.z;
        }
        else if (event.type == ASENSOR_TYPE_ACCELEROMETER)
        {
            // Normalize gravity to [-1, 1] range (standard gravity = 9.81 m/s^2)
            constexpr float kGravity = 9.81f;
            state.gyro_.gravityX = event.acceleration.x / kGravity;
            state.gyro_.gravityY = event.acceleration.y / kGravity;
            state.gyro_.gravityZ = event.acceleration.z / kGravity;
        }
    }
}

}  // namespace engine::platform

#endif  // __ANDROID__
