#pragma once

#include <TargetConditionals.h>

#include <cstdint>

namespace engine::input
{
class InputState;
}

namespace engine::platform::ios
{

// ---------------------------------------------------------------------------
// IosGyro — wraps CMMotionManager and pushes its samples into an InputState.
//
// Mirrors AndroidGyro's interface (init / shutdown / setEnabled / update) so
// the engine's per-frame call site can stay platform-agnostic:
//
//   if (gyro_) gyro_->update(state);
//
// The implementation differs from Android in one important way: CMMotion
// uses a pull model.  There is no event queue to drain — instead we ask the
// motion manager for the latest deviceMotion sample on every update().  We
// document this divergence here because games that assume "every gyro tick
// is delivered" would over-count on Android and under-count on iOS;  in
// practice game code reads the most-recent state so this is fine.
//
// Sampling rate defaults to 60 Hz to match AndroidGyro::sampleRateUs_.
// ---------------------------------------------------------------------------

class IosGyro
{
public:
    IosGyro();
    ~IosGyro();

    IosGyro(const IosGyro&) = delete;
    IosGyro& operator=(const IosGyro&) = delete;
    IosGyro(IosGyro&&) = delete;
    IosGyro& operator=(IosGyro&&) = delete;

    // Create the underlying CMMotionManager.  Returns false if device motion
    // is unavailable (e.g. simulator without sensor support, or the user
    // denied motion permission on iOS 17+).
    bool init();

    // Stop updates and release the motion manager.  Idempotent.
    void shutdown();

    // Pull the latest device motion sample into state.gyro_.  Cheap to call
    // every frame; if the motion manager has no sample yet this leaves the
    // previous state in place.
    void update(engine::input::InputState& state);

    // Start/stop CMMotion updates.  Off by default; the platform layer turns
    // them on once the engine is ready to consume them.
    void setEnabled(bool enabled);
    [[nodiscard]] bool isEnabled() const
    {
        return enabled_;
    }

    // Available regardless of enabled state — set by init() based on what
    // the device reports.  Maps to InputState::GyroState::available so games
    // can branch when no gyro is present (iPad mini in some configurations).
    [[nodiscard]] bool gyroAvailable() const
    {
        return gyroAvailable_;
    }

private:
    // Held as void* so the header stays plain C++ — this lets non-ObjC++
    // translation units include it without dragging in CoreMotion headers.
    // Cast back to CMMotionManager* inside the .mm.
    void* motionManager_ = nullptr;

    bool enabled_ = false;
    bool gyroAvailable_ = false;
    bool accelAvailable_ = false;

    // Update interval in seconds.  CMMotion uses NSTimeInterval (double).
    double updateIntervalSec_ = 1.0 / 60.0;
};

}  // namespace engine::platform::ios
