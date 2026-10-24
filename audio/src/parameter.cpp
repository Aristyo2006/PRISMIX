#include "audiocore/parameter.hpp"

namespace audiocore {

Parameter::Parameter(std::string name, float defaultValue, float minValue, float maxValue, std::string unit)
    : name_(std::move(name))
    , unit_(std::move(unit))
    , min_value_(minValue)
    , max_value_(maxValue)
    , default_value_(std::clamp(defaultValue, minValue, maxValue))
    , value_(default_value_)
{
}

void Parameter::setValue(float val) noexcept {
    const float clamped = std::clamp(val, min_value_, max_value_);
    value_.store(clamped, std::memory_order_release);
}

float Parameter::getValue() const noexcept {
    return value_.load(std::memory_order_relaxed);
}

void Parameter::setNormalized(float norm) noexcept {
    const float clamped_norm = std::clamp(norm, 0.0f, 1.0f);
    const float scaled = min_value_ + clamped_norm * (max_value_ - min_value_);
    setValue(scaled);
}

float Parameter::getNormalized() const noexcept {
    if (max_value_ <= min_value_) return 0.0f;
    const float current = getValue();
    return std::clamp((current - min_value_) / (max_value_ - min_value_), 0.0f, 1.0f);
}

void Parameter::resetToDefault() noexcept {
    setValue(default_value_);
}

} // namespace audiocore

