
# Samsung 300K Tool for GNU/Linux Systems

A lightweight, copyleft C utility designed for **Linux** that forces connected Samsung Galaxy devices into **Download Mode (Odin)** via AT commands over USB serial interfaces (`/dev/ttyACM*` or `/dev/ttyUSB*`).

This project serves as a native, fully open-source alternative to the Windows-based Samsung 300K Tool. It is a vital lifesaver for devices with broken physical buttons (Volume Down / Power / Home).


## 🚀 Features

- **Port Auto-Detection**: Dynamically scans `/dev/` to identify the correct active Samsung modem interface.
- **Silent Sequence**: Suppresses intermediate modem outputs (`BOOTING COMPLETED`, `ERROR`, `OK`) to keep your terminal screen clean.
- **Enhanced Payload Sequence**: Uses a robust chain of AT commands to successfully bypass security layers on newer device firmwares.
- **Safety Safeguard**: Requires explicit user confirmation via the `[ENTER]` key before triggering the reboot (`Ctrl+C` to safely abort).
- **Multi-device support**: Lists every Samsung device connected into a menu from which you can choose which one you want to reboot.


## 📋 Requirements

- A C compiler (**GCC** or **Clang**)
- **CMake** (version 3.10 or higher)
- A GNU/Linux operating system
- **Root privileges** (required to read/write directly to hardware device descriptors under `/dev/`)

## 🐧 Platform support

**Linux only.** The tool reads the USB vendor ID from
`/sys/class/tty/<dev>/device/../idVendor` to make sure it only talks to
Samsung devices (VID `04e8`). This mechanism is Linux-specific, so
macOS, BSD, and Windows are not supported.

On Windows, use the original Samsung 300K Tool instead.


## 🛠️ Installation & Compilation

Follow these clean standard steps to build the utility using CMake:

1. **Clone the repository:**
   ```bash
   git clone https://github.com/alexjune1206-maker/samsung-300k-tool-4-linux.git
   cd samsung-300k-tool-4-linux
   ```

2. **Generate the build directory and compile:**
   ```bash
   mkdir build && cd build
   cmake ..
   cmake --build .
   ```

3. **(Optional) Install globally in your system:**
   ```bash
   sudo cmake --install .
   ```
   *This places the executable inside `/usr/local/bin`, allowing you to run it from anywhere.*
   
4. **(Optional) Install to a custom prefix:**
   ```bash
   cmake -DCMAKE_INSTALL_PREFIX=/opt/samsung-300k-tool ..
   cmake --build .
   sudo cmake --install .
   ```

### Build manually with GCC

If you prefer not to use CMake:

    gcc -Wall -Wextra -O2 -o samsung-300k-tool samsung-300k-tool.c

### Uninstall

If you installed with CMake:

    sudo cmake --build build --target uninstall

Or manually:

    sudo rm /usr/local/bin/samsung-300k-tool
    
    
Replace `/usr/local/bin/samsung-300k-tool` with the path where the tool was installed.


## 📱 How to Use

1. Connect your Samsung device to your machine using a reliable USB cable.
2. **Important:** Ensure the device is fully booted up and the screen is **unlocked** (on the home screen), otherwise the Android subsystem will reject system-altering AT commands.
3. Launch the tool with superuser permissions:
   ```bash
   sudo ./samsung-300k-tool
   # Or simply 'sudo samsung-300k-tool' if you installed it globally
   ```
4. If more than one device is detected, a menu like this will be shown:
```text
[*] Found 2 Samsung device(s):
    [1] /dev/ttyACM0  (serial: R5CW20RRDRM)
    [2] /dev/ttyACM1  (serial: R3CN30ABCDE)

Select device(s):
  - Enter a number (1-2) for one device
  - Enter 'a' for all devices
  - Enter 'q' to quit
```
  Enter a number to pick one device, or press `a` to process all of them in parallel.
  
5. Press [ENTER] to initiate the sequence. The device will reboot into the bright blue/turquoise Download Mode screen.

6. The tool waits up to 30 seconds for the device to re-enumerate in Download Mode and confirms it by reading the USB PID (`04e8:685d, 685e, or 6860`). This works even if the screen is broken.


## ⚙️ Command-line options

```text
Usage: samsung-300k-tool [OPTIONS]

Options:
  -h, --help           Show help and exit
  -n, --dry-run        Detect devices and show the AT sequence
                       without sending anything
  -v, --verbose        Print modem responses for each command
  -t, --timeout SEC    Seconds to wait for Download Mode (1-600, default 30)
  -V, --version        Show version and exit
```
   
## 🔍 Troubleshooting

### "No Samsung serial port detected"

- Check that the USB cable is data-capable (some cables are charge-only).
- Check `lsusb | grep 04e8` — the phone should appear with a Samsung VID.
- Check that `/dev/ttyACM0` or `/dev/ttyUSB0` exists:

   ```bash
   ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null
   ```

- Check that you can read the sysfs VID:

   ```bash
   cat /sys/class/tty/ttyACM0/device/../idVendor
   # should print: 04e8
   ```

### "Could not open /dev/ttyACM0: Permission denied"

Run with `sudo`, or add your user to the `dialout` group:

    sudo usermod -aG dialout "$USER"
    # log out and back in for the change to take effect

### "Timed out waiting for Download Mode"

- Some devices need a longer timeout: try `--timeout 60`.
- Check `lsusb | grep 04e8` — the device may have re-enumerated with a
  PID not in the known list. If so, open an issue with the PID.
- Make sure the screen was unlocked before running the tool.

### The device rebooted but the tool didn't confirm Download Mode

The tool uses a 5-second grace period to match the exact hardware
serial, then falls back to any Samsung Download Mode PID. If your device
drops its serial in Download Mode, this is expected and still works.


## ⚠️ Disclaimer

Use this tool only on devices you own. Modifying firmware or forcing
Download Mode may void your warranty. This project is unofficial and is
not affiliated with or endorsed by Samsung.


## 🤝 Contributing & Copyleft Compliance

Contributions, bug reports, and pull requests are welcome! 

As this project is strictly bound by copyleft protections, **any derivative works, forks, or modifications distributed by third parties must also be made fully public and open-source under the same license terms.** Proprietary implementation or locking this code inside closed-source commercial flashing tools is strictly prohibited by law.

## 👤 Author


**alexjune1206-maker**

- GitHub: [alexjune1206-maker](https://github.com/alexjune1206-maker)
- Contact: alexjune1206@gmail.com


## 📄 License

This program is free software: you can redistribute it and/or modify it under the terms of the **GNU General Public License v3** as published by the Free Software Foundation. See the [LICENSE](LICENSE) file for the full text.
