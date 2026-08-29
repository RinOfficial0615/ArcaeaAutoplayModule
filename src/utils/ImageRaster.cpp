#include "utils/ImageRaster.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#define STBI_NO_THREAD_LOCALS
#define STBI_NO_FAILURE_STRINGS
#define STBI_WRITE_NO_STDIO
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wimplicit-fallthrough"
#pragma clang diagnostic ignored "-Wdouble-promotion"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#endif
#include <stb/stb_image.h>
#include <stb/stb_image_write.h>
#include <stb/stb_image_resize2.h>
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

namespace arc_helper {
namespace {

constexpr int kJpegQuality = 85;
constexpr int kOfficialBackgroundWidth = 1920;
constexpr int kOfficialBackgroundHeight = 1440;
constexpr uint64_t kMaxBackgroundBytes = 128ull * 1024 * 1024;
// Bounds the decoded pixel buffer, not just the compressed input: a crafted
// PNG can expand far beyond its encoded size.
constexpr uint64_t kMaxDecodedPixels = 16ull * 1024 * 1024;

bool IsJpeg(std::span<const uint8_t> bytes) {
    return bytes.size() >= 3 && bytes[0] == 0xFF && bytes[1] == 0xD8 && bytes[2] == 0xFF;
}

void AppendJpeg(void *context, void *data, int size) {
    auto *out = static_cast<std::vector<uint8_t> *>(context);
    const auto *bytes = static_cast<const uint8_t *>(data);
    out->insert(out->end(), bytes, bytes + size);
}

} // namespace

std::optional<RasterImage> NormalizeBackgroundImage(std::span<const uint8_t> bytes,
                                                    std::string *error) {
    if (bytes.empty() || bytes.size() > kMaxBackgroundBytes) {
        if (error) *error = "background image empty or too large";
        return std::nullopt;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    if (!stbi_info_from_memory(bytes.data(), static_cast<int>(bytes.size()),
                               &width, &height, &channels) ||
        width <= 0 || height <= 0 ||
        static_cast<uint64_t>(width) * static_cast<uint64_t>(height) > kMaxDecodedPixels) {
        if (error) *error = "background image empty or dimensions too large";
        return std::nullopt;
    }
    stbi_uc *pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()),
                                            &width, &height, &channels, 3);
    if (!pixels || width <= 0 || height <= 0) {
        if (pixels) stbi_image_free(pixels);
        if (error) *error = "background image decode failed";
        return std::nullopt;
    }

    RasterImage result;
    result.source_width = width;
    result.source_height = height;
    const int dst_w = kOfficialBackgroundWidth;
    const int dst_h = kOfficialBackgroundHeight;
    if (width == dst_w && height == dst_h && IsJpeg(bytes)) {
        stbi_image_free(pixels);
        result.jpeg.assign(bytes.begin(), bytes.end());
        return result;
    }

    const double src_aspect = static_cast<double>(width) / static_cast<double>(height);
    const double dst_aspect = static_cast<double>(dst_w) / static_cast<double>(dst_h);
    int crop_x = 0;
    int crop_y = 0;
    int crop_w = width;
    int crop_h = height;
    if (std::abs(src_aspect - dst_aspect) > 0.01) {
        if (src_aspect > dst_aspect) {
            // lround can overshoot in either direction once the aspect ratios
            // differ, so the clamp guarantees a non-empty in-bounds crop.
            crop_w = std::clamp(static_cast<int>(std::lround(height * dst_aspect)), 1, width);
            crop_x = (width - crop_w) / 2;
        } else {
            crop_h = std::clamp(static_cast<int>(std::lround(width / dst_aspect)), 1, height);
            crop_y = (height - crop_h) / 2;
        }
    }

    std::vector<uint8_t> resized(static_cast<size_t>(dst_w) * static_cast<size_t>(dst_h) * 3);
    const unsigned char *src = pixels + (static_cast<size_t>(crop_y) * width + crop_x) * 3;
    if (!stbir_resize_uint8_srgb(src, crop_w, crop_h, width * 3, resized.data(), dst_w, dst_h, 0,
                                 STBIR_RGB)) {
        stbi_image_free(pixels);
        if (error) *error = "background image resize failed";
        return std::nullopt;
    }
    stbi_image_free(pixels);

    if (!stbi_write_jpg_to_func(AppendJpeg, &result.jpeg, dst_w, dst_h, 3, resized.data(),
                                kJpegQuality) ||
        result.jpeg.empty()) {
        if (error) *error = "background jpeg encode failed";
        return std::nullopt;
    }
    result.resized = width != dst_w || height != dst_h;
    return result;
}

} // namespace arc_helper
