#include "nus3bank.h"

#include <spdlog/spdlog.h>
#include <cstring>
#include <fstream>

extern "C" {
#include <g719.h>
}

namespace gen4 {

namespace {

uint32_t read_be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
uint16_t read_be16(const uint8_t* p) {
    return (uint16_t)(((uint32_t)p[0] << 8) | p[1]);
}
uint32_t read_le32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

struct Bnsf {
    int    channels      = 0;
    int    sample_rate   = 0;
    int    num_samples   = 0;
    int    block_size    = 0;   // bytes per frame, all channels together
    int    block_samples = 0;   // samples per frame per channel, 960 for G.719
    size_t data_offset   = 0;
    size_t data_size     = 0;
};

bool parse_container(const std::vector<uint8_t>& file, Bnsf& out) {
    if (file.size() < 0x18 || memcmp(file.data(), "NUS3", 4) != 0) {
        spdlog::warn("gen4 audio: not a NUS3 container");
        return false;
    }
    if (memcmp(file.data() + 8, "BANKTOC ", 8) != 0) {
        spdlog::warn("gen4 audio: no BANKTOC where one is expected");
        return false;
    }

    // The chunks follow the table of contents, each one named and sized, so
    // the table itself does not have to be understood to find PACK.
    size_t pos = 0x14 + read_le32(file.data() + 0x10);
    size_t pack = 0, pack_size = 0;
    while (pos + 8 <= file.size()) {
        uint32_t size = read_le32(file.data() + pos + 4);
        if (memcmp(file.data() + pos, "PACK", 4) == 0) {
            pack      = pos + 8;
            pack_size = size;
            break;
        }
        pos += 8 + size;
    }
    if (!pack || pack + pack_size > file.size()) {
        spdlog::warn("gen4 audio: no PACK chunk");
        return false;
    }

    const uint8_t* p = file.data() + pack;
    if (pack_size < 0x2C || memcmp(p, "BNSF", 4) != 0) {
        spdlog::warn("gen4 audio: PACK does not start with a BNSF stream");
        return false;
    }
    if (memcmp(p + 8, "IS22", 4) != 0) {
        spdlog::warn("gen4 audio: codec tag is {:.4s}, only IS22 is supported",
                     (const char*)(p + 8));
        return false;
    }
    if (memcmp(p + 0x0C, "sfmt", 4) != 0) {
        spdlog::warn("gen4 audio: no sfmt where one is expected");
        return false;
    }

    const uint8_t* fmt = p + 0x14;
    uint16_t flags     = read_be16(fmt);
    out.channels       = read_be16(fmt + 2);
    out.sample_rate    = (int)read_be32(fmt + 4);
    out.num_samples    = (int)read_be32(fmt + 8);
    out.block_size     = read_be16(fmt + 0x10);
    out.block_samples  = read_be16(fmt + 0x12);

    if (flags != 0) {
        spdlog::warn("gen4 audio: stream is flagged {}, expected plain", flags);
        return false;
    }
    if (out.channels < 1 || out.channels > 2 || out.block_size <= 0) {
        spdlog::warn("gen4 audio: unsupported stream shape ({} ch, block {})",
                     out.channels, out.block_size);
        return false;
    }

    // sdat follows sfmt and holds the codec payload.
    size_t sfmt_size = read_be32(p + 0x10);
    size_t sdat = 0x0C + 4 + 4 + sfmt_size;
    if (pack + sdat + 8 > file.size() || memcmp(p + sdat, "sdat", 4) != 0) {
        spdlog::warn("gen4 audio: no sdat where one is expected");
        return false;
    }
    out.data_size   = read_be32(p + sdat + 4);
    out.data_offset = pack + sdat + 8;
    if (out.data_offset + out.data_size > file.size())
        out.data_size = file.size() - out.data_offset;

    return true;
}

}  // namespace

bool decode_nus3bank(const fs::path& path, DecodedAudio& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        spdlog::warn("gen4 audio: cannot open {}", path.string());
        return false;
    }
    std::vector<uint8_t> file((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());

    Bnsf info;
    if (!parse_container(file, info)) return false;

    // One decoder per channel, each fed its own frames: the channels are
    // interleaved a whole frame at a time, not sample by sample.
    int frame_bytes = info.block_size / info.channels;
    std::vector<g719_handle*> decoders(info.channels, nullptr);
    for (int c = 0; c < info.channels; c++) {
        decoders[c] = g719_init(frame_bytes);
        if (!decoders[c]) {
            spdlog::warn("gen4 audio: decoder would not start for {}", path.string());
            for (g719_handle* h : decoders) if (h) g719_free(h);
            return false;
        }
    }

    out.channels    = info.channels;
    out.sample_rate = info.sample_rate;
    out.samples.clear();
    out.samples.reserve((size_t)info.num_samples * info.channels);

    const int    samples_per_frame = info.block_samples > 0 ? info.block_samples : 960;
    const uint8_t* data = file.data() + info.data_offset;
    size_t       pos    = 0;
    std::vector<std::vector<int16_t>> pcm(info.channels,
                                          std::vector<int16_t>(samples_per_frame));

    while (pos + (size_t)info.block_size <= info.data_size) {
        for (int c = 0; c < info.channels; c++) {
            g719_decode_frame(decoders[c],
                              (void*)(data + pos + (size_t)c * frame_bytes),
                              pcm[c].data());
        }
        pos += info.block_size;

        for (int s = 0; s < samples_per_frame; s++)
            for (int c = 0; c < info.channels; c++)
                out.samples.push_back((float)pcm[c][s] / 32768.0f);
    }

    for (g719_handle* h : decoders) g719_free(h);

    // The header's sample count is the real length; the last frame is padding.
    size_t wanted = (size_t)info.num_samples * info.channels;
    if (info.num_samples > 0 && wanted < out.samples.size())
        out.samples.resize(wanted);

    if (out.samples.empty()) {
        spdlog::warn("gen4 audio: {} decoded to nothing", path.filename().string());
        return false;
    }

    spdlog::debug("gen4 audio: {} decoded, {} frames, {} Hz, {} ch",
                  path.filename().string(), out.frame_count(), out.sample_rate, out.channels);
    return true;
}

}  // namespace gen4
