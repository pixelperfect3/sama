#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <string>
#include <vector>

#include "tools/asset_tool/AssetProcessor.h"
#include "tools/asset_tool/AudioProcessor.h"

namespace fs = std::filesystem;
using namespace engine::tools;

namespace
{

// Write a minimal little-endian PCM-16 WAV file.
void writeSineWav(const fs::path& path, int sampleRate, int channels, float durationSec,
                  float freqHz)
{
    const int totalFrames = static_cast<int>(sampleRate * durationSec);
    const int totalSamples = totalFrames * channels;
    const int byteRate = sampleRate * channels * 2;
    const int dataBytes = totalSamples * 2;
    const int fileBytes = 36 + dataBytes;

    std::ofstream f(path, std::ios::binary);
    auto wU32 = [&](uint32_t v)
    {
        char b[4] = {static_cast<char>(v & 0xFF), static_cast<char>((v >> 8) & 0xFF),
                     static_cast<char>((v >> 16) & 0xFF), static_cast<char>((v >> 24) & 0xFF)};
        f.write(b, 4);
    };
    auto wU16 = [&](uint16_t v)
    {
        char b[2] = {static_cast<char>(v & 0xFF), static_cast<char>((v >> 8) & 0xFF)};
        f.write(b, 2);
    };

    f.write("RIFF", 4);
    wU32(static_cast<uint32_t>(fileBytes));
    f.write("WAVE", 4);
    f.write("fmt ", 4);
    wU32(16);
    wU16(1);  // PCM
    wU16(static_cast<uint16_t>(channels));
    wU32(static_cast<uint32_t>(sampleRate));
    wU32(static_cast<uint32_t>(byteRate));
    wU16(static_cast<uint16_t>(channels * 2));  // block align
    wU16(16);                                   // bits per sample
    f.write("data", 4);
    wU32(static_cast<uint32_t>(dataBytes));

    for (int i = 0; i < totalFrames; ++i)
    {
        const float t = static_cast<float>(i) / sampleRate;
        const float v = std::sin(2.0f * 3.14159265358979f * freqHz * t);
        const int16_t s = static_cast<int16_t>(v * 30000.0f);
        for (int c = 0; c < channels; ++c)
        {
            wU16(static_cast<uint16_t>(s));
        }
    }
}

class TempDir
{
public:
    TempDir()
    {
        path_ = fs::temp_directory_path() /
                ("sama_audio_test_" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(path_);
    }

    ~TempDir()
    {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }

    const fs::path& path() const
    {
        return path_;
    }

private:
    fs::path path_;
};

}  // namespace

// ---------------------------------------------------------------------------
// 1. Bitrate tag is observable in the encoded blob (works even in fallback,
//    since the test crafts its own blob if needed — but only meaningful when
//    encoding succeeded).
// ---------------------------------------------------------------------------

TEST_CASE("AudioProcessor::readEncodedBitrate finds tag string", "[asset_tool][audio]")
{
    std::vector<uint8_t> blob;
    const std::string tag = "SAMA_BITRATE_BPS=64000";
    blob.assign(tag.begin(), tag.end());
    CHECK(AudioProcessor::readEncodedBitrate(blob) == 64000);

    // No tag.
    std::vector<uint8_t> empty;
    CHECK(AudioProcessor::readEncodedBitrate(empty) == -1);

    // Embedded in larger buffer with binary prefix/suffix.
    std::vector<uint8_t> withPad = {'j', 'u', 'n', 'k', 0x00, 0x01, 0x02};
    const std::string mid = "SAMA_BITRATE_BPS=96000";
    withPad.insert(withPad.end(), mid.begin(), mid.end());
    const std::string tail = "\nmore_data";
    withPad.insert(withPad.end(), tail.begin(), tail.end());
    CHECK(AudioProcessor::readEncodedBitrate(withPad) == 96000);
}

#ifdef SAMA_WITH_OPUS

namespace
{

// PSNR between two mono float buffers, computed against the reference's RMS.
double computePsnrDb(const std::vector<float>& ref, const std::vector<float>& test)
{
    const size_t n = std::min(ref.size(), test.size());
    if (n == 0)
        return 0.0;
    double sumSqErr = 0.0;
    double refPeak = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        const double d = static_cast<double>(test[i]) - static_cast<double>(ref[i]);
        sumSqErr += d * d;
        refPeak = std::max(refPeak, std::abs(static_cast<double>(ref[i])));
    }
    if (sumSqErr <= 0.0)
        return 120.0;
    const double mse = sumSqErr / n;
    const double peak = std::max(refPeak, 1e-6);
    return 10.0 * std::log10((peak * peak) / mse);
}

}  // namespace

// ---------------------------------------------------------------------------
// 2. Sine wave round-trip: PSNR > 20 dB at 64 kbps.
// ---------------------------------------------------------------------------

TEST_CASE("AudioProcessor: sine-wave WAV->Opus->PCM round-trip PSNR > 20dB", "[asset_tool][audio]")
{
    REQUIRE(AudioProcessor::hasOpusSupport());

    // 1.0 second of 440 Hz at 48 kHz mono.  Longer than the encoder's
    // transient settling so the steady-state PSNR dominates the comparison.
    const int sampleRate = 48000;
    const int totalFrames = sampleRate;
    std::vector<float> samples(totalFrames);
    for (int i = 0; i < totalFrames; ++i)
    {
        const float t = static_cast<float>(i) / sampleRate;
        samples[i] = 0.5f * std::sin(2.0f * 3.14159265358979f * 440.0f * t);
    }

    std::vector<uint8_t> encoded;
    REQUIRE(AudioProcessor::encodeOpus(samples, 1, sampleRate, 64000, encoded));
    REQUIRE(encoded.size() > 100);

    // Bitrate tag observable.
    CHECK(AudioProcessor::readEncodedBitrate(encoded) == 64000);

    std::vector<float> decoded;
    int chOut = 0;
    REQUIRE(AudioProcessor::decodeOpus(encoded, decoded, chOut));
    REQUIRE(chOut == 1);

    // Opus introduces a constant decoder delay (~312 samples at 48 kHz).
    // Recover it via a small cross-correlation sweep rather than hardcoding,
    // since libopus's internal lookahead may evolve across versions.
    const size_t maxScan = std::min<size_t>(1500, decoded.size() / 4);
    size_t bestDelay = 0;
    double bestCorr = -1e30;
    for (size_t d = 0; d < maxScan; ++d)
    {
        double sum = 0.0;
        int n = 0;
        for (size_t i = d; i + 2000 < std::min(decoded.size(), samples.size() + d); ++i)
        {
            sum += static_cast<double>(decoded[i]) * static_cast<double>(samples[i - d]);
            ++n;
            if (n >= 4000)
                break;
        }
        if (n > 100 && sum / n > bestCorr)
        {
            bestCorr = sum / n;
            bestDelay = d;
        }
    }

    REQUIRE(decoded.size() > bestDelay + 4000);
    // Skip the first ~2000 samples (encoder warm-up transient) before comparing.
    const size_t transient = 2000;
    std::vector<float> refAligned(samples.begin() + transient,
                                  samples.begin() + (decoded.size() - bestDelay));
    std::vector<float> testAligned(decoded.begin() + bestDelay + transient, decoded.end());
    const size_t cmpN = std::min(refAligned.size(), testAligned.size());
    refAligned.resize(cmpN);
    testAligned.resize(cmpN);

    const double psnr = computePsnrDb(refAligned, testAligned);
    INFO("PSNR = " << psnr << " dB (delay=" << bestDelay << " samples)");
    CHECK(psnr > 20.0);
}

// ---------------------------------------------------------------------------
// 3. Per-tier bitrate is reflected in the encoded blob.
// ---------------------------------------------------------------------------

TEST_CASE("AudioProcessor: per-tier bitrate observable", "[asset_tool][audio]")
{
    REQUIRE(AudioProcessor::hasOpusSupport());

    const int sampleRate = 48000;
    const int totalFrames = sampleRate / 8;
    std::vector<float> samples(totalFrames);
    for (int i = 0; i < totalFrames; ++i)
    {
        const float t = static_cast<float>(i) / sampleRate;
        samples[i] = 0.4f * std::sin(2.0f * 3.14159265358979f * 1000.0f * t);
    }

    struct Case
    {
        std::string tier;
        int expected;
    };
    const Case cases[] = {{"low", 48000}, {"mid", 64000}, {"high", 96000}};

    for (const auto& c : cases)
    {
        TierConfig tc = getTierConfig(c.tier);
        REQUIRE(tc.opusBitrate == c.expected);

        std::vector<uint8_t> blob;
        REQUIRE(AudioProcessor::encodeOpus(samples, 1, sampleRate, tc.opusBitrate, blob));
        CHECK(AudioProcessor::readEncodedBitrate(blob) == c.expected);
    }
}

// ---------------------------------------------------------------------------
// 4. End-to-end: WAV in input dir, AssetProcessor at "android"/"mid" emits
//    an .opus file with the tagged bitrate.
// ---------------------------------------------------------------------------

TEST_CASE("AudioProcessor: end-to-end CLI emits .opus on .wav input", "[asset_tool][audio]")
{
    REQUIRE(AudioProcessor::hasOpusSupport());

    TempDir in;
    TempDir out;
    writeSineWav(in.path() / "beep.wav", 48000, 1, 0.2f, 880.0f);

    CliArgs args;
    args.inputDir = in.path().string();
    args.outputDir = out.path().string();
    args.target = "android";
    args.tier = "mid";

    AssetProcessor proc(args);
    REQUIRE(proc.run() == 0);

    fs::path produced = out.path() / "beep.opus";
    REQUIRE(fs::exists(produced));

    std::ifstream f(produced, std::ios::binary);
    std::vector<uint8_t> blob((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());

    CHECK(blob.size() > 100);
    CHECK(AudioProcessor::readEncodedBitrate(blob) == 64000);

    // Recognizable Ogg + OpusHead magic at the start of the blob.
    REQUIRE(blob.size() > 32);
    CHECK(std::memcmp(blob.data(), "OggS", 4) == 0);
    // OpusHead appears in the first page's payload.
    bool foundOpusHead = false;
    for (size_t i = 0; i + 8 < blob.size() && i < 64; ++i)
    {
        if (std::memcmp(blob.data() + i, "OpusHead", 8) == 0)
        {
            foundOpusHead = true;
            break;
        }
    }
    CHECK(foundOpusHead);
}

#else  // !SAMA_WITH_OPUS

TEST_CASE("AudioProcessor: build did not include libopus — encode path skipped",
          "[asset_tool][audio]")
{
    CHECK_FALSE(AudioProcessor::hasOpusSupport());
}

#endif
