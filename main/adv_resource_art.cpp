#include "adv_resource_art.h"

#include <string.h>
#include "lvgl.h"
#include "lvgl_private.h"

#if !LV_USE_LODEPNG
#error "Adventure resource images require LV_USE_LODEPNG"
#endif

namespace Adv {

bool decodeResourcePng(const uint8_t *data, size_t size, ResourceArt &out)
{
    out.ready = false;
    static const uint8_t signature[] = {137, 80, 78, 71, 13, 10, 26, 10};
    // Bound the file and dimensions before the decoder can allocate its output.
    if (!data || size < 33 || size > 16384 || memcmp(data, signature, 8) ||
        memcmp(data + 12, "IHDR", 4)) return false;
    const uint8_t dimension[] = {0, 0, 0, 32, 0, 0, 0, 32};
    if (memcmp(data + 16, dimension, sizeof dimension)) return false;
    lv_image_dsc_t image{};
    image.header.magic = LV_IMAGE_HEADER_MAGIC;
    image.header.cf = LV_COLOR_FORMAT_RAW_ALPHA;
    image.data = data;
    image.data_size = size;
    lv_image_decoder_dsc_t decoded{};
    lv_image_decoder_args_t args{};
    args.no_cache = true;
    if (lv_image_decoder_open(&decoded, &image, &args) != LV_RESULT_OK) return false;
    const auto *buf = decoded.decoded;
    if (buf && buf->header.w == 32 && buf->header.h == 32 &&
        buf->header.cf == LV_COLOR_FORMAT_ARGB8888 && buf->header.stride >= 128) {
        for (int y = 0; y < 32; ++y)
            memcpy(out.bgra + y * 128, buf->data + y * buf->header.stride, 128);
        out.ready = true;
    }
    lv_image_decoder_close(&decoded);
    return out.ready;
}

} // namespace Adv
