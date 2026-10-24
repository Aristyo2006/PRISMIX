#pragma once
#include <array>
#include <string>
#include <vector>

struct RoutingPoint {
    std::string source_name;
    std::string dest_name;
    bool connected = false;
    float gain_db = 0.0f;
};

class RoutingMatrix {
public:
    static constexpr size_t NUM_INPUTS = 16;
    static constexpr size_t NUM_CHANNELS = 32;
    static constexpr size_t NUM_BUSES = 8;
    static constexpr size_t NUM_AUX = 4;
    static constexpr size_t NUM_OUTPUTS = 8;

    RoutingMatrix();

    // Channel input assignment
    int get_channel_input_source(size_t ch) const;
    void set_channel_input_source(size_t ch, int input_source);

    // Channel to Bus matrix
    bool get_channel_bus(size_t ch, size_t bus) const;
    void set_channel_bus(size_t ch, size_t bus, bool connected);
    void toggle_channel_bus(size_t ch, size_t bus);

    // Channel to Direct Out / Main L/R
    bool get_channel_main_lr(size_t ch) const;
    void set_channel_main_lr(size_t ch, bool connected);

    // Bus to Main L/R
    bool get_bus_main_lr(size_t bus) const;
    void set_bus_main_lr(size_t bus, bool connected);

    // Physical Output patching: which internal signal goes to output port (0..7)
    // 0..7: Bus 1..8, 8..9: Main L/R, 10..13: Aux 1..4, 14..21: Direct Out 1..8
    int get_output_patch(size_t out_port) const;
    void set_output_patch(size_t out_port, int source_id);

    // Source names
    static const char* get_input_source_name(int idx);
    static const char* get_output_destination_name(size_t idx);
    static const char* get_internal_bus_name(size_t idx);

private:
    std::array<int, NUM_CHANNELS> channel_input_sources_{};
    std::array<std::array<bool, NUM_BUSES>, NUM_CHANNELS> channel_bus_matrix_{};
    std::array<bool, NUM_CHANNELS> channel_main_lr_{};
    std::array<bool, NUM_BUSES> bus_main_lr_{};
    std::array<int, NUM_OUTPUTS> output_patch_{};
};

extern RoutingMatrix g_routing_matrix;

