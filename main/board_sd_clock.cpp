#include "sdkconfig.h"
#include "esp_log.h"
#include "sdmmc_cmd.h"

static_assert(CONFIG_ESP_HOSTED_HOST_SDIO_CLK_KHZ == 50000,
              "The 50 MHz experiment requires C6 SDIO at 50000 kHz");

static const char *TAG = "sdclock";

extern "C" esp_err_t __real_sdmmc_io_enable_hs_mode(sdmmc_card_t *card);
extern "C" esp_err_t __real_sdmmc_init_host_frequency(sdmmc_card_t *card);

// ESP-IDF v6.1 caps SDIO HS at 40 MHz. As in MyPi-Hotspot, lift the cap only
// after successful HS negotiation. SD memory cards already negotiate 50 MHz.
extern "C" esp_err_t __wrap_sdmmc_io_enable_hs_mode(sdmmc_card_t *card)
{
    const esp_err_t err = __real_sdmmc_io_enable_hs_mode(card);
    if (err == ESP_OK && card->max_freq_khz == SDMMC_FREQ_HIGHSPEED &&
        card->host.max_freq_khz == 50000) {
        card->max_freq_khz = 50000;
        ESP_LOGI(TAG, "slot %d SDIO high-speed negotiated; requesting 50000 kHz",
                 card->host.slot);
    }
    return err;
}

extern "C" esp_err_t __wrap_sdmmc_init_host_frequency(sdmmc_card_t *card)
{
    if (card->host.max_freq_khz == 50000 && card->max_freq_khz != 50000) {
        ESP_LOGE(TAG, "slot %d cannot negotiate 50 MHz: negotiated=%d kHz",
                 card->host.slot, (int)card->max_freq_khz);
        return ESP_ERR_NOT_SUPPORTED;
    }
    const esp_err_t err = __real_sdmmc_init_host_frequency(card);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "slot %d clock initialization failed: %s",
                 card->host.slot, esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "slot %d requested=%d negotiated=%d actual=%d kHz",
             card->host.slot, card->host.max_freq_khz, (int)card->max_freq_khz,
             card->real_freq_khz);
    if (card->host.max_freq_khz == 50000 && card->real_freq_khz != 50000) {
        ESP_LOGE(TAG, "slot %d did not reach 50 MHz", card->host.slot);
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}
