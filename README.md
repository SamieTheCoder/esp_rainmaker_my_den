# Touch Panel RainMaker - ESP32-S3 Smart Home Controller

A complete ESP-IDF project that bridges ESP RainMaker with a custom touch panel switch over UART. Control 7 devices (4 bulbs, 1 curtain, 1 AC, 1 fan) from the RainMaker mobile app.

---

## Overview

| Feature           | Detail                            |
| ----------------- | --------------------------------- |
| **MCU**           | ESP32-S3                          |
| **Framework**     | ESP-IDF v5.5 + ESP RainMaker      |
| **Communication** | UART1 @ 9600 baud to touch switch |
| **Devices**       | 4 bulbs, curtain, AC, fan         |
| **App Control**   | ESP RainMaker (iOS & Android)     |

---

## Hardware Setup

```
ESP32-S3              Touch Panel Switch
────────────          ────────────────
TX (GPIO 17)  ───────►  RX
RX (GPIO 18)  ◄───────  TX
GND           ────────  GND
```

> **Note:** Ensure both boards share the same GND reference.

---

## Pin Assignment

| GPIO | Function                                      |
| ---- | --------------------------------------------- |
| 17   | UART1 TX (to touch switch RX)                 |
| 18   | UART1 RX (from touch switch TX)               |
| 21   | WS2812 RGB status LED (ESP32-S3-Zero onboard) |

---

## UART Protocol

Commands sent to the touch switch follow this 8-byte format:

```
| 0x7B | 0x00 | 0x04 | NODE | ON/OFF | VALUE | CRC | 0x7D |
  Start  Cmd   Len   Addr   State  Dimmer Check  End
```

### Node Address Map

| Node | Device       | ON Byte | OFF Byte | Value ON | Value OFF |
| ---- | ------------ | ------- | -------- | -------- | --------- |
| 0x01 | Fan          | 0x00    | 0xFF     | 0xFE     | 0x00      |
| 0x02 | East Bulb    | 0x00    | 0xFF     | 0xFE     | 0x00      |
| 0x03 | North Bulb   | 0x00    | 0xFF     | 0xFE     | 0x00      |
| 0x04 | Terrace Bulb | 0x00    | 0xFF     | 0xFE     | 0x00      |
| 0x05 | West Bulb    | 0x00    | 0xFF     | 0xFE     | 0x00      |
| 0x06 | Desk Setup   | 0x00    | 0xFF     | 0xFE     | 0x00      |
| 0x07 | PC (AC)      | 0x00    | 0xFF     | 0xFE     | 0x00      |

### Checksum Calculation

```
CRC = byte1 + byte2 + byte3 + byte4 + byte5 (lower 8 bits)
```

### Example Commands

| Action            | Hex Bytes                 |
| ----------------- | ------------------------- |
| **East Bulb ON**  | `7B 00 04 02 00 FE 04 7D` |
| **East Bulb OFF** | `7B 00 04 02 FF 00 05 7D` |
| **Fan ON**        | `7B 00 04 01 00 FE 03 7D` |
| **Fan OFF**       | `7B 00 04 01 FF 00 04 7D` |

---

## Devices in RainMaker App

| #   | Device Name  | Type   | Control                         |
| --- | ------------ | ------ | ------------------------------- |
| 1   | East Bulb    | Light  | On/Off                          |
| 2   | North Bulb   | Light  | On/Off                          |
| 3   | Terrace Bulb | Light  | On/Off                          |
| 4   | West Bulb    | Light  | On/Off                          |
| 5   | Desk Setup   | Switch | On/Off                          |
| 6   | PC           | Switch | On/Off                          |
| 7   | Fan          | Fan    | On/Off + Speed (0/25/50/75/100) |

---

## Data Flow

```
┌──────────────┐         ┌──────────────┐         ┌──────────────┐
│  RainMaker   │         │   ESP32-S3   │         │  Touch Panel │
│     App      │◄───────►│  (ESP-IDF)   │◄───────►│    Switch    │
└──────────────┘   WiFi   └──────────────┘  UART   └──────────────┘
                              │
                              │ 30sec poll
                              ▼
                    ┌──────────────┐
                    │  Touch panel │
                    │  sends status│
                    │  response    │
                    └──────────────┘
```

### Flow Sequence

1. **App → ESP32**: User toggles switch in RainMaker app
2. **ESP32 → Touch Panel**: UART command sent immediately
3. **Touch Panel → ESP32**: Status response (polled every 5 seconds)
4. **ESP32 → App**: If state changed, update reported to app after 15 sec delay
5. **App**: Reflects the actual hardware state

---

## Building the Project

### Prerequisites

- ESP-IDF v5.5+ installed
- ESP32-S3 target selected

### Commands

```bash
# Navigate to project
cd touchPanel_rainmaker

# Set target
idf.py set-target esp32s3

# Build
idf.py build

# Flash
idf.py -p PORT flash

# Monitor serial output
idf.py monitor
```

> Replace `PORT` with your ESP32-S3 COM port (e.g., `COM3` on Windows, `/dev/ttyUSB0` on Linux).

---

## Configuration

### Node Name

To change the device name seen in the RainMaker app, edit [app_devices.c](main/app_devices.c):

```c
#define NODE_NAME "Samie's Den"
```

### Device Names

Rename devices by editing the defines at the top of [app_devices.c](main/app_devices.c):

```c
#define BULB1_DEVICE_NAME "East Bulb"   // Change to your preferred name
#define BULB2_DEVICE_NAME "North Bulb"
#define FAN_DEVICE_NAME   "Fan"
```

### UART Pins

If you need different GPIO pins:

```c
#define TOUCH_UART_TX_PIN  17   // Change as needed 17
#define TOUCH_UART_RX_PIN  18   // Change as needed 18
```

### Poll Interval

Status polling interval (default 5 seconds):

```c
const TickType_t poll_interval = pdMS_TO_TICKS(5000);
```

### Update Delay

Delay before reporting state change to app (default 15 seconds):

```c
vTaskDelay(pdMS_TO_TICKS(15000));  // Per device in parse_and_update_switch_states
```

### BOOT Button Reset (GPIO0)

`app_main.c` monitors the BOOT button and triggers RainMaker resets based on hold duration:

```c
#define RESET_BUTTON_GPIO               GPIO_NUM_0
#define WIFI_RESET_HOLD_TIME_MS         3000   // Hold >= 3s
#define FACTORY_RESET_HOLD_TIME_MS      10000  // Hold >= 10s
```

On button release:

- Hold for **3-9.9s** → Wi-Fi credentials reset
- Hold for **10s or more** → Factory reset

### WS2812 Status LED (GPIO21)

- **Setup mode / reconnecting / Wi-Fi reset**: Solid **Blue**
- **Provision/connection complete**: **Green**, then LED **Off**
- **Factory reset**: Blinking **Red**
- **Switch ON command**: brief **Green**
- **Switch OFF command**: brief **Red**

---

## Project Structure

```
touchPanel_rainmaker/
├── main/
│   ├── app_devices.c      ← All device logic, UART, RainMaker
│   ├── app_devices.h
│   ├── app_main.c        ← RainMaker framework entry
│   ├── app_network.c     ← Wi-Fi provisioning
│   └── CMakeLists.txt
├── CMakeLists.txt
├── sdkconfig
└── README.md
```

---

## Key Functions

| Function                           | Purpose                         |
| ---------------------------------- | ------------------------------- |
| `app_driver_init()`                | Initialize UART hardware        |
| `app_device_create_node()`         | Create RainMaker node           |
| `app_device_create()`              | Create 7 devices with callbacks |
| `app_device_bulk_write_cb()`       | Handle app write commands       |
| `parse_and_update_switch_states()` | Process touch panel responses   |
| `send_uart_command()`              | Build and send UART command     |
| `fan_speed_control()`              | Send fan speed command          |

---

## Serial Monitor Output

Expected log output during normal operation:

```
I (3200) app_devices: UART initialized (TX=17 RX=18 9600 baud)
I (3210) app_devices: All devices created and added to node
I (5000) app_devices: UART RX: 7B 51 0E 00 19 00 FE 00 FE 00 FE 00 FE 00 FE 00 FE 00 FE 7D
I (5000) app_devices: Status response: 7 nodes
I (5000) app_devices: Fan changed: power=ON speed=25
```

---

## Troubleshooting

### Bulb/Switch doesn't turn OFF from app

- Verify UART TX bytes are correct (check serial monitor)
- Confirm touch switch firmware expects `0xFF` for OFF
- Check GND connection between ESP32-S3 and touch switch

### App doesn't update after physical switch press

- Touch panel must respond to status poll (every 5 sec)
- Response must match format: `7B 51 [len] [states...] 7D`
- Check serial monitor for incoming `UART RX:` lines

### Build fails

- Ensure ESP-IDF v5.5 is properly installed
- Run `idf.py fullclean` then `idf.py build`
- Check `sdkconfig` exists and is valid

---

## Credits

- ESP RainMaker framework by Espressif
- Custom UART protocol for touch panel switch integration
