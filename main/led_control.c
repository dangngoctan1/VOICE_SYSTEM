#include "led_control.h"
#include "driver/ledc.h"
#include "hal/gpio_types.h"
#include "esp_log.h"

#define TAG "LED"

#define LED_R_GPIO  GPIO_NUM_15
#define LED_G_GPIO  GPIO_NUM_16
#define LED_B_GPIO  GPIO_NUM_17

#define LEDC_TIMER      LEDC_TIMER_0
#define LEDC_MODE       LEDC_LOW_SPEED_MODE
#define LEDC_FREQ_HZ    10000
#define LEDC_RES        LEDC_TIMER_8_BIT

QueueHandle_t led_queue;

static void set_led_color(uint8_t r, uint8_t g, uint8_t b)
{
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_0, r);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_0);
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_1, g);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_1);
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_2, b);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_2);
}

void led_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode      = LEDC_MODE,
        .timer_num       = LEDC_TIMER,
        .duty_resolution = LEDC_RES,
        .freq_hz         = LEDC_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    ledc_channel_config_t ch = {
        .speed_mode = LEDC_MODE,
        .timer_sel  = LEDC_TIMER,
        .intr_type  = LEDC_INTR_DISABLE,
        .duty       = 0,
        .hpoint     = 0,
    };

    ch.channel = LEDC_CHANNEL_0; ch.gpio_num = LED_R_GPIO;
    ESP_ERROR_CHECK(ledc_channel_config(&ch));
    ch.channel = LEDC_CHANNEL_1; ch.gpio_num = LED_G_GPIO;
    ESP_ERROR_CHECK(ledc_channel_config(&ch));
    ch.channel = LEDC_CHANNEL_2; ch.gpio_num = LED_B_GPIO;
    ESP_ERROR_CHECK(ledc_channel_config(&ch));

    led_queue = xQueueCreate(4, sizeof(led_event_t));
}

void led_control_task(void *arg)
{
    led_event_t ev;
    while (1) {
        if (xQueueReceive(led_queue, &ev, portMAX_DELAY)) {
            set_led_color(ev.r, ev.g, ev.b);
            ESP_LOGI(TAG, "Color → R:%d G:%d B:%d", ev.r, ev.g, ev.b);
        }
    }
}