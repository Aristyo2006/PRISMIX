#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace testbench {

struct CorrectnessTestItem {
    std::string testId;
    std::string description;
    bool passed{false};
    double expected{0.0};
    double actual{0.0};
    double tolerance{0.0};
    std::string details;
};

struct CorrectnessSuiteResult {
    bool allPassed{true};
    int passedCount{0};
    int failedCount{0};
    std::vector<CorrectnessTestItem> items;
};

class CorrectnessVerifier {
public:
    static CorrectnessSuiteResult runAllTests(double sampleRate = 48000.0, uint32_t blockSize = 128);
};

} // namespace testbench

