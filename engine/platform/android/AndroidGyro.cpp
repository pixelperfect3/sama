#ifdef __ANDROID__

#include "engine/platform/android/AndroidGyro.h"

#include <android/looper.h>
#include <android/sensor.h>

#include "engine/input/InputState.h"

namespace engine::platform
{

bool AndroidGyro::init(ALooper* looper)
{
#if __ANDROID_API__ >= 26
    sensorManager_ = ASensorManager_getInstanceForPackage("");
#else
    sensorManager_ = ASensorManager_getInstance();
#endif
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
    if (!eventQueue_)
        return false;

    // Resume-before-init race fix: NativeActivity can fire APP_CMD_RESUME
    // before Engine::initAndroid reaches the gyro-init section.  The
    // resume handler calls setEnabled(true) on the AndroidGyro object —
    // which exists (it was constructed at the top of initAndroid) but
    // its eventQueue_ / gyroscope_ / accelerometer_ pointers are still
    // null.  setEnabled flips enabled_ = true and silently no-ops on the
    // null pointers.  Then by the time initAndroid eventually calls
    // setEnabled(true), the early-return `enabled == enabled_` check
    // fires and the sensors are never actually enabled.  Net: gyro
    // reports avail=1 but pitch/yaw/roll all stuck at 0 forever.
    //
    // Fix: latch the desired state before init, apply it here once the
    // sensor handles are real.
    if (enabled_)
    {
        const bool desired = true;
        enabled_ = false;       // force setEnabled to apply, not no-op
        setEnabled(desired);
    }
    return true;
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

    // Defensive: if init() hasn't run yet, eventQueue_ is null and we
    // can't actually toggle the sensors.  Leave enabled_ latched to the
    // requested value — init() reads it after creating the queue and
    // applies the deferred state.  See "Resume-before-init race fix" in
    // init() for the full story.
    if (!eventQueue_)
        return;

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
