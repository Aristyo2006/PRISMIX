#include "audiocore/audio_engine.hpp"
#include "audiocore/gain_node.hpp"

namespace audiocore {

AudioEngine::AudioEngine()
    : state_(EngineState::Unconfigured)
    , sample_rate_(48000.0)
    , block_size_(128)
    , channels_(2)
{
}

AudioEngine::~AudioEngine() {
    stop();
}

bool AudioEngine::configure(
    double sampleRate,
    uint32_t blockSize,
    uint32_t channels)
{
    if (sampleRate <= 0.0 || blockSize == 0 || channels == 0) {
        state_.store(EngineState::Error, std::memory_order_release);
        return false;
    }

    sample_rate_ = sampleRate;
    block_size_ = blockSize;
    channels_ = channels;

    // Preallocate all internal scratch and graph buffers
    graph_.configure(channels_, block_size_);

    // If graph has no nodes yet, install default initial pipeline: Input -> GainNode -> Output
    if (graph_.getNodeCount() == 0) {
        graph_.addNode(std::make_shared<GainNode>(0.0f));
    }

    if (backend_) {
        if (!backend_->initialize(sample_rate_, block_size_, channels_)) {
            state_.store(EngineState::Error, std::memory_order_release);
            return false;
        }
    }

    state_.store(EngineState::Stopped, std::memory_order_release);
    return true;
}

bool AudioEngine::start() {
    if (state_.load(std::memory_order_relaxed) == EngineState::Unconfigured) {
        // Auto-configure with defaults if started unconfigured
        if (!configure(sample_rate_, block_size_, channels_)) {
            return false;
        }
    }

    graph_.reset();

    if (backend_) {
        if (!backend_->start()) {
            state_.store(EngineState::Error, std::memory_order_release);
            return false;
        }
    }

    state_.store(EngineState::Running, std::memory_order_release);
    return true;
}

void AudioEngine::stop() {
    if (state_.load(std::memory_order_relaxed) == EngineState::Running) {
        if (backend_) {
            backend_->stop();
        }
        state_.store(EngineState::Stopped, std::memory_order_release);
    }
}

void AudioEngine::process(
    const AudioBuffer& input,
    AudioBuffer& output,
    uint32_t frames)
{
    if (state_.load(std::memory_order_relaxed) != EngineState::Running) {
        output.clear(frames);
        return;
    }

    graph_.process(input, output, frames);
}

void AudioEngine::setBackend(std::shared_ptr<AudioBackend> backend) {
    if (backend_ && backend_->isRunning()) {
        backend_->stop();
        backend_->setEngine(nullptr);
    }
    backend_ = std::move(backend);
    if (backend_) {
        backend_->setEngine(this);
        if (state_.load(std::memory_order_relaxed) != EngineState::Unconfigured) {
            backend_->initialize(sample_rate_, block_size_, channels_);
        }
    }
}

} // namespace audiocore

