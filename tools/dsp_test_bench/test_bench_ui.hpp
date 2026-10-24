#pragma once

#include <string>

namespace testbench {

class TestBenchUi {
public:
    static int runDesktopUi(const std::string& initialWavPath = "");
};

} // namespace testbench

