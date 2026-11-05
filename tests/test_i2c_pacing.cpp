#include <atomic>
#include <cassert>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

#include "otr_i2c.h"

constexpr int ESP_ERR_INVALID_STATE = 0x103;
constexpr int pdTRUE = 1;
constexpr int portMAX_DELAY = -1;
using SemaphoreHandle_t = std::mutex *;
std::mutex busMutex;
bool failAllocation = true;
bool failLock = false;
int errorLogs = 0;
int allocations = 0;
std::atomic<int64_t> clockUs{0};
std::atomic<int> delayCalls{0};
std::atomic<int> active{0};
std::vector<int64_t> starts;
std::vector<int64_t> ends;
int nextResult = ESP_OK;

#define ESP_LOGE(...) (++errorLogs)
#define ESP_LOGI(...) ((void)0)
int64_t esp_timer_get_time() { return clockUs.load(); }
void esp_rom_delay_us(uint32_t us)
{
    assert(us > 0 && us <= 100);
    ++delayCalls;
    clockUs += us;
}
SemaphoreHandle_t xSemaphoreCreateMutex()
{
    ++allocations;
    return failAllocation ? nullptr : &busMutex;
}
int xSemaphoreTake(SemaphoreHandle_t mutex, int timeout)
{
    assert(timeout == portMAX_DELAY);
    if (failLock) return 0;
    mutex->lock();
    return pdTRUE;
}
void xSemaphoreGive(SemaphoreHandle_t mutex) { mutex->unlock(); }

int transfer()
{
    assert(active.fetch_add(1) == 0);
    const int64_t start = clockUs.load();
    starts.push_back(start);
#ifdef CONFIG_OTR_HW_PROFILE_S3_35B
    if (!ends.empty()) assert(start - ends.back() >= 100);
#endif
    std::this_thread::yield();
    clockUs += 20;
    ends.push_back(clockUs.load());
    assert(active.fetch_sub(1) == 1);
    return nextResult;
}

esp_err_t i2c_master_probe(i2c_master_bus_handle_t bus, uint16_t address, int timeout)
{
    assert(bus && address == 0x20 && timeout == 30);
    return transfer();
}
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t device, const uint8_t *data, size_t size, int timeout)
{
    assert(device && data && size == 1 && timeout == 30);
    return transfer();
}
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t device, const uint8_t *tx, size_t txSize,
                                     uint8_t *rx, size_t rxSize, int timeout)
{
    assert(device && tx && txSize == 1 && rx && rxSize == 1 && timeout == 100);
    *rx = 0x42;
    return transfer();
}

// PACING_UNDER_TEST

int main()
{
    auto *bus = reinterpret_cast<void *>(1);
    uint8_t data = 0;
#ifdef CONFIG_OTR_HW_PROFILE_S3_35B
    assert(OtrI2c::probe(bus, 0x20, 30) == ESP_ERR_INVALID_STATE && starts.empty());
    assert(!OtrI2c::begin() && errorLogs == 2);
    failAllocation = false;
    assert(OtrI2c::begin() && OtrI2c::begin() && allocations == 2);
#endif
    assert(OtrI2c::probe(bus, 0x20, 30) == ESP_OK && delayCalls == 0);
    assert(OtrI2c::transmit(bus, &data, 1, 30) == ESP_OK);
    assert(OtrI2c::transmitReceive(bus, &data, 1, &data, 1, 100) == ESP_OK && data == 0x42);
#ifdef CONFIG_OTR_HW_PROFILE_S3_35B
    assert(delayCalls == 2);
    clockUs += 100;
    assert(OtrI2c::probe(bus, 0x20, 30) == ESP_OK && delayCalls == 2);
    clockUs += 40;
    assert(OtrI2c::probe(bus, 0x20, 30) == ESP_OK && delayCalls == 3);
#endif
    nextResult = ESP_FAIL;
    assert(OtrI2c::transmit(bus, &data, 1, 30) == ESP_FAIL);
    assert(OtrI2c::transmitReceive(bus, &data, 1, &data, 1, 100) == ESP_FAIL);
    assert(OtrI2c::probe(bus, 0x20, 30) == ESP_FAIL);
    nextResult = ESP_OK;
    assert(OtrI2c::probe(bus, 0x20, 30) == ESP_OK);
#ifdef CONFIG_OTR_HW_PROFILE_S3_35B
    failLock = true;
    const auto count = starts.size();
    assert(OtrI2c::probe(bus, 0x20, 30) == ESP_ERR_INVALID_STATE);
    assert(starts.size() == count && errorLogs == 3);
    failLock = false;
    std::vector<std::thread> tasks;
    for (int i = 0; i < 4; ++i) {
        tasks.emplace_back([bus] {
            uint8_t byte = 0;
            for (int j = 0; j < 100; ++j) {
                assert(OtrI2c::probe(bus, 0x20, 30) == ESP_OK);
                assert(OtrI2c::transmit(bus, &byte, 1, 30) == ESP_OK);
                assert(OtrI2c::transmitReceive(bus, &byte, 1, &byte, 1, 100) == ESP_OK);
            }
        });
    }
    for (auto &task : tasks) task.join();
    assert(starts.size() == count + 1200);
#else
    assert(delayCalls == 0 && allocations == 0);
#endif
    puts("I2C pacing: gap, transaction atomicity, failures and profile isolation passed");
}
