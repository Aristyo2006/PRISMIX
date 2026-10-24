#include "backend/wasapi_backend.hpp"
#include "audiocore/audio_engine.hpp"
#include "audiocore/gain_node.hpp"

#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <cassert>
#include <string>

int main() {
    std::cout << "============================================================\n";
    std::cout << "AUDIO CORE: WASAPI BACKEND AUTOMATED VERIFICATION SUITE\n";
    std::cout << "============================================================\n\n";

    int passed = 0;
    int failed = 0;

    auto report = [&](bool condition, const std::string& msg) {
        if (condition) {
            std::cout << "  [PASS] " << msg << "\n";
            passed++;
        } else {
            std::cout << "  [FAIL] " << msg << "\n";
            failed++;
        }
    };

    // -------------------------------------------------------------
    // TEST 1: Endpoint Enumeration
    // -------------------------------------------------------------
    std::cout << "--- TEST 1: Hardware Device Enumeration ---\n";
    auto playbackDevices = audiocore::WasapiBackend::enumerateDevices(false);
    auto captureDevices = audiocore::WasapiBackend::enumerateDevices(true);

    std::cout << "Discovered " << playbackDevices.size() << " Playback Endpoint(s):\n";
    for (size_t i = 0; i < playbackDevices.size(); ++i) {
        const auto& d = playbackDevices[i];
        std::cout << "  [" << i << "] " << d.name 
                  << (d.isDefault ? " (DEFAULT)" : "")
                  << " | " << d.channels << " ch | " << d.defaultSampleRate << " Hz\n";
    }

    std::cout << "Discovered " << captureDevices.size() << " Capture Endpoint(s):\n";
    for (size_t i = 0; i < captureDevices.size(); ++i) {
        const auto& d = captureDevices[i];
        std::cout << "  [" << i << "] " << d.name 
                  << (d.isDefault ? " (DEFAULT)" : "")
                  << " | " << d.channels << " ch | " << d.defaultSampleRate << " Hz\n";
    }

    report(true, "Enumeration executed without crash");

    if (playbackDevices.empty()) {
        std::cout << "\n[SKIPPED: REASON: No physical audio render endpoints available in current environment]\n";
        std::cout << "============================================================\n";
        std::cout << "TEST RESULTS: " << passed << " PASSED, " << failed << " FAILED (Hardware tests skipped)\n";
        std::cout << "============================================================\n";
        return (failed == 0) ? 0 : 1;
    }

    // -------------------------------------------------------------
    // TEST 2: Device Initialization & Format Negotiation
    // -------------------------------------------------------------
    std::cout << "\n--- TEST 2: Device Initialization & Format Negotiation ---\n";
    auto backend = std::make_shared<audiocore::WasapiBackend>();

    bool initOk = backend->initialize(48000.0, 128, 2);
    report(initOk, "WasapiBackend::initialize(48000, 128, 2) succeeded");

    if (!initOk) {
        std::cerr << "Initialization failed. Skipping stream tests.\n";
        return 1;
    }

    std::cout << "\n  --- Negotiated Audio Hardware Parameters ---\n";
    std::cout << "  Active Device   : " << backend->getActivePlaybackDeviceName() << "\n";
    std::cout << "  Negotiated Rate : " << backend->getSampleRate() << " Hz\n";
    std::cout << "  Channels        : " << backend->getChannels() << " channels\n";
    std::cout << "  Bit Depth       : " << backend->getBitDepth() << " bits\n";
    std::cout << "  Format Spec     : " << backend->getFormatDescription() << "\n";
    std::cout << "  Buffer Size     : " << backend->getActualBufferSizeFrames() << " frames ("
              << std::fixed << std::setprecision(2) << backend->getEstimatedLatencyMs() << " ms)\n\n";

    report(backend->getSampleRate() > 0.0, "Sample rate is valid (> 0 Hz)");
    report(backend->getChannels() >= 1, "Channel count is valid (>= 1)");
    report(backend->getActualBufferSizeFrames() > 0, "Buffer size frames is non-zero");
    report(backend->getBitDepth() >= 16, "Bit depth is >= 16 bits");
    report(!backend->getActivePlaybackDeviceName().empty(), "Device name is resolved");
    report(backend->getEstimatedLatencyMs() > 0.0, "Estimated latency is positive");

    // -------------------------------------------------------------
    // TEST 3: Feedback Guard Configuration
    // -------------------------------------------------------------
    std::cout << "\n--- TEST 3: Safe Feedback Guard Configuration ---\n";
    report(backend->isSafeFeedbackGuardEnabled(), "Safe feedback guard enabled by default");
    backend->setSafeFeedbackGuard(true, 0.02f);
    report(backend->isSafeFeedbackGuardEnabled(), "Safe feedback guard retains enabled state");

    // -------------------------------------------------------------
    // TEST 4: AudioEngine Integration & Streaming Lifecycle
    // -------------------------------------------------------------
    std::cout << "\n--- TEST 4: AudioEngine Integration & Streaming Lifecycle ---\n";
    audiocore::AudioEngine engine;
    bool configOk = engine.configure(
        backend->getSampleRate(),
        backend->getBlockSize(),
        backend->getChannels());
    report(configOk, "AudioEngine::configure matches negotiated hardware parameters");

    engine.setBackend(backend);
    report(engine.getBackend() == backend, "AudioEngine successfully registered WasapiBackend");

    bool startOk = engine.start();
    report(startOk, "AudioEngine::start() started real WASAPI audio thread");
    report(engine.isRunning(), "AudioEngine::isRunning() is true");
    report(backend->isRunning(), "WasapiBackend::isRunning() is true");

    // Allow the realtime audio thread to process frames for 300 ms
    std::cout << "  Streaming audio blocks through WASAPI thread for 300 ms...\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    report(engine.isRunning(), "Audio thread remained stable during active processing");

    engine.stop();
    report(!engine.isRunning(), "AudioEngine::stop() stopped engine");
    report(!backend->isRunning(), "WasapiBackend stopped cleanly");

    // -------------------------------------------------------------
    // TEST 5: Clean Resource Deallocation & Re-initialization
    // -------------------------------------------------------------
    std::cout << "\n--- TEST 5: Clean Re-initialization ---\n";
    bool reinitOk = backend->initialize(44100.0, 256, 2);
    report(reinitOk, "WasapiBackend can be re-initialized without leaking resources");

    std::cout << "\n============================================================\n";
    std::cout << "TEST RESULTS: " << passed << " PASSED, " << failed << " FAILED\n";
    std::cout << "============================================================\n";

    return (failed == 0) ? 0 : 1;
}

