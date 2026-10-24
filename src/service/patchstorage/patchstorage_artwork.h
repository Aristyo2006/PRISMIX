#pragma once

// -----------------------------------------------------------------------------
// PatchStorage artwork pipeline (worker-thread side).
//
// PatchStorage exposes `artwork.url` + `artwork.thumbnail_url` (PNG, verified
// against the live beta API) on every catalog item. This module downloads
// nothing itself: it decodes PNG bytes into a fixed pixel format and manages a
// small bounded on-disk cache so the same artwork is not downloaded or decoded
// again on later visits.
//
// The decoder is a self-contained, non-interlaced PNG reader (color types
// 0/2/3/4/6, bit depths 1/2/4/8/16) built on zlib, which is already a
// dependency of this library. It is deliberately independent of LVGL: it runs
// on the PatchStorage service worker thread, never on the LVGL or audio
// threads, and therefore can not touch LVGL caches or global state.
//
// Output pixels are in LVGL XRGB8888 memory order: bytes B, G, R, X.
// -----------------------------------------------------------------------------

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace prismix::patchstorage::artwork {

// Longest decoded side kept in memory / on disk. PatchStorage thumbnails are
// ~90-150 px; full artwork can be much larger, so we downsample to keep every
// card image small and uniform.
constexpr int kArtworkMaxDimension = 384;

// Safety limits for untrusted artwork payloads.
constexpr uint64_t kArtworkMaxDownloadBytes = 8ull * 1024ull * 1024ull;
constexpr uint64_t kArtworkMaxPixels = 4096ull * 4096ull;

// On-disk cache bounds (oldest entries pruned first).
constexpr size_t kArtworkCacheMaxFiles = 200;
constexpr uint64_t kArtworkCacheMaxBytes = 96ull * 1024ull * 1024ull;

// File name for a cache entry, e.g. "art_198161.png" / "art_198161.rgb".
std::filesystem::path cache_file_for(const std::filesystem::path& dir,
                                     int patch_id,
                                     const char* extension);

// Reads the PNG IHDR without decoding. Returns false when the buffer is not a
// PNG or the header is malformed.
bool peek_png_size(const uint8_t* data, size_t len, int& out_w, int& out_h, std::string& out_error);

// Decodes PNG bytes, downsamples so max(width, height) <= max_dimension
// (never upscales) and converts to B,G,R,X bytes.
bool decode_png_to_pixels(const uint8_t* data,
                          size_t len,
                          int max_dimension,
                          int& out_w,
                          int& out_h,
                          std::vector<uint8_t>& out_pixels,
                          std::string& out_error);

// Convenience wrapper for a file on disk.
bool decode_png_file_to_pixels(const std::filesystem::path& path,
                               int max_dimension,
                               int& out_w,
                               int& out_h,
                               std::vector<uint8_t>& out_pixels,
                               std::string& out_error);

// Raw decoded cache ("PSRAW1" header + dims + pixels). Loading it avoids both
// the download and the PNG decode on repeat visits.
bool save_raw_cache(const std::filesystem::path& path,
                    int w,
                    int h,
                    const std::vector<uint8_t>& pixels,
                    std::string& out_error);

bool load_raw_cache(const std::filesystem::path& path,
                    int& out_w,
                    int& out_h,
                    std::vector<uint8_t>& out_pixels);

// Deletes oldest files (by mtime) until the cache satisfies both bounds.
// Never throws; ignores files it cannot stat/remove.
void prune_cache_dir(const std::filesystem::path& dir,
                     size_t max_files = kArtworkCacheMaxFiles,
                     uint64_t max_bytes = kArtworkCacheMaxBytes);

} // namespace prismix::patchstorage::artwork
