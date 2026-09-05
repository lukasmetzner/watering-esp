#include "moisture.h"

#include <strings.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_log.h>

#include "esp_log.h"

#define ADC_PIN       ADC_CHANNEL_6     // ADC1 channel 6 -> GPIO34 (input-only)
#define ADC_UNIT      ADC_UNIT_1        // ADC1: ADC2 is co-used by the Wi-Fi driver and
                                        // adc_oneshot_read() times out once Wi-Fi is started
#define ADC_BITWIDTH  ADC_BITWIDTH_12   // 12-bit resolution (0-4095)
#define ADC_ATTEN     ADC_ATTEN_DB_12   // ~3.3V full-scale voltage

static const char *TAG = "moisture";
static adc_oneshot_unit_handle_t adc_handle;

void moisture_init(void) {
    // Initialize ADC Oneshot Mode Driver on the ADC Unit
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT,
        .clk_src = ADC_RTC_CLK_SRC_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc_handle));

    // Configure ADC channel
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH,
        .atten = ADC_ATTEN,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, ADC_PIN, &config));
}

int read_moisture_sensor() {
    int adc_value;
    esp_err_t err = adc_oneshot_read(adc_handle, ADC_PIN, &adc_value);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "adc_oneshot_read failed: %s", esp_err_to_name(err));
        return -1;
    }
    return adc_value;
}