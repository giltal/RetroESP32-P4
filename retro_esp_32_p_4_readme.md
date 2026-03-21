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

It's cheap and works great, soon I will release a board that will make the platform a stand alone console.
In order to get it working, prepare a SD card (FAT32), place on it the content from the SD card folder.
Flash the 

## 🎬 Demo (Add GIFs!)

> Replace these with real recordings for maximum impact ⭐

```
![Launcher Demo](docs/images/launcher.gif)
![SNES Gameplay](docs/images/snes.gif)
```

---

## 📸 Screenshots

<img width="461" height="464" alt="image" src="https://github.com/user-attachments/assets/3823e33e-b138-437e-b063-3f2558e1ef88" />
<img width="376" height="385" alt="image" src="https://github.com/user-attachments/assets/78103cdd-7762-476b-90b0-7cfaa3a09947" />

## 🚀 Features

- 🎮 13 emulators + OpenTyrian
- ⚡ Near full-speed emulation (60 FPS on all systems except for SNES - I will try to improve)
- 💾 Save / Load states
- 🖥️ Touchscreen-optimized UI
- 🎯 USB controller support
- 🕹️ Paddle support for Atari (Via IO51)

## 🎮 Supported Systems

- NES
- Game Boy / GBC / GBA
- Sega Master System / Game Gear
- Atari 2600 / 7800 / 800XL / 5200
- Atari Lynx
- PC Engine (PCE)
- ZX Spectrum (Kempston + virtual keyboard)
- ColecoVision
- SNES
- OpenTyrian full game with stunning looks and speed

## ⚡ Performance Breakdown

| System | Status | Notes |
|--------|--------|------|
| NES / GB / GBA | ✅ 60 FPS | Full speed |
| SMS / GG | ✅ 60 FPS | Stable |
| Atari family | ✅ 60 FPS | Fixed legacy bugs |
| PCE | ✅ 60 FPS | Major improvements |
| SNES | ⚠️ ~50 FPS | No SuperFX |
The P4's PPA (Pixel Processing Accelerator) is in action for smooth graphics and speed

### SNES Limitations

- ❌ No Super FX support
- ✔️ Most standard titles run well

---

## 🧩 Architecture (Developer Insight)

This project is designed with performance and modularity in mind:

- Separate emulator cores per system
- Shared rendering pipeline (LCD optimized)
- Input abstraction layer (USB + touch)
- Optimized memory usage for large ROMs

### Key Challenges Solved

- Frame pacing for consistent 60 FPS
- Efficient LCD updates (480x800)
- Input latency minimization
- Cross-emulator UI integration

---

## 🎨 User Interface

- Fully redesigned launcher
- Smooth transitions
- Touch-first navigation
- Clean, modern layout

---

## 🎮 Controls

- USB SNES controller (recommended)
- Touchscreen (UI + keyboard)
- Atari paddle support

---

## 🔧 Improvements Over Original RetroESP32

- Fixed Atari 2600 issues
- PCE now runs at full speed
- Better stability across all systems
- Enhanced UI/UX

---

## 🛠️ Hardware Requirements

- ESP32-P4 board
- 4.3" 480x800 touchscreen
- USB controller

---

## 📦 Installation

```bash
# Clone
git clone https://github.com/your-repo/RetroESP32-P4.git
cd RetroESP32-P4

# Build
idf.py build

# Flash
idf.py flash monitor
```

---

## 📁 ROM Setup

Place ROMs in their respective folders.

> ⚠️ Use only legally owned ROMs.

---

## 🧩 Roadmap

- [ ] Improve SNES performance
- [ ] Investigate SuperFX feasibility
- [ ] Add more emulators
- [ ] UI polish & animations
- [ ] Bluetooth controller support

---

## 📊 Why This Project Matters

- Pushes ESP32-P4 to real limits
- Demonstrates advanced embedded graphics
- Combines multiple systems in one device
- Great reference for embedded + UI integration

---

## 🤝 Contributing

Contributions welcome:

- Performance tuning
- Emulator fixes
- UI improvements

---

## 📜 License

MIT License

---

## ⭐ Support

If you like this project:

- ⭐ Star the repo
- 🍴 Fork it
- 🧑‍💻 Contribute

---

## 💥 Final Note

RetroESP32-P4 is not just another emulator project — it's a demonstration of how far modern microcontrollers can go when optimized correctly.

