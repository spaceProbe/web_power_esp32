# Universal ESP32 Wireless Control Platform

A robust, generalized IoT platform for controlling PWM-driven devices (lights, pumps, motors, fans), solenoid latches, and simple on/off loads — with built-in battery management, daily scheduling, an interactive offline-capable web dashboard, and zero-configuration Home Assistant MQTT integration.

## 🌟 Features

* **Selectable Device Modes:** On first power-up the device asks what it controls — a **Dimmer** (PWM), a **Solenoid / Latch**, or a simple **On/Off Switch**. The dashboard, scheduler, and Home Assistant entity all adapt to the chosen mode, which can be changed any time from the Config page.
* **Solenoid / Latch Control:** Drives door strikes, latches, and locks with either a configurable **momentary pulse** (auto-release — the safe default for latches) or a **sustained hold** (with optional auto-relock timeout). Triggerable from the web button, the physical button, the daily schedule, and Home Assistant (as an MQTT lock entity).
* **Locally Hosted Web Dashboard:** A sleek, responsive UI with zero external internet dependencies. Features a native HTML5 Canvas engine for zooming and panning battery history graphs, and background AJAX polling so the page updates in real-time without refreshing.
* **Home Assistant Auto-Discovery (MQTT):** Seamlessly integrates into your smart home. Just enter your broker credentials, and the ESP32 will automatically build its own Light, Switch, and Sensor entities inside Home Assistant.
* **Battery Smart Mode:** Automatically scales device power based on the real-time battery voltage to protect battery chemistry and maximize runtime. Includes configurable 0% and 100% voltage thresholds for custom battery types (AGM, LiFePO4, etc.).
* **Daily Routine Scheduler:** Set daily time-based actions. Features NTP atomic clock syncing, automatic Daylight Saving Time (DST) adjustments, and "catch-up" state machine logic.
* **Configurable Timezone:** Pick your zone from a dropdown, or let the device **auto-detect it from the network** (geo-IP) — re-checked periodically so it follows DST. Replaces the old hard-coded timezone.
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
* **Solenoid / Latch (optional):** Any solenoid, electric strike, or relay driven through a logic-level MOSFET on the output pin, **with a flyback diode across the coil** (see [Solenoid Wiring & Flyback Diode](#solenoid-wiring--flyback-diode)). For active-low relay/driver boards, enable **Active-low output** in the Solenoid config.

### Pinout / Wiring
* **I2C SDA / SCL:** Default ESP32 I2C pins -> ADS1115 SDA/SCL
* **ADS1115 A0:** Connected to the center of the 1/5th Voltage Divider measuring the battery.
* **Output Pin:** `GPIO 4` (Configurable in code as `pwmPin`) — drives the dimmer, solenoid, or switch load depending on the selected Device Mode.
* **Manual Button Pin:** `GPIO 9` (Configurable in code as `bootButtonPin`, wired to switch to GND). On the ESP32-S3/C3 this is the onboard BOOT button.

### Solenoid Wiring & Flyback Diode

A solenoid/relay coil is an inductive load: when the MOSFET switches off, the collapsing magnetic field produces a large reverse-voltage spike that **will destroy the MOSFET** unless a **flyback (freewheeling) diode** clamps it.

* **Placement & orientation:** Wire the diode directly across the coil terminals, **reverse-biased** — **cathode (banded end) to the +V supply** side of the coil, **anode to the MOSFET-drain / low side**. It stays off in normal operation and only conducts the kickback when the coil de-energizes. Mount it physically close to the coil.
* **On/off latches (default — Energize level = 255):** a standard rectifier is fine:
  * **1N4007** (1000 V, 1 A) — small 12 V coils drawing up to ~0.75 A.
  * **1N5408** (1000 V, 3 A) — larger / higher-current solenoids.
* **PWM hold (Energize level < 255):** the 1N400x series is too slow for PWM switching and will run hot — use a **Schottky** (**1N5822**, 3 A, or **SS34**) or **fast-recovery** (**UF4007**) diode instead.
* **Sizing rule of thumb:** forward-current rating ≥ the coil's steady current; reverse-voltage rating ≥ ~2× the supply voltage (a 1N4007's 1000 V is comfortable for a 12 V system).
* **Pre-protected boards:** most relay-module and motor-driver breakout boards already include the flyback diode on-board — if you're using one, you don't need to add another.

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
4. On the first-run welcome screen, choose whether to **enable Solar / Battery Management**, then **pick your Device Mode** — **Dimmer**, **Solenoid / Latch**, or **On/Off Switch**. (Both are changeable later from Config.)
5. Click on **⚙️ Config** to set up Wi-Fi and tune the rest (including the battery chemistry curve).

### 3. System Tuning (The Settings Page)
On the Configuration page, you can tailor the platform entirely to your specific hardware:

**System Identity**
* **Device Mode:** Switch between **Dimmer (PWM)**, **Solenoid / Latch**, and **On/Off Switch** at any time.
* **Device Name:** What you want the device to be called. This automatically becomes your OTA hostname and the root topic for MQTT (e.g., "Patio Lights" becomes `patio-lights`).
* **OTA Password:** Secure your over-the-air update port.
* **Web Page Title:** The text displayed on your browser tab.

**Solenoid / Latch (used in Solenoid mode)**
* **Actuation:** **Momentary Pulse** (energize, then auto-release) or **Sustained Hold** (energize until locked).
* **Pulse duration (ms):** How long the coil is energized for a pulse.
* **Hold auto-relock (ms):** Optional safety timeout for Hold mode (`0` = stay unlocked until told to lock).
* **Energize level (0-255):** Drive strength applied while energized (255 = full on).
* **Active-low output:** Invert the pin for relay/driver boards that energize on a LOW signal.

**Time & Date**
* **Auto-detect timezone from network:** When enabled, the device looks up its timezone via geo-IP (ip-api.com) on connect and re-checks every few hours, so DST is handled automatically. *(Requires outbound internet access.)*
* **Manual timezone:** Pick your region from the dropdown instead. Known zones carry full DST rules; an auto-detected zone outside the built-in list falls back to a fixed UTC offset (correct now, but won't auto-shift for DST until the next re-check).

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

Once you enter your MQTT credentials in the web dashboard and save, the ESP32 will immediately connect to your broker and broadcast its configuration payloads. The **primary entity matches the selected Device Mode**:
1. **Dimmer mode → a Light Entity:** For turning the device ON/OFF and controlling the brightness slider. Also exposes a **Switch Entity** for toggling Battery Smart Mode.
2. **Solenoid mode → a Lock Entity:** `UNLOCK` triggers the latch (pulse or hold) and `LOCK` releases it.
3. **Switch mode → a Switch Entity:** Simple ON/OFF.
4. **Sensor Entities:** Real-time Battery Percentage and Voltage readouts (in any mode, when Battery Management is enabled).

Switching modes automatically removes the old entity from Home Assistant and publishes the new one. Whenever a change is made locally (via the physical button, the web dashboard, or the daily schedule), the device will instantly push the new state to Home Assistant in real-time.

---

## 📅 Managing the Schedule

The Daily Routine page allows you to set actions for specific times. The available actions adapt to the Device Mode — power levels (OFF/MIN/LOW/MED/HIGH/Smart) in Dimmer mode, **Switch ON/OFF** in Switch mode, and **Unlock/Trigger** & **Lock** in Solenoid mode.
* **Catch-Up Logic:** If the device loses power or is manually overridden, it will constantly evaluate the schedule and snap back to the intended state as soon as it is safe to do so. (Solenoid **pulse** actions are edge-triggered — they fire once at the scheduled time rather than being continuously re-applied.)
* **Battery Failsafe:** If Battery Management is enabled, the scheduler enforces a 15% failsafe. If the battery drops below this threshold, scheduled power-on events will be paused to protect the battery until it charges.