#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>

namespace audiocore {
class GainNode;
class PanNode;
class GateNode;
class ParametricEQNode;
class GraphicEQNode;
class CompressorNode;
class LimiterNode;
class DelayNode;
class ReverbNode;
class AfsNode;
class AlignmentDelayNode;
class AudioGraph;
}

namespace testbench {

/**
 * @brief Channel Strip Parameters (Gain, Pan, Gate, PEQ, GEQ, Comp, Limiter, Sends).
 */
struct ChannelStripParams {
    float gainDb{0.0f};             // -120.0f to +12.0f dB (0.0 dB unity)
    float pan{0.0f};                // -1.0f (L100) to +1.0f (R100) (0.0 Center)
    float reverbSendDb{-6.0f};      // -120.0f to +6.0f dB
    float delaySendDb{-6.0f};       // -120.0f to +6.0f dB
    float fxReturnDb{0.0f};         // -120.0f to +6.0f dB

    // Noise Gate
    bool gateEnabled{true};
    bool gateStereoLink{true};
    float gateThresholdDb{-40.0f};  // -80.0f to 0.0f dB
    float gateRangeDb{-60.0f};      // -90.0f to 0.0f dB
    float gateAttackMs{1.0f};       // 0.1f to 50.0f ms
    float gateHoldMs{20.0f};        // 0.0f to 500.0f ms
    float gateReleaseMs{100.0f};    // 5.0f to 1000.0f ms

    // 4-Band Parametric EQ
    bool peqEnabled{true};
    struct PeqBand {
        bool enabled{true};
        int type{0};                // 0: Peaking, 1: LowShelf, 2: HighShelf
        float frequency{1000.0f};   // 20.0f to 20000.0f Hz
        float gainDb{0.0f};         // -12.0f to +12.0f dB
        float q{1.0f};              // 0.1f to 10.0f
    } peqBands[4];

    // 31-Band ISO 266 Graphic EQ
    bool geqEnabled{true};
    float geqBands[31]{0.0f};       // -12.0f to +12.0f dB per band

    // Compressor
    bool compEnabled{true};
    float compThresholdDb{-12.0f};  // -60.0f to 0.0f dB
    float compRatio{4.0f};          // 1.0f to 20.0f (:1)
    float compAttackMs{10.0f};      // 0.1f to 200.0f ms
    float compReleaseMs{100.0f};    // 10.0f to 1000.0f ms
    float compMakeupDb{0.0f};       // -12.0f to +24.0f dB

    // Limiter
    bool limEnabled{true};
    float limCeilingDb{-1.0f};      // -12.0f to 0.0f dB
    float limReleaseMs{50.0f};      // 10.0f to 1000.0f ms
};

/**
 * @brief Algorithmic Reverb Parameters.
 */
struct ReverbParams {
    bool enabled{false};
    int type{1};                    // 0: Room, 1: Hall, 2: Plate, 3: Ambience
    bool predelayEnabled{true};     // Pre-Delay Enable toggle
    float predelayMs{20.0f};        // 0.0f to 250.0f ms
    float decaySeconds{2.5f};       // 0.1f to 10.0f s
    float damping{0.40f};           // 0.0f to 1.0f (0 to 100%)
    float diffusion{0.85f};         // 0.0f to 1.0f (0 to 100%)
    float lowCutHz{120.0f};         // 20.0f to 1000.0f Hz
    float highCutHz{9000.0f};       // 2000.0f to 20000.0f Hz
    float width{1.0f};              // 0.0f to 1.5f (0 to 150%)
    float mix{0.30f};               // 0.0f (DRY) to 1.0f (WET)
    bool sendReturnMode{false};     // When active: internal reverb output is 100% wet
    bool isModified{false};         // True if modified away from template
};

/**
 * @brief Tap Delay Parameters.
 */
struct DelayParams {
    bool enabled{false};
    float bpm{120.0f};              // 40.0f to 240.0f BPM
    bool tempoSync{true};
    int syncDivision{0};            // 0: 1/4, 1: 1/8, 2: 1/8T, 3: 1/16, 4: 1/2, 5: 1/1
    float feedback{0.25f};          // 0.0f to 0.99f (0 to 99%)
    float lowCutHz{80.0f};          // 20.0f to 1000.0f Hz
    float highCutHz{10000.0f};      // 1000.0f to 20000.0f Hz
    float width{1.0f};              // 0.0f to 1.5f
    float mix{0.20f};               // 0.0f to 1.0f (0 to 100%)

    struct Tap {
        bool enabled{true};
        float timeMs{500.0f};       // 1.0f to 5000.0f ms
        float level{1.0f};          // 0.0f to 1.0f (0 to 100%)
        float pan{0.0f};            // -1.0f to +1.0f
    } taps[4];
};

/**
 * @brief Automatic Feedback Suppressor (AFS) Parameters.
 */
struct AfsParams {
    bool enabled{true};
    float sensitivity{0.75f};       // 0.0f to 1.0f
    int maxNotches{16};             // 1 to 16
    float maxDepthDb{-18.0f};       // -30.0f to -6.0f dB
    float attackMs{20.0f};          // 1.0f to 100.0f ms
    float holdMs{3000.0f};          // 100.0f to 5000.0f ms
    float releaseMs{1000.0f};       // 100.0f to 5000.0f ms
};

/**
 * @brief Output Time Alignment / Speaker Delay Parameters (v1.2).
 */
struct OutputAlignmentParams {
    bool enabled{true};
    bool stereoLink{false};
    int referenceOutput{1};         // 0: MAIN L, 1: MAIN R (Reference has 0 ms relative delay)
    
    // Channel 0: MAIN L
    float delayMsL{0.0f};           // 0.0f to 500.0f ms
    bool polarityInvertL{false};
    bool muteL{false};

    // Channel 1: MAIN R
    float delayMsR{0.0f};           // 0.0f to 500.0f ms
    bool polarityInvertR{false};
    bool muteR{false};
};

/**
 * @brief Complete Digital Mixer Scene / Preset State.
 */
struct MixerState {
    std::string sceneName{"Default Scene"};
    ChannelStripParams channel;
    ReverbParams reverb;
    DelayParams delay;
    AfsParams afs;
    OutputAlignmentParams output;
    bool masterBypass{false};

    void setNeutralDefaults();
};

/**
 * @brief Thread-Safe Control Layer & Preset Serialization.
 *
 * Decouples the UI thread from the DSP realtime audio processing thread.
 * Validates, clamps, queues and applies parameter adjustments safely.
 */
class MixerControlManager {
public:
    MixerControlManager();

    // State Inspection & Mutation
    MixerState getState() const;
    void setState(const MixerState& state);
    void resetToNeutral();

    // Channel Strip Controls
    void setChannelGainDb(float gainDb);
    void setChannelPan(float pan);
    void setReverbSendDb(float db);
    void setDelaySendDb(float db);
    void setFxReturnDb(float db);

    // Gate Controls
    void setGateEnabled(bool en);
    void setGateStereoLink(bool link);
    void setGateThresholdDb(float threshDb);
    void setGateRangeDb(float rangeDb);
    void setGateAttackMs(float attMs);
    void setGateHoldMs(float holdMs);
    void setGateReleaseMs(float relMs);

    // Parametric EQ Controls
    void setPeqEnabled(bool en);
    void setPeqBand(int band, bool en, int type, float freq, float gainDb, float q);

    // Graphic EQ Controls
    void setGeqEnabled(bool en);
    void setGeqBandGainDb(int band, float gainDb);
    void resetGeqToFlat();

    // Compressor Controls
    void setCompEnabled(bool en);
    void setCompThresholdDb(float threshDb);
    void setCompRatio(float ratio);
    void setCompAttackMs(float attMs);
    void setCompReleaseMs(float relMs);
    void setCompMakeupDb(float makeupDb);

    // Limiter Controls
    void setLimEnabled(bool en);
    void setLimCeilingDb(float ceilDb);
    void setLimReleaseMs(float relMs);

    // Reverb Controls
    void setReverbEnabled(bool en);
    void setReverbType(int type);
    void setReverbPredelayEnabled(bool en);
    void setReverbPredelayMs(float ms);
    void setReverbDecay(float decaySec);
    void setReverbDamping(float damp);
    void setReverbDiffusion(float diff);
    void setReverbLowCut(float hz);
    void setReverbHighCut(float hz);
    void setReverbWidth(float width);
    void setReverbMix(float mix);
    void setReverbSendReturnMode(bool sendReturn);
    void resetReverbToTemplate();
    bool isReverbModified() const { return state_.reverb.isModified; }
    std::string getReverbTemplateDisplayName() const;
    void checkReverbModified();

    // Delay Controls
    void setDelayEnabled(bool en);
    void setDelayBpm(float bpm);
    void setDelayTempoSync(bool sync);
    void setDelaySyncDivision(int div);
    void setDelayFeedback(float fb);
    void setDelayLowCut(float hz);
    void setDelayHighCut(float hz);
    void setDelayWidth(float width);
    void setDelayMix(float mix);
    void setDelayTap(int tapIdx, bool en, float timeMs, float level, float pan);

    // AFS Controls
    void setAfsEnabled(bool en);
    void setAfsSensitivity(float sens);
    void setAfsMaxNotches(int count);
    void setAfsMaxDepthDb(float depthDb);
    void setAfsAttackMs(float attMs);
    void setAfsHoldMs(float holdMs);
    void setAfsReleaseMs(float relMs);

    // Output Time Alignment / Speaker Delay Controls (v1.2)
    void setOutputAlignmentEnabled(bool en);
    void setOutputStereoLink(bool link);
    void setOutputReference(int refIdx);
    void setOutputDelayMs(size_t ch, float ms);
    void setOutputPolarityInvert(size_t ch, bool invert);
    void setOutputMute(size_t ch, bool mute);

    // Master Bypass
    void setMasterBypassed(bool bypassed);

    // Realtime DSP Node Binding Application
    void applyToDspNodes(
        audiocore::AudioGraph* graph,
        audiocore::GainNode* gainNode,
        audiocore::PanNode* panNode,
        audiocore::GateNode* gateNode,
        audiocore::ParametricEQNode* peqNode,
        audiocore::GraphicEQNode* geqNode,
        audiocore::CompressorNode* compNode,
        audiocore::LimiterNode* limNode,
        audiocore::DelayNode* delayNode,
        audiocore::ReverbNode* reverbNode,
        audiocore::AfsNode* afsNode,
        audiocore::AlignmentDelayNode* alignNode = nullptr);

    // Preset & Scene Serialization
    static std::string serializeToJson(const MixerState& state);
    static bool deserializeFromJson(const std::string& json, MixerState& outState);
    static bool saveToFile(const std::string& filePath, const MixerState& state);
    static bool loadFromFile(const std::string& filePath, MixerState& outState);

    // Helpers
    static const char* tempoDivisionToString(int division);
    static float calculateTempoDelayMs(float bpm, int division);
    static std::string formatReverbMixLabel(float mix, bool sendReturnMode);

private:
    MixerState state_;
};

} // namespace testbench
