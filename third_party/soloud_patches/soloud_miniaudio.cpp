/*
SoLoud audio engine
Copyright (c) 2013-2020 Jari Komppa

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
claim that you wrote the original software. If you use this software
in a product, an acknowledgment in the product documentation would be
appreciated but is not required.

2. Altered source versions must be plainly marked as such, and must not be
misrepresented as being the original software.

3. This notice may not be removed or altered from any source
distribution.
*/
#include <stdlib.h>

#include "soloud.h"

#if !defined(WITH_MINIAUDIO)

namespace SoLoud
{
    result miniaudio_init(SoLoud::Soloud *aSoloud, unsigned int aFlags, unsigned int aSamplerate, unsigned int aBuffer)
    {
        return NOT_IMPLEMENTED;
    }
}

#else

#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_NULL
#define MA_NO_DECODING
#include "miniaudio.h"
#include <math.h>
#include <atomic>
#include <cstring>

namespace SoLoud
{
    ma_device gDevice;

    // -----------------------------------------------------------------------
    // Bug B1 fix (re-enables AAudio with a race-safe gate)
    //
    // On Android, miniaudio's AAudio backend starts the playback callback
    // thread inside ma_device_init — before SoLoud's postinit_internal has
    // allocated mResampleData[] / mResampleDataOwner[].  The first callback
    // would then call Soloud::mix(), reach mapResampleBuffers_internal, and
    // SIGSEGV at 0x0 dereferencing the null mResampleDataOwner pointer.
    // 100% reproducible on Pixel 9 (Tensor G4, Android 16) with sample_game.
    //
    // Previous fix forced OpenSL ES via a ma_context, which closes the
    // race but trades ~10 ms of audio latency for safety.  The real fix
    // (this one) keeps AAudio for the lower latency and gates the
    // forwarding-to-SoLoud at OUR callback wrapper instead:
    //
    //   * gAudioReady starts false.
    //   * miniaudio_init flips it to true AFTER postinit_internal returns —
    //     at which point mResampleData[] is guaranteed allocated.
    //   * The callback (soloud_miniaudio_audiomixer) reads gAudioReady
    //     with acquire ordering and returns silence when it's still false.
    //
    // Acquire/release pairs guarantee the publisher's writes to mResampleData
    // / mResampleDataOwner happen-before the consumer's reads inside
    // SoLoud::mix.  Atomic bool with a small synchronisation cost (one
    // load.acquire per audio callback = ~1 ns) is the price of correctness;
    // AAudio's ~10 ms latency win is preserved.  The silence-output window
    // is the few-ms gap between ma_device_init and postinit_internal — for
    // a game's app-startup audio this is inaudible (no sounds are loaded
    // yet).
    //
    // The gate ALSO protects against any future miniaudio backend that
    // pre-starts its callback (current candidates: WASAPI on Windows can
    // do this with exclusive-mode streams).  Cheap insurance.
    // -----------------------------------------------------------------------
    static std::atomic<bool> gAudioReady{false};

    void soloud_miniaudio_audiomixer(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
    {
        if (!gAudioReady.load(std::memory_order_acquire))
        {
            // SoLoud not yet fully initialised — output silence so the
            // audio HAL has something to feed the speakers, but don't
            // dereference partially-constructed SoLoud state.
            std::memset(pOutput, 0, frameCount * pDevice->playback.channels * sizeof(float));
            return;
        }
        SoLoud::Soloud *soloud = (SoLoud::Soloud *)pDevice->pUserData;
        soloud->mix((float *)pOutput, frameCount);
    }

    static void soloud_miniaudio_deinit(SoLoud::Soloud *aSoloud)
    {
        // Re-arm the gate so the next init() starts from a clean state.
        // ma_device_uninit will join the callback thread before returning,
        // so racing the store with a final in-flight callback is safe.
        gAudioReady.store(false, std::memory_order_release);
        ma_device_uninit(&gDevice);
    }

    result miniaudio_init(SoLoud::Soloud *aSoloud, unsigned int aFlags, unsigned int aSamplerate, unsigned int aBuffer, unsigned int aChannels)
    {
        ma_device_config config = ma_device_config_init(ma_device_type_playback);
        config.periodSizeInFrames = 128;
        config.playback.format    = ma_format_f32;
        config.playback.channels  = aChannels;
        config.sampleRate         = aSamplerate;
        config.dataCallback       = soloud_miniaudio_audiomixer;
        config.pUserData          = (void *)aSoloud;

        // gAudioReady is false here.  ma_device_init may start the playback
        // callback thread immediately on Android AAudio; that's fine — the
        // callback wrapper above will short-circuit to silence.
        if (ma_device_init(NULL, &config, &gDevice) != MA_SUCCESS)
        {
            return UNKNOWN_ERROR;
        }

        aSoloud->postinit_internal(gDevice.sampleRate, gDevice.playback.internalPeriodSizeInFrames, aFlags, gDevice.playback.channels);

        aSoloud->mBackendCleanupFunc = soloud_miniaudio_deinit;

        // Publication: every prior store in postinit_internal (mResampleData,
        // mResampleDataOwner, mChannels, mSamplerate, mScratch, etc.) is
        // visible to any thread that observes gAudioReady == true via the
        // matching acquire-load in the callback wrapper.
        gAudioReady.store(true, std::memory_order_release);

        ma_device_start(&gDevice);
        aSoloud->mBackendString = "MiniAudio";
        return 0;
    }
};
#endif
