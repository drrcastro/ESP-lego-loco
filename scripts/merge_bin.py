import os
import shutil
import sys
import subprocess

Import("env")

def merge_bin_action(source, target, env):
    try:
        platform = env.get("PIOPLATFORM", "")
        mcu = env.get("BOARD_MCU", "").lower()
        env_name = env["PIOENV"]
        build_dir = env.subst("$BUILD_DIR")
        prog_name = env.subst("$PROGNAME")
        out_dir = os.path.abspath(os.path.join(env.subst("$PROJECT_DIR"), "tools", "web-flasher", "binaries"))
        os.makedirs(out_dir, exist_ok=True)

        target_firmware = os.path.join(build_dir, f"{prog_name}.bin")
        if not os.path.exists(target_firmware):
            return

        # Target output file for web flasher
        output_bin = os.path.join(out_dir, f"{env_name}_merged.bin")

        if platform == "espressif8266":
            # ESP8266 produces a single flashable binary at 0x0
            shutil.copyfile(target_firmware, output_bin)
            print(f"[Web Flasher] Exported ESP8266 firmware: {output_bin}")
            return

        # ESP32 / ESP32-S3 / ESP32-C3
        bootloader_bin = os.path.join(build_dir, "bootloader.bin")
        partitions_bin = os.path.join(build_dir, "partitions.bin")

        if not os.path.exists(bootloader_bin) or not os.path.exists(partitions_bin):
            print(f"[Web Flasher] Bootloader or partitions not found in {build_dir}. Skipping merge.")
            return

        # Offset configuration
        bootloader_offset = "0x0000" if mcu in ["esp32c3", "esp32s3", "esp32c2", "esp32c6"] else "0x1000"
        partitions_offset = "0x8000"
        boot_app0_offset = "0xe000"
        app_offset = "0x10000"

        # Find boot_app0.bin in framework packages
        boot_app0_bin = os.path.join(build_dir, "boot_app0.bin")
        if not os.path.exists(boot_app0_bin):
            framework_dir = env.PioPlatform().get_package_dir("framework-arduinoespressif32")
            if framework_dir:
                candidate = os.path.join(framework_dir, "tools", "partitions", "boot_app0.bin")
                if os.path.exists(candidate):
                    boot_app0_bin = candidate

        flash_size = env.get("BOARD_FLASH_MODE", "dio")
        flash_freq = env.get("BOARD_F_FLASH", "40m").replace("000000L", "m")
        
        cmd_args = [
            "--chip", mcu,
            "merge_bin",
            "-o", output_bin,
            "--flash_mode", flash_size,
            "--flash_freq", flash_freq,
            bootloader_offset, bootloader_bin,
            partitions_offset, partitions_bin
        ]

        if os.path.exists(boot_app0_bin):
            cmd_args.extend([boot_app0_offset, boot_app0_bin])

        cmd_args.extend([app_offset, target_firmware])


        print(f"[Web Flasher] Generating merged binary for {env_name} ({mcu})...")

        python_exe = env.subst("$PYTHONEXE") or sys.executable

        # Build list of candidate ways to invoke esptool
        candidates = []
        try:
            tool_dir = env.PioPlatform().get_package_dir("tool-esptoolpy")
            if tool_dir:
                script_path = os.path.join(tool_dir, "esptool.py")
                if os.path.exists(script_path):
                    candidates.append([python_exe, script_path])
        except Exception:
            pass

        candidates.append([python_exe, "-m", "esptool"])
        candidates.append(["esptool.py"])
        candidates.append(["esptool"])

        merged_ok = False
        last_err = ""
        for base_cmd in candidates:
            try:
                res = subprocess.run(base_cmd + cmd_args, capture_output=True, text=True)
                if res.returncode == 0:
                    print(f"[Web Flasher] SUCCESS! Merged binary created: {output_bin}")
                    merged_ok = True
                    break
                else:
                    last_err = res.stderr or res.stdout
            except (FileNotFoundError, OSError) as e:
                last_err = str(e)
                continue
            except Exception as e:
                last_err = str(e)
                break

        if not merged_ok:
            print(f"[Web Flasher] esptool merge note: {last_err.strip()}. Copying firmware.bin directly.")
            shutil.copyfile(target_firmware, os.path.join(out_dir, f"{env_name}_firmware.bin"))

    except Exception as exc:
        print(f"[Web Flasher] Error during merge_bin post action: {exc}")

env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge_bin_action)
