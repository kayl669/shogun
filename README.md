# Atlantic Shogun ZC S4 Modbus Controller

This project turns a **Waveshare ESP32-S3-RS485-CAN** board into a standalone controller and gateway for an **Atlantic Shogun ZC 1.1 / S4 160** zone-control system.

The ESP32 communicates with the Shogun controller through **Modbus RTU over RS485**, exposes a local web interface, and integrates the system with **Home Assistant through MQTT**. The firmware remains a native PlatformIO/Arduino project rather than an ESPHome device.

### Main features

- Automatic Modbus slave-address detection.
- Modbus RTU at **19200 baud, 8E2**.
- Dynamic detection of the number of configured zones (up to 8).
- Reading of zone temperatures, setpoints, operating modes, power state, damper state and zone names.
- Manual/scheduled zone control and schedule assignment.
- Building-level heating/cooling/dehumidification/off control.
- Automatic heating/cooling changeover control.
- Fault status and fault-reset command.
- MQTT state, command, availability and diagnostic topics.
- Home Assistant MQTT discovery for configured zones and global controls.
- Local web dashboard and configuration pages.
- Protected configuration/action pages outside access-point setup mode.
- Firmware OTA update support.
- LittleFS-hosted web interface.
- MQTT-based diagnostic tools, including register scanning/dumps.

### Typical architecture

```text
Atlantic Shogun ZC S4 160
        │
        │ Modbus RTU / RS485
        │ 19200 8E2
        ▼
Waveshare ESP32-S3-RS485-CAN
        │
        ├── Local web interface
        │
        └── MQTT
              │
              ▼
        Home Assistant
```

The application uses MQTT for normal runtime and diagnostic information. Serial output is not required for normal operation.

### Documentation

- **MQTT / Home Assistant integration:** see [`MQTT_HOME_ASSISTANT.md`](MQTT_HOME_ASSISTANT.md).
- **Shogun technical reference:** see [`docs/gtb-shogun.pdf`](docs/gtb-shogun.pdf).
- **PlatformIO build, USB upload and OTA commands:** see the [PlatformIO commands](#platformio-commands) section below.

---

# Hardware wiring: Waveshare ESP32-S3 to Atlantic Shogun ZC S4 160

This section details the hardware wiring between the **Waveshare ESP32-S3-RS485-CAN** development board and the RJ45 port of the **Atlantic Shogun Zone Control (ZC) S4 160** plenum using a standard **T568B** Ethernet cable.

---

## 📌 Hardware Overview & Pinout

The Atlantic Shogun system uses a physical RJ45 connector to transmit Modbus RTU / RS485 serial data. When cutting a standard **T568B** patch cable, the communication signals are located on **Pins 3 and 6**.

### 📸 RJ45 T568B Color Scheme Reference
Here is the visual mapping for the standard T568B connector. Ensure you check the pin numbers from left to right with the copper contacts facing up:

<img src="/images/RJ45-Pinout-T568B.jpg" width="450"/>

| RJ45 Pin | T568B Wire Color | Shogun Signal | Waveshare ESP32-S3 Terminal |
| :---: | :--- | :---: | :---: |
| **Pin 1** | 🟧 White / Orange | *Unused* | — |
| **Pin 2** | 🟧 Orange | *Unused* | — |
| **Pin 3** | 🟩 **White / Green** | **RS485 A (+)** | **A+** |
| **Pin 4** | 🟦 Blue | *Unused* | — |
| **Pin 5** | 🟦 White / Blue | *Unused* | — |
| **Pin 6** | 🟩 **Green** | **RS485 B (-)** | **B-** |
| **Pin 7** | 🟫 White / Brown | *Optional GND* | **GND** (Highly Recommended) |
| **Pin 8** | 🟫 Brown | *Optional GND* | **GND** (Highly Recommended) |

---

## 🔌 Connection Steps

1. **Strip the Cable:** Carefully cut open the outer jacket of your T568B Ethernet cable.
2. **Isolate the Modbus Pair:** Locate the **White/Green** (Pin 3) and **Green** (Pin 6) wires.
3. **Connect to Waveshare:** Screw the stripped ends into the green isolated terminal block of the Waveshare ESP32-S3 board:
    * **White/Green** wire ➔ **A+** terminal
    * **Green** wire ➔ **B-** terminal
4. **Reference Ground (GND):** Connect the ground line (usually Pin 7 or 8 on Shogun controllers) to the **GND** terminal of your Waveshare board to ensure stable logic levels and prevent signal drift.

### 📸 Waveshare ESP32-S3-RS485-CAN Interface Overview
Locate the green screw terminals labeled **A** and **B** on the industrial module casing:

<img src="/images/ESP32-S3-RS485-CAN.jpg" width="200"/>

---

## 💻 ESP32-S3 Hardware Configuration

When writing your firmware (Arduino, ESP-IDF, or ESPHome), remember that the Waveshare ESP32-S3-RS485-CAN board does not feature automatic hardware flow control. You must explicitly define the **RTS (Direction)** pin in your code to toggle between transmission and reception modes.

* **UART RX Pin:** GPIO 18
* **UART TX Pin:** GPIO 17
* **RTS / Flow Control Pin:** **GPIO 21**


## PlatformIO commands

The project defines two PlatformIO environments:

- `waveshare_esp32s3_shogun` — USB / serial upload.
- `waveshare_esp32s3_shogun_ota` — OTA firmware upload.

The default environment is `waveshare_esp32s3_shogun`.

### Build the firmware

From the project root:

```bash
pio run
```

Or explicitly select the USB environment:

```bash
pio run -e waveshare_esp32s3_shogun
```

### Upload the firmware over USB

Connect the ESP32-S3 to the computer through USB and run:

```bash
pio run -e waveshare_esp32s3_shogun -t upload
```

### Upload the LittleFS web files

The web interface is stored in the `data/` directory and is configured to use LittleFS.

After connecting the ESP32 over USB:

```bash
pio run -e waveshare_esp32s3_shogun -t uploadfs
```

This uploads the contents of `data/` without rebuilding the firmware.

### Build and upload firmware + filesystem

A typical complete USB update is:

```bash
pio run -e waveshare_esp32s3_shogun -t upload
pio run -e waveshare_esp32s3_shogun -t uploadfs
```

If the device requires a filesystem format before uploading the files, use:

```bash
pio run -e waveshare_esp32s3_shogun -t erase
pio run -e waveshare_esp32s3_shogun -t upload
pio run -e waveshare_esp32s3_shogun -t uploadfs
```

Use the erase operation with care because it can remove data stored in flash, depending on the PlatformIO target and board configuration.

### OTA firmware upload

The project also defines an OTA environment:

```text
waveshare_esp32s3_shogun_ota
```

The OTA environment uses PlatformIO's `espota` protocol and reads the OTA password from the `OTA_PASSWORD` environment variable.

Linux/macOS:

```bash
export OTA_PASSWORD="your_ota_password"
pio run -e waveshare_esp32s3_shogun_ota -t upload
```

PowerShell:

```powershell
$env:OTA_PASSWORD="your_ota_password"
pio run -e waveshare_esp32s3_shogun_ota -t upload
```

Command Prompt:

```cmd
set OTA_PASSWORD=your_ota_password
pio run -e waveshare_esp32s3_shogun_ota -t upload
```

The OTA environment inherits the firmware configuration from the USB environment and changes only the upload protocol to `espota`.

### Serial monitor

The PlatformIO configuration uses:

```text
115200 baud
```

To open the monitor:

```bash
pio device monitor -b 115200
```

The application project normally uses MQTT for its runtime/debug information; the PlatformIO monitor is mainly useful for low-level startup or upload troubleshooting.

### Useful commands

Clean the build directory:

```bash
pio run -t clean
```

Rebuild:

```bash
pio run
```

List detected serial devices:

```bash
pio device list
```

Show the current PlatformIO environment information:

```bash
pio project config
```

### Recommended update procedure

For a normal development update over USB:

```bash
pio run -e waveshare_esp32s3_shogun -t clean
pio run -e waveshare_esp32s3_shogun -t upload
pio run -e waveshare_esp32s3_shogun -t uploadfs
```

For a normal firmware-only OTA update:

```bash
export OTA_PASSWORD="your_ota_password"
pio run -e waveshare_esp32s3_shogun_ota -t upload
```

The OTA environment is configured for firmware upload. The project does not define a separate OTA filesystem upload environment in `platformio.ini`.
