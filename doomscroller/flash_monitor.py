#!/usr/bin/env python3
import os
import sys
import time
import shutil
import subprocess

PROJECT_DIR = os.path.dirname(os.path.abspath(__file__))
UF2_PATH = os.path.join(PROJECT_DIR, "build", "Seeeduino.nrf52.xiaonRF52840", "doomscroller.uf2")

print("=" * 60)
print("Doomscroller Auto-Flash Monitor")
print("Target UF2:", UF2_PATH)
print("=" * 60)

if not os.path.exists(UF2_PATH):
    print("Error: UF2 file not found! Compile first.")
    sys.exit(1)

print("\nWaiting for Doomscroller to enter Bootloader mode...")
print("-> Please double-tap the magnet over the letter 'C' on the back PCB.")
print("-> The LED will begin pulsing/breathing, and the drive will appear.\n")

start_time = time.time()
while time.time() - start_time < 120:
    # Check /Volumes for mounted bootloader drive
    try:
        volumes = os.listdir("/Volumes")
        for v in volumes:
            v_lower = v.lower()
            if any(k in v_lower for k in ["xiao", "boot", "nrf52", "seeed", "adafruit"]):
                vol_path = os.path.join("/Volumes", v)
                print(f"\n[FOUND] Bootloader drive detected at: {vol_path}")
                print(f"Flashing {os.path.basename(UF2_PATH)}...")
                dest = os.path.join(vol_path, "doomscroller.uf2")
                shutil.copy2(UF2_PATH, dest)
                print("[SUCCESS] Firmware copied! The device is now rebooting into the new firmware...")
                sys.exit(0)
    except Exception as e:
        pass

    time.sleep(0.5)

print("\nTimeout waiting for bootloader. Please try again.")
sys.exit(1)
