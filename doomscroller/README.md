# Doomscroller Universal Firmware (v2.0)

A universal, cross-platform firmware for Andrew McCalip's **Doomscroller** hardware (built on the Seeed XIAO nRF52840 and MPS MagAlpha MA780 magnetic angle encoder).

---

## What Changed & Why This Fixes Previous Issues

### 1. Fixed: Doesn't work on macOS & iOS
* **The Root Cause:** The original firmware sent touch digitizer packets (Report ID 1) and Microsoft Surface Dial radial packets (Report ID 3). macOS and iOS ignore both without specialized drivers. The original HID descriptor declared a standard Mouse Wheel (Report ID 2), but the firmware *never actually sent any wheel reports*.
* **The Fix:** The new firmware implements standard **BLE HID Mouse Scroll Wheel** reports. macOS, iOS, Android, Windows, and Linux all natively support standard mouse wheel reports out of the box with zero third-party drivers or helper apps.

### 2. Fixed: Accidental Clicks on Android (e.g. Opening Twitter Posts)
* **The Root Cause:** The v0.1 firmware tried to emulate a touchscreen stylus finger dragging across `(5000, 5000)`. When the wheel slowed down or stopped, the finger lifted. If it lifted within a 40px bounding box of where it started, the Android kernel interpreted it as a screen tap/click.
* **The Fix:** Standard HID Mouse Scroll Wheel (`MotionEvent.ACTION_SCROLL`) scrolls views, social media feeds, and browsers natively with hardware inertia and acceleration, **with 0% chance of clicking or tapping posts**.

### 3. Fixed: Device Freezing / "Swipe Before & After Pairing" Bug
* **The Root Cause:** In `setup()`, the original firmware had a 10-second loop that called `suspendLoop()` if not connected. However, `connect_callback` was never registered to call `resumeLoop()`. If you didn't pair within 10 seconds of booting, the board was permanently inert until magnetically hard-reset.
* **The Fix:** Full BLE event lifecycle management with `connect_callback` and `disconnect_callback`. Reconnections immediately resume the loop and flash the green LED. The device advertises smoothly and never hangs.

### 4. Added: Sub-Detent Accumulator (High Resolution & Slow Speed Accuracy)
* Slow movements no longer get lost in integer division. Fractional rotational steps are carried across cycles in an accumulator, so even a single degree of slow, deliberate rotation produces clean, smooth scrolling.

### 5. Added: Dynamic Flick Acceleration
* Gentle turns provide precise, 1:1 line scrolling.
* Fast spins/flicks automatically apply a progressive acceleration curve so you can glide through long feeds with a quick flick.

### 6. Added: Battery Service (BLE BAS)
* Monitors the LiPo battery voltage via the onboard divider on the Seeed XIAO nRF52840.
* Reports actual battery percentage (0–100%) directly to macOS Bluetooth settings, iOS battery widget, and Android Bluetooth device info.

### 7. Added: Smart Low-Power Sleep & Instant Wakeup
* Automatically enters low-power standby after 30 seconds of inactivity.
* Checks for movement every 100ms (<100ms wakeup latency) while consuming minimal standby current (~50µA).

### 8. Added: Interactive Serial CLI & Flash Persistence
* Connect via USB at 115200 baud to configure speed, flick acceleration, deadzone, scroll inversion, and Bluetooth device name. Settings are saved permanently to on-chip flash (`InternalFS`).

---

## Flashing Instructions

The compiled binaries are available in `doomscroller/build/Seeeduino.nrf52.xiaonRF52840/`:
* `doomscroller.uf2` (for USB drag-and-drop)
* `doomscroller.ino.zip` (for serial DFU)

### Method A: Drag-and-Drop UF2 (Recommended)
1. Plug the Doomscroller into your Mac via USB-C.
2. Double-tap the magnet over the letter **"C"** on the back PCB lid (mimicking a double-click of the reset switch).
3. The onboard LED will start a slow, breathing pulse, and a USB drive named `Seeed XIAO` or `XIAO-BOOT` will mount on your Mac.
4. Drag and drop `doomscroller/build/Seeeduino.nrf52.xiaonRF52840/doomscroller.uf2` onto the drive (or run `./flash_monitor.py` which does it automatically).
5. The device will automatically flash and reboot into the new firmware!

### Method B: Auto-Flash Monitor
Run the included python monitor:
```bash
./doomscroller/flash_monitor.py
```
Then double-tap the magnet on the letter "C". The script will detect the volume the instant it mounts and copy the firmware over.

---

## Pairing Guide

### macOS
1. Open **System Settings > Bluetooth**.
2. Look for **"Doomscroller"** in the Nearby Devices list.
3. Click **Connect**.
4. Scroll away! (If you have "Natural Scrolling" enabled or disabled in macOS Trackpad/Mouse settings and prefer the opposite direction, simply type `invert` into the serial monitor).

### Android
1. Open **Settings > Connected Devices > Pair new device**.
2. Select **"Doomscroller"**.
3. Confirm pairing.
4. Feeds and web pages will scroll cleanly without accidental clicks!

### iOS / iPadOS
1. Open **Settings > Accessibility > Touch > AssistiveTouch** and ensure AssistiveTouch is enabled (or use native mouse pointer in iPadOS).
2. Go to **Settings > Bluetooth** and tap **Doomscroller**.

---

## Serial Console Commands (USB @ 115200 Baud)

Open any serial terminal (e.g. `arduino-cli monitor -p /dev/cu.usbmodem101` or screen):
* `status` - Displays battery voltage/percentage, connection state, sensor angle, and active tuning.
* `invert` - Flips the scroll wheel direction (persists to flash).
* `speed <val>` - Sets degrees per wheel step (default `10.0`; lower = faster, higher = slower).
* `accel <val>` - Sets flick acceleration factor (default `0.05`; `0.0` = linear).
* `deadzone <val>` - Sets resting noise deadzone in degrees (default `0.18`).
* `name <string>` - Changes the advertised Bluetooth name.
* `stream` - Toggles real-time streaming of angle, velocity, and scroll output for debugging.
* `defaults` - Resets all settings to factory defaults.
* `reboot` - Soft reboots the microcontroller.
