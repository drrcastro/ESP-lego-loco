#!/usr/bin/env python3
"""
Automated Build Script for ESP-lego-loco Web Flasher Binaries
Compiles all 3 profiles across supported ESP chipsets:
- Master (ESP32, ESP32-S3)
- Loco (ESP32-C3, ESP8266, ESP32)
- Track & Station (ESP32, ESP8266)
"""

import os
import shutil
import subprocess
import sys

ENVIRONMENTS = [
    ("master", "Master Gateway (ESP32)"),
    ("master_s3", "Master Gateway (ESP32-S3)"),
    ("loco_c3", "Locomotive (ESP32-C3)"),
    ("loco_esp8266", "Locomotive (ESP8266 D1 Mini)"),
    ("loco_esp32", "Locomotive (ESP32)"),
    ("track", "Track & Station (ESP32)"),
    ("track_esp8266", "Track & Station (ESP8266 D1 Mini)"),
]

def get_pio_cmd():
    which_pio = shutil.which("pio")
    if which_pio:
        return which_pio
    default_win_pio = os.path.expanduser(r"~/.platformio/penv/Scripts/pio.exe")
    if os.path.exists(default_win_pio):
        return default_win_pio
    return "pio"

def main():
    root_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    os.chdir(root_dir)

    pio_bin = get_pio_cmd()

    print("=" * 60)
    print("  ESP LEGO LOCO - BATCH COMPILATION FOR WEB FLASHER")
    print("=" * 60)

    success_count = 0
    failed = []

    for env_name, description in ENVIRONMENTS:
        print(f"\n[BUILD] Target: {env_name} ({description})...")
        cmd = [pio_bin, "run", "-e", env_name]
        ret = subprocess.run(cmd)
        if ret.returncode == 0:
            print(f"[OK] {env_name} successfully built!")
            success_count += 1
        else:
            print(f"[ERROR] Failed to compile {env_name}!")
            failed.append(env_name)

    print("\n" + "=" * 60)
    print(f"Compilation summary: {success_count}/{len(ENVIRONMENTS)} targets successful.")
    if failed:
        print(f"Failed environments: {', '.join(failed)}")
        sys.exit(1)
    else:
        print("All firmware images generated into tools/web-flasher/binaries/!")
        sys.exit(0)

if __name__ == "__main__":
    main()
