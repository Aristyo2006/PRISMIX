# PRISMIX

> **High-Performance Digital Audio Mixing Console & Live DSP Engine**

PRISMIX is a modern, modular digital mixing console and live DSP workstation built in **C++20** and powered by an ultra-responsive, hardware-grade **LVGL 9** user interface. Designed for live performance, studio tracking, and guitar/instrument rigs, PRISMIX combines zero-latency native audio processing, flexible multi-backend I/O, native LV2 & NAM neural modeling, and a MODEP-inspired web pedalboard editor.

---

## Highlights & Features

### 🎛️ Hardware-Grade Live Mixing Console
- **Dark Hardware Aesthetic**: Neutral charcoal and graphite rack styling with precision rotary dials, calibrated faders, and stereo LED metering.
- **Dedicated Section Navigation**: Quick access to **Mixer**, **Channel Strip**, **EQ**, **Dynamics**, **FX Rack**, **Matrix Routing**, and **System Settings**.
- **Interactive EQ & Dynamics**:
  - **4-Band Parametric EQ**: Interactive drag-and-drop filter nodes with real-time frequency response visualization.
  - **31-Band Graphic EQ**: Standard 1/3-octave ISO faders with built-in real-time spectrum analyzer (RTA) overlay.
  - **Studio Compressor & Gate**: Dynamic transfer curve display, adjustable knee visualizer, and live gain-reduction telemetry.
- **Customizable Themes**: Built-in theme manager with presets (*Graphite*, *Studio Blue*, *Lexicon Bronze*, *Monochrome*) and a semantic RGB color customizer.

### 🔌 Native Plugin & Neural Amp Hosting
- **8-Slot Insert FX Rack**: Up to 8 ordered processor slots per channel with reordering, bypass, and parameter automation.
- **Native LV2 Host**: In-process LV2 plugin hosting with manifest parsing, Atom message ports, and worker thread extensions.
- **NAM Lite Neural Modeling**: Integrated fast LSTM and WaveNet neural guitar amplifier and speaker cabinet emulation.
- **Built-in Studio Processors**: Stereo Echo Delay, Plate & Hall Reverb, Studio Compressor, Brickwall Limiter, Expander/Gate, and Auto Feedback Suppressor (AFS).
- **Pedalboard Presets**: Save and recall full multi-unit processing chains with instant auditioning.

### 🌐 MODEP-Style Web Pedalboard Editor
- **Embedded Web Server**: Non-blocking HTTP and Server-Sent Events (SSE) server running on `http://localhost:8888`.
- **Visual Stompboxes**: Graphic pedalboard interface with rotary knobs, jewel LED status indicators, and footswitch toggles.
- **Bidirectional Sync**: Real-time parameter and fader synchronization between the native LVGL mixer and connected web clients.
- **Zero Realtime Impact**: Runs strictly off the audio processing thread with thread-safe lock-free state access.

### 📦 PatchStorage LV2 Catalog Integration
- **Direct Plugin Discovery**: Search and browse thousands of community LV2 plugins directly within PRISMIX via the official PatchStorage API.
- **Responsive Browser Layout**: Adaptive grid layout supporting high-resolution desktop viewports down to compact 800×600 touchscreens.
- **One-Click Installation**: Download, unpack, validate, and assign plugins to channel racks in real time.

### 🔊 Flexible Multi-Backend Audio Engine
- **Cross-Platform Support**: Native support for **JACK** (Linux), **ALSA**, **WASAPI** (Windows Shared & Event-Driven), and **CoreAudio** (macOS).
- **Mock Audio Engine**: Built-in multi-channel signal generator with pink noise, sine sweeps, and real-time metering for offline testing and UI development.
- **Matrix Routing**: Channel bus assignments, AUX sends, and direct OMNI outputs.

---

## Architecture Overview

```text
               ┌────────────────────────────────────────────────────────┐
               │                  PRISMIX AUDIO ENGINE                  │
               │   64-bit Float DSP • Hard-Realtime Safe • Zero Locks   │
               └───────────┬────────────────────────────────┬───────────┘
                           │                                │
            Atomic State / Control Queue         Thread-Safe Mutex Bridge
                           │                                │
                           ▼                                ▼
              ┌─────────────────────────┐      ┌─────────────────────────┐
              │     LVGL 9 UI CORE      │      │     WEB CONTROL API     │
              │  • Mixer & Meters       │      │  • REST JSON Endpoints  │
              │  • Parametric/Graphic EQ│      │  • SSE Event Broadcast  │
              │  • Dynamics Curves      │      │  • Non-blocking Socket  │
              │  • LV2 / PatchStorage   │      │    Server (:8888)       │
              └─────────────────────────┘      └────────────┬────────────┘
                                                            │
                                                            ▼
                                               ┌─────────────────────────┐
                                               │   MODEP WEB PEDALBOARD  │
                                               │  • Visual Stompboxes    │
                                               │  • Rotary Knob Controls │
                                               │  • Chain Reordering     │
                                               └─────────────────────────┘
```

---

## Getting Started

### Prerequisites

- **C++ Compiler**: Clang 14+, GCC 12+, or MSVC 2022+ (C++20 support required)
- **CMake**: Version 3.22 or newer
- **SDL2**: Development headers and runtime libraries
- **zlib**: Required for PNG decoding and archive unpacking
- **JACK Audio Connection Kit** *(optional, for Linux pro-audio I/O)*: `libjack-jackd2-dev`

#### Ubuntu / Debian Dependencies
```bash
sudo apt update
sudo apt install build-essential cmake libsdl2-dev zlib1g-dev libjack-jackd2-dev
```

---

### Building PRISMIX

#### Linux / macOS
```bash
# Configure build
cmake -B build-linux -DCMAKE_BUILD_TYPE=Release

# Compile
cmake --build build-linux -j$(nproc)
```

#### Windows (PowerShell / Visual Studio)
```powershell
# Configure build
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Compile
cmake --build build --config Release
```

---

### Running PRISMIX

```bash
# Launch the mixer console
./build-linux/AudioCore

# Open the Web Pedalboard Editor in your browser:
# http://localhost:8888
```

#### Command-Line Options
| Option | Description |
|---|---|
| `--width <w>` | Set custom window width (e.g. `--width 1280`) |
| `--height <h>` | Set custom window height (e.g. `--height 720`) |
| `--zoom <factor>` | Scale UI display factor (e.g. `--zoom 1.0`) |
| `--real-audio-ui` | Start directly with live audio hardware backend |
| `--qa-test` | Run automated headless QA stress & stability suite |
| `--screenshot <path>` | Capture mixer viewport screenshot and exit |

---

### Running the Test Suite

PRISMIX includes comprehensive unit tests covering the audio DSP pipeline, dynamics curves, LV2 hosting, Web server APIs, and UI state integrity.

```bash
# Run all CTest suites
ctest --test-dir build-linux --output-on-failure

# Run the complete UI & Engine QA stress suite
./build-linux/AudioCore --qa-test
```

---

## License

This project is licensed under the GNU General Public License v3.0. See [LICENSE](LICENSE) for details.
