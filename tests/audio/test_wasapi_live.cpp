#include "backend/wasapi_backend.hpp"
#include "audiocore/audio_engine.hpp"
#include "audiocore/gain_node.hpp"
#include "audiocore/audio_node.hpp"

#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <cmath>
#include <string>
#include <vector>

namespace {

class ToneGeneratorNode : public audiocore::AudioNode {
public:
    ToneGeneratorNode(double sampleRate, double freqHz = 440.0, float gainLinear = 0.25f)
        : sample_rate_(sampleRate), freq_(freqHz), gain_(gainLinear) {}

    void process(const audiocore::AudioBuffer& input, audiocore::AudioBuffer& output, uint32_t frames) override {
        (void)input;
        for (uint32_t i = 0; i < frames; ++i) {
            float s = static_cast<float>(std::sin(phase_)) * gain_;
            phase_ += 2.0 * 3.14159265358979323846 * freq_ / sample_rate_;
            if (phase_ >= 2.0 * 3.14159265358979323846) {
                phase_ -= 2.0 * 3.14159265358979323846;
            }
            for (uint32_t ch = 0; ch < output.getChannelCount(); ++ch) {
                output.getChannelData(ch)[i] = s;
            }
        }
    }

    void reset() override { phase_ = 0.0; }

private:
    double sample_rate_{48000.0};
    double freq_{440.0};
    float gain_{0.25f};
    double phase_{0.0};
};

void printUsage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n"
              << "Options:\n"
              << "  --list             List available audio endpoints and exit\n"
              << "  --tone             Generate a 440 Hz sine test tone (-12 dB) to verify speaker output\n"
              << "  --duplex           Run duplex microphone capture -> engine -> speaker loopback\n"
              << "  --safe             Enable safe feedback guard for loopback (default: ON)\n"
              << "  --no-safe          Disable safe feedback guard (USE HEADPHONES ONLY)\n"
              << "  --gain <dB>        Master gain in dB (default: 0 dB, tone default: -12 dB)\n"
              << "  --input-id <id>    Select an explicit capture endpoint ID\n"
              << "  --output-id <id>   Select an explicit render endpoint ID\n"
              << "  --rate <Hz>        Request an engine rate (default: 48000)\n"
              << "  --buffer <frames>  Request a processing block (default: 128)\n"
              << "  --duration <sec>   Run duration in seconds (default: 3 seconds)\n\n";
}

} // namespace

int main(int argc, char* argv[]) {
    std::cout << "============================================================\n";
    std::cout << "AUDIO CORE: WASAPI LIVE HARDWARE INTERACTIVE TEST\n";
    std::cout << "============================================================\n\n";

    bool listOnly = false;
    bool playTone = false;
    bool duplex = false;
    bool safeGuard = true;
    float gainDb = 0.0f;
    int durationSec = 3;
    double requestedRate = 48000.0;
    uint32_t requestedBlock = 128;
    std::string inputId;
    std::string outputId;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--list") {
            listOnly = true;
        } else if (arg == "--tone") {
            playTone = true;
        } else if (arg == "--duplex") {
            duplex = true;
        } else if (arg == "--safe") {
            safeGuard = true;
        } else if (arg == "--no-safe") {
            safeGuard = false;
        } else if (arg == "--gain" && i + 1 < argc) {
            gainDb = std::stof(argv[++i]);
        } else if (arg == "--duration" && i + 1 < argc) {
            durationSec = std::stoi(argv[++i]);
        } else if (arg == "--input-id" && i + 1 < argc) {
            inputId = argv[++i];
        } else if (arg == "--output-id" && i + 1 < argc) {
            outputId = argv[++i];
        } else if (arg == "--rate" && i + 1 < argc) {
            requestedRate = std::stod(argv[++i]);
        } else if (arg == "--buffer" && i + 1 < argc) {
            requestedBlock = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        }
    }

    if (listOnly) {
        auto playDevs = audiocore::WasapiBackend::enumerateDevices(false);
        auto capDevs = audiocore::WasapiBackend::enumerateDevices(true);

        std::cout << "[Playback Endpoints (" << playDevs.size() << ")]\n";
        for (size_t i = 0; i < playDevs.size(); ++i) {
            std::cout << "  [" << i << "] " << playDevs[i].name 
                      << (playDevs[i].isDefault ? " [DEFAULT]" : "")
                      << " | ID: " << playDevs[i].id << "\n";
        }

        std::cout << "\n[Capture Endpoints (" << capDevs.size() << ")]\n";
        for (size_t i = 0; i < capDevs.size(); ++i) {
            std::cout << "  [" << i << "] " << capDevs[i].name 
                      << (capDevs[i].isDefault ? " [DEFAULT]" : "")
                      << " | ID: " << capDevs[i].id << "\n";
        }
        return 0;
    }

    if (!playTone && !duplex) {
        // Default mode is tone playback for audible confirmation
        playTone = true;
        gainDb = -12.0f;
    }

    if (duplex) {
        std::cout << "************************************************************\n";
        std::cout << "* [WARNING] LIVE DUPLEX CAPTURE & LOOPBACK TEST            *\n";
        std::cout << "* Acoustic feedback howl may occur if using open speakers! *\n";
        std::cout << "* Recommendations:                                         *\n";
        std::cout << "* - Use headphones                                         *\n";
        std::cout << "* - Safe feedback guard is " << (safeGuard ? "ACTIVE (-26 dB)" : "OFF") << "                *\n";
        std::cout << "************************************************************\n\n";
    }

    auto backend = std::make_shared<audiocore::WasapiBackend>();
    backend->setSafeFeedbackGuard(safeGuard, 0.05f); // -26 dB
    if (!inputId.empty()) backend->setCaptureDeviceId(inputId);
    if (!outputId.empty()) backend->setPlaybackDeviceId(outputId);

    if (duplex) {
        backend->setStreamMode(audiocore::WasapiStreamMode::Duplex);
        backend->setAudioSourceType(audiocore::AudioSourceType::AudioInterface);
    } else {
        backend->setStreamMode(audiocore::WasapiStreamMode::PlaybackOnly);
    }

    if (!backend->initialize(requestedRate, requestedBlock, 2)) {
        std::cerr << "[ERROR] Could not initialize WASAPI endpoint. No audio device available?\n";
        return 1;
    }

    std::cout << "Active Hardware Output : " << backend->getActivePlaybackDeviceName() << "\n";
    if (duplex) {
        std::cout << "Active Hardware Input  : " << backend->getActiveCaptureDeviceName() << "\n";
    }
    std::cout << "Negotiated Sample Rate : " << backend->getSampleRate() << " Hz\n";
    std::cout << "Negotiated Channels    : " << backend->getChannels() << " ch\n";
    std::cout << "Negotiated Format      : " << backend->getFormatDescription() << "\n";
    std::cout << "Requested Rate/Block  : " << requestedRate << " Hz / " << requestedBlock << " frames\n";
    if (duplex) {
        std::cout << "Capture Rate/Channels  : " << backend->getCaptureSampleRate() << " Hz / "
                  << backend->getCaptureChannels() << " ch | Capture Buffer: "
                  << backend->getCaptureBufferSizeFrames() << " frames | Format: "
                  << backend->getCaptureFormatDescription() << "\n";
    }
    std::cout << "Hardware Buffer Size   : " << backend->getActualBufferSizeFrames() << " frames ("
              << std::fixed << std::setprecision(2) << backend->getEstimatedLatencyMs() << " ms)\n\n";

    audiocore::AudioEngine engine;
    engine.configure(backend->getSampleRate(), requestedBlock, backend->getChannels());

    auto& graph = engine.getGraph();
    graph.clearNodes();

    if (playTone) {
        std::cout << "--> Inserting 440 Hz Sine Tone Generator into AudioGraph\n";
        graph.addNode(std::make_shared<ToneGeneratorNode>(backend->getSampleRate(), 440.0, 0.25f));
    }

    auto masterGain = std::make_shared<audiocore::GainNode>(gainDb);
    graph.addNode(masterGain);
    std::cout << "--> Master Gain set to: " << gainDb << " dB\n";

    engine.setBackend(backend);

    std::cout << "\nStarting audio streaming for " << durationSec << " seconds...\n";
    if (!engine.start()) {
        std::cerr << "[ERROR] Failed to start AudioEngine!\n";
        return 1;
    }

    for (int sec = 0; sec < durationSec; ++sec) {
        std::cout << "  [Streaming] " << (sec + 1) << " / " << durationSec << " s...\n";
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    const auto diagnostics = backend->getDiagnostics();
    std::cout << "\nWASAPI diagnostics:\n"
              << "  Capture/DSP/Render: " << (diagnostics.captureOperational ? "RUNNING" : "STOPPED") << "/"
              << (diagnostics.dspOperational ? "RUNNING" : "STOPPED") << "/"
              << (diagnostics.renderOperational ? "RUNNING" : "STOPPED") << "\n"
              << "  Callbacks: " << diagnostics.totalCallbacks << " | Captured frames: " << diagnostics.totalFramesCaptured
              << " | DSP frames: " << diagnostics.totalFramesProcessed
              << " | Submitted frames: " << diagnostics.totalFramesSubmitted << "\n"
              << "  Callback us avg/P95/P99/max: " << diagnostics.avgCallbackTimeUs << "/"
              << diagnostics.p95CallbackTimeUs << "/" << diagnostics.p99CallbackTimeUs << "/" << diagnostics.maxCallbackTimeUs
              << " | DSP avg/max: " << diagnostics.avgDspTimeUs << "/" << diagnostics.maxDspTimeUs << "\n"
              << "  Render underruns: " << diagnostics.renderUnderruns << " | Capture under/over: "
              << diagnostics.captureUnderflows << "/" << diagnostics.captureOverruns
              << " | Deadline misses: " << diagnostics.deadlineMisses << " | Buffer errors: "
              << (diagnostics.getBufferFailures + diagnostics.releaseBufferFailures + diagnostics.captureErrors)
              << " | Wait errors/timeouts: " << diagnostics.eventWaitErrors << "/" << diagnostics.eventWaitTimeouts << "\n"
              << "  Capture queue drops: " << diagnostics.totalCaptureFramesDropped << " | Clock drift: "
              << (diagnostics.clockDriftMeasurementValid
                    ? (std::to_string(static_cast<int>(std::round(diagnostics.clockDriftPpm))) + " ppm")
                    : "not measured (<30 s baseline window)")
              << " | SRC correction: " << std::fixed << std::setprecision(1)
              << diagnostics.captureClockCorrectionPpm << " ppm | Capture queue: "
              << std::setprecision(1) << diagnostics.bufferOccupancyPercent << "%\n";
    if (!diagnostics.lastErrorMessage.empty()) std::cout << "  Last error: " << diagnostics.lastErrorMessage << "\n";

    std::cout << "\nStopping audio engine...\n";
    engine.stop();
    std::cout << "[SUCCESS] Audio stream completed and cleanly stopped.\n";

    return 0;
}
