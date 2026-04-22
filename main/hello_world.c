#include <stdio.h>
#include <stdlib.h>
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "model_path.h"

static const char *TAG = "MIC";
i2s_chan_handle_t rx_handle;

// AFE handle - sẽ được khởi tạo trong app_main
const esp_afe_sr_iface_t *afe_handle = NULL;
afe_config_t *afe_config = NULL;
esp_afe_sr_data_t *afe_data = NULL;

void init_i2s(void) {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &rx_handle));

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = GPIO_NUM_5,
            .ws   = GPIO_NUM_6,
            .dout = I2S_GPIO_UNUSED,
            .din  = GPIO_NUM_4,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle));
    ESP_LOGI(TAG, "I2S initialized OK");
}

void mic_read_task(void *arg) {
    const int SAMPLE_COUNT = 256;
    int32_t *raw = malloc(SAMPLE_COUNT * sizeof(int32_t));
    if (!raw) {
        ESP_LOGE(TAG, "malloc failed");
        vTaskDelete(NULL);
        return;
    }

    size_t bytes_read = 0;

    while (1) {
        esp_err_t ret = i2s_channel_read(
            rx_handle,
            raw,
            SAMPLE_COUNT * sizeof(int32_t),
            &bytes_read,
            portMAX_DELAY
        );

        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "I2S read error: %s", esp_err_to_name(ret));
            continue;
        }

        int samples_read = bytes_read / sizeof(int32_t);
        int32_t peak = 0;
        int64_t sum_sq = 0;

        for (int i = 0; i < samples_read; i++) {
            int32_t val = raw[i] >> 8;
            int32_t abs_val = val < 0 ? -val : val;

            if (abs_val > peak) peak = abs_val;
            sum_sq += (int64_t)val * val;
        }

        int32_t rms = (int32_t)(sum_sq / samples_read);

        ESP_LOGI(TAG, "samples=%d | peak=%-8ld | rms=%-8ld | noise_floor=%s",
            samples_read,
            peak,
            rms,
            peak < 5000 ? "SILENT" : peak < 50000 ? "AMBIENT" : "VOICE/LOUD"
        );
    }

    free(raw);
    vTaskDelete(NULL);
}

void app_main(void) {
    init_i2s();

    // Khởi tạo AFE (Audio Front-End)
    // Sử dụng API mới: esp_afe_handle_from_config()
    afe_config = afe_config_init("M", NULL, AFE_TYPE_SR, AFE_MODE_HIGH_PERF);
    if (afe_config == NULL) {
        ESP_LOGE(TAG, "AFE config init failed!");
        return;
    }

    // Cấu hình AFE
    afe_config->aec_init = false;        // Không có loa phản hồi → tắt AEC
    afe_config->se_init = true;          // Bật Speech Enhancement
    afe_config->vad_init = true;         // Bật Voice Activity Detection
    afe_config->wakenet_init = true;     // Bật WakeNet
    // wakenet_model_name sẽ được tự động chọn từ menuconfig

    // Tạo AFE handle từ config
    afe_handle = esp_afe_handle_from_config(afe_config);
    if (afe_handle == NULL) {
        ESP_LOGE(TAG, "AFE handle creation failed!");
        return;
    }

    // Tạo AFE data
    afe_data = afe_handle->create_from_config(afe_config);
    if (afe_data == NULL) {
        ESP_LOGE(TAG, "AFE data creation failed!");
        return;
    }
    ESP_LOGI(TAG, "AFE initialized successfully");

    xTaskCreatePinnedToCore(
        mic_read_task,
        "mic_read",
        4096,
        NULL,
        5,
        NULL,
        1
    );
}