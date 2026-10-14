# Doomscroller Universal Firmware (v2.0-Universal)

[![Platform: Seeed XIAO nRF52840 Sense](https://img.shields.io/badge/Platform-Seeed%20XIAO%20nRF52840%20Sense-blue.svg)](https://wiki.seeedstudio.com/XIAO-BLE/)
[![Sensor: MPS MagAlpha MA780](https://img.shields.io/badge/Sensor-MagAlpha%20MA780%2016--bit-orange.svg)](https://www.monolithicpower.com/en/ma780.html)
[![Protocol: Driverless BLE HID](https://img.shields.io/badge/BLE-HID%20Mouse%20%2B%20Battery%20Service-green.svg)](https://www.bluetooth.com/)
[![License: Apache 2.0](https://img.shields.io/badge/License-Apache%202.0-lightgrey.svg)](LICENSE)

A high-performance, driverless, universal firmware for Andrew McCalip's **Doomscroller** hardware scroll controller.

Engineered for native compatibility with **Android**, **macOS**, **iOS / iPadOS**, **Windows**, and **Linux** without companion apps, background daemons, or custom drivers.

---

## Table of Contents

- [The Problem & Why This Firmware Exists](#the-problem--why-this-firmware-exists)
- [Key Features](#key-features)
- [Hardware Architecture & Pinout](#hardware-architecture--pinout)
- [Quick Start: Flashing the Firmware](#quick-start-flashing-the-firmware)
- [Pairing Across Platforms](#pairing-across-platforms)
- [Interactive Serial CLI Commands](#interactive-serial-cli-commands)
- [Configuration & Tuning](#configuration--tuning)
- [Building from Source](#building-from-source)
- [Troubleshooting & FAQ](#troubleshooting--faq)

---

## The Problem & Why This Firmware Exists

The original upstream Doomscroller firmware attempted to emulate a composite touch digitizer stylus and Microsoft Surface Dial radial device:
1. **Passkey / PIN Failure:** Host operating systems (especially Android and macOS) mistook composite HID declarations for a keyboard or secure input terminal, demanding 6-digit PINs or passkeys that a dial cannot enter.
2. **Accidental Clicks on Social Feeds:** Emulating a touch digitizer finger dragging at fixed screen coordinates caused Android to misinterpret wheel slowdowns as tap/click gestures, accidentally opening posts and ads instead of scrolling.
3. **No macOS or iOS Support:** Desktop and Apple platforms ignore touch digitizer packets without proprietary drivers.
4. **Stale Bond Lockouts:** Once paired, changing operating systems or forgetting the device resulted in persistent connection errors (`Doomscroller063` name caching and bonding mismatches).
5. **No Native Battery Percentage:** Battery status was not exposed through standard Bluetooth GATT services.

### How v2.0-Universal Solves This

* **Pure BLE HID Mouse Wheel:** Uses standard relative mouse reports (`MotionEvent.ACTION_SCROLL` / generic desktop wheel). Universal support across all platforms out of the box with zero chance of triggering accidental screen taps.
* **Just-Works Pairing:** Explicitly disables Keyboard and Display I/O capabilities (`setIOCaps(false, false, false)`), guaranteeing instant, PIN-free pairing on Android, macOS, and iOS.
* **Smart Right-Margin Cursor Auto-Parking:** Solves the mobile video feed dilemma. Android defaults to centering mouse pointers. The firmware automatically glides the cursor smoothly to the far right margin at mid-height ~1.5s after connecting. The center screen (video, faces, subtitles) stays completely clean, while the pointer remains within the active video container (over the Like/Comment column) so TikTok, Instagram Reels, and Shorts scroll 100% reliably.
* **Native Battery Service (0x180F):** Safely samples the Seeed XIAO internal voltage divider and reports live battery level (0–100%) directly to Android and macOS Bluetooth menus.
* **Hardware Magnetic Reset ("C"):** Swiping a magnet over the letter "C" on the back PCB clears all stored bonding keys, increments the random static Bluetooth MAC address, and gives 5 blue LED flashes. This forces all nearby host devices to discover the Doomscroller as a brand-new device without stale bond conflicts.

---

## Key Features

| Feature | Description |
| :--- | :--- |
| **Driverless Operation** | Standard Bluetooth Low Energy HID Mouse descriptor recognized natively by every major operating system. |
| **Edge-Parked Pointer** | Cursor slides unobtrusively to the right margin (~1.5s post-connect), keeping video feeds clear while preserving full scrolling in vertical video apps. |
| **Dynamic Flick Acceleration** | Smooth 1:1 precision (30°/click) during slow turns; automatically scales up to an 8x velocity multiplier on quick flicks for fast browsing. |
| **Sub-Detent Accumulation** | Carries fractional angle changes across loop cycles so slow, deliberate rotations never get lost in integer division. |
| **Native Battery Service** | Standard BLE BAS characteristic (`0x180F`) broadcast to system menus without ADC initialization lockups. |
| **Magnetic Reset & Bootloader** | Single swipe over "C" clears bonds and refreshes BLE identity; double-tap enters UF2 bootloader drive. |
| **USB Serial CLI** | Interactive configuration console over USB CDC at 115200 baud for tuning speed, deadzone, direction, and name. |
| **Ultra-Low Power Standby** | MagAlpha sensor sleeps when Bluetooth disconnects to conserve battery life. |

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
| **MagAlpha MA780** | PWR | D6 | P0.04 | Sensor VCC Power Control (HIGH = ON) |
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
2. **Double-tap a magnet** over the letter **"C"** on the back PCB (mimics double-clicking a reset button). Alternatively, if connected over serial, type `dfu`.
3. The on-board LED will begin a slow, breathing pulse, and a USB mass storage drive named **`XIAO-SENSE`** or **`XIAO-BOOT`** will appear on your desktop.
4. Drag and drop `doomscroller.uf2` onto the drive.
5. The device will write the firmware, disconnect, and reboot automatically into v2.0-Universal. *(Any OS warning that "The disk was not ejected properly" is normal and confirms successful reboot).*

### Method B: Automated Python Flash Monitor

From the project root directory, run:
```bash
python3 doomscroller/flash_monitor.py
```
Then double-tap the magnet over the letter "C". The script will automatically detect the drive the moment it mounts, copy the UF2 file, and verify completion.

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
3. Scrolling works instantly. *(If you use macOS "Natural Scrolling" and want the opposite direction, enter `invert` in the USB serial console).*

### iOS & iPadOS
1. Go to **Settings > Bluetooth** and tap **Doomscroller** to connect.
2. In **Settings > Accessibility > Touch > AssistiveTouch**, verify pointer device support is enabled if using an iPhone, or use native pointer in iPadOS.

### Windows & Linux
1. Open your system's Bluetooth settings and select **Add Device**.
2. Choose **Doomscroller**. The device pairs immediately as an HID-compliant mouse.

> [!TIP]
> **Switching Devices / Fresh Pairing:** Whenever pairing with a new device, **swipe the magnet once across the letter "C"**. The blue LED will flash 5 times, clearing stored bonds and rotating the static MAC address so the new host sees a completely fresh device.

---

## Interactive Serial CLI Commands

Connect via USB at **115200 baud** using any serial terminal (Arduino Serial Monitor, screen, or minicom):

```text
--- Doomscroller Serial Commands ---
  status         - Show current status and settings
  battery        - Read battery voltage and estimated percentage
  invert         - Toggle scroll direction (invert)
  speed <deg>    - Set degrees per wheel click (e.g. 'speed 30.0')
  accel <gain>   - Set flick acceleration (e.g. 'accel 0.02', 0=off)
  deadzone <deg> - Set deadzone degrees (e.g. 'deadzone 0.18')
  name <str>     - Set Bluetooth device name (e.g. 'name Doomscroller')
  edge           - Slide mouse cursor to right margin (out of video center)
  center         - Move mouse cursor back towards center
  dfu            - Reboot into UF2 bootloader drive for flashing
  unpair         - Clear all BLE bonding keys and re-advertise
  defaults       - Reset settings to factory defaults
  reboot         - Reboot controller
  help           - Print this menu
```

### Example: Checking Device Status

```text
--- Device Status ---
Firmware:         v2.0-Universal
Device Name:      Doomscroller
BLE Status:       Connected
Battery:          3.60V (10%)
Raw Angle:        8917
Scroll Direction: NORMAL
Degrees/Step:     30.00
Accel Factor:     0.020
Deadzone:         0.18°
Uptime:           42s
```

---

## Configuration & Tuning

Key tuning parameters are defined in `doomscroller/config.h`:

```c
#define DEFAULT_DEGREES_PER_STEP   30.0f  // Degrees of rotation per scroll click (30° feels like a traditional mouse wheel detent)
#define DEFAULT_ACCEL_FACTOR       0.02f  // Dynamic acceleration gain for fast flicks (0.0 = linear)
#define DEFAULT_DEADZONE_DEG       0.18f  // Deadzone filter in degrees to prevent resting vibration drift
#define DEFAULT_INVERT_SCROLL      false  // Scroll direction toggle
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
**Answer:** This is an intentional feature. Android spawns mouse pointers in the dead-center of the screen. Because full-screen vertical video apps (TikTok, Instagram Reels) only process scroll events when the cursor is over the active video container, moving the cursor to the bottom corner breaks scrolling. Moving it horizontally to the right margin keeps it hovering over the Like/Comment icon column—clearing the center of your screen while ensuring 100% reliable scrolling.

### Q: The scrolling direction feels backwards on my computer
**Solution:** Open the USB serial console at 115200 baud and type `invert`. Alternatively, toggle "Natural Scrolling" in your operating system's mouse settings.

### Q: How do I know when the battery is low?
**Answer:** Check the Bluetooth device list on your phone or Mac. The battery percentage is reported natively via BLE Battery Service (BAS). You can also type `battery` in the serial console to read the raw voltage (e.g. `3.60V (10%)`).

---

## License

This project is licensed under the Apache License 2.0. See [LICENSE](LICENSE) for details.
Original hardware design and concept by [Andrew McCalip](https://github.com/andrewmccalip/doomscroller).
