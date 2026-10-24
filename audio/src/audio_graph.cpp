#include "audiocore/audio_graph.hpp"
#include <cassert>

namespace audiocore {

AudioGraph::AudioGraph() = default;

void AudioGraph::configure(uint32_t channels, uint32_t maxFrames) {
    channels_ = channels;
    max_frames_ = maxFrames;

    scratch_buffer_a_.resize(channels_, max_frames_);
    scratch_buffer_b_.resize(channels_, max_frames_);
}

void AudioGraph::addNode(std::shared_ptr<AudioNode> node) {
    if (node) {
        nodes_.push_back(std::move(node));
    }
}

void AudioGraph::removeNode(size_t index) {
    if (index < nodes_.size()) {
        nodes_.erase(nodes_.begin() + index);
    }
}

void AudioGraph::clearNodes() {
    nodes_.clear();
}

std::shared_ptr<AudioNode> AudioGraph::getNode(size_t index) const {
    if (index < nodes_.size()) {
        return nodes_[index];
    }
    return nullptr;
}

void AudioGraph::reset() {
    for (auto& node : nodes_) {
        if (node) {
            node->reset();
        }
    }
}

void AudioGraph::process(
    const AudioBuffer& input,
    AudioBuffer& output,
    uint32_t frames)
{
    if (bypassed_.load(std::memory_order_relaxed)) {
        if (&input != &output) {
            output.copyFrom(input, frames);
        }
        return;
    }

    const size_t count = nodes_.size();

    if (count == 0) {
        if (&input != &output) {
            output.copyFrom(input, frames);
        }
        return;
    }

    if (count == 1) {
        nodes_[0]->process(input, output, frames);
        return;
    }

    // Pipeline with 2 or more nodes using preallocated scratch buffers
    // Ensure scratch buffers are configured
    if (scratch_buffer_a_.isEmpty() || scratch_buffer_a_.getFrameCount() < frames) {
        // Fallback: in-place processing using output if scratch unconfigured
        nodes_[0]->process(input, output, frames);
        for (size_t i = 1; i < count; ++i) {
            nodes_[i]->process(output, output, frames);
        }
        return;
    }

    // Node 0: input -> scratch_buffer_a_
    nodes_[0]->process(input, scratch_buffer_a_, frames);

    AudioBuffer* current_in = &scratch_buffer_a_;
    AudioBuffer* current_out = &scratch_buffer_b_;

    // Intermediate nodes
    for (size_t i = 1; i < count - 1; ++i) {
        nodes_[i]->process(*current_in, *current_out, frames);
        std::swap(current_in, current_out);
    }

    // Final node: current_in -> output
    nodes_.back()->process(*current_in, output, frames);
}

} // namespace audiocore

