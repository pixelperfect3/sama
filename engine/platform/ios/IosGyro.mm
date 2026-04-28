#include "engine/platform/ios/IosGyro.h"

#include "engine/input/InputState.h"

#if defined(__APPLE__) && TARGET_OS_IPHONE
#import <CoreMotion/CoreMotion.h>
#endif

namespace engine::platform::ios
{

IosGyro::IosGyro() = default;

IosGyro::~IosGyro()
{
    shutdown();
}

bool IosGyro::init()
{
#if defined(__APPLE__) && TARGET_OS_IPHONE
    CMMotionManager* mgr = [[CMMotionManager alloc] init];
    if (mgr == nil)
        return false;

    // Probe sensor availability up front so we can report it through
    // InputState::gyro_.available even when the user temporarily disables
    // updates via setEnabled(false).
    gyroAvailable_ = mgr.gyroAvailable;
    accelAvailable_ = mgr.accelerometerAvailable;

    if (!gyroAvailable_ && !accelAvailable_)
    {
        // Nothing to read — bail out cleanly.  We do not retain the manager
        // so ARC will release it as soon as this scope exits.
        return false;
    }

    mgr.deviceMotionUpdateInterval = updateIntervalSec_;
    mgr.gyroUpdateInterval = updateIntervalSec_;
    mgr.accelerometerUpdateInterval = updateIntervalSec_;

    // Bridge-retain into a void*: we manually balance with __bridge_transfer
    // in shutdown() so the manager survives across update() calls without
    // needing an ObjC++ ivar in the header.
    motionManager_ = (__bridge_retained void*)mgr;
    return true;
#else
    return false;
#endif
}

void IosGyro::shutdown()
{
#if defined(__APPLE__) && TARGET_OS_IPHONE
    if (!motionManager_)
        return;

    setEnabled(false);

    // __bridge_transfer hands ownership back to ARC; the local strong
    // reference is then released at scope exit.
    CMMotionManager* mgr = (__bridge_transfer CMMotionManager*)motionManager_;
    (void)mgr;
    motionManager_ = nullptr;
#endif
    enabled_ = false;
    gyroAvailable_ = false;
    accelAvailable_ = false;
}

void IosGyro::setEnabled(bool enabled)
{
#if defined(__APPLE__) && TARGET_OS_IPHONE
    if (!motionManager_)
    {
        enabled_ = false;
        return;
    }
    if (enabled == enabled_)
        return;
    enabled_ = enabled;

    CMMotionManager* mgr = (__bridge CMMotionManager*)motionManager_;
    if (enabled)
    {
        // Prefer deviceMotion (sensor fusion) when both gyro and accel are
        // available — it gives stable attitude + gravity in one stream.
        // Otherwise fall back to the raw streams.
        if (mgr.deviceMotionAvailable)
        {
            // Use the X-arbitrary-Z-vertical reference frame: yaw is left
            // undefined at start (no magnetometer dependency), pitch/roll
            // are anchored to gravity.  Matches CMMotion documentation
            // recommendation for games.
            [mgr startDeviceMotionUpdatesUsingReferenceFrame:
                     CMAttitudeReferenceFrameXArbitraryZVertical];
        }
        else
        {
            if (mgr.gyroAvailable)
                [mgr startGyroUpdates];
            if (mgr.accelerometerAvailable)
                [mgr startAccelerometerUpdates];
        }
    }
    else
    {
        if (mgr.deviceMotionActive)
            [mgr stopDeviceMotionUpdates];
        if (mgr.gyroActive)
            [mgr stopGyroUpdates];
        if (mgr.accelerometerActive)
            [mgr stopAccelerometerUpdates];
    }
#else
    (void)enabled;
    enabled_ = false;
#endif
}

void IosGyro::update(engine::input::InputState& state)
{
    state.gyro_.available = gyroAvailable_;

#if defined(__APPLE__) && TARGET_OS_IPHONE
    if (!enabled_ || !motionManager_)
        return;

    CMMotionManager* mgr = (__bridge CMMotionManager*)motionManager_;

    // Pull model: ask for the most recent device-motion sample.  CMMotion
    // returns nil between samples (especially in the first few frames after
    // start-up); when that happens we leave the previous state in place
    // rather than zeroing it, which would manifest as a jolt to game logic.
    CMDeviceMotion* motion = mgr.deviceMotion;
    if (motion != nil)
    {
        // CMRotationRate is in radians/second around the device's local axes
        // (x = roll axis, y = pitch axis, z = yaw axis when held in
        // portrait).  We map to engine convention pitch=X, yaw=Y, roll=Z to
        // match AndroidGyro.  Game code does its own per-orientation
        // remapping if needed.
        state.gyro_.pitchRate = static_cast<float>(motion.rotationRate.x);
        state.gyro_.yawRate = static_cast<float>(motion.rotationRate.y);
        state.gyro_.rollRate = static_cast<float>(motion.rotationRate.z);

        // CMAcceleration's gravity component is already in g-units (-1..1
        // per axis), so no division by 9.81 like Android's accelerometer.
        state.gyro_.gravityX = static_cast<float>(motion.gravity.x);
        state.gyro_.gravityY = static_cast<float>(motion.gravity.y);
        state.gyro_.gravityZ = static_cast<float>(motion.gravity.z);
        return;
    }

    // Fall back to raw gyro / accel data when deviceMotion is not available.
    if (mgr.gyroActive)
    {
        CMGyroData* g = mgr.gyroData;
        if (g != nil)
        {
            state.gyro_.pitchRate = static_cast<float>(g.rotationRate.x);
            state.gyro_.yawRate = static_cast<float>(g.rotationRate.y);
            state.gyro_.rollRate = static_cast<float>(g.rotationRate.z);
        }
    }
    if (mgr.accelerometerActive)
    {
        CMAccelerometerData* a = mgr.accelerometerData;
        if (a != nil)
        {
            // accelerometerData.acceleration is in g-units already.
            state.gyro_.gravityX = static_cast<float>(a.acceleration.x);
            state.gyro_.gravityY = static_cast<float>(a.acceleration.y);
            state.gyro_.gravityZ = static_cast<float>(a.acceleration.z);
        }
    }
#else
    (void)state;
#endif
}

}  // namespace engine::platform::ios
