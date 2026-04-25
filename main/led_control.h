#pragma once
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

typedef struct { uint8_t r; uint8_t g; uint8_t b; } led_event_t;

extern QueueHandle_t led_queue;

void led_init(void);
void led_control_task(void *arg);