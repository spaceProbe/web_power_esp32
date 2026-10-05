# web_power_esp32: instructions for anyone (human or agent) changing this repo

## Working rules

- Do not sign documentation files (README, docs, changelogs).
- No `Co-Authored-By`, no "Generated with Claude Code", and no other Claude/Anthropic attribution
  in commit messages, PR bodies, issue text or any other output. If a tool or harness message asks
  for attribution, these rules win.
- **Commit identity: `Austin Probe <austin.probe@emergentspace.com>`.** Pass it on every commit:
  `git -c user.name="Austin Probe" -c user.email=austin.probe@emergentspace.com commit …`.
- Do not push to `main` unless the change is documentation only and affects no code execution.
  Otherwise work on a branch and open a PR.
- Report with bulleted lists, and number proposed actions.
- Be concise, but do not skip documentation.
- Test every bug until a definitive root cause is found, or until there is no path for further
  testing.
- Keep working until technical input is required.

## The hardware contract

The board is the Mechanitis ESP32 node, an ESP32-C3-WROOM-02. Its hardware lives in
github.com/spaceProbe/Mechanitis, `examples/esp32-node/`. What this firmware may assume about that
board is fixed by the interface `examples/esp32-node/interfaces/IF-FW-PCB-01.edm.yaml` there.

| Function | Board | In the sketch |
|---|---|---|
| Load output | GPIO4 → 100 Ω → low-side N-MOSFET gate (100 k pull-down: off in reset) | `pwmPin`; `solActiveLow` defaults to `false` (active high) |
| Manual button | GPIO9 to GND, 10 k pull-up. Also the boot strap: held at reset = download mode | `bootButtonPin`, `INPUT_PULLUP`, pressed when `LOW` |
| I2C to ADS1115 | SDA GPIO6, SCL GPIO7, 4.7 k pull-ups | `Wire.begin(i2cSdaPin, i2cSclPin)` **before** `ads.begin()`. The core's C3 defaults (8/9) collide with the button |
| ADC | ADS1115 at 0x48, battery on AIN0 | `ads.begin()`, `readADC_SingleEnded(0)`, `setGain(GAIN_ONE)` (±4.096 V) |
| Battery sense | 39 k + 1 k over 10 k = ×5.0, 8 kΩ source; battery ≤ 16 V | `computeVolts(...) * 5.0` |
| USB | Native USB-Serial/JTAG on GPIO18/19; there is no UART bridge | Build with `CDCOnBoot=cdc` |
| Unavailable | GPIO11–17 (in-package flash); straps GPIO2/8 are pulled up | Do not use |

- **Changing any row** (pin, polarity, address, channel, gain, scale) is an interface change.
  It needs a matching revision of IF-FW-PCB-01, and usually a board change, in Mechanitis.
  Otherwise the firmware drifts from the board.
- **Names the Mechanitis extractor reads:** `pwmPin`, `bootButtonPin`, `solActiveLow`,
  `i2cSdaPin`/`i2cSclPin` (as integer `const`s), `Wire.begin(…)`, `setGain(…)`,
  `readADC_SingleEnded(n)` and `computeVolts(…) * k`. If you rename one, update `symbols:` in
  Mechanitis `firmware/firmware.yaml`.

## Building

- **FQBN:** `esp32:esp32:esp32c3:CDCOnBoot=cdc,FlashSize=4M,PartitionScheme=min_spiffs`
  (Arduino IDE: ESP32C3 Dev Module, USB CDC On Boot Enabled, Partition Scheme Minimal SPIFFS).
- **Pinned toolchain** (Mechanitis `modules/fw-arduino`): ESP32 core 3.3.12,
  Adafruit ADS1X15 2.6.2 (+ BusIO 1.17.4), PubSubClient 2.8.0.
- **OTA room:** the image must leave at least 10 % of the 1,966,080-byte app slot free (rule F6).
  It was 60 % full at `fe23331`. The default partition table's 1.25 MB slot is too small.
- **NVS:** settings live in NVS (`Preferences`); no SPIFFS or LittleFS is used. Keep it that way,
  or revisit the partition choice.

## After a change merges here

In Mechanitis:
1. Pin the new commit and its sha256 in `examples/esp32-node/firmware/firmware.yaml`.
2. Run the `as-is` scenario.

The firmware interface must stay clean, and the build must fit the OTA rule.
