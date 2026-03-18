# 🎮 RetroESP32-P4

![Platform](https://img.shields.io/badge/platform-ESP32--P4-blue)
![Status](https://img.shields.io/badge/status-active-success)
![License](https://img.shields.io/badge/license-MIT-green)
![Performance](https://img.shields.io/badge/performance-60FPS-brightgreen)
![UI](https://img.shields.io/badge/UI-touch--optimized-orange)

---

## 🧠 Overview

**RetroESP32-P4** is a high-performance retro gaming platform built on the ESP32-P4, bringing together multiple classic systems into a single, compact device with a modern touchscreen interface.

Designed for both **performance and usability**, it combines optimized emulation with a clean, responsive GUI.

---

## 🎬 Demo (Add GIFs!)

> Replace these with real recordings for maximum impact ⭐

```
![Launcher Demo](docs/images/launcher.gif)
![SNES Gameplay](docs/images/snes.gif)
```

---

## 📸 Screenshots

```
![Launcher](docs/images/launcher.png)
![Game](docs/images/gameplay.png)
![Hardware](docs/images/hardware.png)
```

---

## 🚀 Features

- 🎮 13 emulators + OpenTyrian
- ⚡ Near full-speed emulation (60 FPS on most systems)
- 💾 Save / Load states
- 🖥️ Touchscreen-optimized UI
- 🎯 USB controller support
- 🕹️ Paddle support for Atari

---

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

---

## ⚡ Performance Breakdown

| System | Status | Notes |
|--------|--------|------|
| NES / GB / GBA | ✅ 60 FPS | Full speed |
| SMS / GG | ✅ 60 FPS | Stable |
| Atari family | ✅ 60 FPS | Fixed legacy bugs |
| PCE | ✅ 60 FPS | Major improvement |
| SNES | ⚠️ ~50 FPS | No SuperFX |

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

