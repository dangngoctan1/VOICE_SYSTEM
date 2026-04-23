#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "esp_mn_speech_commands.h"
#include "esp_mn_models.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_mn_iface.h"
#include "model_path.h"

static const char *TAG = "AI_MIC";
static i2s_chan_handle_t rx_handle;
static const esp_afe_sr_iface_t *afe_handle = NULL;
static esp_afe_sr_data_t        *afe_data   = NULL;
static const esp_mn_iface_t     *multinet   = NULL;
static model_iface_data_t       *model_data = NULL;


static void init_i2s(void) {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &rx_handle));

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_32BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode      = I2S_SLOT_MODE_STEREO,   /* đọc cả 2 slot */
            .slot_mask      = I2S_STD_SLOT_LEFT,       /* chỉ lấy left */
            .ws_width       = 32,
            .ws_pol         = false,
            .bit_shift      = true,                    /* Philips: data lệch 1 bit */
            .left_align     = false,
            .big_endian     = false,
            .bit_order_lsb  = false,
        },
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = GPIO_NUM_5,
            .ws   = GPIO_NUM_6,
            .dout = I2S_GPIO_UNUSED,
            .din  = GPIO_NUM_4,
            .invert_flags = { false, false, false },
        },
    };

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle));
    ESP_LOGI(TAG, "I2S OK");
}

static void init_afe(void)
{
    srmodel_list_t *models = esp_srmodel_init("model");
    if (!models || models->num == 0) {
        ESP_LOGE(TAG, "No SR models found in SPIFFS!");
        abort();
    }

    for (int i = 0; i < models->num; i++) {
        ESP_LOGI(TAG, "Found model[%d]: %s", i, models->model_name[i]);
    }

    /* Dùng filter để lấy đúng wakenet name từ SPIFFS thay vì hardcode */
    char *wn_name = esp_srmodel_filter(models, ESP_WN_PREFIX, NULL);
    if (!wn_name) {
        ESP_LOGE(TAG, "No WakeNet model found! Check menuconfig ESP Speech Recognition");
        abort();
    }
    ESP_LOGI(TAG, "Using WakeNet: %s", wn_name);

    /* "M" = 1 mic, không AEC => đúng cho INMP441 single mic */
    afe_config_t *cfg =
        afe_config_init("M", models, AFE_TYPE_SR, AFE_MODE_HIGH_PERF);

    if (!cfg) {
        ESP_LOGE(TAG, "afe_config_init failed");
        abort();
    }

    cfg->aec_init     = false;
    cfg->se_init      = false;  
    cfg->vad_init     = true;
    cfg->wakenet_init = true;
    cfg->wakenet_model_name = wn_name;

    /* In ra config để debug */
    afe_config_print(cfg);

    afe_handle = esp_afe_handle_from_config(cfg);
    if (!afe_handle) {
        ESP_LOGE(TAG, "esp_afe_handle_from_config failed");
        abort();
    }

    afe_data = afe_handle->create_from_config(cfg);
    if (!afe_data) {
        ESP_LOGE(TAG, "afe create_from_config failed");
        abort();
    }

    afe_handle->set_wakenet_threshold(afe_data, 1, 0.45f);

    int chunk   = afe_handle->get_feed_chunksize(afe_data);
    int nch     = afe_handle->get_feed_channel_num(afe_data); /* v2 API */

    ESP_LOGI(TAG, "AFE ready | feed_chunksize=%d | feed_channels=%d", chunk, nch);
}


static void init_multinet(void)
{
    multinet = esp_mn_handle_from_name("mn7_en");
    if (!multinet) {
        ESP_LOGE(TAG, "MultiNet7 handle failed");
        abort();
    }

    model_data = multinet->create("mn7_en", 6000);
    if (!model_data) {
        ESP_LOGE(TAG, "MultiNet7 create failed");
        abort();
    }

    /* v2 API: dùng global command manager */
    ESP_ERROR_CHECK(esp_mn_commands_alloc(multinet, model_data));

    ESP_ERROR_CHECK(esp_mn_commands_add(0, "turn on red"));
    ESP_ERROR_CHECK(esp_mn_commands_add(1, "make it red"));
    ESP_ERROR_CHECK(esp_mn_commands_add(2, "turn on green"));
    ESP_ERROR_CHECK(esp_mn_commands_add(3, "make it green"));
    ESP_ERROR_CHECK(esp_mn_commands_add(4, "turn on blue"));
    ESP_ERROR_CHECK(esp_mn_commands_add(5, "make it blue"));
    ESP_ERROR_CHECK(esp_mn_commands_add(6, "turn off"));
    ESP_ERROR_CHECK(esp_mn_commands_add(7, "lights off"));

    esp_mn_error_t *err = esp_mn_commands_update();
    if (err && err->num > 0) {
        for (int i = 0; i < err->num; i++) {
            ESP_LOGW(TAG, "Rejected command: %s", err->phrases[i]->string);
        }
    }

    esp_mn_commands_print();
    ESP_LOGI(TAG, "MultiNet7 ready");
}

static void audio_feed_task(void *arg)
{
    int chunk = afe_handle->get_feed_chunksize(afe_data);
    int nch   = afe_handle->get_feed_channel_num(afe_data);

    int32_t *buf32 = heap_caps_malloc(chunk * sizeof(int32_t), MALLOC_CAP_INTERNAL);
    int16_t *buf16 = heap_caps_malloc(chunk * nch * sizeof(int16_t), MALLOC_CAP_INTERNAL);

    if (!buf32 || !buf16) {
        ESP_LOGE(TAG, "Feed task buffer alloc failed (chunk=%d nch=%d)", chunk, nch);
        abort();
    }

    ESP_LOGI(TAG, "Feed task started: chunk=%d nch=%d", chunk, nch);

    while (1) {
        size_t bytes_read = 0;

        esp_err_t ret = i2s_channel_read(
            rx_handle,
            buf32,
            chunk * sizeof(int32_t),
            &bytes_read,
            portMAX_DELAY
        );

        if (ret != ESP_OK || bytes_read == 0) {
            ESP_LOGW(TAG, "I2S read error: %s, bytes=%d", esp_err_to_name(ret), bytes_read);
            continue;
        }

        int samples = bytes_read / sizeof(int32_t);

        for (int i = 0; i < samples; i++) {
            buf16[i] = (int16_t)(buf32[i] >> 16);
        }

        afe_handle->feed(afe_data, buf16);
    }
}

static void audio_detect_task(void *arg) {
    bool listening = false;

    ESP_LOGI(TAG, "detect_task OK — say wake word");

    while (1) {
        afe_fetch_result_t *res = afe_handle->fetch(afe_data);
        if (!res) continue;

        if (res->wakeup_state == WAKENET_DETECTED) {
            ESP_LOGI(TAG, ">>> WAKE WORD detected!");
            listening = true;
            multinet->clean(model_data);
        }

        if (listening) {
            esp_mn_state_t state = multinet->detect(model_data, res->data);

            if (state == ESP_MN_STATE_DETECTED) {
                esp_mn_results_t *r = multinet->get_results(model_data);

                ESP_LOGI(TAG, ">>> COMMAND id=%d phrase='%s' prob=%.2f",
                         r->command_id[0], r->string, r->prob[0]);

                listening = false;
                ESP_LOGI(TAG, "Free heap: %lu", esp_get_free_heap_size());
            }
            else if (state == ESP_MN_STATE_TIMEOUT) {
                ESP_LOGW(TAG, ">>> TIMEOUT");
                listening = false;
            }
        }
    }

    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "System start");

    init_i2s();
    init_afe();
    init_multinet();

    xTaskCreatePinnedToCore(audio_feed_task,
                            "feed_task",
                            8192,       
                            NULL,
                            8,
                            NULL,
                            1);

    xTaskCreatePinnedToCore(audio_detect_task,
                            "detect_task",
                            8192,
                            NULL,
                            7,
                            NULL,
                            1);
}