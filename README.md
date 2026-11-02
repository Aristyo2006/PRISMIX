# PRISMIX — Canary Branch 🧪

> **⚠️ DEVELOPMENT & EXPERIMENTAL CHANNEL (`Canary`)**
>
> You are viewing the **Canary** branch of **PRISMIX (`AudioCore`)**. This branch is strictly for active development, bleeding-edge DSP/UI prototyping, backend audio routing experiments, and continuous QA testing.
> - **Stability**: Features on `Canary` may be incomplete, undergoing active refactoring, or subject to breaking changes without notice.
> - **Production / Stable Use**: Switch to the [`main`](../../tree/main) branch for stable builds.

---

## Canary Channel Overview

PRISMIX (`AudioCore`) is a modular digital mixing console and live DSP workstation built in **C++20** (with a standalone **C++17** core DSP & hardware audio library) and powered by a hardware-grade **LVGL 9** user interface over **SDL2**.

| Channel Property | Details |
|---|---|
| **Branch** | `Canary` (Development / Pre-Release) |
| **Target Audience** | Core developers, DSP engineers, and QA testers |
| **Build Recommended** | `Debug` or `RelWithDebInfo` with CTest & `--qa-test` verification |
| **Web Remote UI** | Full 7-View Industrial Browser Console (`http://127.0.0.1:8888`) |

---

## Active Subsystems & Features

### 🎛️ Hardware-Grade Live Mixing Console
- **Scalable Channel & Bus Architecture**: Dynamic input strip allocation (`0` to `32` channels, Mono or Stereo) plus `4` stereo AUX buses and a dedicated Master L/R bus with scene persistence.
- **Dedicated Section Navigation**: Quick access across 7 console views: **Mixer**, **Channel**, **EQ**, **Dynamics**, **FX**, **Routing**, and **System**.
- **Interactive EQ & Real-Time Spectrum Analyzer (RTA)**:
  - **4-Band Parametric EQ**: Drag-and-drop filter nodes (Low Shelf, Bell, High Shelf, HPF, LPF) with real-time frequency response curves.
  - **31-Band Graphic EQ**: Standard 1/3-octave ISO faders with copy/reset tools.
  - **4096-Point Hann-Windowed RTA**: Live Pre-EQ / Post-EQ spectrum overlay supporting 31-Band ISO, 63-Band, and 64-Bin smoothed FFT views with peak hold and selectable ballistics (Fast, Medium, Slow).
- **Dynamics & Master Output Processing**:
  - **Channel & Bus Dynamics**: Noise Gate / Expander with hold/release state telemetry and sidechain filter, Studio Compressor with interactive transfer curve and knee visualizer, and Brickwall Lookahead Limiter.
  - **Output Time Alignment**: Dedicated Master L/R speaker alignment delay (up to 500 ms with linkable L/R channels).
- **Semantic Theme Engine**: Built-in theme manager with 5 presets (*Graphite*, *Lexicon-Inspired*, *Studio Blue*, *Monochrome*, *Custom*) and a live 12-role RGB/Hex color customizer with hardware widget preview.

### 🔌 Native Plugin & Neural Amp Hosting
- **8-Slot FX Rack**: Up to 8 ordered processor slots per input channel or AUX bus (Insert or Send/Return mode) with live Pre-FX and Post-FX stereo metering, reordering, bypass, and per-slot telemetry.
- **Built-in Studio Processors** (with dedicated custom hardware faceplates):
  - **Plate & Hall Reverb**: Room, Hall, Plate, and Ambience acoustic templates with configurable Pre-Delay, decay, damping, diffusion, stereo width, and HPF/LPF filters.
  - **Stereo Dual-Tap Echo Delay**: Tempo-syncable tap delay with feedback and filtering.
  - **Auto Feedback Suppressor (AFS)**: Live 8-notch automatic feedback detection and narrow-band suppression with per-notch status telemetry.
  - **Rack Dynamics**: Studio Compressor (with vintage VU gain-reduction meter), Expander / Gate (with rolling history strip chart), and Brickwall Limiter (with dual input/GR needle meters).
- **Native LV2 Host**: In-process LV2 plugin discovery and hosting (`manifest.ttl` parsing, control/audio ports) including the bundled in-tree `prismix_overdrive.lv2` plugin.
- **NAM Lite Neural Modeling**: Integrated neural guitar amplifier and cabinet emulation supporting `.nam` models (bundled with *British Lead* and *Clean Tweed* models).
- **Pedalboard Presets**: Save, load, and audition complete multi-unit effect chains.

### 📦 PatchStorage LV2 Catalog Integration
- **Direct Plugin Discovery**: Browse, search, filter by category/compatibility state, and sort community LV2 plugins directly from the FX modal via the PatchStorage API.
- **Artwork & Responsive Grid**: Asynchronous thumbnail decoding and adaptive card grid reflowing down to compact 800×600 viewports.
- **One-Click Installation**: Download, validate, unpack, and load LV2 bundles directly into channel FX racks.

### 🌐 Full Web Remote Console & Control API
- **Complete 7-View Browser Console**: Full industrial web mirror of the hardware console (**Mixer**, **Channel**, **EQ**, **Dynamics**, **FX**, **Routing**, and **System**) with responsive layout, interactive curves, RTA spectrum display, and live LED/VU metering.
- **Embedded HTTP & SSE Server**: Non-blocking HTTP JSON API (`/api/state`, `/api/telemetry`, `/api/catalog`, `/api/command`) and Server-Sent Events (`/api/events`) broadcast server on port `8888` (configurable via `PRISMIX_WEB_PORT`).
- **Bidirectional Real-Time Sync**: Instant state, fader, EQ/dynamics, FX rack, PatchStorage, and metering synchronization between the native LVGL console and connected web browsers.
- **Local-First Binding**: Binds to `127.0.0.1` by default and dynamically rebinds to `0.0.0.0` only when LAN access is explicitly enabled by an authenticated user.

### 🔒 Security, Network & System Authority
- **Shared PIN & Session Authority**: Unified security policy across the LVGL console and Web UI using salted `yescrypt` (`libxcrypt`) PIN hashing, per-client rate limiting, and in-memory 256-bit `HttpOnly` session tokens.
- **Console Lock & LAN Policy**: Instant console locking and explicit LAN remote-access toggling from either the System screen or Web UI.
- **System & Wi-Fi Management**: Asynchronous `nmcli` NetworkManager Wi-Fi scanning/connection and safe system power controls (Restart / Shutdown with active audio stream teardown).

### 🔊 Realtime Audio Engine & DSP Test Bench
- **Cross-Platform Audio Backends**:
  - **WASAPI** (Windows): Shared and Exclusive modes, event-driven streaming, device enumeration, sample-rate conversion (Sinc & Linear), and File Playback, Audio Interface Input, and Windows System Loopback sources.
  - **JACK** (Linux/macOS): Native JACK client streaming and port routing.
  - **Simulation Mode**: Built-in multi-channel synthetic signal generator for offline workflow and UI verification without audio hardware.
- **Standalone DSP Test Bench (`dsp_test_bench`)**: Dedicated CLI and Win32 GUI profiler for offline WAV processing, synthetic signal generation, 22-point DSP correctness verification, multi-rate FX/WASAPI benchmarking, CPU workload telemetry, and JSON/CSV/TXT report export.

---

## Developer Quickstart (`Canary`)

### Clone & Checkout Canary
```bash
git clone -b Canary https://github.com/Aristyo2006/PRISMIX.git
cd PRISMIX
```

### Prerequisites
- **C++ Compiler**: Clang 14+, GCC 12+, or MSVC 2022+ (C++20 required for `AudioCore` console; C++17 for `audiocore` engine library)
- **CMake**: Version 3.20 or newer
- **SDL2**: Development headers and runtime libraries (automatically fetched on Windows/MSVC)
- **zlib**: Required for PNG decoding and archive unpacking
- **libxcrypt**: Required for `yescrypt` PIN hashing (`pkg-config` package `libxcrypt`)
- **JACK Audio Connection Kit** *(optional, for Linux/macOS live audio I/O)*: `libjack-jackd2-dev`

#### Ubuntu / Debian Dependencies
```bash
sudo apt update
sudo apt install build-essential cmake pkg-config libsdl2-dev zlib1g-dev libcrypt-dev libjack-jackd2-dev
```

---

### Building & Debugging

#### Linux / macOS (Debug / Development Build)
```bash
# Configure with Debug symbols for Canary testing
cmake -B build-linux -DCMAKE_BUILD_TYPE=Debug

# Compile all targets (AudioCore, prismix symlink, dsp_test_bench, prismix_overdrive, and tests)
cmake --build build-linux -j$(nproc)
```

#### Windows (PowerShell / Visual Studio)
```powershell
# Configure build
cmake -B build

# Compile Debug or Release
cmake --build build --config Debug
```

---

### Running & Verification

```bash
# Launch the mixer console (or ./build-linux/prismix on Unix)
./build-linux/AudioCore

# Open the Web Remote Console in your browser:
# http://127.0.0.1:8888 (or custom port via PRISMIX_WEB_PORT=8888)
```

#### Console Command-Line Options (`AudioCore`)
| Option | Description |
|---|---|
| `--width <w>` | Set custom window width (default: `1920`) |
| `--height <h>` | Set custom window height (default: `1080`) |
| `--zoom <factor>` | Set display zoom scale factor (auto-scaled on screens smaller than 1920×1080) |
| `--real-audio-ui` | Start directly in Real Audio hardware mode |
| `--channel-setup-preview` | Open the channel setup modal on startup |
| `--fx-add-modal` | Open the FX Add Plugin / Preset modal on startup |
| `--patchstorage-tab` | Open directly to the PatchStorage catalog tab in the FX modal |
| `--qa-test` | Run the automated LVGL stability & QA test suite and write `qa_test_results.txt` |
| `--navigation-test` | Run automated Real Audio / Simulation tab navigation verification |
| `--screenshot <path>` | Capture a BMP screenshot of the active viewport and exit |
| `--screenshot-tab <0..6>` | Navigate to a specific console tab (`0` = Mixer .. `6` = System) for screenshot capture |
| `--dump-screens` | Render and capture verification screenshots across all console tabs and modals |

#### DSP Test Bench & Verification Suites
Before merging changes from `Canary` into `main`, verify that all unit tests, DSP correctness checks, and UI stress tests pass:

```bash
# 1. Run all registered CTest unit & integration suites
ctest --test-dir build-linux --output-on-failure

# 2. Run the complete LVGL UI & Engine QA stress suite
./build-linux/AudioCore --qa-test

# 3. Run the DSP Test Bench self-test, 22-point correctness suite, and benchmark
./build-linux/tools/dsp_test_bench/dsp_test_bench --run-all
```

---

## License

This project is licensed under the GNU General Public License v3.0. See [LICENSE](LICENSE) for details.

