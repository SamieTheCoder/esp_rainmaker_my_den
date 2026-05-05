/* ESP RainMaker Template - Generic App Main

   This template provides a fully generic app_main.c that can be used
   across all ESP RainMaker device types. All device-specific logic
   is handled in app_devices.c and app_driver.c files.

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <driver/gpio.h>
#include <esp_event.h>
#include <esp_wifi.h>

#include <esp_rmaker_console.h>
#include <esp_rmaker_core.h>
#include <esp_rmaker_standard_params.h>
#include <esp_rmaker_standard_devices.h>
#include <esp_rmaker_schedule.h>
#include <esp_rmaker_console.h>
#include <esp_rmaker_scenes.h>
#include <esp_rmaker_utils.h>

#include <app_network.h>
#include <app_insights.h>

#include "app_devices.h"
#include "app_status_led.h"

static const char *TAG = "app_main";

#define RESET_BUTTON_GPIO               GPIO_NUM_0
#define WIFI_RESET_HOLD_TIME_MS         3000
#define FACTORY_RESET_HOLD_TIME_MS      10000
#define RESET_ACTION_DELAY_SECONDS      2
#define RESET_ACTION_REBOOT_SECONDS     2
#define BUTTON_POLL_INTERVAL_MS         50

static void app_connectivity_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        app_status_led_indicate_setup_mode();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        app_status_led_indicate_complete();
    }
}

static void boot_button_monitor_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "BOOT button reset monitor started on GPIO %d", RESET_BUTTON_GPIO);

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << RESET_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    bool prev_pressed = false;
    TickType_t press_start = 0;

    while (true) {
        bool pressed = (gpio_get_level(RESET_BUTTON_GPIO) == 0);

        if (pressed && !prev_pressed) {
            press_start = xTaskGetTickCount();
        } else if (!pressed && prev_pressed) {
            uint32_t press_duration_ms = (uint32_t)((xTaskGetTickCount() - press_start) * portTICK_PERIOD_MS);
            if (press_duration_ms >= FACTORY_RESET_HOLD_TIME_MS) {
                ESP_LOGW(TAG, "BOOT button held %lu ms. Triggering factory reset.", (unsigned long)press_duration_ms);
                app_status_led_indicate_factory_reset();
                ESP_ERROR_CHECK(esp_rmaker_factory_reset(RESET_ACTION_DELAY_SECONDS, RESET_ACTION_REBOOT_SECONDS));
            } else if (press_duration_ms >= WIFI_RESET_HOLD_TIME_MS) {
                ESP_LOGW(TAG, "BOOT button held %lu ms. Triggering Wi-Fi reset.", (unsigned long)press_duration_ms);
                app_status_led_indicate_wifi_reset();
                ESP_ERROR_CHECK(esp_rmaker_wifi_reset(RESET_ACTION_DELAY_SECONDS, RESET_ACTION_REBOOT_SECONDS));
            }
        }

        prev_pressed = pressed;
        vTaskDelay(pdMS_TO_TICKS(BUTTON_POLL_INTERVAL_MS));
    }
}

void app_main()
{
    /* Initialize Application specific hardware drivers and set initial state */
    app_driver_init();
    app_status_led_init();
    esp_rmaker_console_init();

    /* Initialize NVS */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK( err );

    /* Initialize Wi-Fi/Thread. Note that, this should be called before esp_rmaker_node_init() */
    app_network_init();
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, app_connectivity_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, app_connectivity_event_handler, NULL));

    /* Initialize the ESP RainMaker Agent.
     * Note that this should be called after app_network_init() but before app_network_start()
     */
    esp_rmaker_config_t rainmaker_cfg = {
        .enable_time_sync = false,
    };
    esp_rmaker_node_t *node = app_device_create_node(&rainmaker_cfg);
    if (!node) {
        vTaskDelay(5000/portTICK_PERIOD_MS);
        abort();
    }

    /* Create device and add it to the node */
    app_device_create(node);

    /* Enable OTA */
    esp_rmaker_ota_enable_default();

    /* Enable timezone service which will be require for setting appropriate timezone
     * from the phone apps for scheduling to work correctly.
     * For more information on the various ways of setting timezone, please check
     * https://rainmaker.espressif.com/docs/time-service.html.
     */
    esp_rmaker_timezone_service_enable();

    /* Enable scheduling */
    esp_rmaker_schedule_enable();

    /* Enable Scenes */
    esp_rmaker_scenes_enable();

    /* Enable system service */
    esp_rmaker_system_serv_config_t system_serv_config = {
        .flags = SYSTEM_SERV_FLAGS_ALL,
        .reboot_seconds = 2,
        .reset_seconds = 2,
        .reset_reboot_seconds = 2,
    };
    esp_rmaker_system_service_enable(&system_serv_config);

    /* Enable Insights. Requires CONFIG_ESP_INSIGHTS_ENABLED=y */
    app_insights_enable();

    xTaskCreate(boot_button_monitor_task, "boot_btn_reset", 4096, NULL, 5, NULL);

    /* Start the ESP RainMaker Agent */
    esp_rmaker_start();

    err = app_device_set_mfg_data();
    /* Start the Wi-Fi/Thread.
     * If the node is provisioned, it will start connection attempts,
     * else, it will start Wi-Fi provisioning. The function will return
     * after a connection has been successfully established
     */
    app_status_led_indicate_setup_mode();
    err = app_network_start(POP_TYPE_RANDOM);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not start network. Aborting!!!");
        vTaskDelay(5000/portTICK_PERIOD_MS);
        abort();
    }
}
