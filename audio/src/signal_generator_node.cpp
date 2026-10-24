#include "audiocore/signal_generator_node.hpp"
#include <cmath>
#include <algorithm>
#include <cstring>

namespace audiocore {

namespace {
constexpr double kTwoPi = 6.283185307179586476925286766559;
}

SignalGeneratorNode::SignalGeneratorNode(double sampleRate)
    : sample_rate_(sampleRate > 0.0 ? sampleRate : 48000.0)
{
    setLevelDb(-20.0f);
}

void SignalGeneratorNode::setSampleRate(double sampleRate) {
    if (sampleRate > 0.0) {
        sample_rate_ = sampleRate;
        reset();
    }
}

void SignalGeneratorNode::setSignalType(TestSignalType type) {
    signal_type_.store(type, std::memory_order_release);
    reset();
}

void SignalGeneratorNode::setLevelDb(float levelDb) {
    // Safety clamp: strictly clamp between -96 dBFS and 0 dBFS to prevent accidental ear damage
    float clamped = std::clamp(levelDb, -96.0f, 0.0f);
    level_db_.store(clamped, std::memory_order_release);
    float gain = std::pow(10.0f, clamped / 20.0f);
    linear_gain_.store(gain, std::memory_order_release);
}

void SignalGeneratorNode::setEnabled(bool enabled) {
    enabled_.store(enabled, std::memory_order_release);
    if (!enabled) {
        reset();
    }
}

void SignalGeneratorNode::reset() {
    phase_ = 0.0;
    sweep_time_ = 0.0;
    pink_b0_ = pink_b1_ = pink_b2_ = pink_b3_ = pink_b4_ = pink_b5_ = pink_b6_ = 0.0f;
    prng_state_ = 123456789;
}

float SignalGeneratorNode::nextWhiteSample() noexcept {
    // Fast LCG: x_n+1 = (a * x_n + c) mod 2^32
    prng_state_ = prng_state_ * 1664525u + 1013904223u;
    // Map to [-1.0f, 1.0f]
    constexpr float kInvMax = 2.0f / 4294967295.0f;
    return (static_cast<float>(prng_state_) * kInvMax) - 1.0f;
}

void SignalGeneratorNode::process(const AudioBuffer& input, AudioBuffer& output, uint32_t frames) {
    const uint32_t outChannels = output.getChannelCount();
    if (outChannels == 0 || frames == 0) return;

    if (!enabled_.load(std::memory_order_relaxed)) {
        // Pass through or silence if input empty
        if (input.getFrameCount() >= frames && input.getChannelCount() > 0) {
            for (uint32_t ch = 0; ch < outChannels; ++ch) {
                uint32_t inCh = std::min(ch, input.getChannelCount() - 1);
                std::memcpy(output.getChannelData(ch), input.getChannelData(inCh), frames * sizeof(float));
            }
        } else {
            output.clear(frames);
        }
        return;
    }

    const TestSignalType type = signal_type_.load(std::memory_order_relaxed);
    const float gain = linear_gain_.load(std::memory_order_relaxed);
    const double dt = 1.0 / sample_rate_;

    for (uint32_t i = 0; i < frames; ++i) {
        float sample = 0.0f;

        switch (type) {
            case TestSignalType::Silence:
                sample = 0.0f;
                break;

            case TestSignalType::Sine1kHz: {
                sample = static_cast<float>(std::sin(phase_)) * gain;
                phase_ += kTwoPi * 1000.0 * dt;
                if (phase_ >= kTwoPi) phase_ -= kTwoPi;
                break;
            }

            case TestSignalType::Sine100Hz: {
                sample = static_cast<float>(std::sin(phase_)) * gain;
                phase_ += kTwoPi * 100.0 * dt;
                if (phase_ >= kTwoPi) phase_ -= kTwoPi;
                break;
            }

            case TestSignalType::Sine10kHz: {
                sample = static_cast<float>(std::sin(phase_)) * gain;
                phase_ += kTwoPi * 10000.0 * dt;
                if (phase_ >= kTwoPi) phase_ -= kTwoPi;
                break;
            }

            case TestSignalType::LogSweep: {
                // Logarithmic frequency sweep: f(t) = f0 * (f1 / f0)^(t / T)
                // phase(t) = 2*pi * f0 * T / ln(f1 / f0) * ((f1 / f0)^(t / T) - 1)
                constexpr double ratio = kSweepEndFreq / kSweepStartFreq;
                const double logRatio = std::log(ratio);
                const double normTime = sweep_time_ / kSweepDurationSec;
                const double currentFreq = kSweepStartFreq * std::pow(ratio, normTime);

                sample = static_cast<float>(std::sin(phase_)) * gain;
                phase_ += kTwoPi * currentFreq * dt;
                if (phase_ >= kTwoPi) phase_ -= kTwoPi;

                sweep_time_ += dt;
                if (sweep_time_ >= kSweepDurationSec) {
                    sweep_time_ = 0.0;
                    phase_ = 0.0;
                }
                break;
            }

            case TestSignalType::PinkNoise: {
                // Paul Kellet's filter method for pink noise (-3 dB/octave)
                float white = nextWhiteSample();
                pink_b0_ = 0.99886f * pink_b0_ + white * 0.0555179f;
                pink_b1_ = 0.99332f * pink_b1_ + white * 0.0750759f;
                pink_b2_ = 0.96900f * pink_b2_ + white * 0.1538520f;
                pink_b3_ = 0.86650f * pink_b3_ + white * 0.3104856f;
                pink_b4_ = 0.55000f * pink_b4_ + white * 0.5329522f;
                pink_b5_ = -0.7616f * pink_b5_ - white * 0.0168980f;
                float pink = pink_b0_ + pink_b1_ + pink_b2_ + pink_b3_ + pink_b4_ + pink_b5_ + pink_b6_ + white * 0.5362f;
                pink_b6_ = white * 0.115926f;
                sample = pink * 0.11f * gain; // Scaled to ~unity peak before gain
                break;
            }

            case TestSignalType::WhiteNoise: {
                sample = nextWhiteSample() * gain;
                break;
            }
        }

        for (uint32_t ch = 0; ch < outChannels; ++ch) {
            output.getChannelData(ch)[i] = sample;
        }
    }
}

const char* SignalGeneratorNode::signalTypeToString(TestSignalType type) noexcept {
    switch (type) {
        case TestSignalType::Silence:    return "Silence";
        case TestSignalType::Sine1kHz:   return "1 kHz Sine";
        case TestSignalType::Sine100Hz:  return "100 Hz Sine";
        case TestSignalType::Sine10kHz:  return "10 kHz Sine";
        case TestSignalType::LogSweep:   return "Log Sweep (20 Hz - 20 kHz)";
        case TestSignalType::PinkNoise:  return "Pink Noise";
        case TestSignalType::WhiteNoise: return "White Noise";
        default:                         return "Unknown";
    }
}

TestSignalType SignalGeneratorNode::stringToSignalType(const std::string& name) noexcept {
    if (name == "Silence") return TestSignalType::Silence;
    if (name == "100 Hz Sine" || name == "100hz") return TestSignalType::Sine100Hz;
    if (name == "10 kHz Sine" || name == "10khz") return TestSignalType::Sine10kHz;
    if (name == "Log Sweep (20 Hz - 20 kHz)" || name == "sweep") return TestSignalType::LogSweep;
    if (name == "Pink Noise" || name == "pink") return TestSignalType::PinkNoise;
    if (name == "White Noise" || name == "white") return TestSignalType::WhiteNoise;
    return TestSignalType::Sine1kHz;
}

} // namespace audiocore

