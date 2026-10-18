# Samsung 300K Tool for Unix-like Systems

A lightweight, copyleft C utility designed for **Linux** and **macOS** that forces connected Samsung Galaxy devices into **Download Mode (Odin)** via AT commands over USB serial interfaces (`/dev/ttyACM*` or `/dev/tty.usbmodem*`).

This project serves as a native, fully open-source alternative to the Windows-based Samsung 300K Tool. It is a vital lifesaver for devices with broken physical buttons (Volume Down / Power / Home).

## 🚀 Features

- **Port Auto-Detection**: Dynamically scans `/dev/` to identify the correct active Samsung modem interface.
- **Silent Sequence**: Suppresses intermediate modem outputs (`BOOTING COMPLETED`, `ERROR`, `OK`) to keep your terminal screen clean.
- **Enhanced Payload Sequence**: Uses a robust chain of AT commands to successfully bypass security layers on newer device firmwares.
- **Safety Safeguard**: Requires explicit user confirmation via the `[ENTER]` key before triggering the reboot (`Ctrl+C` to safely abort).

## 📋 Requirements

- A C compiler (**GCC** or **Clang**)
- **CMake** (version 3.10 or higher)
- A Unix-like Operating System (Linux distributions or macOS)
- **Root privileges** (required to read/write directly to hardware device descriptors under `/dev/`)

## 🛠️ Installation & Compilation

Follow these clean standard steps to build the utility using CMake:

1. **Clone the repository:**
   ```bash
   git clone https://github.com/alexjune1206-maker/samsung-300k-tool-4-linux
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

## 📱 How to Use

1. Connect your Samsung device to your machine using a reliable USB cable.
2. **Important:** The device must have USB Debugging enabled in Developer Options. Ensure the device is fully booted up and the screen is **unlocked** (on the home screen), otherwise the Android subsystem will reject system-altering AT commands.
3. Launch the tool with superuser permissions:
   ```bash
   sudo ./samsung-300k-tool
   # Or simply 'sudo samsung-300k-tool' if you installed it globally
   ```
4. Verify the detected port, then press **`[ENTER]`** to initiate the sequence. The device will instantly reboot into the bright blue/turquoise Download Mode screen.

## 🤝 Contributing & Copyleft Compliance

Contributions, bug reports, and pull requests are welcome! 

As this project is strictly bound by copyleft protections, **any derivative works, forks, or modifications distributed by third parties must also be made fully public and open-source under the same license terms.** Proprietary implementation or locking this code inside closed-source commercial flashing tools is strictly prohibited by law.

## 📄 License

This program is free software: you can redistribute it and/or modify it under the terms of the **GNU General Public License v3** as published by the Free Software Foundation. See the [LICENSE](LICENSE) file for the full text.
