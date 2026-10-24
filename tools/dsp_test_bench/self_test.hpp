#pragma once

#include <string>
#include <vector>

namespace testbench {

enum class SelfTestStatus {
    Pass,
    Fail,
    NotTested
};

struct SelfTestItem {
    int index{0};
    std::string name;
    SelfTestStatus status{SelfTestStatus::Pass};
    bool passed{false}; // True only if status == Pass
    std::string details;
};

struct SelfTestSuiteResult {
    bool allPassed{true};
    int passedCount{0};
    int failedCount{0};
    int notTestedCount{0};
    std::vector<SelfTestItem> items;
};

class SelfTester {
public:
    static SelfTestSuiteResult runAll();
    static SelfTestSuiteResult runCpuTelemetryTests();
    static SelfTestSuiteResult runSrcTests();
    static SelfTestSuiteResult runGateTests();
    static SelfTestSuiteResult runGeqTests();
    static SelfTestSuiteResult runDelayTests();
    static SelfTestSuiteResult runReverbTests();
    static SelfTestSuiteResult runAfsTests();
    static SelfTestSuiteResult runFxRegressionTests();
    static SelfTestSuiteResult runUiV2Tests();
    static SelfTestSuiteResult runAlignmentTests();
    static SelfTestSuiteResult runReverbPredelayTests();
    static SelfTestSuiteResult runReverbTemplateTests();
    static SelfTestSuiteResult runWasapiDeviceTests();
    static SelfTestSuiteResult runWasapiLoopbackTests();
    static void printFxQualitySelfDeclaration();
    static void printUiV2SelfDeclaration();
    static void printAlignmentSelfDeclaration();
    static void printReverbPredelaySelfDeclaration();
    static void printWasapiDeviceSelfDeclaration();
    static void printWasapiLoopbackSelfDeclaration();
};

} // namespace testbench
