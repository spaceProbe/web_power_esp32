# Universal ESP32 Wireless Control Platform

A robust, generalized IoT platform for controlling PWM-driven devices (lights, pumps, motors, fans) with built-in battery management, daily scheduling, an interactive offline-capable web dashboard, and zero-configuration Home Assistant MQTT integration.

## 🌟 Features

* **Locally Hosted Web Dashboard:** A sleek, responsive UI with zero external internet dependencies. Features a native HTML5 Canvas engine for zooming and panning battery history graphs, and background AJAX polling so the page updates in real-time without refreshing.
* **Home Assistant Auto-Discovery (MQTT):** Seamlessly integrates into your smart home. Just enter your broker credentials, and the ESP32 will automatically build its own Light, Switch, and Sensor entities inside Home Assistant.
* **Battery Smart Mode:** Automatically scales device power based on the real-time battery voltage to protect battery chemistry and maximize runtime. Includes configurable 0% and 100% voltage thresholds for custom battery types (AGM, LiFePO4, etc.).
* **Daily Routine Scheduler:** Set daily time-based actions. Features NTP atomic clock syncing, automatic Daylight Saving Time (DST) adjustments, and "catch-up" state machine logic.
* **Dynamic Power & UI Tuning:** Easily configure the exact PWM values (0-255) for MIN, LOW, MED, and HIGH states, customize the animation/breathing mode speed, and toggle between viewing raw PWM values or 0-100% on the web slider.
* **Over-The-Air (OTA) Updates:** Flash new firmware wirelessly. The OTA hostname automatically matches your custom device name and uses your custom password.
* **Captive Portal Setup:** If no known Wi-Fi network is found, the device broadcasts a "Wireless Control Setup" Access Point for easy onboarding.

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
* `PubSubClient` (Search for "PubSubClient" by Nick O'Leary in the Library Manager)
* Core ESP32 Libraries (Included with board package): `WiFi.h`, `Preferences.h`, `ESPmDNS.h`, `WiFiUdp.h`, `ArduinoOTA.h`, `Wire.h`

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

### 3. System Tuning (The Settings Page)
On the Configuration page, you can tailor the platform entirely to your specific hardware:

**System Identity**
* **Device Name:** What you want the device to be called. This automatically becomes your OTA hostname and the root topic for MQTT (e.g., "Patio Lights" becomes `patio-lights`).
* **OTA Password:** Secure your over-the-air update port.
* **Web Page Title:** The text displayed on your browser tab.

**UI & Hardware Config**
* **Enable Battery Management:** Toggle the ADC polling and battery failsafes on or off.
* **Display Power as Percentage:** Changes the main UI slider to display a human-readable 0-100% scale instead of raw 0-255 hardware values.
* **Battery Chemistry Curve:** Define the exact voltages that represent 0% (Discharged) and 100% (Charged) for your specific battery pack.
* **Power Tuning:** Assign raw PWM values (0-255) to the preset MIN, LOW, MED, and HIGH buttons.
* **Animation Tuning:** Set the Peak Power (0-255) and the transition Speed (ms per step) for the "Play Animation" breathing effect.

**MQTT Broker (Home Assistant)**
* **Enable Home Assistant MQTT:** Turn on the MQTT engine.
* Enter your Broker IP, Port (default 1883), Username, and Password. 
* A live status indicator (🟢/🔴) will instantly show if the device successfully connected to your broker.

**Network Setup**
* Enter your home Wi-Fi SSID and Password.
* Click **Save Configurations**. The device will save these to non-volatile storage and instantly apply them.

---

## 🏠 Home Assistant Integration

This platform features **Zero-Configuration MQTT Auto-Discovery**. 

Once you enter your MQTT credentials in the web dashboard and save, the ESP32 will immediately connect to your broker and broadcast its configuration payloads. It will automatically appear in your Home Assistant Devices list featuring:
1. **A Light Entity:** For turning the device ON/OFF and controlling the brightness slider.
2. **A Switch Entity:** For toggling the Battery Smart Mode.
3. **Sensor Entities:** Real-time Battery Percentage and Voltage readouts.

Whenever a change is made locally (via the physical button, the web dashboard, or the daily schedule), the device will instantly push the new state to Home Assistant in real-time.

---

## 📅 Managing the Schedule

The Daily Routine page allows you to set actions for specific times. 
* **Catch-Up Logic:** If the device loses power or is manually overridden, it will constantly evaluate the schedule and snap back to the intended state as soon as it is safe to do so.
* **Battery Failsafe:** If Battery Management is enabled, the scheduler enforces a 15% failsafe. If the battery drops below this threshold, scheduled power-on events will be paused to protect the battery until it charges.