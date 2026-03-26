# 🎮 RetroESP32-P4

![Platform](https://img.shields.io/badge/platform-ESP32--P4-blue)
![Status](https://img.shields.io/badge/status-active-success)
![License](https://img.shields.io/badge/license-MIT-green)
![Performance](https://img.shields.io/badge/performance-60FPS-brightgreen)
![UI](https://img.shields.io/badge/UI-touch--optimized-orange)

---

## 🧠 Overview

**RetroESP32-P4** is a high-performance retro gaming platform built on off the shelf ESP32-P4 platform (see picture), bringing together multiple classic systems into a single, compact device.
The platform and the USB SNES controller available at AliExpress for around 35$ (both)

🚀 Getting Started

This platform is affordable, reliable, and performs great.
Soon, I’ll be releasing a dedicated board that turns it into a fully standalone console.

🧰 Setup Instructions

Prepare SD Card

Format your SD card as FAT32

Copy all files from the SD folder onto the card

**Flash Firmware**

Flash RetroESP32_P4_v1.bin to address 0x0

Recommended tool:
👉 https://espressif.github.io/esptool-js/

Add ROMs

Copy your ROM files to the SD card (same structure as RetroESP32)

Power On & Enjoy 🎮

🖥️ GUI Improvements

Files are now sorted

File browser supports Page Up / Page Down

Improved usability compared to the original RetroESP32

⚠️ Recovery Mode

If an emulator gets stuck:

Press RESET

Immediately touch and hold the screen

The system will reboot back into the launcher

🎮 Controls Notes

SNES requires full button access, so:

Menu and volume controls are handled via the touch screen

🕹️ Atari Paddle Support

To use paddle input:

Connect a potentiometer:

One side → 3.3V

Other side → GND

Middle (wiper) → IO51

If rotation is reversed:

Simply swap 3.3V and GND

## 🎬 Demo (Add GIFs!)

> Replace these with real recordings for maximum impact ⭐

```
![Launcher Demo](docs/images/launcher.gif)
![SNES Gameplay](docs/images/snes.gif)
```
## 📸 Screenshots

<img width="461" height="464" alt="image" src="https://github.com/user-attachments/assets/3823e33e-b138-437e-b063-3f2558e1ef88" />
<img width="376" height="385" alt="image" src="https://github.com/user-attachments/assets/78103cdd-7762-476b-90b0-7cfaa3a09947" />

<img src="C:\ESPIDFprojects\RetroESP32_P4\SDcard\system_art\papp.png" alt="papp" style="zoom: 80%;" />

## 🚀 Features

- 🎮 **14 emulators** (biggest number ever on an ESP!) + support to run apps from the SD card (folder named papp)
- Apps currently In the SD card: **Open Tyrian, DOOM and Quake**
- ⚡ Near full-speed emulation (60 FPS on all systems except for SNES and Mega Drive- I will try to improve)
- 💾 Save / Load states (**SNES and Mega Drive from within the game**)
- 🖥️ Touchscreen-optimized UI
- 🎯 USB controller support (currently the one showed in the picture)
- 🕹️ Paddle support for Atari (Via IO51)

## 🎮 Supported Systems

- NES
- Game Boy / GBC
- Sega Master System / Game Gear
- Atari 2600 / 7800 / 800XL / 5200
- Atari Lynx
- PC Engine (PCE)
- ZX Spectrum (Kempston + virtual keyboard)
- ColecoVision
- SNES (Titles which uses the FX accelerator are not supported for now)
- SEGA Genesis - Mega Drive (Titles which uses the DSP accelerator are not supported for now)
- Open Tyrian, DOOM and Quake via **Run Apps** entry in the launcher (more apps will be added in time)

## ⚡ Performance Breakdown

| System | Status | Notes |
|--------|--------|------|
| NES / GB / GBA | ✅ 60 FPS ||
| SMS / GG | ✅ 60 FPS ||
| Atari family | ✅ 60 FPS | Fixed legacy bugs |
| PCE | ✅ 60 FPS | Major improvements |
| SNES / Mega Drive | ⚠️ ~50 FPS | No SuperFX / DSP |
|The P4's PPA (Pixel Processing Accelerator) is in action for smooth graphics and speed|||

### SNES Mega Drive \ Limitations

- ❌ No Super FX\DSP support
- ✔️ Most standard titles run well

## 🧩 Architecture (Developer Insight)

This project is designed with performance and modularity in mind:

- Separate emulator cores per system
- Shared rendering pipeline (LCD optimized), using the **2D accelator** of the P4 for scale and rotate
- Input abstraction layer (USB + touch)
- Optimized memory usage for large ROMs

### Key Challenges Solved

- Frame pacing for consistent 60 FPS
- Efficient LCD updates (480x800)
- Input latency minimization
- Cross-emulator UI integration

## 🎮 Controls

- USB SNES controller (recommended)
- Atari paddle support

## 🔧 Improvements Over Original RetroESP32

- Fixed Atari 2600 issues
- PCE now runs at full speed
- Better stability across all systems
- Enhanced UI/UX
- Added more emulators
- Save / load support for all emulators

## 🛠️ Hardware Requirements

- ESP32-P4 board
- 4.3" 480x800 touchscreen
- USB controller

## 📁 ROM Setup

Place ROMs in their respective folders.

> ⚠️ Use only legally owned ROMs.

## 🧩 Roadmap

- [ ] Improve SNES performance
- [ ] Investigate SuperFX feasibility
- [ ] UI polish & animations
- [ ] Bluetooth controller support
- [ ] Dedicated board to make it a stand alone console (WIP)

## 🤝 Contributing

Contributions welcome:

- Performance tuning
- Emulator fixes
- UI improvements
- New apps running from SD card directly

## 📜 License

MIT License

## ⭐ Support

If you like this project:

- ⭐ Star the repo
- 🍴 Fork it
- 🧑‍💻 Contribute

---

## 💥 Final Note

RetroESP32-P4 is not just another emulator project — it's a demonstration of how far modern microcontrollers can go when optimized correctly.

