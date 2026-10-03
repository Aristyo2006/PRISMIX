#pragma once

class MockAudioBackend {
public:
    void init();
    void update();
};

extern MockAudioBackend g_backend;
