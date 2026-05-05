
#include <esp_log.h>
#include <esp_rmaker_core.h>
#include <stdio.h>
#include <esp_rmaker_standard_types.h>
#include <esp_rmaker_standard_devices.h>
#include <esp_rmaker_standard_params.h>
#include <driver/uart.h>
#include <app_network.h>
#include "app_devices.h"
#include "app_status_led.h"

#define NODE_NAME "Samie's Den"
#define NODE_TYPE "touchpannel"

/*********** Bulb1 *************/
#define BULB1_DEVICE_NAME "East Bulb"
#define BULB1_DEVICE_TYPE "esp.device.lightbulb"
#define BULB1_POWER_PARAM_NAME "Power"
#define BULB1_POWER_PARAM_TYPE "esp.param.power"

/*********** Bulb2 *************/
#define BULB2_DEVICE_NAME "North Buld"
#define BULB2_DEVICE_TYPE "esp.device.lightbulb"
#define BULB2_POWER_PARAM_NAME "Power"
#define BULB2_POWER_PARAM_TYPE "esp.param.power"

/*********** Bulb3 *************/
#define BULB3_DEVICE_NAME "Terrace Bulb"
#define BULB3_DEVICE_TYPE "esp.device.lightbulb"
#define BULB3_POWER_PARAM_NAME "Power"
#define BULB3_POWER_PARAM_TYPE "esp.param.power"

/*********** Bulb4 *************/
#define BULB4_DEVICE_NAME "West Bulb"
#define BULB4_DEVICE_TYPE "esp.device.lightbulb"
#define BULB4_POWER_PARAM_NAME "Power"
#define BULB4_POWER_PARAM_TYPE "esp.param.power"

/*********** Curtain *************/
#define CURTAIN_DEVICE_NAME "Desk Setup"
#define CURTAIN_DEVICE_TYPE "esp.device.switch"
#define CURTAIN_POWER_PARAM_NAME "Power"
#define CURTAIN_POWER_PARAM_TYPE "esp.param.power"

/*********** AC *************/
#define AC_DEVICE_NAME "PC"
#define AC_DEVICE_TYPE "esp.device.switch"
#define AC_POWER_PARAM_NAME "Power"
#define AC_POWER_PARAM_TYPE "esp.param.power"

/*********** Fan *************/
#define FAN_DEVICE_NAME "Fan"
#define FAN_DEVICE_TYPE "esp.device.fan"
#define FAN_POWER_PARAM_NAME "Power"
#define FAN_POWER_PARAM_TYPE "esp.param.power"
#define FAN_SPEED_PARAM_NAME "Fan Speed"

/*********** UART config *************/
#define TOUCH_UART_NUM        UART_NUM_1
#define TOUCH_UART_TX_PIN     17
#define TOUCH_UART_RX_PIN     18
#define TOUCH_UART_BAUD       9600
#define TOUCH_UART_BUF_SIZE   256
#define TOUCH_UART_TASK_STACK 4096
#define TOUCH_UART_TASK_PRIO  5

static const char *TAG = "app_devices";

/********** Device handles **********/
static esp_rmaker_device_t *bulb1_device;
static esp_rmaker_device_t *bulb2_device;
static esp_rmaker_device_t *bulb3_device;
static esp_rmaker_device_t *bulb4_device;
static esp_rmaker_device_t *curtain_device;
static esp_rmaker_device_t *ac_device;
static esp_rmaker_device_t *fan_device;

/********** State variables **********/
static bool toggle_state_bulb1 = false;
static bool toggle_state_bulb2 = false;
static bool toggle_state_bulb3 = false;
static bool toggle_state_bulb4 = false;
static bool toggle_state_curtain = false;
static bool toggle_state_ac = false;
static bool toggle_state_fan = false;
static int  fan_speed = 0;

static bool uart_initialized = false;

/********** UART Command Sending **********/

/* Node mapping (matches your touch switch firmware):
 * Node 1 = Fan
 * Node 2 = Bulb1
 * Node 3 = Bulb2
 * Node 4 = Bulb3
 * Node 5 = Bulb4
 * Node 6 = Curtain
 * Node 7 = AC
 *
 * Command format: {0x7B, cmd, len, node, on/off, value, checksum, 0x7D}
 */
static void send_uart_command(uint8_t node, bool state)
{
    uint8_t cmd[8];

    cmd[0] = 0x7B;                      /* Start Code */
    cmd[1] = 0x00;                      /* Command */
    cmd[2] = 0x04;                      /* Length */
    cmd[3] = node;                      /* Node: 1=Fan, 2=Bulb1, 3=Bulb2, 4=Bulb3, 5=Bulb4, 6=Curtain, 7=AC */
    cmd[4] = state ? 0x00 : 0xFF;       /* ON=0x00, OFF=0xFF */
    cmd[5] = state ? 0xFE : 0x00;       /* ON value=0xFE, OFF value=0x00 */
    cmd[6] = (cmd[1] + cmd[2] + cmd[3] + cmd[4] + cmd[5]) & 0xFF;  /* Checksum: sum bytes 1-5 */
    cmd[7] = 0x7D;                      /* End Code */

    if (uart_initialized) {
        uart_write_bytes(TOUCH_UART_NUM, cmd, sizeof(cmd));
        printf("UART TX: ");
        for (int i = 0; i < 8; i++) printf("%02X ", cmd[i]);
        printf("\n");
        ESP_LOGI(TAG, "UART cmd node=%d state=%s ck=0x%02X", node, state ? "ON" : "OFF", cmd[6]);
    }
}

static void send_bulb1_command(bool state)  { send_uart_command(0x02, state); }
static void send_bulb2_command(bool state)  { send_uart_command(0x03, state); }
static void send_bulb3_command(bool state)  { send_uart_command(0x04, state); }
static void send_bulb4_command(bool state)  { send_uart_command(0x05, state); }
static void send_curtain_command(bool state){ send_uart_command(0x06, state); }
static void send_ac_command(bool state)     { send_uart_command(0x07, state); }

/* Fan speed levels: 0=off, 25, 50, 75, 100 */
static void fan_speed_control(int speed)
{
    if (!uart_initialized) return;

    uint8_t reg_off[]  = {0x7B, 0x00, 0x04, 0x01, 0xFF, 0x00, 0x04, 0x7D};
    uint8_t reg_25[]    = {0x7B, 0x00, 0x04, 0x01, 0x00, 0x19, 0x1E, 0x7D};
    uint8_t reg_50[]    = {0x7B, 0x00, 0x04, 0x01, 0x00, 0x32, 0x37, 0x7D};
    uint8_t reg_75[]    = {0x7B, 0x00, 0x04, 0x01, 0x00, 0x4B, 0x50, 0x7D};
    uint8_t reg_100[]   = {0x7B, 0x00, 0x04, 0x01, 0x00, 0x64, 0x69, 0x7D};
    uint8_t *cmd = NULL;
    int len = 0;

    switch (speed) {
        case 0:   cmd = reg_off;  len = sizeof(reg_off);  break;
        case 25:  cmd = reg_25;   len = sizeof(reg_25);    break;
        case 50:  cmd = reg_50;   len = sizeof(reg_50);    break;
        case 75:  cmd = reg_75;   len = sizeof(reg_75);    break;
        case 100: cmd = reg_100;  len = sizeof(reg_100);   break;
        default:  return;
    }
    uart_write_bytes(TOUCH_UART_NUM, cmd, len);
    ESP_LOGD(TAG, "Fan speed set to %d%%", speed);
}

/* Send periodic get-status command to touch switch */
static void send_get_status_command(void)
{
    if (!uart_initialized) return;
    uint8_t cmd[] = {0x7B, 0x02, 0x01, 0x01, 0x7D};
    uart_write_bytes(TOUCH_UART_NUM, cmd, sizeof(cmd));
}

/********** Response Parsing **********/
/* Status response format:
 * {0x7B, 0x51, len, [node1_state, node1_val, node2_state, node2_val, ...], 0x7D}
 * state 0x00 = ON, 0xFF = OFF
 */
static void parse_and_update_switch_states(uint8_t *data, int length)
{
    if (length < 6 || data[0] != 0x7B || data[1] != 0x51 || data[length - 1] != 0x7D) {
        return;
    }

    int payload_len = data[2];
    int num_nodes = (payload_len - 1) / 2;

    ESP_LOGI(TAG, "Status response: %d nodes", num_nodes);

    for (int i = 0; i < num_nodes && (3 + i * 2 + 1) < length; i++) {
        uint8_t node_num  = i + 1;
        uint8_t sw_state  = data[3 + i * 2];
        uint8_t dim_val    = data[3 + i * 2 + 1];
        bool power_on = (sw_state == 0x00);

        switch (node_num) {
            case 0x01: /* Fan */
                if (toggle_state_fan != power_on || fan_speed != dim_val) {
                    toggle_state_fan = power_on;
                    app_status_led_indicate_switch(toggle_state_fan);
                    if (dim_val != 0xFE) {
                        fan_speed = dim_val;
                    }
                    ESP_LOGI(TAG, "Fan changed: power=%s speed=%d", toggle_state_fan ? "ON" : "OFF", fan_speed);
                    vTaskDelay(pdMS_TO_TICKS(15000));
                    {
                        esp_rmaker_param_t *p = esp_rmaker_device_get_param_by_name(fan_device, FAN_POWER_PARAM_NAME);
                        if (p) esp_rmaker_param_update_and_report(p, esp_rmaker_bool(toggle_state_fan));
                    }
                    {
                        esp_rmaker_param_t *p = esp_rmaker_device_get_param_by_name(fan_device, FAN_SPEED_PARAM_NAME);
                        if (p) esp_rmaker_param_update_and_report(p, esp_rmaker_int(fan_speed));
                    }
                }
                break;
            case 0x02: /* Bulb1 */
                if (toggle_state_bulb1 != power_on) {
                    toggle_state_bulb1 = power_on;
                    app_status_led_indicate_switch(toggle_state_bulb1);
                    ESP_LOGI(TAG, "Bulb1 changed: %s", toggle_state_bulb1 ? "ON" : "OFF");
                    vTaskDelay(pdMS_TO_TICKS(15000));
                    esp_rmaker_param_t *p = esp_rmaker_device_get_param_by_name(bulb1_device, BULB1_POWER_PARAM_NAME);
                    if (p) esp_rmaker_param_update_and_report(p, esp_rmaker_bool(toggle_state_bulb1));
                }
                break;
            case 0x03: /* Bulb2 */
                if (toggle_state_bulb2 != power_on) {
                    toggle_state_bulb2 = power_on;
                    app_status_led_indicate_switch(toggle_state_bulb2);
                    ESP_LOGI(TAG, "Bulb2 changed: %s", toggle_state_bulb2 ? "ON" : "OFF");
                    vTaskDelay(pdMS_TO_TICKS(15000));
                    esp_rmaker_param_t *p = esp_rmaker_device_get_param_by_name(bulb2_device, BULB2_POWER_PARAM_NAME);
                    if (p) esp_rmaker_param_update_and_report(p, esp_rmaker_bool(toggle_state_bulb2));
                }
                break;
            case 0x04: /* Bulb3 */
                if (toggle_state_bulb3 != power_on) {
                    toggle_state_bulb3 = power_on;
                    app_status_led_indicate_switch(toggle_state_bulb3);
                    ESP_LOGI(TAG, "Bulb3 changed: %s", toggle_state_bulb3 ? "ON" : "OFF");
                    vTaskDelay(pdMS_TO_TICKS(15000));
                    esp_rmaker_param_t *p = esp_rmaker_device_get_param_by_name(bulb3_device, BULB3_POWER_PARAM_NAME);
                    if (p) esp_rmaker_param_update_and_report(p, esp_rmaker_bool(toggle_state_bulb3));
                }
                break;
            case 0x05: /* Bulb4 */
                if (toggle_state_bulb4 != power_on) {
                    toggle_state_bulb4 = power_on;
                    app_status_led_indicate_switch(toggle_state_bulb4);
                    ESP_LOGI(TAG, "Bulb4 changed: %s", toggle_state_bulb4 ? "ON" : "OFF");
                    vTaskDelay(pdMS_TO_TICKS(15000));
                    esp_rmaker_param_t *p = esp_rmaker_device_get_param_by_name(bulb4_device, BULB4_POWER_PARAM_NAME);
                    if (p) esp_rmaker_param_update_and_report(p, esp_rmaker_bool(toggle_state_bulb4));
                }
                break;
            case 0x06: /* Curtain */
                if (toggle_state_curtain != power_on) {
                    toggle_state_curtain = power_on;
                    app_status_led_indicate_switch(toggle_state_curtain);
                    ESP_LOGI(TAG, "Curtain changed: %s", toggle_state_curtain ? "ON" : "OFF");
                    vTaskDelay(pdMS_TO_TICKS(15000));
                    esp_rmaker_param_t *p = esp_rmaker_device_get_param_by_name(curtain_device, CURTAIN_POWER_PARAM_NAME);
                    if (p) esp_rmaker_param_update_and_report(p, esp_rmaker_bool(toggle_state_curtain));
                }
                break;
            case 0x07: /* AC */
                if (toggle_state_ac != power_on) {
                    toggle_state_ac = power_on;
                    app_status_led_indicate_switch(toggle_state_ac);
                    ESP_LOGI(TAG, "AC changed: %s", toggle_state_ac ? "ON" : "OFF");
                    vTaskDelay(pdMS_TO_TICKS(15000));
                    esp_rmaker_param_t *p = esp_rmaker_device_get_param_by_name(ac_device, AC_POWER_PARAM_NAME);
                    if (p) esp_rmaker_param_update_and_report(p, esp_rmaker_bool(toggle_state_ac));
                }
                break;
        }
    }
}

/********** UART Task **********/
static void uart_receive_task(void *arg)
{
    uint8_t buffer[TOUCH_UART_BUF_SIZE];

    ESP_LOGI(TAG, "UART task started");

    while (1) {
        int len = uart_read_bytes(TOUCH_UART_NUM, buffer, TOUCH_UART_BUF_SIZE - 1, pdMS_TO_TICKS(100));
        if (len > 0) {
            buffer[len] = '\0';

            /* Log raw bytes */
            char hex_str[TOUCH_UART_BUF_SIZE * 3] = {0};
            for (int i = 0; i < len; i++) {
                char tmp[4];
                snprintf(tmp, sizeof(tmp), "%02X ", buffer[i]);
                strncat(hex_str, tmp, sizeof(hex_str) - strlen(hex_str) - 1);
            }
            /* Print raw bytes to UART console like Arduino Serial.print(data, HEX) */
            printf("UART RX: ");
            for (int i = 0; i < len; i++) {
                printf("%02X ", buffer[i]);
            }
            printf("\n");

            ESP_LOGI(TAG, "UART RX: %s", hex_str);

            parse_and_update_switch_states(buffer, len);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void uart_poll_task(void *arg)
{
    TickType_t last_wake = xTaskGetTickCount();
    const TickType_t poll_interval = pdMS_TO_TICKS(5000); /* 30 seconds */

    while (1) {
        send_get_status_command();
        vTaskDelayUntil(&last_wake, poll_interval);
    }
}

/********** RainMaker Callback **********/
static esp_err_t app_device_bulk_write_cb(
    const esp_rmaker_device_t *device,
    const esp_rmaker_param_write_req_t *write_req,
    uint8_t count,
    void *priv_data,
    esp_rmaker_write_ctx_t *ctx)
{
    if (ctx) {
        ESP_LOGI(TAG, "Received write request via: %s", esp_rmaker_device_cb_src_to_str(ctx->src));
    }

    for (int i = 0; i < count; i++) {
        const esp_rmaker_param_t *param = write_req[i].param;
        esp_rmaker_param_val_t val = write_req[i].val;
        const char *device_name = esp_rmaker_device_get_name(device);
        const char *param_name = esp_rmaker_param_get_name(param);

        ESP_LOGI(TAG, "Device '%s' param '%s' new value", device_name, param_name);

        /********** Bulb1 **********/
        if (device == bulb1_device) {
            if (strcmp(param_name, BULB1_POWER_PARAM_NAME) == 0) {
                toggle_state_bulb1 = val.val.b;
                send_bulb1_command(toggle_state_bulb1);
                app_status_led_indicate_switch(toggle_state_bulb1);
                ESP_LOGI(TAG, "Bulb1 command sent: %s", toggle_state_bulb1 ? "ON" : "OFF");
            }
        }
        /********** Bulb2 **********/
        else if (device == bulb2_device) {
            if (strcmp(param_name, BULB2_POWER_PARAM_NAME) == 0) {
                toggle_state_bulb2 = val.val.b;
                send_bulb2_command(toggle_state_bulb2);
                app_status_led_indicate_switch(toggle_state_bulb2);
                ESP_LOGI(TAG, "Bulb2 command sent: %s", toggle_state_bulb2 ? "ON" : "OFF");
            }
        }
        /********** Bulb3 **********/
        else if (device == bulb3_device) {
            if (strcmp(param_name, BULB3_POWER_PARAM_NAME) == 0) {
                toggle_state_bulb3 = val.val.b;
                send_bulb3_command(toggle_state_bulb3);
                app_status_led_indicate_switch(toggle_state_bulb3);
                ESP_LOGI(TAG, "Bulb3 command sent: %s", toggle_state_bulb3 ? "ON" : "OFF");
            }
        }
        /********** Bulb4 **********/
        else if (device == bulb4_device) {
            if (strcmp(param_name, BULB4_POWER_PARAM_NAME) == 0) {
                toggle_state_bulb4 = val.val.b;
                send_bulb4_command(toggle_state_bulb4);
                app_status_led_indicate_switch(toggle_state_bulb4);
                ESP_LOGI(TAG, "Bulb4 command sent: %s", toggle_state_bulb4 ? "ON" : "OFF");
            }
        }
        /********** Curtain **********/
        else if (device == curtain_device) {
            if (strcmp(param_name, CURTAIN_POWER_PARAM_NAME) == 0) {
                toggle_state_curtain = val.val.b;
                send_curtain_command(toggle_state_curtain);
                app_status_led_indicate_switch(toggle_state_curtain);
                ESP_LOGI(TAG, "Curtain command sent: %s", toggle_state_curtain ? "ON" : "OFF");
            }
        }
        /********** AC **********/
        else if (device == ac_device) {
            if (strcmp(param_name, AC_POWER_PARAM_NAME) == 0) {
                toggle_state_ac = val.val.b;
                send_ac_command(toggle_state_ac);
                app_status_led_indicate_switch(toggle_state_ac);
                ESP_LOGI(TAG, "AC command sent: %s", toggle_state_ac ? "ON" : "OFF");
            }
        }
        /********** Fan **********/
        else if (device == fan_device) {
            if (strcmp(param_name, FAN_POWER_PARAM_NAME) == 0) {
                toggle_state_fan = val.val.b;
                if (toggle_state_fan) {
                    fan_speed_control(fan_speed > 0 ? fan_speed : 25);
                } else {
                    fan_speed_control(0);
                }
                app_status_led_indicate_switch(toggle_state_fan);
                ESP_LOGI(TAG, "Fan command sent: %s", toggle_state_fan ? "ON" : "OFF");
            } else if (strcmp(param_name, FAN_SPEED_PARAM_NAME) == 0) {
                fan_speed = val.val.i;
                if (toggle_state_fan) {
                    fan_speed_control(fan_speed);
                }
                ESP_LOGI(TAG, "Fan speed command sent: %d%%", fan_speed);
            }
        }
        /********** Unknown device **********/
        else {
            ESP_LOGW(TAG, "Unknown device in callback");
        }
    }
    return ESP_OK;
}

/********** Hardware / UART init **********/
void app_uart_init(void)
{
    uart_config_t uart_config = {
        .baud_rate  = TOUCH_UART_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity      = UART_PARITY_DISABLE,
        .stop_bits   = UART_STOP_BITS_1,
        .flow_ctrl   = UART_HW_FLOWCTRL_DISABLE,
    };

    ESP_ERROR_CHECK(uart_param_config(TOUCH_UART_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(TOUCH_UART_NUM,
                                 TOUCH_UART_TX_PIN,
                                 TOUCH_UART_RX_PIN,
                                 UART_PIN_NO_CHANGE,
                                 UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(TOUCH_UART_NUM,
                                        TOUCH_UART_BUF_SIZE,
                                        TOUCH_UART_BUF_SIZE,
                                        0, NULL, 0));
    uart_initialized = true;

    xTaskCreate(uart_receive_task, "uart_rx", TOUCH_UART_TASK_STACK, NULL, TOUCH_UART_TASK_PRIO, NULL);
    xTaskCreate(uart_poll_task,    "uart_poll", TOUCH_UART_TASK_STACK, NULL, TOUCH_UART_TASK_PRIO, NULL);

    ESP_LOGI(TAG, "UART initialized (TX=%d RX=%d %d baud)", TOUCH_UART_TX_PIN, TOUCH_UART_RX_PIN, TOUCH_UART_BAUD);
}

/********** Driver init (called from app_main) **********/
void app_driver_init(void)
{
    /* Initialize UART for touch switch communication */
    app_uart_init();
    ESP_LOGI(TAG, "Driver init complete");
}

/********** Node creation **********/
esp_rmaker_node_t *app_device_create_node(esp_rmaker_config_t *config)
{
    esp_rmaker_node_t *node = esp_rmaker_node_init(config, NODE_NAME, NODE_TYPE);
    if (!node) {
        ESP_LOGE(TAG, "Could not initialise node. Aborting!!!");
        return NULL;
    }
    return node;
}

/********** Device creation **********/
esp_rmaker_device_t *app_device_create(esp_rmaker_node_t *node)
{
    /* Bulb1 */
    bulb1_device = esp_rmaker_lightbulb_device_create(BULB1_DEVICE_NAME, NULL, false);
    if (!bulb1_device) {
        ESP_LOGE(TAG, "Failed to create %s", BULB1_DEVICE_NAME);
        return NULL;
    }
    esp_rmaker_device_add_bulk_cb(bulb1_device, app_device_bulk_write_cb, NULL);
    esp_rmaker_node_add_device(node, bulb1_device);

    /* Bulb2 */
    bulb2_device = esp_rmaker_lightbulb_device_create(BULB2_DEVICE_NAME, NULL, false);
    if (!bulb2_device) {
        ESP_LOGE(TAG, "Failed to create %s", BULB2_DEVICE_NAME);
        return NULL;
    }
    esp_rmaker_device_add_bulk_cb(bulb2_device, app_device_bulk_write_cb, NULL);
    esp_rmaker_node_add_device(node, bulb2_device);

    /* Bulb3 */
    bulb3_device = esp_rmaker_lightbulb_device_create(BULB3_DEVICE_NAME, NULL, false);
    if (!bulb3_device) {
        ESP_LOGE(TAG, "Failed to create %s", BULB3_DEVICE_NAME);
        return NULL;
    }
    esp_rmaker_device_add_bulk_cb(bulb3_device, app_device_bulk_write_cb, NULL);
    esp_rmaker_node_add_device(node, bulb3_device);

    /* Bulb4 */
    bulb4_device = esp_rmaker_lightbulb_device_create(BULB4_DEVICE_NAME, NULL, false);
    if (!bulb4_device) {
        ESP_LOGE(TAG, "Failed to create %s", BULB4_DEVICE_NAME);
        return NULL;
    }
    esp_rmaker_device_add_bulk_cb(bulb4_device, app_device_bulk_write_cb, NULL);
    esp_rmaker_node_add_device(node, bulb4_device);

    /* Curtain (switch type) */
    curtain_device = esp_rmaker_switch_device_create(CURTAIN_DEVICE_NAME, NULL, false);
    if (!curtain_device) {
        ESP_LOGE(TAG, "Failed to create %s", CURTAIN_DEVICE_NAME);
        return NULL;
    }
    esp_rmaker_device_add_bulk_cb(curtain_device, app_device_bulk_write_cb, NULL);
    esp_rmaker_node_add_device(node, curtain_device);

    /* AC (switch type) */
    ac_device = esp_rmaker_switch_device_create(AC_DEVICE_NAME, NULL, false);
    if (!ac_device) {
        ESP_LOGE(TAG, "Failed to create %s", AC_DEVICE_NAME);
        return NULL;
    }
    esp_rmaker_device_add_bulk_cb(ac_device, app_device_bulk_write_cb, NULL);
    esp_rmaker_node_add_device(node, ac_device);

    /* Fan with speed parameter */
    fan_device = esp_rmaker_fan_device_create(FAN_DEVICE_NAME, NULL, false);
    if (!fan_device) {
        ESP_LOGE(TAG, "Failed to create %s", FAN_DEVICE_NAME);
        return NULL;
    }
    esp_rmaker_device_add_bulk_cb(fan_device, app_device_bulk_write_cb, NULL);
    /* Speed param: 0=off, 25, 50, 75, 100. Matches touch switch dimmer values. */
    esp_rmaker_param_t *fan_speed = esp_rmaker_param_create(
        FAN_SPEED_PARAM_NAME, ESP_RMAKER_PARAM_SPEED,
        esp_rmaker_int(0), PROP_FLAG_READ | PROP_FLAG_WRITE);
    if (fan_speed) {

        esp_rmaker_param_add_ui_type(fan_speed, ESP_RMAKER_UI_SLIDER);
        esp_rmaker_param_add_bounds(fan_speed, esp_rmaker_int(0), esp_rmaker_int(100), esp_rmaker_int(25));
        esp_rmaker_device_add_param(fan_device, fan_speed);
    }
    esp_rmaker_node_add_device(node, fan_device);

    ESP_LOGI(TAG, "All devices created and added to node");
    return fan_device; /* Return last device as success indicator */
}

/********** Manufacturing data **********/
esp_err_t app_device_set_mfg_data(void)
{
    ESP_LOGW(TAG, "app_device_set_mfg_data() not implemented");
    return ESP_OK;
}
