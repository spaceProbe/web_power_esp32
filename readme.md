# Universal ESP32 Wireless Control Platform

A robust, generalized IoT platform for controlling PWM-driven devices (lights, pumps, motors, fans) with built-in battery management, daily scheduling, and an interactive, offline-capable web dashboard.

## 🌟 Features

* **Locally Hosted Web Dashboard:** A sleek, responsive UI with zero external internet dependencies. Features a native HTML5 Canvas engine for zooming and panning battery history graphs.
* **Battery Smart Mode:** Automatically scales device power based on the real-time battery voltage to protect battery chemistry and maximize runtime. Includes configurable 0% and 100% voltage thresholds for custom battery types (AGM, LiFePO4, etc.).
* **Daily Routine Scheduler:** Set daily time-based actions. Features NTP atomic clock syncing, automatic Daylight Saving Time (DST) adjustments, and "catch-up" state machine logic.
* **Dynamic Power Tuning:** Easily configure the exact PWM values (0-255) for MIN, LOW, MED, and HIGH states directly from the web UI. Includes a customizable animation/breathing mode.
* **Over-The-Air (OTA) Updates:** Flash new firmware wirelessly. The OTA hostname automatically matches your custom device name.
* **Captive Portal Setup:** If no known Wi-Fi network is found, the device broadcasts a "Wireless Control Setup" Access Point for easy onboarding.
* **Persistent Memory:** All configurations, Wi-Fi credentials, schedules, and historical battery logs are saved securely to the ESP32's non-volatile storage (NVS) to survive reboots and power loss.

---

## 🛠️ Hardware Requirements

* **Microcontroller:** ESP32 Development Board
* **ADC:** Adafruit ADS1115 16-Bit I2C ADC
* **Voltage Divider:** Designed for a 1/5th reduction (e.g., four 1MΩ resistors in series to the battery positive, one 1MΩ resistor to ground).
* **Power Output:** Logic-level MOSFET or Motor Driver attached to the PWM pin.
* **Manual Override:** Physical momentary push button.

### Pinout / Wiring
* **I2C SDA / SCL:** Default ESP32 I2C pins -> ADS1115 SDA/SCL
* **ADS1115 A0:** Connected to the center of the 1/5th Voltage Divider measuring the battery.
* **PWM Output Pin:** `GPIO 4` (Configurable in code as `pwmPin`)
* **Manual Button Pin:** `GPIO 9` (Configurable in code as `bootButtonPin`, wired to switch to GND).

---

## 💻 Software Dependencies

To compile this project, you will need the **Arduino IDE** with the **ESP32 Board Manager** installed, along with the following libraries:

* `Adafruit_ADS1X15` (Search for "Adafruit ADS1X15" in the Library Manager)
* Core ESP32 Libraries (Included with board package):
  * `WiFi.h`
  * `Preferences.h`
  * `ESPmDNS.h`
  * `WiFiUdp.h`
  * `ArduinoOTA.h`
  * `Wire.h`

---

## 🚀 Setup & Flashing Instructions

### 1. First-Time Flash (USB)
1. Open the `.ino` file in the Arduino IDE.
2. Select your specific ESP32 board and COM port.
3. Compile and Upload the code via USB.

### 2. Initial Configuration (Access Point Mode)
1. Once powered on, if the ESP32 cannot find a saved Wi-Fi network, it will broadcast its own network.
2. Connect your phone or computer to the Wi-Fi network named **`Wireless Control Setup`** (Password: `password123`).
3. Open a web browser and navigate to **`http://192.168.4.1`**.
4. Click on **⚙️ Config**.

### 3. System Tuning
On the Configuration page, you must set up the following parameters:
* **Device Name:** What you want the device to be called (This will also become the OTA hostname, e.g., `patio-lights`).
* **Web Page Title:** The title displayed on the browser tab.
* **Battery Management:** Enable or disable ADC battery tracking. If disabled, the UI will hide all battery graphs and failsafes.
* **Battery Chemistry Curve:** Define the exact voltages that represent 0% (Discharged) and 100% (Charged) for your specific battery pack.
* **Power Tuning:** Assign raw PWM values (0-255) to the preset buttons.
* **Network Setup:** Enter your home Wi-Fi SSID and Password.
* Click **Save Configuration**. The device will save these to NVS and reboot.

### 4. Normal Operation
Once connected to your home Wi-Fi, you can access the dashboard by navigating to the device's IP address on your local network, or by using its mDNS hostname (e.g., `http://device-name.local`).

All future code updates can be pushed wirelessly using the OTA port in the Arduino IDE.

---

## 📅 Managing the Schedule

The Daily Routine page allows you to set actions for specific times. 
* If **Battery Management** is enabled, the scheduler features a 15% failsafe. If the battery is critically low, scheduled power-on events will be ignored to protect the battery.
* **Catch-Up Logic:** If the device loses power or is manually overridden, it will constantly evaluate the schedule and snap back to the intended state as soon as it is safe to do so.
