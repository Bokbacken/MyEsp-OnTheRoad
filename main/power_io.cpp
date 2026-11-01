#include "power_io.h"

#include <atomic>
#include <string.h>
#include "esp_partition.h"

namespace {
std::atomic<uint32_t> s_sdRead{0}, s_sdWrite{0}, s_flashRead{0}, s_flashWrite{0};
bool ffat(const esp_partition_t *p)
{
    return p && p->type == ESP_PARTITION_TYPE_DATA && p->subtype == ESP_PARTITION_SUBTYPE_DATA_FAT &&
           strcmp(p->label, "ffat") == 0;
}
}

namespace PowerIo {
void sdRead(size_t bytes) { s_sdRead.fetch_add(bytes, std::memory_order_relaxed); }
void sdWrite(size_t bytes) { s_sdWrite.fetch_add(bytes, std::memory_order_relaxed); }
Counts counts() { return {s_sdRead.load(), s_sdWrite.load(), s_flashRead.load(), s_flashWrite.load()}; }
}

extern "C" esp_err_t __real_esp_partition_read(const esp_partition_t *, size_t, void *, size_t);
extern "C" esp_err_t __real_esp_partition_write(const esp_partition_t *, size_t, const void *, size_t);

extern "C" esp_err_t __wrap_esp_partition_read(const esp_partition_t *p, size_t offset, void *data, size_t size)
{
    const esp_err_t err = __real_esp_partition_read(p, offset, data, size);
    if (err == ESP_OK && ffat(p)) s_flashRead.fetch_add(size, std::memory_order_relaxed);
    return err;
}

extern "C" esp_err_t __wrap_esp_partition_write(const esp_partition_t *p, size_t offset, const void *data, size_t size)
{
    const esp_err_t err = __real_esp_partition_write(p, offset, data, size);
    if (err == ESP_OK && ffat(p)) s_flashWrite.fetch_add(size, std::memory_order_relaxed);
    return err;
}
