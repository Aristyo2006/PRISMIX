#include "patchstorage_artwork.h"

#include <zlib.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <system_error>

namespace prismix::patchstorage::artwork {

namespace {

constexpr char kRawMagic[] = "PSRAW1";
constexpr size_t kRawMagicLen = 6;

// PNG color types
constexpr int kColorGrey = 0;
constexpr int kColorRgb = 2;
constexpr int kColorPalette = 3;
constexpr int kColorGreyAlpha = 4;
constexpr int kColorRgba = 6;

bool is_png_signature(const uint8_t* data, size_t len) {
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    return len >= 8 && std::memcmp(data, sig, 8) == 0;
}

uint32_t read_be32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

int channels_for_color_type(int color_type) {
    switch (color_type) {
        case kColorGrey: return 1;
        case kColorRgb: return 3;
        case kColorPalette: return 1;
        case kColorGreyAlpha: return 2;
        case kColorRgba: return 4;
        default: return 0;
    }
}

bool depth_allowed_for_color_type(int color_type, int depth) {
    switch (color_type) {
        case kColorGrey: return depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16;
        case kColorRgb: return depth == 8 || depth == 16;
        case kColorPalette: return depth == 1 || depth == 2 || depth == 4 || depth == 8;
        case kColorGreyAlpha: return depth == 8 || depth == 16;
        case kColorRgba: return depth == 8 || depth == 16;
        default: return false;
    }
}

uint8_t paeth_predictor(int a, int b, int c) {
    const int p = a + b - c;
    const int pa = std::abs(p - a);
    const int pb = std::abs(p - b);
    const int pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) return static_cast<uint8_t>(a);
    if (pb <= pc) return static_cast<uint8_t>(b);
    return static_cast<uint8_t>(c);
}

// Reverses PNG row filters in place. `bpp` is the distance to the pixel on the
// left in bytes (at least 1).
void unfilter_row(uint8_t* row, const uint8_t* prev_row, size_t row_bytes, size_t bpp, int filter_type) {
    switch (filter_type) {
        case 0:
            break;
        case 1:
            for (size_t i = bpp; i < row_bytes; ++i) {
                row[i] = static_cast<uint8_t>(row[i] + row[i - bpp]);
            }
            break;
        case 2:
            if (prev_row) {
                for (size_t i = 0; i < row_bytes; ++i) {
                    row[i] = static_cast<uint8_t>(row[i] + prev_row[i]);
                }
            }
            break;
        case 3:
            for (size_t i = 0; i < row_bytes; ++i) {
                const int left = (i >= bpp) ? row[i - bpp] : 0;
                const int up = prev_row ? prev_row[i] : 0;
                row[i] = static_cast<uint8_t>(row[i] + ((left + up) >> 1));
            }
            break;
        case 4:
            for (size_t i = 0; i < row_bytes; ++i) {
                const int left = (i >= bpp) ? row[i - bpp] : 0;
                const int up = prev_row ? prev_row[i] : 0;
                const int up_left = (prev_row && i >= bpp) ? prev_row[i - bpp] : 0;
                row[i] = static_cast<uint8_t>(row[i] + paeth_predictor(left, up, up_left));
            }
            break;
        default:
            break;
    }
}

// Extracts the n-th sample of a row for sub-byte and byte-based depths.
int sample_at(const uint8_t* row, size_t sample_index, int bit_depth) {
    switch (bit_depth) {
        case 1: {
            const uint8_t byte = row[sample_index >> 3];
            return (byte >> (7 - (sample_index & 7))) & 0x01;
        }
        case 2: {
            const uint8_t byte = row[sample_index >> 2];
            return (byte >> (6 - 2 * (sample_index & 3))) & 0x03;
        }
        case 4: {
            const uint8_t byte = row[sample_index >> 1];
            return (sample_index & 1) ? (byte & 0x0F) : ((byte >> 4) & 0x0F);
        }
        case 8:
            return row[sample_index];
        case 16:
            return row[sample_index * 2];  // high byte
        default:
            return 0;
    }
}

int max_sample_for_depth(int depth) {
    return depth >= 8 ? 255 : ((1 << depth) - 1);
}

// Expands decoded scanlines into a straight RGBA8888 buffer.
void expand_to_rgba(const std::vector<uint8_t>& raw,
                    int w,
                    int h,
                    int bit_depth,
                    int color_type,
                    const std::vector<uint8_t>& palette,
                    const std::vector<uint8_t>& palette_alpha,
                    std::vector<uint8_t>& rgba) {
    const int channels = channels_for_color_type(color_type);
    const size_t samples_per_row = static_cast<size_t>(w) * static_cast<size_t>(channels);
    const size_t row_bytes = (samples_per_row * static_cast<size_t>(bit_depth) + 7u) / 8u;

    rgba.assign(static_cast<size_t>(w) * static_cast<size_t>(h) * 4u, 255);
    const int grey_max = max_sample_for_depth(bit_depth);

    for (int y = 0; y < h; ++y) {
        const uint8_t* row = raw.data() + static_cast<size_t>(y) * (row_bytes + 1u) + 1u;
        uint8_t* dst = rgba.data() + static_cast<size_t>(y) * static_cast<size_t>(w) * 4u;

        for (int x = 0; x < w; ++x) {
            const size_t base = static_cast<size_t>(x) * static_cast<size_t>(channels);
            uint8_t* px = dst + static_cast<size_t>(x) * 4u;

            if (color_type == kColorGrey) {
                const int v = sample_at(row, base, bit_depth);
                const uint8_t g = bit_depth == 16 ? static_cast<uint8_t>(v)
                                                  : static_cast<uint8_t>((v * 255) / std::max(1, grey_max));
                px[0] = px[1] = px[2] = g;
                px[3] = 255;
            } else if (color_type == kColorRgb) {
                px[0] = static_cast<uint8_t>(sample_at(row, base + 0, bit_depth));
                px[1] = static_cast<uint8_t>(sample_at(row, base + 1, bit_depth));
                px[2] = static_cast<uint8_t>(sample_at(row, base + 2, bit_depth));
                px[3] = 255;
            } else if (color_type == kColorPalette) {
                const int idx = sample_at(row, base, bit_depth);
                const size_t po = static_cast<size_t>(idx) * 3u;
                if (po + 2 < palette.size()) {
                    px[0] = palette[po + 0];
                    px[1] = palette[po + 1];
                    px[2] = palette[po + 2];
                } else {
                    px[0] = px[1] = px[2] = 0;
                }
                px[3] = (static_cast<size_t>(idx) < palette_alpha.size())
                            ? palette_alpha[static_cast<size_t>(idx)]
                            : 255;
            } else if (color_type == kColorGreyAlpha) {
                const int v = sample_at(row, base + 0, bit_depth);
                const uint8_t g = bit_depth == 16 ? static_cast<uint8_t>(v)
                                                  : static_cast<uint8_t>((v * 255) / std::max(1, grey_max));
                px[0] = px[1] = px[2] = g;
                px[3] = static_cast<uint8_t>(sample_at(row, base + 1, bit_depth));
            } else {  // RGBA
                px[0] = static_cast<uint8_t>(sample_at(row, base + 0, bit_depth));
                px[1] = static_cast<uint8_t>(sample_at(row, base + 1, bit_depth));
                px[2] = static_cast<uint8_t>(sample_at(row, base + 2, bit_depth));
                px[3] = static_cast<uint8_t>(sample_at(row, base + 3, bit_depth));
            }

            // Fold straight alpha into the pixels over the dark recessed frame
            // background so soft / transparent edges stay clean in XRGB8888.
            const uint8_t a = px[3];
            if (a != 255) {
                static constexpr uint8_t kBg[3] = {11, 13, 15};  // matches the card artwork frame
                px[0] = static_cast<uint8_t>((static_cast<int>(px[0]) * a + kBg[0] * (255 - a)) / 255);
                px[1] = static_cast<uint8_t>((static_cast<int>(px[1]) * a + kBg[1] * (255 - a)) / 255);
                px[2] = static_cast<uint8_t>((static_cast<int>(px[2]) * a + kBg[2] * (255 - a)) / 255);
            }
            px[3] = 255;
        }
    }
}

// Box-filter downsample of an RGBA8888 buffer into B,G,R,X pixel order.
void resample_to_bgra(const std::vector<uint8_t>& rgba,
                      int w,
                      int h,
                      int max_dimension,
                      int& out_w,
                      int& out_h,
                      std::vector<uint8_t>& out_pixels) {
    double scale = 1.0;
    if (max_dimension > 0) {
        const int longest = std::max(w, h);
        if (longest > max_dimension) {
            scale = static_cast<double>(max_dimension) / static_cast<double>(longest);
        }
    }

    out_w = std::max(1, static_cast<int>(std::lround(static_cast<double>(w) * scale)));
    out_h = std::max(1, static_cast<int>(std::lround(static_cast<double>(h) * scale)));
    out_pixels.assign(static_cast<size_t>(out_w) * static_cast<size_t>(out_h) * 4u, 0);

    for (int oy = 0; oy < out_h; ++oy) {
        const int y0 = static_cast<int>((static_cast<int64_t>(oy) * h) / out_h);
        int y1 = static_cast<int>((static_cast<int64_t>(oy + 1) * h) / out_h);
        if (y1 <= y0) y1 = y0 + 1;
        if (y1 > h) y1 = h;

        for (int ox = 0; ox < out_w; ++ox) {
            const int x0 = static_cast<int>((static_cast<int64_t>(ox) * w) / out_w);
            int x1 = static_cast<int>((static_cast<int64_t>(ox + 1) * w) / out_w);
            if (x1 <= x0) x1 = x0 + 1;
            if (x1 > w) x1 = w;

            uint64_t acc[3] = {0, 0, 0};
            uint64_t count = 0;
            for (int y = y0; y < y1; ++y) {
                const uint8_t* row = rgba.data() + static_cast<size_t>(y) * static_cast<size_t>(w) * 4u;
                for (int x = x0; x < x1; ++x) {
                    const uint8_t* px = row + static_cast<size_t>(x) * 4u;
                    acc[0] += px[0];
                    acc[1] += px[1];
                    acc[2] += px[2];
                    ++count;
                }
            }
            if (count == 0) count = 1;

            uint8_t* dst = out_pixels.data() +
                           (static_cast<size_t>(oy) * static_cast<size_t>(out_w) + static_cast<size_t>(ox)) * 4u;
            dst[0] = static_cast<uint8_t>(acc[2] / count);  // B
            dst[1] = static_cast<uint8_t>(acc[1] / count);  // G
            dst[2] = static_cast<uint8_t>(acc[0] / count);  // R
            dst[3] = 0xFF;                                  // X (ignored by XRGB8888)
        }
    }
}

bool inflate_exact(const std::vector<uint8_t>& input, size_t expected_size, std::vector<uint8_t>& out) {
    if (input.empty()) return false;
    out.resize(expected_size);
    uLongf dest_len = static_cast<uLongf>(expected_size);
    const int rc = uncompress(out.data(), &dest_len, input.data(), static_cast<uLong>(input.size()));
    if (rc != Z_OK || dest_len != expected_size) {
        out.clear();
        return false;
    }
    return true;
}

} // namespace

std::filesystem::path cache_file_for(const std::filesystem::path& dir,
                                     int patch_id,
                                     const char* extension) {
    return dir / ("art_" + std::to_string(patch_id) + "." + extension);
}

bool peek_png_size(const uint8_t* data, size_t len, int& out_w, int& out_h, std::string& out_error) {
    out_w = 0;
    out_h = 0;
    if (!is_png_signature(data, len)) {
        out_error = "not a PNG file";
        return false;
    }
    if (len < 24 || read_be32(data + 8) != 13 || std::memcmp(data + 12, "IHDR", 4) != 0) {
        out_error = "PNG header missing";
        return false;
    }
    out_w = static_cast<int>(read_be32(data + 16));
    out_h = static_cast<int>(read_be32(data + 20));
    if (out_w <= 0 || out_h <= 0) {
        out_error = "invalid PNG dimensions";
        return false;
    }
    return true;
}

bool decode_png_to_pixels(const uint8_t* data,
                          size_t len,
                          int max_dimension,
                          int& out_w,
                          int& out_h,
                          std::vector<uint8_t>& out_pixels,
                          std::string& out_error) {
    out_w = 0;
    out_h = 0;
    out_pixels.clear();

    int w = 0;
    int h = 0;
    if (!peek_png_size(data, len, w, h, out_error)) {
        return false;
    }
    if (static_cast<uint64_t>(w) * static_cast<uint64_t>(h) > kArtworkMaxPixels) {
        out_error = "image dimensions exceed limit";
        return false;
    }

    int bit_depth = 0;
    int color_type = -1;
    int interlace = 1;
    bool have_ihdr = false;
    std::vector<uint8_t> palette;
    std::vector<uint8_t> palette_alpha;
    std::vector<uint8_t> idat;

    size_t offset = 8;
    bool have_iend = false;
    while (offset + 12 <= len) {
        const uint32_t chunk_len = read_be32(data + offset);
        if (chunk_len > len || offset + 12u + chunk_len > len) break;

        const uint8_t* type = data + offset + 4;
        const uint8_t* chunk_data = data + offset + 8;

        const uint32_t stored_crc = read_be32(chunk_data + chunk_len);
        const uLong computed_crc = crc32(0L, type, static_cast<uInt>(4 + chunk_len));
        if (computed_crc != stored_crc) {
            out_error = "PNG chunk CRC mismatch";
            return false;
        }

        if (std::memcmp(type, "IHDR", 4) == 0 && chunk_len >= 13) {
            bit_depth = chunk_data[8];
            color_type = chunk_data[9];
            const int compression = chunk_data[10];
            const int filter_method = chunk_data[11];
            interlace = chunk_data[12];
            have_ihdr = true;
            if (compression != 0 || filter_method != 0) {
                out_error = "unsupported PNG compression/filter method";
                return false;
            }
            if (!depth_allowed_for_color_type(color_type, bit_depth)) {
                out_error = "unsupported PNG color type/bit depth";
                return false;
            }
        } else if (std::memcmp(type, "PLTE", 4) == 0) {
            palette.assign(chunk_data, chunk_data + chunk_len);
        } else if (std::memcmp(type, "tRNS", 4) == 0) {
            palette_alpha.assign(chunk_data, chunk_data + chunk_len);
        } else if (std::memcmp(type, "IDAT", 4) == 0) {
            if (idat.size() + chunk_len > kArtworkMaxDownloadBytes * 4) {
                out_error = "PNG image data too large";
                return false;
            }
            idat.insert(idat.end(), chunk_data, chunk_data + chunk_len);
        } else if (std::memcmp(type, "IEND", 4) == 0) {
            have_iend = true;
            break;
        }

        offset += 12u + chunk_len;
    }

    if (!have_ihdr || idat.empty() || !have_iend) {
        out_error = "incomplete PNG file";
        return false;
    }
    if (interlace != 0) {
        out_error = "interlaced PNG not supported";
        return false;
    }

    const int channels = channels_for_color_type(color_type);
    const size_t bits_per_pixel = static_cast<size_t>(channels) * static_cast<size_t>(bit_depth);
    const size_t row_bytes = (static_cast<size_t>(w) * bits_per_pixel + 7u) / 8u;
    const size_t expected_raw = static_cast<size_t>(h) * (row_bytes + 1u);

    std::vector<uint8_t> raw;
    if (!inflate_exact(idat, expected_raw, raw)) {
        out_error = "PNG decompression failed";
        return false;
    }

    // Reverse the per-row filters.
    const size_t filter_bpp = std::max<size_t>(1, bits_per_pixel / 8u);
    for (int y = 0; y < h; ++y) {
        uint8_t* row = raw.data() + static_cast<size_t>(y) * (row_bytes + 1u);
        const int filter_type = row[0];
        uint8_t* pixels_row = row + 1;
        const uint8_t* prev = (y > 0) ? raw.data() + static_cast<size_t>(y - 1) * (row_bytes + 1u) + 1u : nullptr;
        unfilter_row(pixels_row, prev, row_bytes, filter_bpp, filter_type);
    }

    std::vector<uint8_t> rgba;
    expand_to_rgba(raw, w, h, bit_depth, color_type, palette, palette_alpha, rgba);
    resample_to_bgra(rgba, w, h, max_dimension, out_w, out_h, out_pixels);
    return true;
}

bool decode_png_file_to_pixels(const std::filesystem::path& path,
                               int max_dimension,
                               int& out_w,
                               int& out_h,
                               std::vector<uint8_t>& out_pixels,
                               std::string& out_error) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size == 0) {
        out_error = "artwork file missing or empty";
        return false;
    }
    if (size > kArtworkMaxDownloadBytes) {
        out_error = "artwork file exceeds size limit";
        return false;
    }

    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    std::FILE* f = std::fopen(path.string().c_str(), "rb");
    if (!f) {
        out_error = "cannot open artwork file";
        return false;
    }
    const size_t read = std::fread(bytes.data(), 1, bytes.size(), f);
    std::fclose(f);
    if (read != bytes.size()) {
        out_error = "short read on artwork file";
        return false;
    }

    return decode_png_to_pixels(bytes.data(), bytes.size(), max_dimension, out_w, out_h, out_pixels, out_error);
}

bool save_raw_cache(const std::filesystem::path& path,
                    int w,
                    int h,
                    const std::vector<uint8_t>& pixels,
                    std::string& out_error) {
    if (w <= 0 || h <= 0 || pixels.size() != static_cast<size_t>(w) * static_cast<size_t>(h) * 4u) {
        out_error = "invalid raw cache payload";
        return false;
    }

    std::filesystem::path tmp = path;
    tmp += ".tmp";

    std::FILE* f = std::fopen(tmp.string().c_str(), "wb");
    if (!f) {
        out_error = "cannot open raw cache for write";
        return false;
    }
    char header[64];
    const int header_len = std::snprintf(header, sizeof(header), "%s\n%d %d\n", kRawMagic, w, h);
    bool ok = header_len > 0 && static_cast<size_t>(header_len) < sizeof(header);
    if (ok) ok = std::fwrite(header, 1, static_cast<size_t>(header_len), f) == static_cast<size_t>(header_len);
    if (ok) ok = std::fwrite(pixels.data(), 1, pixels.size(), f) == pixels.size();
    std::fclose(f);

    if (!ok) {
        std::error_code ec;
        std::filesystem::remove(tmp, ec);
        out_error = "failed writing raw cache";
        return false;
    }

    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        out_error = "failed finalizing raw cache";
        return false;
    }
    return true;
}

bool load_raw_cache(const std::filesystem::path& path,
                    int& out_w,
                    int& out_h,
                    std::vector<uint8_t>& out_pixels) {
    out_w = 0;
    out_h = 0;
    out_pixels.clear();

    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size <= kRawMagicLen + 4) return false;

    std::FILE* f = std::fopen(path.string().c_str(), "rb");
    if (!f) return false;

    char header[64] = {0};
    const size_t header_read = std::fread(header, 1, sizeof(header) - 1, f);
    if (header_read < kRawMagicLen + 4 || std::memcmp(header, kRawMagic, kRawMagicLen) != 0 ||
        header[kRawMagicLen] != '\n') {
        std::fclose(f);
        return false;
    }

    int w = 0;
    int h = 0;
    int consumed = 0;
    if (std::sscanf(header + kRawMagicLen + 1, "%d %d%n", &w, &h, &consumed) != 2 || w <= 0 || h <= 0) {
        std::fclose(f);
        return false;
    }
    const size_t pixel_offset = kRawMagicLen + 1 + static_cast<size_t>(consumed) + 1;  // + '\n'
    const size_t expected = static_cast<size_t>(w) * static_cast<size_t>(h) * 4u;
    if (pixel_offset + expected != static_cast<size_t>(size)) {
        std::fclose(f);
        return false;
    }

    if (std::fseek(f, static_cast<long>(pixel_offset), SEEK_SET) != 0) {
        std::fclose(f);
        return false;
    }
    out_pixels.resize(expected);
    const size_t read = std::fread(out_pixels.data(), 1, expected, f);
    std::fclose(f);
    if (read != expected) {
        out_pixels.clear();
        return false;
    }

    out_w = w;
    out_h = h;
    return true;
}

void prune_cache_dir(const std::filesystem::path& dir, size_t max_files, uint64_t max_bytes) {
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return;

    struct Entry {
        std::filesystem::path path;
        uint64_t size;
        std::filesystem::file_time_type mtime;
    };
    std::vector<Entry> entries;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        Entry e;
        e.path = it->path();
        e.size = static_cast<uint64_t>(it->file_size(ec));
        if (ec) {
            ec.clear();
            continue;
        }
        e.mtime = it->last_write_time(ec);
        if (ec) {
            ec.clear();
            continue;
        }
        entries.push_back(std::move(e));
    }

    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        return a.mtime > b.mtime;  // newest first
    });

    size_t kept = 0;
    uint64_t kept_bytes = 0;
    for (const auto& e : entries) {
        const bool fits = kept < max_files && (kept_bytes + e.size) <= max_bytes;
        if (fits) {
            ++kept;
            kept_bytes += e.size;
        } else {
            std::error_code rm_ec;
            std::filesystem::remove(e.path, rm_ec);
        }
    }
}

} // namespace prismix::patchstorage::artwork
