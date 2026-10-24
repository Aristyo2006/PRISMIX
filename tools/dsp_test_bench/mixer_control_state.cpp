#include "mixer_control_state.hpp"

#include <audiocore/audio_graph.hpp>
#include <audiocore/gain_node.hpp>
#include <audiocore/pan_node.hpp>
#include <audiocore/gate_node.hpp>
#include <audiocore/eq_node.hpp>
#include <audiocore/geq_node.hpp>
#include <audiocore/compressor_node.hpp>
#include <audiocore/limiter_node.hpp>
#include <audiocore/delay_node.hpp>
#include <audiocore/reverb_node.hpp>
#include <audiocore/afs_node.hpp>
#include <audiocore/alignment_delay_node.hpp>

#include <sstream>
#include <fstream>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <map>

namespace testbench {

void MixerState::setNeutralDefaults() {
    sceneName = "Default Neutral Scene";

    // Channel Strip
    channel.gainDb = 0.0f;
    channel.pan = 0.0f;
    channel.reverbSendDb = -6.0f;
    channel.delaySendDb = -6.0f;
    channel.fxReturnDb = 0.0f;

    // Gate
    channel.gateEnabled = true;
    channel.gateStereoLink = true;
    channel.gateThresholdDb = -40.0f;
    channel.gateRangeDb = -60.0f;
    channel.gateAttackMs = 1.0f;
    channel.gateHoldMs = 20.0f;
    channel.gateReleaseMs = 100.0f;

    // PEQ (4 Bands)
    channel.peqEnabled = true;
    channel.peqBands[0] = { true, 1, 100.0f, 0.0f, 0.71f };   // Band 1: LowShelf 100 Hz
    channel.peqBands[1] = { true, 0, 1000.0f, 0.0f, 1.00f };  // Band 2: Peaking 1 kHz
    channel.peqBands[2] = { true, 0, 3000.0f, 0.0f, 1.00f };  // Band 3: Peaking 3 kHz
    channel.peqBands[3] = { true, 2, 10000.0f, 0.0f, 0.71f }; // Band 4: HighShelf 10 kHz

    // GEQ (31 Bands Flat)
    channel.geqEnabled = true;
    for (int i = 0; i < 31; ++i) {
        channel.geqBands[i] = 0.0f;
    }

    // Compressor
    channel.compEnabled = true;
    channel.compThresholdDb = -12.0f;
    channel.compRatio = 4.0f;
    channel.compAttackMs = 10.0f;
    channel.compReleaseMs = 100.0f;
    channel.compMakeupDb = 0.0f;

    // Limiter
    channel.limEnabled = true;
    channel.limCeilingDb = -1.0f;
    channel.limReleaseMs = 50.0f;

    // Reverb
    reverb.enabled = false;
    reverb.type = 1; // Hall
    reverb.predelayEnabled = true;
    auto hallTmpl = audiocore::ReverbNode::getTemplate(audiocore::ReverbType::Hall);
    reverb.predelayMs = hallTmpl.preDelayMs;
    reverb.decaySeconds = hallTmpl.decaySeconds;
    reverb.damping = hallTmpl.damping;
    reverb.diffusion = hallTmpl.diffusion;
    reverb.lowCutHz = hallTmpl.lowCutHz;
    reverb.highCutHz = hallTmpl.highCutHz;
    reverb.width = hallTmpl.width;
    reverb.mix = hallTmpl.mix;
    reverb.sendReturnMode = false;
    reverb.isModified = false;

    // Delay
    delay.enabled = false;
    delay.bpm = 120.0f;
    delay.tempoSync = true;
    delay.syncDivision = 0; // 1/4
    delay.feedback = 0.25f;
    delay.lowCutHz = 80.0f;
    delay.highCutHz = 10000.0f;
    delay.width = 1.0f;
    delay.mix = 0.20f;
    delay.taps[0] = { true, 500.0f, 1.0f, -0.25f };
    delay.taps[1] = { true, 250.0f, 0.75f, 0.25f };
    delay.taps[2] = { true, 167.0f, 0.50f, -0.50f };
    delay.taps[3] = { true, 125.0f, 0.35f, 0.50f };

    // AFS
    afs.enabled = true;
    afs.sensitivity = 0.75f;
    afs.maxNotches = 16;
    afs.maxDepthDb = -18.0f;
    afs.attackMs = 20.0f;
    afs.holdMs = 3000.0f;
    afs.releaseMs = 1000.0f;

    // Output Time Alignment (v1.2)
    output.enabled = true;
    output.stereoLink = false;
    output.referenceOutput = 1; // Default reference: MAIN R
    output.delayMsL = 0.0f;
    output.polarityInvertL = false;
    output.muteL = false;
    output.delayMsR = 0.0f;
    output.polarityInvertR = false;
    output.muteR = false;

    masterBypass = false;
}

MixerControlManager::MixerControlManager() {
    state_.setNeutralDefaults();
}

MixerState MixerControlManager::getState() const {
    return state_;
}

void MixerControlManager::setState(const MixerState& state) {
    state_ = state;
}

void MixerControlManager::resetToNeutral() {
    state_.setNeutralDefaults();
}

void MixerControlManager::setChannelGainDb(float gainDb) {
    state_.channel.gainDb = std::max(-120.0f, std::min(12.0f, gainDb));
}

void MixerControlManager::setChannelPan(float pan) {
    state_.channel.pan = std::max(-1.0f, std::min(1.0f, pan));
}

void MixerControlManager::setReverbSendDb(float db) {
    state_.channel.reverbSendDb = std::max(-120.0f, std::min(6.0f, db));
}

void MixerControlManager::setDelaySendDb(float db) {
    state_.channel.delaySendDb = std::max(-120.0f, std::min(6.0f, db));
}

void MixerControlManager::setFxReturnDb(float db) {
    state_.channel.fxReturnDb = std::max(-120.0f, std::min(6.0f, db));
}

void MixerControlManager::setGateEnabled(bool en) {
    state_.channel.gateEnabled = en;
}

void MixerControlManager::setGateStereoLink(bool link) {
    state_.channel.gateStereoLink = link;
}

void MixerControlManager::setGateThresholdDb(float threshDb) {
    state_.channel.gateThresholdDb = std::max(-80.0f, std::min(0.0f, threshDb));
}

void MixerControlManager::setGateRangeDb(float rangeDb) {
    state_.channel.gateRangeDb = std::max(-90.0f, std::min(0.0f, rangeDb));
}

void MixerControlManager::setGateAttackMs(float attMs) {
    state_.channel.gateAttackMs = std::max(0.1f, std::min(50.0f, attMs));
}

void MixerControlManager::setGateHoldMs(float holdMs) {
    state_.channel.gateHoldMs = std::max(0.0f, std::min(500.0f, holdMs));
}

void MixerControlManager::setGateReleaseMs(float relMs) {
    state_.channel.gateReleaseMs = std::max(5.0f, std::min(1000.0f, relMs));
}

void MixerControlManager::setPeqEnabled(bool en) {
    state_.channel.peqEnabled = en;
}

void MixerControlManager::setPeqBand(int band, bool en, int type, float freq, float gainDb, float q) {
    if (band < 0 || band >= 4) return;
    state_.channel.peqBands[band].enabled = en;
    state_.channel.peqBands[band].type = std::max(0, std::min(2, type));
    state_.channel.peqBands[band].frequency = std::max(20.0f, std::min(20000.0f, freq));
    state_.channel.peqBands[band].gainDb = std::max(-12.0f, std::min(12.0f, gainDb));
    state_.channel.peqBands[band].q = std::max(0.1f, std::min(10.0f, q));
}

void MixerControlManager::setGeqEnabled(bool en) {
    state_.channel.geqEnabled = en;
}

void MixerControlManager::setGeqBandGainDb(int band, float gainDb) {
    if (band < 0 || band >= 31) return;
    state_.channel.geqBands[band] = std::max(-12.0f, std::min(12.0f, gainDb));
}

void MixerControlManager::resetGeqToFlat() {
    for (int i = 0; i < 31; ++i) {
        state_.channel.geqBands[i] = 0.0f;
    }
}

void MixerControlManager::setCompEnabled(bool en) {
    state_.channel.compEnabled = en;
}

void MixerControlManager::setCompThresholdDb(float threshDb) {
    state_.channel.compThresholdDb = std::max(-60.0f, std::min(0.0f, threshDb));
}

void MixerControlManager::setCompRatio(float ratio) {
    state_.channel.compRatio = std::max(1.0f, std::min(20.0f, ratio));
}

void MixerControlManager::setCompAttackMs(float attMs) {
    state_.channel.compAttackMs = std::max(0.1f, std::min(200.0f, attMs));
}

void MixerControlManager::setCompReleaseMs(float relMs) {
    state_.channel.compReleaseMs = std::max(10.0f, std::min(1000.0f, relMs));
}

void MixerControlManager::setCompMakeupDb(float makeupDb) {
    state_.channel.compMakeupDb = std::max(-12.0f, std::min(24.0f, makeupDb));
}

void MixerControlManager::setLimEnabled(bool en) {
    state_.channel.limEnabled = en;
}

void MixerControlManager::setLimCeilingDb(float ceilDb) {
    state_.channel.limCeilingDb = std::max(-12.0f, std::min(0.0f, ceilDb));
}

void MixerControlManager::setLimReleaseMs(float relMs) {
    state_.channel.limReleaseMs = std::max(10.0f, std::min(1000.0f, relMs));
}

void MixerControlManager::setReverbEnabled(bool en) {
    state_.reverb.enabled = en;
}

void MixerControlManager::setReverbType(int type) {
    state_.reverb.type = std::max(0, std::min(3, type));
    auto tmpl = audiocore::ReverbNode::getTemplate(static_cast<audiocore::ReverbType>(state_.reverb.type));
    state_.reverb.predelayMs = tmpl.preDelayMs;
    state_.reverb.decaySeconds = tmpl.decaySeconds;
    state_.reverb.damping = tmpl.damping;
    state_.reverb.diffusion = tmpl.diffusion;
    state_.reverb.lowCutHz = tmpl.lowCutHz;
    state_.reverb.highCutHz = tmpl.highCutHz;
    state_.reverb.width = tmpl.width;
    state_.reverb.mix = tmpl.mix;
    state_.reverb.isModified = false;
}

void MixerControlManager::setReverbPredelayEnabled(bool en) {
    state_.reverb.predelayEnabled = en;
}

void MixerControlManager::setReverbPredelayMs(float ms) {
    state_.reverb.predelayMs = std::max(0.0f, std::min(250.0f, ms));
    checkReverbModified();
}

void MixerControlManager::setReverbDecay(float decaySec) {
    state_.reverb.decaySeconds = std::max(0.1f, std::min(10.0f, decaySec));
    checkReverbModified();
}

void MixerControlManager::setReverbDamping(float damp) {
    state_.reverb.damping = std::max(0.0f, std::min(1.0f, damp));
    checkReverbModified();
}

void MixerControlManager::setReverbDiffusion(float diff) {
    state_.reverb.diffusion = std::max(0.0f, std::min(1.0f, diff));
    checkReverbModified();
}

void MixerControlManager::setReverbLowCut(float hz) {
    state_.reverb.lowCutHz = std::max(20.0f, std::min(1000.0f, hz));
    checkReverbModified();
}

void MixerControlManager::setReverbHighCut(float hz) {
    state_.reverb.highCutHz = std::max(2000.0f, std::min(20000.0f, hz));
    checkReverbModified();
}

void MixerControlManager::setReverbWidth(float width) {
    state_.reverb.width = std::max(0.0f, std::min(1.5f, width));
    checkReverbModified();
}

void MixerControlManager::setReverbMix(float mix) {
    state_.reverb.mix = std::max(0.0f, std::min(1.0f, mix));
    checkReverbModified();
}

void MixerControlManager::setReverbSendReturnMode(bool sendReturn) {
    state_.reverb.sendReturnMode = sendReturn;
}

void MixerControlManager::checkReverbModified() {
    auto tmpl = audiocore::ReverbNode::getTemplate(static_cast<audiocore::ReverbType>(state_.reverb.type));
    bool diff = (std::abs(state_.reverb.predelayMs - tmpl.preDelayMs) > 0.01f) ||
                (std::abs(state_.reverb.decaySeconds - tmpl.decaySeconds) > 0.01f) ||
                (std::abs(state_.reverb.damping - tmpl.damping) > 0.01f) ||
                (std::abs(state_.reverb.diffusion - tmpl.diffusion) > 0.01f) ||
                (std::abs(state_.reverb.lowCutHz - tmpl.lowCutHz) > 0.1f) ||
                (std::abs(state_.reverb.highCutHz - tmpl.highCutHz) > 0.1f) ||
                (std::abs(state_.reverb.width - tmpl.width) > 0.01f) ||
                (std::abs(state_.reverb.mix - tmpl.mix) > 0.01f);
    state_.reverb.isModified = diff;
}

void MixerControlManager::resetReverbToTemplate() {
    setReverbType(state_.reverb.type);
}

std::string MixerControlManager::getReverbTemplateDisplayName() const {
    const char* names[] = {"Room", "Hall", "Plate", "Ambience"};
    int t = std::max(0, std::min(3, state_.reverb.type));
    std::string s = names[t];
    if (state_.reverb.isModified) {
        s += "*";
    }
    return s;
}

void MixerControlManager::setDelayEnabled(bool en) {
    state_.delay.enabled = en;
}

void MixerControlManager::setDelayBpm(float bpm) {
    state_.delay.bpm = std::max(40.0f, std::min(240.0f, bpm));
}

void MixerControlManager::setDelayTempoSync(bool sync) {
    state_.delay.tempoSync = sync;
}

void MixerControlManager::setDelaySyncDivision(int div) {
    state_.delay.syncDivision = std::max(0, std::min(5, div));
}

void MixerControlManager::setDelayFeedback(float fb) {
    state_.delay.feedback = std::max(0.0f, std::min(0.99f, fb));
}

void MixerControlManager::setDelayLowCut(float hz) {
    state_.delay.lowCutHz = std::max(20.0f, std::min(1000.0f, hz));
}

void MixerControlManager::setDelayHighCut(float hz) {
    state_.delay.highCutHz = std::max(1000.0f, std::min(20000.0f, hz));
}

void MixerControlManager::setDelayWidth(float width) {
    state_.delay.width = std::max(0.0f, std::min(1.5f, width));
}

void MixerControlManager::setDelayMix(float mix) {
    state_.delay.mix = std::max(0.0f, std::min(1.0f, mix));
}

void MixerControlManager::setDelayTap(int tapIdx, bool en, float timeMs, float level, float pan) {
    if (tapIdx < 0 || tapIdx >= 4) return;
    state_.delay.taps[tapIdx].enabled = en;
    state_.delay.taps[tapIdx].timeMs = std::max(1.0f, std::min(5000.0f, timeMs));
    state_.delay.taps[tapIdx].level = std::max(0.0f, std::min(1.0f, level));
    state_.delay.taps[tapIdx].pan = std::max(-1.0f, std::min(1.0f, pan));
}

void MixerControlManager::setAfsEnabled(bool en) {
    state_.afs.enabled = en;
}

void MixerControlManager::setAfsSensitivity(float sens) {
    state_.afs.sensitivity = std::max(0.0f, std::min(1.0f, sens));
}

void MixerControlManager::setAfsMaxNotches(int count) {
    state_.afs.maxNotches = std::max(1, std::min(16, count));
}

void MixerControlManager::setAfsMaxDepthDb(float depthDb) {
    state_.afs.maxDepthDb = std::max(-30.0f, std::min(-6.0f, depthDb));
}

void MixerControlManager::setAfsAttackMs(float attMs) {
    state_.afs.attackMs = std::max(1.0f, std::min(100.0f, attMs));
}

void MixerControlManager::setAfsHoldMs(float holdMs) {
    state_.afs.holdMs = std::max(100.0f, std::min(5000.0f, holdMs));
}

void MixerControlManager::setAfsReleaseMs(float relMs) {
    state_.afs.releaseMs = std::max(100.0f, std::min(5000.0f, relMs));
}

void MixerControlManager::setOutputAlignmentEnabled(bool en) {
    state_.output.enabled = en;
}

void MixerControlManager::setOutputStereoLink(bool link) {
    state_.output.stereoLink = link;
}

void MixerControlManager::setOutputReference(int refIdx) {
    state_.output.referenceOutput = std::max(0, std::min(1, refIdx));
}

void MixerControlManager::setOutputDelayMs(size_t ch, float ms) {
    float clamped = std::max(0.0f, std::min(500.0f, ms));
    if (ch == 0) {
        state_.output.delayMsL = clamped;
        if (state_.output.stereoLink) {
            state_.output.delayMsR = clamped;
        }
    } else if (ch == 1) {
        state_.output.delayMsR = clamped;
        if (state_.output.stereoLink) {
            state_.output.delayMsL = clamped;
        }
    }
}

void MixerControlManager::setOutputPolarityInvert(size_t ch, bool invert) {
    if (ch == 0) state_.output.polarityInvertL = invert;
    else if (ch == 1) state_.output.polarityInvertR = invert;
}

void MixerControlManager::setOutputMute(size_t ch, bool mute) {
    if (ch == 0) state_.output.muteL = mute;
    else if (ch == 1) state_.output.muteR = mute;
}

void MixerControlManager::setMasterBypassed(bool bypassed) {
    state_.masterBypass = bypassed;
}

void MixerControlManager::applyToDspNodes(
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
    audiocore::AlignmentDelayNode* alignNode)
{
    if (graph) {
        graph->setBypassed(state_.masterBypass);
    }
    if (state_.masterBypass) {
        return;
    }

    if (gainNode) {
        gainNode->setGainDb(state_.channel.gainDb);
    }

    if (panNode) {
        panNode->setPan(state_.channel.pan);
    }

    if (gateNode) {
        gateNode->setBypassed(!state_.channel.gateEnabled);
        gateNode->setStereoLinked(state_.channel.gateStereoLink);
        gateNode->setThresholdDb(state_.channel.gateThresholdDb);
        gateNode->setRangeDb(state_.channel.gateRangeDb);
        gateNode->setAttackMs(state_.channel.gateAttackMs);
        gateNode->setHoldMs(state_.channel.gateHoldMs);
        gateNode->setReleaseMs(state_.channel.gateReleaseMs);
    }

    if (peqNode) {
        peqNode->setBypassed(!state_.channel.peqEnabled);
        for (int b = 0; b < 4; ++b) {
            const auto& band = state_.channel.peqBands[b];
            audiocore::FilterType fType = audiocore::FilterType::Peaking;
            if (band.type == 1) fType = audiocore::FilterType::LowShelf;
            else if (band.type == 2) fType = audiocore::FilterType::HighShelf;

            peqNode->setBandType(b, fType);
            peqNode->setBandFrequency(b, band.frequency);
            peqNode->setBandGainDb(b, band.gainDb);
            peqNode->setBandQ(b, band.q);
            peqNode->setBandEnabled(b, band.enabled);
        }
    }

    if (geqNode) {
        geqNode->setBypassed(!state_.channel.geqEnabled);
        for (int b = 0; b < 31; ++b) {
            geqNode->setBandGainDb(b, state_.channel.geqBands[b]);
        }
    }

    if (compNode) {
        compNode->setBypassed(!state_.channel.compEnabled);
        compNode->setThresholdDb(state_.channel.compThresholdDb);
        compNode->setRatio(state_.channel.compRatio);
        compNode->setAttackMs(state_.channel.compAttackMs);
        compNode->setReleaseMs(state_.channel.compReleaseMs);
        compNode->setMakeupGainDb(state_.channel.compMakeupDb);
    }

    if (limNode) {
        limNode->setBypassed(!state_.channel.limEnabled);
        limNode->setCeilingDb(state_.channel.limCeilingDb);
        limNode->setReleaseMs(state_.channel.limReleaseMs);
    }

    if (delayNode) {
        delayNode->setBypassed(!state_.delay.enabled);
        delayNode->setBpm(state_.delay.bpm);
        delayNode->setTempoSyncEnabled(state_.delay.tempoSync);
        delayNode->setFeedback(state_.delay.feedback);
        delayNode->setLowCutHz(state_.delay.lowCutHz);
        delayNode->setHighCutHz(state_.delay.highCutHz);
        delayNode->setStereoWidth(state_.delay.width);
        delayNode->setMix(state_.delay.mix);

        for (size_t t = 0; t < 4; ++t) {
            float tapTime = state_.delay.taps[t].timeMs;
            if (state_.delay.tempoSync) {
                tapTime = calculateTempoDelayMs(state_.delay.bpm, state_.delay.syncDivision);
                if (t == 1) tapTime *= 0.75f;
                else if (t == 2) tapTime *= 0.5f;
                else if (t == 3) tapTime *= 1.5f;
            }
            delayNode->setTapDelayMs(t, tapTime);
            float lvlDb = (state_.delay.taps[t].level > 1e-4f)
                ? (20.0f * std::log10(state_.delay.taps[t].level)) : -60.0f;
            delayNode->setTapLevelDb(t, lvlDb);
            delayNode->setTapPan(t, state_.delay.taps[t].pan);
            delayNode->setTapEnabled(t, state_.delay.taps[t].enabled);
        }
    }

    if (reverbNode) {
        reverbNode->setBypassed(!state_.reverb.enabled);
        reverbNode->setType(static_cast<audiocore::ReverbType>(state_.reverb.type));
        reverbNode->setPreDelayEnabled(state_.reverb.predelayEnabled);
        reverbNode->setPreDelayMs(state_.reverb.predelayMs);
        reverbNode->setDecay(state_.reverb.decaySeconds);
        reverbNode->setDamping(state_.reverb.damping);
        reverbNode->setDiffusion(state_.reverb.diffusion);
        reverbNode->setLowCutHz(state_.reverb.lowCutHz);
        reverbNode->setHighCutHz(state_.reverb.highCutHz);
        reverbNode->setWidth(state_.reverb.width);

        if (state_.reverb.sendReturnMode) {
            reverbNode->setRoutingMode(audiocore::ReverbRoutingMode::SendReturn);
            reverbNode->setMix(1.0f);
        } else {
            reverbNode->setRoutingMode(audiocore::ReverbRoutingMode::Insert);
            reverbNode->setMix(state_.reverb.mix);
        }
    }

    if (afsNode) {
        afsNode->setBypassed(!state_.afs.enabled);
        afsNode->setSensitivity(state_.afs.sensitivity);
        afsNode->setMaxNotches(static_cast<size_t>(state_.afs.maxNotches));
        afsNode->setMaxDepthDb(state_.afs.maxDepthDb);
        afsNode->setAttackMs(state_.afs.attackMs);
        afsNode->setHoldMs(state_.afs.holdMs);
        afsNode->setReleaseMs(state_.afs.releaseMs);
    }

    if (alignNode) {
        alignNode->setBypassed(!state_.output.enabled);
        alignNode->setStereoLink(state_.output.stereoLink);
        alignNode->setChannelDelayMs(0, state_.output.delayMsL);
        alignNode->setChannelPolarity(0, state_.output.polarityInvertL);
        alignNode->setChannelMute(0, state_.output.muteL);
        alignNode->setChannelDelayMs(1, state_.output.delayMsR);
        alignNode->setChannelPolarity(1, state_.output.polarityInvertR);
        alignNode->setChannelMute(1, state_.output.muteR);
    }
}

const char* MixerControlManager::tempoDivisionToString(int division) {
    switch (division) {
        case 0: return "1/4";
        case 1: return "1/8";
        case 2: return "1/8T";
        case 3: return "1/16";
        case 4: return "1/2";
        case 5: return "1/1";
        default: return "1/4";
    }
}

float MixerControlManager::calculateTempoDelayMs(float bpm, int division) {
    if (bpm <= 0.0f) bpm = 120.0f;
    float beatMs = 60000.0f / bpm;
    switch (division) {
        case 0: return beatMs;              // 1/4 (quarter note)
        case 1: return beatMs * 0.5f;       // 1/8 (eighth note)
        case 2: return beatMs * (1.0f / 3.0f); // 1/8T (triplet eighth)
        case 3: return beatMs * 0.25f;      // 1/16 (sixteenth note)
        case 4: return beatMs * 2.0f;       // 1/2 (half note)
        case 5: return beatMs * 4.0f;       // 1/1 (whole note)
        default: return beatMs;
    }
}

std::string MixerControlManager::formatReverbMixLabel(float mix, bool sendReturnMode) {
    if (sendReturnMode) {
        return "SEND/RETURN: 100% WET (Return controls wet level)";
    }
    int pct = std::max(0, std::min(100, static_cast<int>(std::round(mix * 100.0f))));
    int dotPos = std::max(0, std::min(12, (pct * 12 + 50) / 100));
    std::string s;
    for (int i = 0; i < 13; ++i) {
        if (i == dotPos) s += "●";
        else s += "-";
    }
    return "DRY <" + s + "> WET (" + std::to_string(pct) + "%)";
}

// ------------------------------------------------------------
// Preset / Scene Serialization & JSON Parser
// ------------------------------------------------------------

std::string MixerControlManager::serializeToJson(const MixerState& s) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(3);
    ss << "{\n";
    ss << "  \"version\": 1,\n";
    ss << "  \"scene_name\": \"" << s.sceneName << "\",\n";
    ss << "  \"master.bypassed\": " << (s.masterBypass ? "true" : "false") << ",\n";

    // Channel Strip
    ss << "  \"channel.gain\": " << s.channel.gainDb << ",\n";
    ss << "  \"channel.pan\": " << s.channel.pan << ",\n";
    ss << "  \"fx.reverb_send\": " << s.channel.reverbSendDb << ",\n";
    ss << "  \"fx.delay_send\": " << s.channel.delaySendDb << ",\n";
    ss << "  \"fx.fx_return\": " << s.channel.fxReturnDb << ",\n";

    // Gate
    ss << "  \"gate.enabled\": " << (s.channel.gateEnabled ? "true" : "false") << ",\n";
    ss << "  \"gate.stereo_link\": " << (s.channel.gateStereoLink ? "true" : "false") << ",\n";
    ss << "  \"gate.threshold\": " << s.channel.gateThresholdDb << ",\n";
    ss << "  \"gate.range\": " << s.channel.gateRangeDb << ",\n";
    ss << "  \"gate.attack\": " << s.channel.gateAttackMs << ",\n";
    ss << "  \"gate.hold\": " << s.channel.gateHoldMs << ",\n";
    ss << "  \"gate.release\": " << s.channel.gateReleaseMs << ",\n";

    // PEQ
    ss << "  \"peq.enabled\": " << (s.channel.peqEnabled ? "true" : "false") << ",\n";
    for (int b = 0; b < 4; ++b) {
        std::string p = "peq.band" + std::to_string(b);
        ss << "  \"" << p << ".enabled\": " << (s.channel.peqBands[b].enabled ? "true" : "false") << ",\n";
        ss << "  \"" << p << ".type\": " << s.channel.peqBands[b].type << ",\n";
        ss << "  \"" << p << ".frequency\": " << s.channel.peqBands[b].frequency << ",\n";
        ss << "  \"" << p << ".gain\": " << s.channel.peqBands[b].gainDb << ",\n";
        ss << "  \"" << p << ".q\": " << s.channel.peqBands[b].q << ",\n";
    }

    // GEQ
    ss << "  \"geq.enabled\": " << (s.channel.geqEnabled ? "true" : "false") << ",\n";
    for (int b = 0; b < 31; ++b) {
        ss << "  \"geq.band" << b << ".gain\": " << s.channel.geqBands[b] << ",\n";
    }

    // Compressor
    ss << "  \"compressor.enabled\": " << (s.channel.compEnabled ? "true" : "false") << ",\n";
    ss << "  \"compressor.threshold\": " << s.channel.compThresholdDb << ",\n";
    ss << "  \"compressor.ratio\": " << s.channel.compRatio << ",\n";
    ss << "  \"compressor.attack\": " << s.channel.compAttackMs << ",\n";
    ss << "  \"compressor.release\": " << s.channel.compReleaseMs << ",\n";
    ss << "  \"compressor.makeup\": " << s.channel.compMakeupDb << ",\n";

    // Limiter
    ss << "  \"limiter.enabled\": " << (s.channel.limEnabled ? "true" : "false") << ",\n";
    ss << "  \"limiter.ceiling\": " << s.channel.limCeilingDb << ",\n";
    ss << "  \"limiter.release\": " << s.channel.limReleaseMs << ",\n";

    // Reverb
    ss << "  \"reverb.enabled\": " << (s.reverb.enabled ? "true" : "false") << ",\n";
    ss << "  \"reverb.type\": " << s.reverb.type << ",\n";
    ss << "  \"reverb.predelay_enabled\": " << (s.reverb.predelayEnabled ? "true" : "false") << ",\n";
    ss << "  \"reverb.predelay\": " << s.reverb.predelayMs << ",\n";
    ss << "  \"reverb.decay\": " << s.reverb.decaySeconds << ",\n";
    ss << "  \"reverb.damping\": " << s.reverb.damping << ",\n";
    ss << "  \"reverb.diffusion\": " << s.reverb.diffusion << ",\n";
    ss << "  \"reverb.low_cut\": " << s.reverb.lowCutHz << ",\n";
    ss << "  \"reverb.high_cut\": " << s.reverb.highCutHz << ",\n";
    ss << "  \"reverb.width\": " << s.reverb.width << ",\n";
    ss << "  \"reverb.mix\": " << s.reverb.mix << ",\n";
    ss << "  \"reverb.send_return_mode\": " << (s.reverb.sendReturnMode ? "true" : "false") << ",\n";
    ss << "  \"reverb.is_modified\": " << (s.reverb.isModified ? "true" : "false") << ",\n";

    // Delay
    ss << "  \"delay.enabled\": " << (s.delay.enabled ? "true" : "false") << ",\n";
    ss << "  \"delay.bpm\": " << s.delay.bpm << ",\n";
    ss << "  \"delay.sync_enabled\": " << (s.delay.tempoSync ? "true" : "false") << ",\n";
    ss << "  \"delay.sync_division\": " << s.delay.syncDivision << ",\n";
    ss << "  \"delay.feedback\": " << s.delay.feedback << ",\n";
    ss << "  \"delay.low_cut\": " << s.delay.lowCutHz << ",\n";
    ss << "  \"delay.high_cut\": " << s.delay.highCutHz << ",\n";
    ss << "  \"delay.width\": " << s.delay.width << ",\n";
    ss << "  \"delay.mix\": " << s.delay.mix << ",\n";
    for (int t = 0; t < 4; ++t) {
        std::string tp = "delay.tap" + std::to_string(t);
        ss << "  \"" << tp << ".enabled\": " << (s.delay.taps[t].enabled ? "true" : "false") << ",\n";
        ss << "  \"" << tp << ".time\": " << s.delay.taps[t].timeMs << ",\n";
        ss << "  \"" << tp << ".level\": " << s.delay.taps[t].level << ",\n";
        ss << "  \"" << tp << ".pan\": " << s.delay.taps[t].pan << ",\n";
    }

    // AFS
    ss << "  \"afs.enabled\": " << (s.afs.enabled ? "true" : "false") << ",\n";
    ss << "  \"afs.sensitivity\": " << s.afs.sensitivity << ",\n";
    ss << "  \"afs.max_notches\": " << s.afs.maxNotches << ",\n";
    ss << "  \"afs.max_depth\": " << s.afs.maxDepthDb << ",\n";
    ss << "  \"afs.attack\": " << s.afs.attackMs << ",\n";
    ss << "  \"afs.hold\": " << s.afs.holdMs << ",\n";
    ss << "  \"afs.release\": " << s.afs.releaseMs << ",\n";

    // Output Time Alignment (v1.2)
    ss << "  \"output.enabled\": " << (s.output.enabled ? "true" : "false") << ",\n";
    ss << "  \"output.stereo_link\": " << (s.output.stereoLink ? "true" : "false") << ",\n";
    ss << "  \"output.reference\": " << s.output.referenceOutput << ",\n";
    ss << "  \"output.main_l.delay_ms\": " << s.output.delayMsL << ",\n";
    ss << "  \"output.main_l.polarity_invert\": " << (s.output.polarityInvertL ? "true" : "false") << ",\n";
    ss << "  \"output.main_l.mute\": " << (s.output.muteL ? "true" : "false") << ",\n";
    ss << "  \"output.main_r.delay_ms\": " << s.output.delayMsR << ",\n";
    ss << "  \"output.main_r.polarity_invert\": " << (s.output.polarityInvertR ? "true" : "false") << ",\n";
    ss << "  \"output.main_r.mute\": " << (s.output.muteR ? "true" : "false") << "\n";

    ss << "}\n";
    return ss.str();
}

namespace {

// Lightweight Key-Value JSON Parser
std::map<std::string, std::string> parseJsonFlat(const std::string& json) {
    std::map<std::string, std::string> kv;
    size_t i = 0;
    while (i < json.size()) {
        size_t keyStart = json.find('"', i);
        if (keyStart == std::string::npos) break;
        size_t keyEnd = json.find('"', keyStart + 1);
        if (keyEnd == std::string::npos) break;
        std::string key = json.substr(keyStart + 1, keyEnd - keyStart - 1);

        size_t colon = json.find(':', keyEnd + 1);
        if (colon == std::string::npos) break;

        size_t valStart = json.find_first_not_of(" \t\r\n", colon + 1);
        if (valStart == std::string::npos) break;

        std::string val;
        if (json[valStart] == '"') {
            size_t valEnd = json.find('"', valStart + 1);
            if (valEnd == std::string::npos) break;
            val = json.substr(valStart + 1, valEnd - valStart - 1);
            i = valEnd + 1;
        } else {
            size_t valEnd = json.find_first_of(",}\r\n", valStart);
            if (valEnd == std::string::npos) valEnd = json.size();
            val = json.substr(valStart, valEnd - valStart);
            // Trim whitespace
            while (!val.empty() && (val.back() == ' ' || val.back() == '\t' || val.back() == '\r')) val.pop_back();
            i = valEnd;
        }
        kv[key] = val;
    }
    return kv;
}

float getFloat(const std::map<std::string, std::string>& kv, const std::string& key, float def) {
    auto it = kv.find(key);
    if (it != kv.end()) {
        try { return std::stof(it->second); } catch (...) {}
    }
    return def;
}

int getInt(const std::map<std::string, std::string>& kv, const std::string& key, int def) {
    auto it = kv.find(key);
    if (it != kv.end()) {
        try { return std::stoi(it->second); } catch (...) {}
    }
    return def;
}

bool getBool(const std::map<std::string, std::string>& kv, const std::string& key, bool def) {
    auto it = kv.find(key);
    if (it != kv.end()) {
        if (it->second == "true" || it->second == "1") return true;
        if (it->second == "false" || it->second == "0") return false;
    }
    return def;
}

std::string getString(const std::map<std::string, std::string>& kv, const std::string& key, const std::string& def) {
    auto it = kv.find(key);
    return (it != kv.end()) ? it->second : def;
}

} // namespace

bool MixerControlManager::deserializeFromJson(const std::string& json, MixerState& out) {
    auto kv = parseJsonFlat(json);
    if (kv.empty()) return false;

    out.sceneName = getString(kv, "scene_name", out.sceneName);
    out.masterBypass = getBool(kv, "master.bypassed", out.masterBypass);

    out.channel.gainDb = getFloat(kv, "channel.gain", out.channel.gainDb);
    out.channel.pan = getFloat(kv, "channel.pan", out.channel.pan);
    out.channel.reverbSendDb = getFloat(kv, "fx.reverb_send", out.channel.reverbSendDb);
    out.channel.delaySendDb = getFloat(kv, "fx.delay_send", out.channel.delaySendDb);
    out.channel.fxReturnDb = getFloat(kv, "fx.fx_return", out.channel.fxReturnDb);

    out.channel.gateEnabled = getBool(kv, "gate.enabled", out.channel.gateEnabled);
    out.channel.gateStereoLink = getBool(kv, "gate.stereo_link", out.channel.gateStereoLink);
    out.channel.gateThresholdDb = getFloat(kv, "gate.threshold", out.channel.gateThresholdDb);
    out.channel.gateRangeDb = getFloat(kv, "gate.range", out.channel.gateRangeDb);
    out.channel.gateAttackMs = getFloat(kv, "gate.attack", out.channel.gateAttackMs);
    out.channel.gateHoldMs = getFloat(kv, "gate.hold", out.channel.gateHoldMs);
    out.channel.gateReleaseMs = getFloat(kv, "gate.release", out.channel.gateReleaseMs);

    out.channel.peqEnabled = getBool(kv, "peq.enabled", out.channel.peqEnabled);
    for (int b = 0; b < 4; ++b) {
        std::string p = "peq.band" + std::to_string(b);
        out.channel.peqBands[b].enabled = getBool(kv, p + ".enabled", out.channel.peqBands[b].enabled);
        out.channel.peqBands[b].type = getInt(kv, p + ".type", out.channel.peqBands[b].type);
        out.channel.peqBands[b].frequency = getFloat(kv, p + ".frequency", out.channel.peqBands[b].frequency);
        out.channel.peqBands[b].gainDb = getFloat(kv, p + ".gain", out.channel.peqBands[b].gainDb);
        out.channel.peqBands[b].q = getFloat(kv, p + ".q", out.channel.peqBands[b].q);
    }

    out.channel.geqEnabled = getBool(kv, "geq.enabled", out.channel.geqEnabled);
    for (int b = 0; b < 31; ++b) {
        out.channel.geqBands[b] = getFloat(kv, "geq.band" + std::to_string(b) + ".gain", out.channel.geqBands[b]);
    }

    out.channel.compEnabled = getBool(kv, "compressor.enabled", out.channel.compEnabled);
    out.channel.compThresholdDb = getFloat(kv, "compressor.threshold", out.channel.compThresholdDb);
    out.channel.compRatio = getFloat(kv, "compressor.ratio", out.channel.compRatio);
    out.channel.compAttackMs = getFloat(kv, "compressor.attack", out.channel.compAttackMs);
    out.channel.compReleaseMs = getFloat(kv, "compressor.release", out.channel.compReleaseMs);
    out.channel.compMakeupDb = getFloat(kv, "compressor.makeup", out.channel.compMakeupDb);

    out.channel.limEnabled = getBool(kv, "limiter.enabled", out.channel.limEnabled);
    out.channel.limCeilingDb = getFloat(kv, "limiter.ceiling", out.channel.limCeilingDb);
    out.channel.limReleaseMs = getFloat(kv, "limiter.release", out.channel.limReleaseMs);

    out.reverb.enabled = getBool(kv, "reverb.enabled", out.reverb.enabled);
    out.reverb.type = getInt(kv, "reverb.type", out.reverb.type);
    out.reverb.predelayEnabled = getBool(kv, "reverb.predelay_enabled", out.reverb.predelayEnabled);
    out.reverb.predelayMs = getFloat(kv, "reverb.predelay", out.reverb.predelayMs);
    out.reverb.decaySeconds = getFloat(kv, "reverb.decay", out.reverb.decaySeconds);
    out.reverb.damping = getFloat(kv, "reverb.damping", out.reverb.damping);
    out.reverb.diffusion = getFloat(kv, "reverb.diffusion", out.reverb.diffusion);
    out.reverb.lowCutHz = getFloat(kv, "reverb.low_cut", out.reverb.lowCutHz);
    out.reverb.highCutHz = getFloat(kv, "reverb.high_cut", out.reverb.highCutHz);
    out.reverb.width = getFloat(kv, "reverb.width", out.reverb.width);
    out.reverb.mix = getFloat(kv, "reverb.mix", out.reverb.mix);
    out.reverb.sendReturnMode = getBool(kv, "reverb.send_return_mode", out.reverb.sendReturnMode);
    out.reverb.isModified = getBool(kv, "reverb.is_modified", out.reverb.isModified);

    out.delay.enabled = getBool(kv, "delay.enabled", out.delay.enabled);
    out.delay.bpm = getFloat(kv, "delay.bpm", out.delay.bpm);
    out.delay.tempoSync = getBool(kv, "delay.sync_enabled", out.delay.tempoSync);
    out.delay.syncDivision = getInt(kv, "delay.sync_division", out.delay.syncDivision);
    out.delay.feedback = getFloat(kv, "delay.feedback", out.delay.feedback);
    out.delay.lowCutHz = getFloat(kv, "delay.low_cut", out.delay.lowCutHz);
    out.delay.highCutHz = getFloat(kv, "delay.high_cut", out.delay.highCutHz);
    out.delay.width = getFloat(kv, "delay.width", out.delay.width);
    out.delay.mix = getFloat(kv, "delay.mix", out.delay.mix);
    for (int t = 0; t < 4; ++t) {
        std::string tp = "delay.tap" + std::to_string(t);
        out.delay.taps[t].enabled = getBool(kv, tp + ".enabled", out.delay.taps[t].enabled);
        out.delay.taps[t].timeMs = getFloat(kv, tp + ".time", out.delay.taps[t].timeMs);
        out.delay.taps[t].level = getFloat(kv, tp + ".level", out.delay.taps[t].level);
        out.delay.taps[t].pan = getFloat(kv, tp + ".pan", out.delay.taps[t].pan);
    }

    out.afs.enabled = getBool(kv, "afs.enabled", out.afs.enabled);
    out.afs.sensitivity = getFloat(kv, "afs.sensitivity", out.afs.sensitivity);
    out.afs.maxNotches = getInt(kv, "afs.max_notches", out.afs.maxNotches);
    out.afs.maxDepthDb = getFloat(kv, "afs.max_depth", out.afs.maxDepthDb);
    out.afs.attackMs = getFloat(kv, "afs.attack", out.afs.attackMs);
    out.afs.holdMs = getFloat(kv, "afs.hold", out.afs.holdMs);
    out.afs.releaseMs = getFloat(kv, "afs.release", out.afs.releaseMs);

    // Output Time Alignment (v1.2)
    out.output.enabled = getBool(kv, "output.enabled", out.output.enabled);
    out.output.stereoLink = getBool(kv, "output.stereo_link", out.output.stereoLink);
    out.output.referenceOutput = getInt(kv, "output.reference", out.output.referenceOutput);
    out.output.delayMsL = getFloat(kv, "output.main_l.delay_ms", out.output.delayMsL);
    out.output.polarityInvertL = getBool(kv, "output.main_l.polarity_invert", out.output.polarityInvertL);
    out.output.muteL = getBool(kv, "output.main_l.mute", out.output.muteL);
    out.output.delayMsR = getFloat(kv, "output.main_r.delay_ms", out.output.delayMsR);
    out.output.polarityInvertR = getBool(kv, "output.main_r.polarity_invert", out.output.polarityInvertR);
    out.output.muteR = getBool(kv, "output.main_r.mute", out.output.muteR);

    return true;
}

bool MixerControlManager::saveToFile(const std::string& filePath, const MixerState& state) {
    std::string json = serializeToJson(state);
    std::ofstream ofs(filePath, std::ios::trunc);
    if (!ofs.is_open()) return false;
    ofs << json;
    return ofs.good();
}

bool MixerControlManager::loadFromFile(const std::string& filePath, MixerState& outState) {
    std::ifstream ifs(filePath);
    if (!ifs.is_open()) return false;
    std::stringstream buffer;
    buffer << ifs.rdbuf();
    return deserializeFromJson(buffer.str(), outState);
}

} // namespace testbench
