#include "tools/asset_tool/AudioProcessor.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#ifdef SAMA_WITH_OPUS
#define DR_WAV_IMPLEMENTATION
#include <opus.h>

#include "dr_wav.h"
#endif

namespace fs = std::filesystem;

namespace engine::tools
{

// ---------------------------------------------------------------------------
// AudioProcessor — construction
// ---------------------------------------------------------------------------

AudioProcessor::AudioProcessor(const CliArgs& args, const TierConfig& tier)
    : args_(args), tier_(tier)
{
}

bool AudioProcessor::hasOpusSupport()
{
#ifdef SAMA_WITH_OPUS
    return true;
#else
    return false;
#endif
}

// ---------------------------------------------------------------------------
// Ogg page framing — minimal single-stream encoder.
//
// Each Ogg page is:
//   uint8[4]  capture pattern "OggS"
//   uint8     version          = 0
//   uint8     header flags     (bit0=continued, bit1=BOS, bit2=EOS)
//   uint64    granule position (samples-elapsed for the last packet on page)
//   uint32    stream serial    (arbitrary positive int — we use 0x53414D41 "SAMA")
//   uint32    page sequence    (0,1,2,...)
//   uint32    CRC32            (computed over the full page, with this field zeroed)
//   uint8     segment count    (1..255)
//   uint8[N]  segment table    (per-packet lacing values 0..255; sum = payload bytes)
//   uint8[]   payload          (sum of segment table)
//
// CRC polynomial is 0x04C11DB7 (left-shifting, no inversion). Reference:
// https://datatracker.ietf.org/doc/html/rfc3533
// ---------------------------------------------------------------------------

namespace
{

constexpr uint32_t kOggSerial = 0x53414D41u;  // "SAMA"
constexpr uint32_t kOpusSampleRateOut = 48000u;
constexpr int kFrameSize = 960;  // 20ms @ 48kHz

uint32_t oggCrc32(const uint8_t* data, size_t size)
{
    // Standard Ogg framing CRC32 (poly 0x04C11DB7, no inversion).
    static uint32_t table[256];
    static bool init = false;
    if (!init)
    {
        for (int i = 0; i < 256; ++i)
        {
            uint32_t r = static_cast<uint32_t>(i) << 24;
            for (int j = 0; j < 8; ++j)
            {
                r = (r & 0x80000000u) ? ((r << 1) ^ 0x04C11DB7u) : (r << 1);
            }
            table[i] = r;
        }
        init = true;
    }

    uint32_t crc = 0;
    for (size_t i = 0; i < size; ++i)
    {
        crc = (crc << 8) ^ table[((crc >> 24) ^ data[i]) & 0xFF];
    }
    return crc;
}

void writeOggPage(std::vector<uint8_t>& out, uint8_t headerFlags, uint64_t granulePos,
                  uint32_t pageSeq, const uint8_t* payload, size_t payloadLen)
{
    // Build segment table (lacing).  Each segment carries up to 255 bytes.
    // A segment of exactly 255 means "packet continues"; a segment <255 means
    // "packet ends".  Since we put one packet per page, append a 0-length
    // terminator if the packet size is an exact multiple of 255.
    std::vector<uint8_t> segs;
    size_t remaining = payloadLen;
    while (remaining >= 255)
    {
        segs.push_back(255);
        remaining -= 255;
    }
    segs.push_back(static_cast<uint8_t>(remaining));
    if (segs.size() > 255)
    {
        // Packet too large for a single page.  Caller is responsible for
        // ensuring packets fit (Opus packets are well below 60 KB typical).
        return;
    }

    const size_t headerLen = 27 + segs.size();
    const size_t totalLen = headerLen + payloadLen;
    const size_t base = out.size();
    out.resize(base + totalLen);
    uint8_t* p = out.data() + base;

    std::memcpy(p, "OggS", 4);
    p[4] = 0;            // version
    p[5] = headerFlags;  // flags
    // granule position (LE)
    for (int i = 0; i < 8; ++i)
        p[6 + i] = static_cast<uint8_t>((granulePos >> (i * 8)) & 0xFF);
    // serial
    for (int i = 0; i < 4; ++i)
        p[14 + i] = static_cast<uint8_t>((kOggSerial >> (i * 8)) & 0xFF);
    // sequence
    for (int i = 0; i < 4; ++i)
        p[18 + i] = static_cast<uint8_t>((pageSeq >> (i * 8)) & 0xFF);
    // CRC (placeholder, computed after)
    p[22] = p[23] = p[24] = p[25] = 0;
    p[26] = static_cast<uint8_t>(segs.size());
    std::memcpy(p + 27, segs.data(), segs.size());
    std::memcpy(p + headerLen, payload, payloadLen);

    uint32_t crc = oggCrc32(p, totalLen);
    p[22] = static_cast<uint8_t>(crc & 0xFF);
    p[23] = static_cast<uint8_t>((crc >> 8) & 0xFF);
    p[24] = static_cast<uint8_t>((crc >> 16) & 0xFF);
    p[25] = static_cast<uint8_t>((crc >> 24) & 0xFF);
}

#ifdef SAMA_WITH_OPUS

// OpusHead per RFC 7845 §5.1.  19 bytes minimum (no mapping family 1 channel
// mapping table since we use mapping family 0 for mono/stereo).
std::vector<uint8_t> makeOpusHead(int channels, int origSampleRate)
{
    std::vector<uint8_t> h(19);
    std::memcpy(h.data(), "OpusHead", 8);
    h[8] = 1;                               // version
    h[9] = static_cast<uint8_t>(channels);  // channel count
    // pre-skip (LE16) — 0 for our purposes; libopus reports values for
    // best-quality decoding but 0 is acceptable.
    h[10] = 0;
    h[11] = 0;
    // input sample rate (LE32) — informational, libopusfile/ffmpeg ignore it
    // for playback (Opus always decodes at 48kHz).
    for (int i = 0; i < 4; ++i)
        h[12 + i] = static_cast<uint8_t>((origSampleRate >> (i * 8)) & 0xFF);
    // output gain (LE16, signed Q7.8) — 0 dB
    h[16] = 0;
    h[17] = 0;
    h[18] = 0;  // mapping family 0
    return h;
}

// OpusTags per RFC 7845 §5.2.  Carries the bitrate as a "SAMA_BITRATE_BPS=N"
// user comment so readEncodedBitrate() can recover it later.
std::vector<uint8_t> makeOpusTags(int bitrateBps)
{
    const std::string vendor = "Sama-AssetTool";
    const std::string rateTag = "SAMA_BITRATE_BPS=" + std::to_string(bitrateBps);

    std::vector<uint8_t> buf;
    auto writeU32 = [&](uint32_t v)
    {
        buf.push_back(static_cast<uint8_t>(v & 0xFF));
        buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
        buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
        buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
    };

    buf.insert(buf.end(), {'O', 'p', 'u', 's', 'T', 'a', 'g', 's'});
    writeU32(static_cast<uint32_t>(vendor.size()));
    buf.insert(buf.end(), vendor.begin(), vendor.end());
    writeU32(1);  // one user comment
    writeU32(static_cast<uint32_t>(rateTag.size()));
    buf.insert(buf.end(), rateTag.begin(), rateTag.end());
    return buf;
}

#endif  // SAMA_WITH_OPUS

}  // namespace

// ---------------------------------------------------------------------------
// Bitrate inspection — scans an Ogg-Opus blob for the SAMA_BITRATE_BPS tag.
// Independent of libopus so tests can call it even without opus support.
// ---------------------------------------------------------------------------

int AudioProcessor::readEncodedBitrate(const std::vector<uint8_t>& inBytes)
{
    static const char kPrefix[] = "SAMA_BITRATE_BPS=";
    const size_t prefixLen = sizeof(kPrefix) - 1;
    if (inBytes.size() < prefixLen)
        return -1;

    for (size_t i = 0; i + prefixLen < inBytes.size(); ++i)
    {
        if (std::memcmp(inBytes.data() + i, kPrefix, prefixLen) == 0)
        {
            // Read decimal digits.
            int value = 0;
            size_t j = i + prefixLen;
            bool any = false;
            while (j < inBytes.size() && inBytes[j] >= '0' && inBytes[j] <= '9')
            {
                value = value * 10 + (inBytes[j] - '0');
                ++j;
                any = true;
            }
            return any ? value : -1;
        }
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Opus encode / decode (no-ops in fallback builds)
// ---------------------------------------------------------------------------

#ifdef SAMA_WITH_OPUS

bool AudioProcessor::encodeOpus(const std::vector<float>& samples, int channels, int sampleRate,
                                int bitrateBps, std::vector<uint8_t>& outBytes)
{
    if (channels < 1 || channels > 2 || sampleRate <= 0 || samples.empty())
        return false;
    if (samples.size() % static_cast<size_t>(channels) != 0)
        return false;

    // Opus encoder runs at 48 kHz.  If input differs, resample (linear, simple).
    std::vector<float> work;
    const float* src = samples.data();
    size_t srcFrames = samples.size() / static_cast<size_t>(channels);
    if (sampleRate != static_cast<int>(kOpusSampleRateOut))
    {
        const double ratio = static_cast<double>(kOpusSampleRateOut) / sampleRate;
        const size_t dstFrames = static_cast<size_t>(srcFrames * ratio);
        work.resize(dstFrames * channels);
        for (size_t i = 0; i < dstFrames; ++i)
        {
            const double srcPos = i / ratio;
            const size_t s0 = static_cast<size_t>(srcPos);
            const size_t s1 = std::min(s0 + 1, srcFrames - 1);
            const float a = static_cast<float>(srcPos - s0);
            for (int c = 0; c < channels; ++c)
            {
                const float v0 = samples[s0 * channels + c];
                const float v1 = samples[s1 * channels + c];
                work[i * channels + c] = v0 + (v1 - v0) * a;
            }
        }
        src = work.data();
        srcFrames = dstFrames;
    }

    int err = 0;
    OpusEncoder* enc = opus_encoder_create(static_cast<opus_int32>(kOpusSampleRateOut), channels,
                                           OPUS_APPLICATION_AUDIO, &err);
    if (!enc || err != OPUS_OK)
    {
        if (enc)
            opus_encoder_destroy(enc);
        return false;
    }

    opus_encoder_ctl(enc, OPUS_SET_BITRATE(bitrateBps));
    opus_encoder_ctl(enc, OPUS_SET_VBR(1));

    // Page 0 — OpusHead (BOS).
    outBytes.clear();
    {
        auto head = makeOpusHead(channels, sampleRate);
        writeOggPage(outBytes, 0x02 /*BOS*/, 0, 0, head.data(), head.size());
    }
    // Page 1 — OpusTags.
    {
        auto tags = makeOpusTags(bitrateBps);
        writeOggPage(outBytes, 0, 0, 1, tags.data(), tags.size());
    }

    // Pad to a whole number of frames (zeros) — last frame's tail is silence.
    const size_t totalSrcSamples = srcFrames * channels;
    const size_t framesNeeded = (srcFrames + kFrameSize - 1) / kFrameSize;
    const size_t paddedSamples = framesNeeded * kFrameSize * channels;

    std::vector<float> padded(paddedSamples, 0.0f);
    std::memcpy(padded.data(), src, totalSrcSamples * sizeof(float));

    std::vector<uint8_t> pktBuf(4000);  // generous per-packet ceiling
    uint64_t granule = 0;
    uint32_t seq = 2;

    for (size_t f = 0; f < framesNeeded; ++f)
    {
        const float* frame = padded.data() + f * kFrameSize * channels;
        const int bytes = opus_encode_float(enc, frame, kFrameSize, pktBuf.data(),
                                            static_cast<opus_int32>(pktBuf.size()));
        if (bytes < 0)
        {
            opus_encoder_destroy(enc);
            return false;
        }

        granule += kFrameSize;
        const bool isEos = (f + 1 == framesNeeded);
        const uint8_t flags = isEos ? 0x04u : 0x00u;
        writeOggPage(outBytes, flags, granule, seq++, pktBuf.data(), static_cast<size_t>(bytes));
    }

    opus_encoder_destroy(enc);
    return true;
}

namespace
{

// Minimal Ogg page parser — extracts packets in order.
// We assume one packet per page (matches our encoder).
struct OggPacket
{
    std::vector<uint8_t> data;
};

bool parseOggPackets(const std::vector<uint8_t>& blob, std::vector<OggPacket>& out)
{
    size_t pos = 0;
    while (pos + 27 <= blob.size())
    {
        if (std::memcmp(blob.data() + pos, "OggS", 4) != 0)
            return false;
        const uint8_t segCount = blob[pos + 26];
        if (pos + 27 + segCount > blob.size())
            return false;
        size_t payloadLen = 0;
        for (uint8_t i = 0; i < segCount; ++i)
        {
            payloadLen += blob[pos + 27 + i];
        }
        const size_t payloadStart = pos + 27 + segCount;
        if (payloadStart + payloadLen > blob.size())
            return false;
        OggPacket pkt;
        pkt.data.assign(blob.begin() + payloadStart, blob.begin() + payloadStart + payloadLen);
        out.push_back(std::move(pkt));
        pos = payloadStart + payloadLen;
    }
    return true;
}

}  // namespace

bool AudioProcessor::decodeOpus(const std::vector<uint8_t>& inBytes, std::vector<float>& outSamples,
                                int& channelsOut)
{
    std::vector<OggPacket> packets;
    if (!parseOggPackets(inBytes, packets) || packets.size() < 3)
        return false;

    // packets[0] = OpusHead
    if (packets[0].data.size() < 19 || std::memcmp(packets[0].data.data(), "OpusHead", 8) != 0)
    {
        return false;
    }
    const int channels = packets[0].data[9];
    if (channels < 1 || channels > 2)
        return false;
    channelsOut = channels;

    // packets[1] = OpusTags — skip.

    int err = 0;
    OpusDecoder* dec =
        opus_decoder_create(static_cast<opus_int32>(kOpusSampleRateOut), channels, &err);
    if (!dec || err != OPUS_OK)
    {
        if (dec)
            opus_decoder_destroy(dec);
        return false;
    }

    outSamples.clear();
    std::vector<float> frameBuf(static_cast<size_t>(kFrameSize) * channels);

    for (size_t i = 2; i < packets.size(); ++i)
    {
        const int decoded = opus_decode_float(dec, packets[i].data.data(),
                                              static_cast<opus_int32>(packets[i].data.size()),
                                              frameBuf.data(), kFrameSize, 0);
        if (decoded < 0)
        {
            opus_decoder_destroy(dec);
            return false;
        }
        outSamples.insert(outSamples.end(), frameBuf.begin(),
                          frameBuf.begin() + decoded * channels);
    }

    opus_decoder_destroy(dec);
    return true;
}

#else  // !SAMA_WITH_OPUS

bool AudioProcessor::encodeOpus(const std::vector<float>&, int, int, int, std::vector<uint8_t>&)
{
    return false;
}

bool AudioProcessor::decodeOpus(const std::vector<uint8_t>&, std::vector<float>&, int&)
{
    return false;
}

#endif  // SAMA_WITH_OPUS

// ---------------------------------------------------------------------------
// Discovery / processing
// ---------------------------------------------------------------------------

std::vector<AssetEntry> AudioProcessor::discover()
{
    std::vector<AssetEntry> result;
    std::error_code ec;
    if (!fs::exists(args_.inputDir))
        return result;

    for (auto& p : fs::recursive_directory_iterator(args_.inputDir, ec))
    {
        if (ec)
            break;
        if (!p.is_regular_file())
            continue;

        std::string ext = p.path().extension().string();
        for (auto& c : ext)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext != ".wav")
            continue;

        std::string relPath = fs::relative(p.path(), args_.inputDir).string();
        AssetEntry entry;
        entry.type = "audio";
        entry.source = relPath;

        fs::path outPath = relPath;
        if (hasOpusSupport())
        {
            outPath.replace_extension(".opus");
            entry.format = "opus";
        }
        else
        {
            // Fallback: copy WAV through.
            entry.format = "wav";
        }
        entry.output = outPath.string();

        if (args_.verbose)
        {
            std::cout << "  Found audio: " << relPath << "\n";
        }
        result.push_back(std::move(entry));
    }
    return result;
}

void AudioProcessor::processAll(std::vector<AssetEntry>& entries)
{
    for (auto& entry : entries)
    {
        if (entry.type != "audio")
            continue;
        if (!processOne(entry))
        {
            std::cerr << "Warning: audio processing failed for " << entry.source << "\n";
        }
    }
}

bool AudioProcessor::processOne(AssetEntry& entry)
{
    fs::path srcPath = fs::path(args_.inputDir) / entry.source;
    fs::path dstPath = fs::path(args_.outputDir) / entry.output;
    fs::create_directories(dstPath.parent_path());

#ifdef SAMA_WITH_OPUS
    // Decode WAV via dr_wav.
    drwav wav;
    if (!drwav_init_file(&wav, srcPath.string().c_str(), nullptr))
    {
        return false;
    }
    const int channels = static_cast<int>(wav.channels);
    const int sampleRate = static_cast<int>(wav.sampleRate);
    const size_t frameCount = wav.totalPCMFrameCount;
    std::vector<float> samples(frameCount * channels);
    drwav_read_pcm_frames_f32(&wav, frameCount, samples.data());
    drwav_uninit(&wav);

    if (channels < 1 || channels > 2)
    {
        return false;
    }

    std::vector<uint8_t> bytes;
    if (!encodeOpus(samples, channels, sampleRate, tier_.opusBitrate, bytes))
    {
        return false;
    }

    std::ofstream out(dstPath, std::ios::binary);
    if (!out)
        return false;
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    if (!out)
        return false;

    if (args_.verbose)
    {
        std::cout << "  Wrote audio: " << entry.output << " (" << bytes.size() << " bytes, "
                  << tier_.opusBitrate / 1000 << " kbps)\n";
    }
    return true;
#else
    // Fallback: copy unchanged.
    std::error_code ec;
    fs::copy_file(srcPath, dstPath, fs::copy_options::overwrite_existing, ec);
    if (ec)
    {
        return false;
    }
    if (args_.verbose)
    {
        std::cout << "  Copied audio (no opus): " << entry.source << "\n";
    }
    return true;
#endif
}

}  // namespace engine::tools
