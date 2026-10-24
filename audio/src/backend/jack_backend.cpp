#include "jack_backend.hpp"
#include "audiocore/audio_engine.hpp"

#include <jack/jack.h>
#include <jack/statistics.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#endif

namespace audiocore {

namespace {

constexpr uint32_t kMaxPreallocatedFrames = 8192;

// C-style trampolines for JACK callbacks
int jackProcessCallbackTrampoline(jack_nframes_t nframes, void* arg) {
    if (!arg) return 0;
    return static_cast<JackBackend*>(arg)->processCallback(nframes);
}

int jackBufferSizeCallbackTrampoline(jack_nframes_t nframes, void* arg) {
    if (!arg) return 0;
    return static_cast<JackBackend*>(arg)->bufferSizeCallback(nframes);
}

int jackSampleRateCallbackTrampoline(jack_nframes_t nframes, void* arg) {
    if (!arg) return 0;
    return static_cast<JackBackend*>(arg)->sampleRateCallback(nframes);
}

int jackXrunCallbackTrampoline(void* arg) {
    if (!arg) return 0;
    return static_cast<JackBackend*>(arg)->xrunCallback();
}

void jackShutdownCallbackTrampoline(void* arg) {
    if (!arg) return;
    static_cast<JackBackend*>(arg)->shutdownCallback();
}

} // anonymous namespace

struct JackBackend::Impl {
    jack_client_t* client{nullptr};
    std::string clientName{"AudioCore"};
    std::string selectedCaptureDevice{"system"};
    std::string selectedPlaybackDevice{"system"};

    std::vector<jack_port_t*> inPorts;
    std::vector<jack_port_t*> outPorts;
    std::vector<const float*> inPtrs;
    std::vector<float*> outPtrs;

    std::vector<uint32_t> captureChannelMap;

    AudioBuffer engineBlockIn;
    AudioBuffer engineBlockOut;
    uint32_t preallocatedFrames{kMaxPreallocatedFrames};

    double sampleRate{48000.0};
    uint32_t blockSize{128};
    uint32_t requestedChannels{2};
    uint32_t actualCaptureChannels{2};
    uint32_t actualPlaybackChannels{2};

    double requestedSampleRate{48000.0};
    uint32_t requestedBlockSize{128};

    bool safeFeedbackGuard{false};
    float maxLinearGain{0.05f};

    std::atomic<bool> running{false};
    std::atomic<bool> serverShutdown{false};

    // Telemetry & Diagnostics (realtime-safe atomics)
    std::atomic<uint64_t> totalCallbacks{0};
    std::atomic<uint64_t> totalFrames{0};
    std::atomic<uint32_t> xrunCount{0};
    std::atomic<uint64_t> lastDspTimeUs{0};
    std::atomic<uint64_t> avgDspTimeUs{0};
    std::atomic<bool> captureOperational{false};
    std::atomic<bool> dspOperational{false};
    std::atomic<bool> renderOperational{false};

    mutable std::mutex errorMutex;
    std::string lastErrorMessage;
#if !defined(_WIN32)
    pid_t spawnedJackPid{0};
#endif

    void setErrorMessage(const std::string& msg) {
        std::lock_guard<std::mutex> lock(errorMutex);
        lastErrorMessage = msg;
    }

    std::string getErrorMessage() const {
        std::lock_guard<std::mutex> lock(errorMutex);
        return lastErrorMessage;
    }
};

JackBackend::JackBackend() : impl_(std::make_unique<Impl>()) {}

JackBackend::~JackBackend() {
    stop();
}

bool JackBackend::setClientName(const std::string& name) {
    if (isRunning()) return false;
    impl_->clientName = name.empty() ? "AudioCore" : name;
    return true;
}

std::string JackBackend::getClientName() const {
    return impl_->clientName;
}

bool JackBackend::setCaptureDevice(const std::string& deviceId) {
    if (isRunning()) return false;
    impl_->selectedCaptureDevice = deviceId.empty() ? "system" : deviceId;
    return true;
}

bool JackBackend::setPlaybackDevice(const std::string& deviceId) {
    if (isRunning()) return false;
    impl_->selectedPlaybackDevice = deviceId.empty() ? "system" : deviceId;
    return true;
}

bool JackBackend::setCaptureChannelMap(const std::vector<uint32_t>& physicalChannels) {
    if (isRunning()) return false;
    impl_->captureChannelMap = physicalChannels;
    return true;
}

void JackBackend::setSafeFeedbackGuard(bool enabled, float maxGain) {
    impl_->safeFeedbackGuard = enabled;
    impl_->maxLinearGain = std::clamp(maxGain, 0.001f, 1.0f);
}

bool JackBackend::isSafeFeedbackGuardEnabled() const {
    return impl_->safeFeedbackGuard;
}

bool JackBackend::setRequestedSampleRate(double rate) {
    if (isRunning()) return false;
    impl_->requestedSampleRate = rate > 0.0 ? rate : 48000.0;
    return true;
}

double JackBackend::getRequestedSampleRate() const {
    return impl_->requestedSampleRate;
}

bool JackBackend::setRequestedBlockSize(uint32_t frames) {
    if (isRunning()) return false;
    impl_->requestedBlockSize = frames > 0 ? frames : 128;
    return true;
}

uint32_t JackBackend::getRequestedBlockSize() const {
    return impl_->requestedBlockSize;
}

bool JackBackend::initialize(
    double sampleRate,
    uint32_t blockSize,
    uint32_t channels)
{
    stop();

    impl_->requestedChannels = std::max(2u, channels);
    impl_->serverShutdown.store(false, std::memory_order_release);

    jack_status_t status = static_cast<jack_status_t>(0);
    jack_options_t options = JackNullOption;

    impl_->client = jack_client_open(impl_->clientName.c_str(), options, &status, nullptr);

#if !defined(_WIN32)
    // If connection failed because server is not running, attempt to autostart jackd with requested parameters
    if (!impl_->client && ((status & JackServerFailed) || !impl_->client)) {
        std::string alsaDev = impl_->selectedPlaybackDevice;
        if (alsaDev.empty() || alsaDev == "system") {
            alsaDev = impl_->selectedCaptureDevice;
        }
        if (alsaDev.empty() || alsaDev == "system") {
            // Check if Card 1 (USB Audio) exists in /proc/asound/cards
            std::ifstream procCards("/proc/asound/cards");
            if (procCards.is_open()) {
                std::string line;
                while (std::getline(procCards, line)) {
                    if (line.find("USB-Audio") != std::string::npos || line.find("[Device") != std::string::npos) {
                        alsaDev = "hw:1";
                        break;
                    }
                }
            }
            if (alsaDev.empty() || alsaDev == "system") {
                alsaDev = "hw:0";
            }
        }

        const int reqRate = static_cast<int>(impl_->requestedSampleRate > 0 ? impl_->requestedSampleRate : (sampleRate > 0 ? sampleRate : 48000.0));
        const int reqFrames = static_cast<int>(impl_->requestedBlockSize > 0 ? impl_->requestedBlockSize : (blockSize > 0 ? blockSize : 128));

        pid_t pid = fork();
        if (pid == 0) {
            setenv("JACK_NO_AUDIO_RESERVATION", "1", 1);
            int devNull = open("/dev/null", O_RDWR);
            if (devNull >= 0) {
                dup2(devNull, STDOUT_FILENO);
                dup2(devNull, STDERR_FILENO);
                close(devNull);
            }
            execlp("jackd", "jackd", "-d", "alsa", "-d", alsaDev.c_str(),
                   "-r", std::to_string(reqRate).c_str(),
                   "-p", std::to_string(reqFrames).c_str(),
                   "-n", "2", "-S", nullptr);
            _exit(127);
        } else if (pid > 0) {
            impl_->spawnedJackPid = pid;
            for (int i = 0; i < 20; ++i) {
                struct timespec ts{0, 100000000}; // 100ms
                nanosleep(&ts, nullptr);
                int exitStatus = 0;
                pid_t wp = waitpid(pid, &exitStatus, WNOHANG);
                if (wp == pid) {
                    impl_->spawnedJackPid = 0;
                    break;
                }
                impl_->client = jack_client_open(impl_->clientName.c_str(), options, &status, nullptr);
                if (impl_->client) break;
            }
        }
    }
#endif

    if (!impl_->client) {
        std::string err = "Failed to connect to JACK server";
        if (status & JackServerFailed) err += " (server not running or cannot be started)";
        if (status & JackShmFailure) err += " (shared memory failure)";
        if (status & JackVersionError) err += " (JACK version mismatch)";
        impl_->setErrorMessage(err);
        return false;
    }

    // Retrieve negotiated JACK parameters
    impl_->sampleRate = static_cast<double>(jack_get_sample_rate(impl_->client));
    impl_->blockSize = static_cast<uint32_t>(jack_get_buffer_size(impl_->client));

    // Register process and notification callbacks
    jack_set_process_callback(impl_->client, jackProcessCallbackTrampoline, this);
    jack_set_buffer_size_callback(impl_->client, jackBufferSizeCallbackTrampoline, this);
    jack_set_sample_rate_callback(impl_->client, jackSampleRateCallbackTrampoline, this);
    jack_set_xrun_callback(impl_->client, jackXrunCallbackTrampoline, this);
    jack_on_shutdown(impl_->client, jackShutdownCallbackTrampoline, this);

    // Register input ports
    // Count how many capture ports are needed: at least 2, or according to strips/channels
    uint32_t numInputPorts = 2;
    if (!impl_->captureChannelMap.empty()) {
        for (uint32_t physical : impl_->captureChannelMap) {
            numInputPorts = std::max(numInputPorts, physical + 1);
        }
    }
    impl_->actualCaptureChannels = numInputPorts;

    impl_->inPorts.clear();
    impl_->inPorts.reserve(numInputPorts);
    for (uint32_t i = 0; i < numInputPorts; ++i) {
        std::string portName = "in_" + std::to_string(i + 1);
        jack_port_t* p = jack_port_register(impl_->client, portName.c_str(),
                                           JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
        if (!p) {
            impl_->setErrorMessage("Failed to register JACK input port: " + portName);
            jack_client_close(impl_->client);
            impl_->client = nullptr;
            return false;
        }
        impl_->inPorts.push_back(p);
    }

    // Register output ports (2 channels for Main L/R master render)
    uint32_t numOutputPorts = 2;
    impl_->actualPlaybackChannels = numOutputPorts;
    impl_->outPorts.clear();
    impl_->outPorts.reserve(numOutputPorts);
    for (uint32_t i = 0; i < numOutputPorts; ++i) {
        std::string portName = "out_" + std::to_string(i + 1);
        jack_port_t* p = jack_port_register(impl_->client, portName.c_str(),
                                           JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
        if (!p) {
            impl_->setErrorMessage("Failed to register JACK output port: " + portName);
            jack_client_close(impl_->client);
            impl_->client = nullptr;
            return false;
        }
        impl_->outPorts.push_back(p);
    }

    // Preallocate pointer scratch vectors
    impl_->inPtrs.resize(numInputPorts, nullptr);
    impl_->outPtrs.resize(numOutputPorts, nullptr);

    // Preallocate internal AudioBuffers to max capacity to guarantee zero allocation in process callback
    impl_->preallocatedFrames = std::max(kMaxPreallocatedFrames, impl_->blockSize * 4);
    impl_->engineBlockIn.resize(impl_->requestedChannels, impl_->preallocatedFrames);
    impl_->engineBlockOut.resize(std::max(2u, impl_->actualPlaybackChannels), impl_->preallocatedFrames);

    // Zero out preallocated buffers
    impl_->engineBlockIn.clear();
    impl_->engineBlockOut.clear();

    resetDiagnostics();
    impl_->setErrorMessage("");
    return true;
}

bool JackBackend::start() {
    if (!impl_->client) {
        impl_->setErrorMessage("Cannot start: JACK client not initialized.");
        return false;
    }

    if (impl_->running.load(std::memory_order_acquire)) {
        return true;
    }

    // Activate the JACK client to start processing callbacks
    int res = jack_activate(impl_->client);
    if (res != 0) {
        impl_->setErrorMessage("Failed to activate JACK client: code " + std::to_string(res));
        return false;
    }

    impl_->running.store(true, std::memory_order_release);

    // Handle port connections if auto-connect is requested
    if (autoConnect_) {
        // Disconnect any pre-existing direct capture->playback loopbacks to guarantee audio only routes through DSP
        const char** capPorts = jack_get_ports(impl_->client, nullptr, nullptr,
                                               JackPortIsPhysical | JackPortIsOutput);
        const char** playPorts = jack_get_ports(impl_->client, nullptr, nullptr,
                                                JackPortIsPhysical | JackPortIsInput);
        if (capPorts && playPorts) {
            for (uint32_t c = 0; capPorts[c] != nullptr; ++c) {
                for (uint32_t p = 0; playPorts[p] != nullptr; ++p) {
                    jack_disconnect(impl_->client, capPorts[c], playPorts[p]);
                }
            }
        }

        // Connect inputs to physical capture ports
        if (capPorts) {
            for (uint32_t i = 0; i < impl_->inPorts.size() && capPorts[i] != nullptr; ++i) {
                const char* ourIn = jack_port_name(impl_->inPorts[i]);
                if (!jack_port_connected_to(impl_->inPorts[i], capPorts[i])) {
                    jack_connect(impl_->client, capPorts[i], ourIn);
                }
            }
            jack_free(capPorts);
        }

        // Connect outputs to physical playback ports
        if (playPorts) {
            for (uint32_t i = 0; i < impl_->outPorts.size() && playPorts[i] != nullptr; ++i) {
                const char* ourOut = jack_port_name(impl_->outPorts[i]);
                if (!jack_port_connected_to(impl_->outPorts[i], playPorts[i])) {
                    jack_connect(impl_->client, ourOut, playPorts[i]);
                }
            }
            jack_free(playPorts);
        }
    }

    return true;
}

void JackBackend::stop() {
    if (impl_->running.exchange(false, std::memory_order_acq_rel)) {
        if (impl_->client && !impl_->serverShutdown.load(std::memory_order_relaxed)) {
            jack_deactivate(impl_->client);
        }
    }

    if (impl_->client) {
        jack_client_close(impl_->client);
        impl_->client = nullptr;
    }

#if !defined(_WIN32)
    if (impl_->spawnedJackPid > 0) {
        kill(impl_->spawnedJackPid, SIGTERM);
        int st = 0;
        for (int i = 0; i < 5; ++i) {
            if (waitpid(impl_->spawnedJackPid, &st, WNOHANG) == impl_->spawnedJackPid) {
                impl_->spawnedJackPid = 0;
                break;
            }
            struct timespec ts{0, 100000000};
            nanosleep(&ts, nullptr);
        }
        if (impl_->spawnedJackPid > 0) {
            kill(impl_->spawnedJackPid, SIGKILL);
            waitpid(impl_->spawnedJackPid, &st, 0);
            impl_->spawnedJackPid = 0;
        }
    }
#endif

    impl_->captureOperational.store(false, std::memory_order_relaxed);
    impl_->dspOperational.store(false, std::memory_order_relaxed);
    impl_->renderOperational.store(false, std::memory_order_relaxed);
}

bool JackBackend::isRunning() const {
    return impl_->running.load(std::memory_order_acquire);
}

double JackBackend::getSampleRate() const {
    return impl_->sampleRate;
}

uint32_t JackBackend::getBlockSize() const {
    return impl_->blockSize;
}

uint32_t JackBackend::getChannels() const {
    return impl_->actualPlaybackChannels;
}

uint32_t JackBackend::getCaptureChannels() const {
    return impl_->actualCaptureChannels;
}

uint32_t JackBackend::getActualBufferSizeFrames() const {
    return impl_->blockSize;
}

std::string JackBackend::getActivePlaybackDeviceName() const {
    return "JACK: " + impl_->selectedPlaybackDevice;
}

std::string JackBackend::getActiveCaptureDeviceName() const {
    return "JACK: " + impl_->selectedCaptureDevice;
}

std::string JackBackend::getActivePlaybackDeviceId() const {
    return impl_->selectedPlaybackDevice;
}

std::string JackBackend::getActiveCaptureDeviceId() const {
    return impl_->selectedCaptureDevice;
}

std::string JackBackend::getFormatDescription() const {
    return "32-bit float planar @ " + std::to_string(static_cast<int>(impl_->sampleRate)) + " Hz";
}

std::string JackBackend::getCaptureFormatDescription() const {
    return "32-bit float planar @ " + std::to_string(static_cast<int>(impl_->sampleRate)) + " Hz";
}

std::string JackBackend::getLastErrorMessage() const {
    return impl_->getErrorMessage();
}

JackBackend::RealtimeMetrics JackBackend::getRealtimeMetrics() const noexcept {
    RealtimeMetrics m;
    m.averageDspTimeUs = impl_->avgDspTimeUs.load(std::memory_order_relaxed);
    m.renderUnderruns = impl_->xrunCount.load(std::memory_order_relaxed);
    m.deadlineMisses = 0;
    m.captureOperational = impl_->captureOperational.load(std::memory_order_relaxed);
    m.dspOperational = impl_->dspOperational.load(std::memory_order_relaxed);
    m.renderOperational = impl_->renderOperational.load(std::memory_order_relaxed);
    return m;
}

void JackBackend::resetDiagnostics() {
    impl_->totalCallbacks.store(0, std::memory_order_relaxed);
    impl_->totalFrames.store(0, std::memory_order_relaxed);
    impl_->xrunCount.store(0, std::memory_order_relaxed);
    impl_->lastDspTimeUs.store(0, std::memory_order_relaxed);
    impl_->avgDspTimeUs.store(0, std::memory_order_relaxed);
}

int JackBackend::bufferSizeCallback(uint32_t nframes) noexcept {
    impl_->blockSize = nframes;
    return 0;
}

int JackBackend::sampleRateCallback(uint32_t nframes) noexcept {
    impl_->sampleRate = static_cast<double>(nframes);
    return 0;
}

int JackBackend::xrunCallback() noexcept {
    impl_->xrunCount.fetch_add(1, std::memory_order_relaxed);
    return 0;
}

void JackBackend::shutdownCallback() noexcept {
    impl_->serverShutdown.store(true, std::memory_order_release);
    impl_->running.store(false, std::memory_order_release);
    impl_->client = nullptr;
    impl_->setErrorMessage("JACK server disconnected unexpectedly.");
}

// ==============================================================================
// REALTIME AUDIO PROCESS CALLBACK
// ==============================================================================
// Strictly realtime safe:
// - ZERO heap allocations (preallocated storage only)
// - ZERO file/console/network I/O
// - ZERO blocking locks
// ==============================================================================
int JackBackend::processCallback(uint32_t nframes) noexcept {
    if (!impl_->running.load(std::memory_order_relaxed) || !impl_->client) {
        return 0;
    }

    // Retrieve port buffer pointers for all inputs
    const size_t inCount = impl_->inPorts.size();
    for (size_t i = 0; i < inCount; ++i) {
        impl_->inPtrs[i] = static_cast<const float*>(
            jack_port_get_buffer(impl_->inPorts[i], nframes));
    }

    // Retrieve port buffer pointers for all outputs
    const size_t outCount = impl_->outPorts.size();
    for (size_t i = 0; i < outCount; ++i) {
        impl_->outPtrs[i] = static_cast<float*>(
            jack_port_get_buffer(impl_->outPorts[i], nframes));
    }

    const uint32_t capacity = impl_->preallocatedFrames;
    const float inputGain = impl_->safeFeedbackGuard ? impl_->maxLinearGain : 1.0f;
    const uint32_t reqChannels = impl_->requestedChannels;

    // Process in preallocated chunks if nframes exceeds preallocated capacity
    uint32_t framesProcessed = 0;
    while (framesProcessed < nframes) {
        const uint32_t chunkSize = std::min(nframes - framesProcessed, capacity);

        // Map physical capture inputs to AudioCore engine channels
        if (!impl_->captureChannelMap.empty()) {
            if (impl_->captureChannelMap.size() == reqChannels) {
                for (uint32_t ch = 0; ch < reqChannels; ++ch) {
                    const uint32_t physCh = impl_->captureChannelMap[ch];
                    float* dst = impl_->engineBlockIn[ch];
                    if (physCh < inCount && impl_->inPtrs[physCh]) {
                        const float* src = impl_->inPtrs[physCh] + framesProcessed;
                        for (uint32_t f = 0; f < chunkSize; ++f) {
                            dst[f] = src[f] * inputGain;
                        }
                    } else {
                        std::memset(dst, 0, chunkSize * sizeof(float));
                    }
                }
            } else {
                // Channel-strip mapping: each physical mono input maps to a stereo processing pair
                const size_t numStrips = reqChannels / 2;
                for (size_t s = 0; s < numStrips; ++s) {
                    const uint32_t physCh = s < impl_->captureChannelMap.size()
                        ? impl_->captureChannelMap[s] : 0;
                    float* dstL = impl_->engineBlockIn[static_cast<uint32_t>(s * 2)];
                    float* dstR = impl_->engineBlockIn[static_cast<uint32_t>(s * 2 + 1)];

                    if (physCh < inCount && impl_->inPtrs[physCh]) {
                        const float* src = impl_->inPtrs[physCh] + framesProcessed;
                        for (uint32_t f = 0; f < chunkSize; ++f) {
                            const float sample = src[f] * inputGain;
                            dstL[f] = sample;
                            dstR[f] = sample;
                        }
                    } else {
                        std::memset(dstL, 0, chunkSize * sizeof(float));
                        std::memset(dstR, 0, chunkSize * sizeof(float));
                    }
                }
            }
        } else {
            // Direct 1:1 mapping (default stereo passthrough / multi-channel)
            for (uint32_t ch = 0; ch < reqChannels; ++ch) {
                float* dst = impl_->engineBlockIn[ch];
                if (ch < inCount && impl_->inPtrs[ch]) {
                    const float* src = impl_->inPtrs[ch] + framesProcessed;
                    for (uint32_t f = 0; f < chunkSize; ++f) {
                        dst[f] = src[f] * inputGain;
                    }
                } else {
                    std::memset(dst, 0, chunkSize * sizeof(float));
                }
            }
        }

        // Realtime execution timestamp measurement
        struct timespec tStart{}, tEnd{};
        clock_gettime(CLOCK_MONOTONIC, &tStart);

        // Process audio through the AudioEngine DSP graph
        if (engine_) {
            engine_->process(impl_->engineBlockIn, impl_->engineBlockOut, chunkSize);
        } else {
            impl_->engineBlockOut.clear(chunkSize);
        }

        clock_gettime(CLOCK_MONOTONIC, &tEnd);
        const uint64_t dspUs = static_cast<uint64_t>(
            (tEnd.tv_sec - tStart.tv_sec) * 1000000ULL +
            (tEnd.tv_nsec - tStart.tv_nsec) / 1000ULL);

        // Copy processed audio to JACK output port buffers
        for (size_t ch = 0; ch < outCount; ++ch) {
            float* outBuf = impl_->outPtrs[ch];
            if (outBuf) {
                if (ch < impl_->engineBlockOut.getChannelCount()) {
                    std::memcpy(outBuf + framesProcessed,
                                impl_->engineBlockOut[static_cast<uint32_t>(ch)],
                                chunkSize * sizeof(float));
                } else {
                    std::memset(outBuf + framesProcessed, 0, chunkSize * sizeof(float));
                }
            }
        }

        // Update realtime metrics
        impl_->lastDspTimeUs.store(dspUs, std::memory_order_relaxed);
        const uint64_t prevAvg = impl_->avgDspTimeUs.load(std::memory_order_relaxed);
        impl_->avgDspTimeUs.store(prevAvg ? (prevAvg * 7 + dspUs) / 8 : dspUs, std::memory_order_relaxed);

        framesProcessed += chunkSize;
    }

    impl_->totalFrames.fetch_add(nframes, std::memory_order_relaxed);
    impl_->totalCallbacks.fetch_add(1, std::memory_order_relaxed);
    impl_->captureOperational.store(true, std::memory_order_relaxed);
    impl_->dspOperational.store(true, std::memory_order_relaxed);
    impl_->renderOperational.store(true, std::memory_order_relaxed);

    return 0;
}

std::vector<JackDeviceInfo> JackBackend::enumerateDevices(bool capture) {
    std::vector<JackDeviceInfo> devices;

    // 1. Default system entry
    JackDeviceInfo sysDev;
    sysDev.id = "system";
    sysDev.name = capture ? "System Audio Capture (JACK)" : "System Audio Playback (JACK)";
    sysDev.friendlyName = sysDev.name;
    sysDev.isDefault = true;
    sysDev.isCapture = capture;
    sysDev.channels = 2;
    sysDev.sampleRate = 48000;
    sysDev.isAvailable = true;
    devices.push_back(sysDev);

    // 2. Real ALSA soundcard endpoints from /proc/asound
#if !defined(_WIN32)
    std::ifstream cardsFile("/proc/asound/cards");
    if (cardsFile.is_open()) {
        std::string line;
        while (std::getline(cardsFile, line)) {
            if (line.size() > 5 && (line[0] == ' ' || (line[0] >= '0' && line[0] <= '9'))) {
                auto bracketOpen = line.find('[');
                auto bracketClose = line.find(']');
                auto colon = line.find(':');
                if (bracketOpen != std::string::npos && bracketClose != std::string::npos &&
                    colon != std::string::npos && bracketClose > bracketOpen && colon > bracketClose) {
                    std::string cardNumStr = line.substr(0, bracketOpen);
                    cardNumStr.erase(0, cardNumStr.find_first_not_of(" \t"));
                    cardNumStr.erase(cardNumStr.find_last_not_of(" \t") + 1);

                    std::string cardId = line.substr(bracketOpen + 1, bracketClose - bracketOpen - 1);
                    cardId.erase(0, cardId.find_first_not_of(" \t"));
                    cardId.erase(cardId.find_last_not_of(" \t") + 1);

                    std::string cardDesc = line.substr(colon + 1);
                    cardDesc.erase(0, cardDesc.find_first_not_of(" \t"));
                    cardDesc.erase(cardDesc.find_last_not_of(" \t") + 1);

                    bool hasStream = true;
                    if (capture) {
                        std::string pcmGeneral = "/proc/asound/pcm";
                        std::ifstream pcmGen(pcmGeneral);
                        if (pcmGen.is_open()) {
                            std::string genContent((std::istreambuf_iterator<char>(pcmGen)),
                                                   std::istreambuf_iterator<char>());
                            std::string target = cardNumStr + "-";
                            auto pos = genContent.find(target);
                            if (pos != std::string::npos) {
                                auto nextEnd = genContent.find('\n', pos);
                                std::string cardLine = genContent.substr(pos, nextEnd - pos);
                                if (cardLine.find("capture") == std::string::npos) {
                                    hasStream = false;
                                }
                            }
                        }
                    }

                    if (hasStream) {
                        JackDeviceInfo dev;
                        dev.id = "hw:" + cardNumStr;
                        bool isUsb = cardDesc.find("USB") != std::string::npos;
                        dev.name = cardDesc + " [" + dev.id + "]";
                        dev.friendlyName = (isUsb ? "USB Audio: " : "ALSA Audio: ") + cardDesc;
                        dev.isDefault = false;
                        dev.isCapture = capture;
                        dev.channels = 2;
                        dev.sampleRate = 48000;
                        dev.isAvailable = true;
                        devices.push_back(dev);
                    }
                }
            }
        }
    }
#endif

    // 3. Inspect existing active ports if a JACK server is reachable
    jack_status_t status{};
    jack_client_t* tempClient = jack_client_open("AudioCoreProbe", JackNoStartServer, &status);
    if (tempClient) {
        unsigned long flags = JackPortIsPhysical;
        flags |= capture ? JackPortIsOutput : JackPortIsInput;

        const char** ports = jack_get_ports(tempClient, nullptr, nullptr, flags);
        if (ports) {
            uint32_t portCount = 0;
            while (ports[portCount] != nullptr) {
                ++portCount;
            }
            if (portCount > 0) {
                devices[0].channels = std::max(2u, portCount);
                devices[0].sampleRate = static_cast<uint32_t>(jack_get_sample_rate(tempClient));
            }

            // Expose individual physical ports as endpoints for channel routing
            for (uint32_t i = 0; i < portCount; ++i) {
                JackDeviceInfo portDev;
                portDev.id = ports[i];
                portDev.name = ports[i];
                portDev.friendlyName = std::string("JACK Port: ") + ports[i];
                portDev.isDefault = false;
                portDev.isCapture = capture;
                portDev.channels = 1;
                portDev.sampleRate = devices[0].sampleRate;
                portDev.isAvailable = true;
                devices.push_back(portDev);
            }

            jack_free(ports);
        }
        jack_client_close(tempClient);
    }

    return devices;
}

} // namespace audiocore

