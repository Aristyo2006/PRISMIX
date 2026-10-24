#pragma once

#include <string>

std::string default_mixer_scene_path();
bool save_mixer_scene(const std::string& path, std::string* error = nullptr);
bool load_mixer_scene(const std::string& path, std::string* error = nullptr);
