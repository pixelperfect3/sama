#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "tools/asset_tool/AssetProcessor.h"

namespace engine::tools
{

// ---------------------------------------------------------------------------
// AudioProcessor — discovers .wav inputs and transcodes to Opus-in-Ogg.
//
// Implementation strategy:
//   * Decode WAV with dr_wav (vendored single-header).
//   * Encode with libopus (libopusenc would be cleaner but adds another
//     autoconf dependency; we wrap raw Opus packets in Ogg pages ourselves —
//     the Ogg framing for a single logical stream is small and well-defined).
//   * Per-tier target bitrate: low=48k, mid=64k, high=96k.
//
// Output: <name>.opus (Ogg-wrapped Opus, 48 kHz, mono or stereo matching the
// input WAV).
//
// Compiled conditionally on SAMA_WITH_OPUS — if libopus isn't available the
// processor falls back to copying .wav through untouched (and the
// transcode-path tests are skipped).
// ---------------------------------------------------------------------------

class AudioProcessor
{
public:
    AudioProcessor(const CliArgs& args, const TierConfig& tier);

    /// Discover .wav files under the input directory.
    std::vector<AssetEntry> discover();

    /// Encode/copy all audio entries.
    void processAll(std::vector<AssetEntry>& entries);

    /// True if this build was compiled with libopus support.
    static bool hasOpusSupport();

    /// In-memory: encode a mono/stereo PCM buffer (float32 samples in [-1,1])
    /// at the given sample rate to an Ogg-Opus blob.
    /// Returns true on success.
    static bool encodeOpus(const std::vector<float>& samples, int channels, int sampleRate,
                           int bitrateBps, std::vector<uint8_t>& outBytes);

    /// In-memory: decode an Ogg-Opus blob back to float32 PCM at 48 kHz.
    /// Returns true on success.  Sets channelsOut to 1 or 2.
    static bool decodeOpus(const std::vector<uint8_t>& inBytes, std::vector<float>& outSamples,
                           int& channelsOut);

    /// Read the encoder-bitrate hint we stamp into the OpusTags comment block.
    /// Returns -1 if the blob doesn't carry a Sama bitrate tag.
    static int readEncodedBitrate(const std::vector<uint8_t>& inBytes);

private:
    bool processOne(AssetEntry& entry);

    CliArgs args_;
    TierConfig tier_;
};

}  // namespace engine::tools
