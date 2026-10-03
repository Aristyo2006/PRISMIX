# AudioCore

Professional digital audio mixer UI simulator built with LVGL for a native Windows desktop.

The interface runs at 1920 x 1080 and provides a mixer with eight input channels plus stereo master, channel processing, four band EQ, compressor and gate, effects, routing, and system views. Its deterministic mock engine generates signal and dynamics activity. No audio device is opened and the UI identifies the engine as MOCK.

## Technology

- C++20 and CMake
- LVGL 9.4.0, pinned to commit `c016f72d4c125098287be5e83c0f1abed4706ee5`
- SDL2 2.32.10 official MSVC development package, fetched with SHA-256 verification
- MSVC 19.50.35726.0 (Visual Studio 18 / 2026) and Windows SDK 10.0.26100.0
- CMake 4.2.3

LVGL is fetched from the official repository when `lib/lvgl` is absent. SDL2 is fetched from the official SDL release and its DLL is copied beside the executable.

## Build

From a Visual Studio Developer environment or a Windows terminal where CMake detects Visual Studio:

```powershell
cmake -S . -B build
cmake --build build --config Release
```

## Run

The executable is `build/Release/AudioCore.exe`. The verified local Release build is at `build-final/Release/AudioCore.exe`.

## Controls

- Drag rotary controls vertically or around the arc; double click to reset.
- Drag channel faders vertically; double click to reset to 0 dB.
- Select the Mixer, Channel, EQ, Dynamics, FX, Routing, and System views from the bottom function keys.
- Drag the active EQ node to change its frequency and gain. EQ parameters also have rotary controls.
- Compressor controls update the transfer curve and mock gain reduction. Gate controls update OPEN/CLOSED status.
- Mute, solo, switches, route selectors, and routing matrix crosspoints update mock state.

## Architecture

```text
LVGL UI -> UI State -> Mock Audio Backend
```

The UI communicates through model state. The mock backend updates meter levels, clipping state, gate status, and compressor gain reduction. It is a display simulator, not a real-time audio engine.

Future hardware integration can introduce a Control API between the UI and audio engine:

```text
LVGL -> Control API -> Audio Engine -> JACK -> ALSA -> S905X
```
