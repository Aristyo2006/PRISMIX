#pragma once

#include <string>

namespace prismix::web {

// Returns the complete, self-contained single-page application (HTML/CSS/JS)
// for the PRISMIX MODEP-inspired Web Pedalboard Editor.
// Zero external CDN dependencies, 100% offline-ready.
const std::string& get_web_ui_html();

} // namespace prismix::web

