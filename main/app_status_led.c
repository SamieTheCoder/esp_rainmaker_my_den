#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_err.h>
#include <esp_log.h>
#include <led_strip.h>
#include "app_status_led.h"

#define STATUS_LED_GPIO              21
#define STATUS_LED_COUNT             1
#define STATUS_LED_TASK_STACK        3072
#define STATUS_LED_TASK_PRIO         4
#define FACTORY_RESET_BLINK_MS       250
#define COMPLETE_SHOW_MS             1200
#define SWITCH_EVENT_SHOW_MS         350

typedef enum {
    BASE_MODE_OFF = 0,
    BASE_MODE_SETUP_BLUE,
    BASE_MODE_FACTORY_RESET_BLINK_RED,
} led_base_mode_t;

typedef enum {
    OVERLAY_NONE = 0,
    OVERLAY_RED,
    OVERLAY_GREEN,
} led_overlay_t;

static const char *TAG = "app_status_led";

static led_strip_handle_t s_strip;
static SemaphoreHandle_t s_led_lock;
static led_base_mode_t s_base_mode = BASE_MODE_OFF;
static led_overlay_t s_overlay = OVERLAY_NONE;
static TickType_t s_overlay_until = 0;
static uint8_t s_last_r = 255;
static uint8_t s_last_g = 255;
static uint8_t s_last_b = 255;

static void set_led_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_strip) {
        return;
    }
    if (s_last_r == r && s_last_g == g && s_last_b == b) {
        return;
    }
    s_last_r = r;
    s_last_g = g;
    s_last_b = b;

    led_strip_set_pixel(s_strip, 0, r, g, b);
    led_strip_refresh(s_strip);
}

static void status_led_task(void *arg)
{
    (void)arg;

    while (1) {
        led_base_mode_t base_mode;
        led_overlay_t overlay;
        TickType_t overlay_until;

        xSemaphoreTake(s_led_lock, portMAX_DELAY);
        base_mode = s_base_mode;
        overlay = s_overlay;
        overlay_until = s_overlay_until;
        xSemaphoreGive(s_led_lock);

        TickType_t now = xTaskGetTickCount();

        if (overlay != OVERLAY_NONE && now >= overlay_until) {
            xSemaphoreTake(s_led_lock, portMAX_DELAY);
            s_overlay = OVERLAY_NONE;
            overlay = OVERLAY_NONE;
            xSemaphoreGive(s_led_lock);
        }

        if (overlay == OVERLAY_RED) {
            set_led_rgb(255, 0, 0);
        } else if (overlay == OVERLAY_GREEN) {
            set_led_rgb(0, 255, 0);
        } else {
            switch (base_mode) {
                case BASE_MODE_SETUP_BLUE:
                    set_led_rgb(0, 0, 255);
                    break;
                case BASE_MODE_FACTORY_RESET_BLINK_RED:
                    if (((now * portTICK_PERIOD_MS) / FACTORY_RESET_BLINK_MS) % 2) {
                        set_led_rgb(255, 0, 0);
                    } else {
                        set_led_rgb(0, 0, 0);
                    }
                    break;
                case BASE_MODE_OFF:
                default:
                    set_led_rgb(0, 0, 0);
                    break;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void app_status_led_init(void)
{
    if (s_strip) {
        return;
    }

    s_led_lock = xSemaphoreCreateMutex();
    if (!s_led_lock) {
        ESP_LOGE(TAG, "Failed to create LED mutex");
        return;
    }

    led_strip_config_t strip_config = {
        .strip_gpio_num = STATUS_LED_GPIO,
        .max_leds = STATUS_LED_COUNT,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags = {
            .invert_out = false,
        },
    };
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 0,
        .flags = {
            .with_dma = false,
        },
    };

    esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_strip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WS2812 init failed on GPIO %d: %s", STATUS_LED_GPIO, esp_err_to_name(err));
        return;
    }

    set_led_rgb(0, 0, 0);
    xTaskCreate(status_led_task, "status_led_task", STATUS_LED_TASK_STACK, NULL, STATUS_LED_TASK_PRIO, NULL);
    ESP_LOGI(TAG, "WS2812 status LED initialized on GPIO %d", STATUS_LED_GPIO);
}

void app_status_led_indicate_setup_mode(void)
{
    if (!s_led_lock) {
        return;
    }
    xSemaphoreTake(s_led_lock, portMAX_DELAY);
    s_base_mode = BASE_MODE_SETUP_BLUE;
    s_overlay = OVERLAY_NONE;
    xSemaphoreGive(s_led_lock);
}

void app_status_led_indicate_complete(void)
{
    if (!s_led_lock) {
        return;
    }
    xSemaphoreTake(s_led_lock, portMAX_DELAY);
    s_base_mode = BASE_MODE_OFF;
    s_overlay = OVERLAY_GREEN;
    s_overlay_until = xTaskGetTickCount() + pdMS_TO_TICKS(COMPLETE_SHOW_MS);
    xSemaphoreGive(s_led_lock);
}

void app_status_led_indicate_factory_reset(void)
{
    if (!s_led_lock) {
        return;
    }
    xSemaphoreTake(s_led_lock, portMAX_DELAY);
    s_base_mode = BASE_MODE_FACTORY_RESET_BLINK_RED;
    s_overlay = OVERLAY_NONE;
    xSemaphoreGive(s_led_lock);
}

void app_status_led_indicate_wifi_reset(void)
{
    app_status_led_indicate_setup_mode();
}

void app_status_led_indicate_switch(bool on)
{
    if (!s_led_lock) {
        return;
    }
    xSemaphoreTake(s_led_lock, portMAX_DELAY);
    s_overlay = on ? OVERLAY_RED : OVERLAY_GREEN;
    s_overlay_until = xTaskGetTickCount() + pdMS_TO_TICKS(SWITCH_EVENT_SHOW_MS);
    xSemaphoreGive(s_led_lock);
}
