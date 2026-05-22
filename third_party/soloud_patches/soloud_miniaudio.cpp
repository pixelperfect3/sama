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

namespace SoLoud
{
    ma_device gDevice;
#ifdef __ANDROID__
    // Bug B1 workaround: on Android we explicitly construct a miniaudio
    // context restricted to OpenSL ES (skip AAudio).  See the long comment
    // in miniaudio_init below for the full rationale.  Stored at file
    // scope so deinit can tear it down after ma_device_uninit.
    static ma_context gContext;
    static bool gContextInitialized = false;
#endif

    void soloud_miniaudio_audiomixer(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
    {
        SoLoud::Soloud *soloud = (SoLoud::Soloud *)pDevice->pUserData;
            soloud->mix((float *)pOutput, frameCount);
    }

    static void soloud_miniaudio_deinit(SoLoud::Soloud *aSoloud)
    {
        ma_device_uninit(&gDevice);
#ifdef __ANDROID__
        if (gContextInitialized)
        {
            ma_context_uninit(&gContext);
            gContextInitialized = false;
        }
#endif
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

#ifdef __ANDROID__
        // Bug B1 (sample_game / Pixel 9 SIGSEGV in mapResampleBuffers_internal):
        // when miniaudio's AAudio backend is selected on Android, the AAudio
        // playback callback thread starts running inside ma_device_init —
        // before Soloud::postinit_internal allocates mResampleData[] /
        // mResampleDataOwner[].  The first callback hits a null deref at
        // mapResampleBuffers_internal +120.  100% reproducible on Pixel 9
        // (Tensor G4, Android 16) and would presumably hit any Android 8+
        // device that miniaudio picks AAudio for.
        //
        // Workaround: force OpenSL ES as the only backend on Android.  Its
        // init path does NOT start the callback until ma_device_start, so
        // by the time the callback can fire, postinit_internal has run and
        // mResampleData[] is allocated.  Cost: ~20 ms higher audio latency
        // than AAudio (OpenSL ES is the older Android audio API), which is
        // fine for game SFX.  Re-enable AAudio once the underlying SoLoud
        // ordering is fixed (separate follow-up: patch postinit_internal
        // to allocate mResampleData *before* it calls the backend init at
        // all, so no backend's eager init race matters).
        if (!gContextInitialized)
        {
            ma_backend backends[1] = { ma_backend_opensl };
            ma_context_config contextConfig = ma_context_config_init();
            if (ma_context_init(backends, 1, &contextConfig, &gContext) != MA_SUCCESS)
            {
                return UNKNOWN_ERROR;
            }
            gContextInitialized = true;
        }
        if (ma_device_init(&gContext, &config, &gDevice) != MA_SUCCESS)
        {
            return UNKNOWN_ERROR;
        }
#else
        // Non-Android platforms: keep miniaudio's NULL-context default-
        // backend behaviour (CoreAudio on Apple, WASAPI on Windows, ALSA
        // on Linux).
        if (ma_device_init(NULL, &config, &gDevice) != MA_SUCCESS)
        {
            return UNKNOWN_ERROR;
        }
#endif

        aSoloud->postinit_internal(gDevice.sampleRate, gDevice.playback.internalPeriodSizeInFrames, aFlags, gDevice.playback.channels);

        aSoloud->mBackendCleanupFunc = soloud_miniaudio_deinit;

        ma_device_start(&gDevice);
        aSoloud->mBackendString = "MiniAudio";
        return 0;
    }
};
#endif