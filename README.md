# Doomscroller Universal Firmware (v2.2-Universal)

[![Platform: Seeed XIAO nRF52840 Sense](https://img.shields.io/badge/Platform-Seeed%20XIAO%20nRF52840%20Sense-blue.svg)](https://wiki.seeedstudio.com/XIAO-BLE/)
[![Sensor: MPS MagAlpha MA780](https://img.shields.io/badge/Sensor-MagAlpha%20MA780%2016--bit-orange.svg)](https://www.monolithicpower.com/en/ma780.html)
[![Storage: InternalFS LittleFS](https://img.shields.io/badge/Storage-InternalFS%20Flash%20Persistence-purple.svg)](https://learn.adafruit.com/bluefruit-nrf52-feather-learning-guide/internal-file-system)
[![Configurator: Web Serial & Web Bluetooth](https://img.shields.io/badge/Configurator-Web%20Serial%20%26%20BLE-teal.svg)](https://mrclintonbarnes.github.io/Doomscroller/)
[![Protocol: Driverless BLE HID](https://img.shields.io/badge/BLE-HID%20Mouse%20%2B%20Battery%20Service-green.svg)](https://www.bluetooth.com/)
[![License: Apache 2.0](https://img.shields.io/badge/License-Apache%202.0-lightgrey.svg)](LICENSE)

A high-performance, driverless, universal firmware for Andrew McCalip's **Doomscroller** hardware scroll controller.

Engineered for native compatibility with **Android**, **macOS**, **iOS / iPadOS**, **Windows**, and **Linux** without companion apps, background daemons, or custom drivers.

---

## Table of Contents

- [The Problem & Why This Firmware Exists](#the-problem--why-this-firmware-exists)
- [Key Features](#key-features)
- [Web Configurator GUI (Web Serial & Web Bluetooth)](#web-configurator-gui-web-serial--web-bluetooth)
- [Multi-Profile Architecture & Flash Persistence](#multi-profile-architecture--flash-persistence)
- [Hardware Architecture & Pinout](#hardware-architecture--pinout)
- [Quick Start: Flashing the Firmware](#quick-start-flashing-the-firmware)
- [Pairing Across Platforms](#pairing-across-platforms)
- [Interactive Serial & BLE CLI Commands](#interactive-serial--ble-cli-commands)
- [Configuration & Tuning](#configuration--tuning)
- [Building from Source](#building-from-source)
- [Troubleshooting & FAQ](#troubleshooting--faq)

---

## The Problem & Why This Firmware Exists

The original upstream Doomscroller firmware attempted to emulate a composite touch digitizer stylus and Microsoft Surface Dial radial device:
1. **Passkey / PIN Failure:** Host operating systems (especially Android and macOS) mistook composite HID declarations for a keyboard or secure input terminal, demanding 6-digit PINs or passkeys that a dial cannot enter.
2. **Accidental Clicks on Social Feeds:** Emulating a touch digitizer finger dragging at fixed screen coordinates caused Android to misinterpret wheel slowdowns as tap/click gestures, accidentally opening posts and ads instead of scrolling.
3. **No macOS or iOS Support:** Desktop and Apple platforms ignore touch digitizer packets without proprietary drivers.
4. **Stale Bond Lockouts & Device Latching:** Once paired, changing operating systems or switching between devices resulted in persistent connection errors and hosts aggressively reconnecting before a new device could pair.
5. **No Native Battery Percentage:** Battery status was not exposed through standard Bluetooth GATT services.
6. **Overnight Idle Battery Drain:** The angle sensor remained continuously powered during Bluetooth connections even when stationary, draining ~60% of the battery overnight.

### How v2.2-Universal Solves This

* **Pure BLE HID Mouse Wheel:** Uses standard relative mouse reports (`MotionEvent.ACTION_SCROLL` / generic desktop wheel). Universal support across all platforms out of the box with zero chance of triggering accidental screen taps.
* **Just-Works Pairing:** Explicitly disables Keyboard and Display I/O capabilities (`setIOCaps(false, false, false)`), guaranteeing instant, PIN-free pairing on Android, macOS, and iOS.
* **Per-Profile Cursor Auto-Park Toggle:** Smoothly glides the mouse cursor to the far right margin ~1.5s post-connect so the center screen stays completely clean on mobile video feeds (TikTok, Instagram Reels, Shorts). Can be toggled **OFF** per profile for PC and Mac desktop use.
* **Dual Web Configurator (Web Serial & Web Bluetooth):** Zero-install browser interface (`https://mrclintonbarnes.github.io/Doomscroller/`) supporting both wired USB-C Serial and wireless Web Bluetooth over Nordic UART Service (NUS).
* **Real-Time Disconnect Detection:** The web configurator monitors both USB unplug events and BLE GATT drops, immediately resetting connection status and disabling controls.
* **Anti-Latching Multi-Host Bluetooth Identity:** Each of the 3 profiles uses a distinct static Bluetooth MAC address. Switching profiles or entering fresh pairing mode (`pair`) severs the old host connection so new devices can pair cleanly without needing to "Forget Device" on the old phone.
* **Connected Idle Sleep (~150x Power Reduction):** After 15 seconds of inactivity, the firmware cuts power to the MagAlpha MA780 sensor and throttles the MCU to 100ms low-power polling ticks, eliminating overnight battery drain.
* **InternalFS Flash Storage:** All profile parameters, names, and toggles persist permanently across power cycles and reboots via on-chip LittleFS (`/settings.dat`).
* **Native Battery Service (0x180F):** Safely samples the Seeed XIAO internal voltage divider and reports live battery level (0–100%) directly to Android and macOS Bluetooth menus.
* **Hardware Magnetic Reset ("C"):** Swiping a magnet over the letter "C" on the back PCB clears all stored bonding keys, increments the Bluetooth MAC address, and gives 5 blue LED flashes.

---

## Key Features

| Feature | Description |
| :--- | :--- |
| **Driverless Operation** | Standard Bluetooth Low Energy HID Mouse descriptor recognized natively by every major operating system. |
| **Dual Web Configurator** | Web Serial (USB) and Web Bluetooth (BLE) support side-by-side in Google Chrome, Microsoft Edge, and Opera. |
| **Connected Idle Sleep** | Shuts down MagAlpha sensor and throttles CPU when motionless (>15s), preventing overnight battery drain. |
| **Per-Profile Cursor Auto-Park** | Toggle between right-margin mobile video parking (hides pointer) and standard cursor behavior for PC/Mac. |
| **Host Latching Prevention** | Per-profile MAC address rotation and fresh 60s pairing windows prevent old phones from intercepting connections. |
| **3 Profile Slots** | Instant switching between customized presets (Phone, Precision, FastFlick) saved to InternalFS flash. |
| **InternalFS Flash Persistence** | All settings stored in LittleFS on internal flash (`/settings.dat`) surviving battery drain and reboots. |
| **Dynamic Flick Acceleration** | Smooth 1:1 precision (30°/click) during slow turns; scales up to 8x velocity multiplier on rapid flicks. |
| **Sub-Detent Accumulation** | Carries fractional angle changes across loop cycles so slow, deliberate rotations are never lost. |
| **Native Battery Service** | Standard BLE BAS characteristic (`0x180F`) broadcast to system menus without ADC initialization lockups. |
| **Hardware Magnetic Reset & Bootloader** | Single swipe over "C" clears bonds and refreshes BLE identity; double-tap enters UF2 bootloader drive. |
| **Unified Serial & BLE CLI** | Interactive text console and machine-readable JSON output over both USB Serial and Web Bluetooth. |

---

## Web Configurator GUI (Web Serial & Web Bluetooth)

No command-line or terminal app required! You can configure your Doomscroller directly through your browser over HTTPS:

👉 **[Launch Doomscroller Web Configurator](https://mrclintonbarnes.github.io/Doomscroller/)**

*(Or open [`docs/index.html`](docs/index.html) locally in any Chromium browser)*

### How to Use the Web Configurator:
1. Open the web configurator in any Chromium-based desktop or Android browser (**Google Chrome**, **Microsoft Edge**, **Opera**, or **Brave**).
2. Connect using either method:
   * **Connect via Bluetooth:** Click **⚡ Connect via Bluetooth** and pick your Doomscroller from the pairing dialog. (100% wireless!)
   * **Connect via USB:** Plug in via USB-C and click **🔌 Connect via USB**.
3. The interface will instantly read the controller state and populate:
   * **Profile Tabs:** Select between Profile 1, 2, or 3, or rename them.
   * **Scroll Sensitivity Slider:** Adjust degrees per wheel notch (5° to 90°).
   * **Flick Acceleration Gain:** Adjust speed scaling during fast spins (0.000 to 0.100).
   * **Deadzone Slider:** Fine-tune jitter suppression.
   * **Scroll Inversion:** Instant direction flip toggle.
   * **Auto-Park Cursor Toggle:** Enable pointer hiding on mobile, or disable for desktop.
   * **Quick Tools:** Slide cursor to edge/center, force 60s pairing mode, disconnect host, reboot to UF2 bootloader, or save to flash.

All changes made in the web UI are automatically written directly to InternalFS flash.

---

## Multi-Profile Architecture & Flash Persistence

The firmware provides **3 independent profile slots** stored in `DeviceSettings` on internal flash:

```c
struct TuningProfile {
  char  name[16];          // Friendly profile name (e.g. "Phone", "Precision", "FastFlick")
  float degreesPerStep;    // Degrees of rotation per scroll notch (e.g. 30.0°)
  float accelFactor;       // Dynamic acceleration multiplier (e.g. 0.020)
  float deadzoneDeg;       // Motion deadzone in degrees (e.g. 0.18°)
  bool  invertScroll;      // Scroll direction toggle
  bool  cursorPark;        // Auto-park cursor on connect (true = hide pointer on mobile)
};
```

### Factory Profile Presets:
- **Profile 1 [Balanced / Phone]:** 30.0°/step, 0.020 flick accel, 0.18° deadzone, `cursorPark = true`. Optimal everyday feel for TikTok, Instagram Reels, Bluesky, and mobile feeds.
- **Profile 2 [Precision / PC]:** 45.0°/step, 0.005 flick accel, 0.18° deadzone, `cursorPark = false`. Slower, deliberate line-by-line reading, code review, and desktop PC/Mac browsing.
- **Profile 3 [FastFlick / Phone]:** 20.0°/step, 0.040 flick accel, 0.15° deadzone, `cursorPark = true`. Rapid, energetic flick-scrolling for long timelines.

Switch profiles instantly anytime by entering `profile 1`, `profile 2`, or `profile 3` in the console, or clicking the tabs in the Web Configurator.

---

## Hardware Architecture & Pinout

Built on the **Seeed Studio XIAO nRF52840 Sense** paired with the **Monolithic Power Systems MagAlpha MA780** 16-bit magnetic angle sensor.

### Pinout Mapping

| Component | Signal | XIAO Pin | nRF52840 Port Pin | Function / Notes |
| :--- | :--- | :--- | :--- | :--- |
| **MagAlpha MA780** | SCK | D8 | P0.13 | SPI Clock (5 MHz, Mode 3) |
| **MagAlpha MA780** | MISO | D9 | P0.15 | SPI Master In / Slave Out |
| **MagAlpha MA780** | MOSI | D10 | P0.17 | SPI Master Out / Slave In |
| **MagAlpha MA780** | CS | D7 | P0.20 | Active-low Chip Select |
| **MagAlpha MA780** | PWR | D6 | P0.04 | Sensor VCC Power Control (HIGH = ON, LOW = Low Power Sleep) |
| **Battery Divider** | VBAT_SENSE | D32 | P0.31 / AIN7 | Analog input for voltage divider (1.51MΩ / 510kΩ) |
| **Battery Divider** | VBAT_ENABLE | D14 | P0.14 | Low-side FET switch (LOW = Enable divider, HIGH = Off) |
| **RGB LED** | Red | PIN_LED_RED | P0.26 | Active-low on-board indicator |
| **RGB LED** | Green | PIN_LED_GREEN | P0.30 | Active-low on-board indicator |
| **RGB LED** | Blue | PIN_LED_BLUE | P0.06 | Active-low on-board indicator (BLE status & flash) |
| **Reset Switch** | RESET | RST Pad | P0.18 / RESET | Connected to Hall switch behind letter "C" on back PCB |

---

## Quick Start: Flashing the Firmware

Pre-compiled binary: `doomscroller/build/Seeeduino.nrf52.xiaonRF52840Sense/doomscroller.uf2`

### Method A: Drag-and-Drop (No Tools Required)

1. Connect the Doomscroller to your computer via USB-C.
2. **Double-tap a magnet** over the letter **"C"** on the back PCB (mimics double-clicking a reset button). Alternatively, if connected over serial or web configurator, type or click `dfu`.
3. The on-board LED will begin a slow, breathing pulse, and a USB mass storage drive named **`XIAO-SENSE`** or **`XIAO-BOOT`** will appear on your desktop.
4. Drag and drop `doomscroller.uf2` onto the drive.
5. The device will write the firmware, disconnect, and reboot automatically into v2.2-Universal. *(Any OS warning that "The disk was not ejected properly" is normal and confirms successful reboot).*

### Method B: Automated Python Flash Monitor

From the project root directory, run:
```bash
python3 doomscroller/flash_monitor.py
```
Then double-tap the magnet over the letter "C" or click `dfu` in the web configurator. The script will automatically detect the drive the moment it mounts, copy the UF2 file, and verify completion.

---

## Pairing Across Platforms

### Android
1. Open **Settings > Connected Devices > Pair new device**.
2. Select **Doomscroller** from the list.
3. Accept the pairing confirmation prompt.
4. The mouse cursor will appear briefly at center and then automatically slide to the right margin within 1.5 seconds.
5. Test vertical scrolling in **TikTok**, **Instagram Reels**, **Bluesky**, and **Chrome**.

### macOS
1. Open **System Settings > Bluetooth**.
2. Locate **Doomscroller** under *Nearby Devices* and click **Connect**.
3. Scrolling works instantly. *(If you use macOS "Natural Scrolling" and want the opposite direction, enter `invert` in the console or toggle in the web configurator).*

### iOS & iPadOS
1. Go to **Settings > Bluetooth** and tap **Doomscroller** to connect.
2. In **Settings > Accessibility > Touch > AssistiveTouch**, verify pointer device support is enabled if using an iPhone, or use native pointer in iPadOS.

### Windows & Linux
1. Open your system's Bluetooth settings and select **Add Device**.
2. Choose **Doomscroller**. The device pairs immediately as an HID-compliant mouse.

> [!TIP]
> **Switching Devices / Fresh Pairing:** Whenever pairing with a new device, click **Force 60s Pairing Mode** in the Web Configurator or **swipe the magnet once across the letter "C"**. The blue LED will flash 5 times, clearing stored bonds and rotating the static MAC address so the new host sees a completely fresh device without old devices intercepting.

---

## Interactive Serial & BLE CLI Commands

Connect via USB at **115200 baud** or over Web Bluetooth Nordic UART Service:

```text
--- Doomscroller CLI Commands ---
  status         - Show current status and all profile settings
  profile <1-3>  - Switch active tuning profile (e.g. 'profile 1')
  pname <str>    - Rename active profile (e.g. 'pname Social')
  speed <deg>    - Set degrees per click for active profile
  accel <gain>   - Set flick acceleration for active profile
  deadzone <deg> - Set deadzone degrees for active profile
  invert         - Toggle scroll direction for active profile
  cursorpark <on/off> - Toggle auto-sliding cursor to right edge on connect
  pair           - Force 60s fresh pairing window & rotate MAC
  disconnect     - Disconnect currently connected Bluetooth host
  name <str>     - Set Bluetooth device name
  save           - Explicitly save settings to flash
  edge           - Slide mouse cursor to right margin
  center         - Move mouse cursor back towards center
  battery        - Read battery voltage and estimated percentage
  json           - Output machine-readable JSON status
  dfu            - Reboot into UF2 bootloader drive for flashing
  stream         - Toggle live angle & velocity telemetry
  unpair         - Clear all BLE bonding keys and re-advertise
  defaults       - Reset all settings to factory defaults
  reboot         - Reboot controller
  help           - Print this menu
```

### Example: Checking Device Status (`status`)

```text
--- Device Status ---
Firmware:         v2.2-Universal
Device Name:      Doomscroller
BLE Status:       Connected
Battery:          4.12V (85%)
Raw Angle:        18473
Active Profile:   1 / 3 [Balanced]

--- Active Profile Tuning ---
  Degrees/Step:   30.00
  Accel Factor:   0.020
  Deadzone:       0.18°
  Scroll Invert:  NO (Normal)
  Cursor Auto-Park: YES (Slide to edge)

--- All Profiles ---
 *[1] Balanced: 30.0°/step, accel=0.020, deadzone=0.18°, invert=NO, park=YES
  [2] Precision: 45.0°/step, accel=0.005, deadzone=0.18°, invert=NO, park=NO
  [3] FastFlick: 20.0°/step, accel=0.040, deadzone=0.15°, invert=NO, park=YES
Uptime:           42s
```

---

## Configuration & Tuning

Key tuning parameters are defined in `doomscroller/config.h`:

```c
#define DEFAULT_DEGREES_PER_STEP   30.0f  // Degrees of rotation per scroll click
#define DEFAULT_ACCEL_FACTOR       0.02f  // Dynamic acceleration gain for fast flicks
#define DEFAULT_DEADZONE_DEG       0.18f  // Deadzone filter in degrees to prevent resting vibration drift
#define DEFAULT_INVERT_SCROLL      false  // Scroll direction toggle
#define DEFAULT_IDLE_TIMEOUT_SEC   15     // Seconds before entering ultra-low power sensor sleep
```

- **Scroll Speed:** Lower values = faster scrolling (more scroll packets per revolution). Higher values = slower, finer control.
- **Flick Acceleration:** Multiplies scroll output based on instantaneous angular velocity:
  $$\text{Multiplier} = 1.0 + (\text{Velocity} \times \text{AccelFactor} \times 0.01)$$
  Clamped to a maximum multiplier of $8.0\times$ to avoid runaway scrolling.

---

## Building from Source

### Prerequisites

1. Install [Arduino CLI](https://arduino.github.io/arduino-cli/latest/) or [Arduino IDE 2.x](https://www.arduino.cc/en/software).
2. Add the Seeed Studio board manager URL in preferences:
   ```text
   https://files.seeedstudio.com/arduino/package_seeeduino_boards_index.json
   ```
3. Install the **Seeed nRF52** core:
   ```bash
   arduino-cli core update-index
   arduino-cli core install Seeeduino:nrf52
   ```

### Compile & Generate UF2

```bash
# 1. Compile sketch
arduino-cli compile -b Seeeduino:nrf52:xiaonRF52840Sense --export-binaries doomscroller

# 2. Convert hex to UF2
python3 ~/Library/Arduino15/packages/Seeeduino/hardware/nrf52/1.1.13/tools/uf2conv/uf2conv.py \
  -f 0xADA52840 -c \
  -o doomscroller/build/Seeeduino.nrf52.xiaonRF52840Sense/doomscroller.uf2 \
  doomscroller/build/Seeeduino.nrf52.xiaonRF52840Sense/doomscroller.ino.hex
```

---

## Troubleshooting & FAQ

### Q: My phone says "Couldn't pair because of an incorrect PIN or passkey"
**Cause:** Host device cached a stale bonding table from earlier firmware.
**Solution:** Swipe the magnet once across the letter "C" on the back PCB. The blue LED will blink 5 times, wiping all bonds and rotating the static MAC address. On your phone, tap "Forget Device" and pair fresh.

### Q: Why does the cursor jump to the right side on Android?
**Answer:** This is an intentional feature. Android spawns mouse pointers in the dead-center of the screen. Because full-screen vertical video apps (TikTok, Instagram Reels) only process scroll events when the cursor is over the active video container, moving the cursor to the bottom corner breaks scrolling. Moving it horizontally to the right margin keeps it hovering over the Like/Comment icon column—clearing the center of your screen while ensuring 100% reliable scrolling. If you prefer the cursor to stay where it is (e.g. on PC or Mac), toggle **Auto-Park Cursor** to OFF for that profile in the Web Configurator.

### Q: How do I connect to a new device without my old phone stealing the connection?
**Answer:** In the Web Configurator, click **Force 60s Fresh Pairing Window** or enter `pair` in the console. You can also switch to another profile (e.g. Profile 2) which uses an independent MAC address, or swipe a magnet across the letter "C".

### Q: The scrolling direction feels backwards on my computer
**Solution:** Open the Web Configurator and flip the **Invert Scroll Direction** switch, or type `invert` in the console.

### Q: How do I know when the battery is low?
**Answer:** Check the Bluetooth device list on your phone or Mac. The battery percentage is reported natively via BLE Battery Service (BAS). You can also type `battery` in the serial or BLE console to read the raw voltage (e.g. `4.12V (85%)`).

---

## License

This project is licensed under the Apache License 2.0. See [LICENSE](LICENSE) for details.
Original hardware design and concept by [Andrew McCalip](https://github.com/andrewmccalip/doomscroller).
