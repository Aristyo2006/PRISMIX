#include "lv2_manifest_parser.h"
#include <sstream>
#include <algorithm>
#include <cctype>

namespace prismix::lv2 {

namespace {

std::string trim(const std::string& str) {
    auto start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

std::string stripComments(const std::string& input) {
    std::string result;
    result.reserve(input.size());
    std::istringstream stream(input);
    std::string line;
    for (; std::getline(stream, line); ) {
        bool in_quotes = false;
        bool in_angles = false;
        std::string cleaned;
        for (size_t i = 0; i < line.size(); ++i) {
            char c = line[i];
            if (c == '"') {
                in_quotes = !in_quotes;
            } else if (c == '<' && !in_quotes) {
                in_angles = true;
            } else if (c == '>' && !in_quotes) {
                in_angles = false;
            } else if (c == '#' && !in_quotes && !in_angles) {
                break;
            }
            cleaned.push_back(c);
        }
        result += cleaned + "\n";
    }
    return result;
}

std::string extractIri(const std::string& token) {
    auto open = token.find('<');
    auto close = token.find('>', open == std::string::npos ? 0 : open);
    if (open != std::string::npos && close != std::string::npos && close > open) {
        return token.substr(open + 1, close - open - 1);
    }
    return trim(token);
}

std::string extractQuotedString(const std::string& s) {
    auto q1 = s.find('"');
    if (q1 == std::string::npos) return "";
    auto q2 = s.find('"', q1 + 1);
    if (q2 == std::string::npos) return "";
    return s.substr(q1 + 1, q2 - q1 - 1);
}

size_t findNextStatementEnd(const std::string& str, size_t start_pos) {
    bool in_quotes = false;
    bool in_angles = false;
    for (size_t i = start_pos; i < str.size(); ++i) {
        char c = str[i];
        if (c == '"') {
            in_quotes = !in_quotes;
        } else if (c == '<' && !in_quotes) {
            in_angles = true;
        } else if (c == '>' && !in_quotes) {
            in_angles = false;
        } else if (c == '.' && !in_quotes && !in_angles) {
            return i;
        }
    }
    return std::string::npos;
}

} // namespace

bool Lv2ManifestParser::parseManifest(const std::string& manifest_content,
                                      std::vector<Lv2ParsedPlugin>& out_plugins) {
    std::string clean = stripComments(manifest_content);
    // Find blocks ending with '.' outside angle brackets & quotes
    size_t pos = 0;
    while (pos < clean.size()) {
        size_t dot_pos = findNextStatementEnd(clean, pos);
        if (dot_pos == std::string::npos) break;

        std::string statement = clean.substr(pos, dot_pos - pos);
        pos = dot_pos + 1;

        if (statement.find("lv2:Plugin") != std::string::npos ||
            statement.find("a lv2:Plugin") != std::string::npos ||
            statement.find("http://lv2plug.in/ns/lv2core#Plugin") != std::string::npos) {
            
            Lv2ParsedPlugin plugin;
            // Extract URI: first <...> in statement
            plugin.uri = extractIri(statement);

            // Extract binary: lv2:binary <...>
            auto bin_idx = statement.find("lv2:binary");
            if (bin_idx == std::string::npos) {
                bin_idx = statement.find("binary");
            }
            if (bin_idx != std::string::npos) {
                plugin.binary_rel_path = extractIri(statement.substr(bin_idx));
            }

            // Extract seeAlso: rdfs:seeAlso <...>
            auto see_idx = statement.find("rdfs:seeAlso");
            if (see_idx == std::string::npos) {
                see_idx = statement.find("seeAlso");
            }
            if (see_idx != std::string::npos) {
                plugin.see_also_rel_path = extractIri(statement.substr(see_idx));
            }

            if (!plugin.uri.empty()) {
                out_plugins.push_back(std::move(plugin));
            }
        }
    }
    return !out_plugins.empty();
}

bool Lv2ManifestParser::parsePluginTtl(const std::string& ttl_content,
                                       Lv2ParsedPlugin& plugin) {
    std::string clean = stripComments(ttl_content);

    // Extract doap:name
    auto name_idx = clean.find("doap:name");
    if (name_idx != std::string::npos) {
        plugin.name = extractQuotedString(clean.substr(name_idx));
    }
    if (plugin.name.empty()) {
        auto lv2_name_idx = clean.find("lv2:name");
        if (lv2_name_idx != std::string::npos) {
            plugin.name = extractQuotedString(clean.substr(lv2_name_idx));
        }
    }

    // Extract category
    if (clean.find("DistortionPlugin") != std::string::npos ||
        clean.find("AmplifierPlugin") != std::string::npos ||
        clean.find("SimulatorPlugin") != std::string::npos) {
        plugin.category = "Distortion";
    } else if (clean.find("ReverbPlugin") != std::string::npos) {
        plugin.category = "Reverb";
    } else if (clean.find("DelayPlugin") != std::string::npos) {
        plugin.category = "Delay";
    } else if (clean.find("CompressorPlugin") != std::string::npos ||
               clean.find("DynamicsPlugin") != std::string::npos ||
               clean.find("LimiterPlugin") != std::string::npos ||
               clean.find("GatePlugin") != std::string::npos) {
        plugin.category = "Dynamics";
    } else if (clean.find("EQPlugin") != std::string::npos ||
               clean.find("FilterPlugin") != std::string::npos) {
        plugin.category = "EQ";
    } else if (clean.find("ModulatorPlugin") != std::string::npos ||
               clean.find("ChorusPlugin") != std::string::npos ||
               clean.find("PhaserPlugin") != std::string::npos ||
               clean.find("FlangerPlugin") != std::string::npos) {
        plugin.category = "Modulation";
    } else {
        plugin.category = "LV2 Effect";
    }

    // Extract maintainer/author
    auto m_idx = clean.find("doap:maintainer");
    if (m_idx != std::string::npos) {
        auto sub = clean.substr(m_idx, 200);
        auto f_idx = sub.find("foaf:name");
        if (f_idx != std::string::npos) {
            plugin.author = extractQuotedString(sub.substr(f_idx));
        }
    }
    if (plugin.author.empty()) {
        plugin.author = "LV2 Community";
    }

    // Required features: lv2:requiredFeature <...>
    size_t rf_pos = 0;
    while ((rf_pos = clean.find("requiredFeature", rf_pos)) != std::string::npos) {
        auto open = clean.find('<', rf_pos);
        if (open == std::string::npos) break;
        auto close = clean.find('>', open);
        if (close == std::string::npos) break;
        std::string feat = clean.substr(open + 1, close - open - 1);
        if (!feat.empty()) plugin.required_features.push_back(feat);
        rf_pos = close + 1;
    }

    // Optional features: lv2:optionalFeature <...>
    size_t of_pos = 0;
    while ((of_pos = clean.find("optionalFeature", of_pos)) != std::string::npos) {
        auto open = clean.find('<', of_pos);
        if (open == std::string::npos) break;
        auto close = clean.find('>', open);
        if (close == std::string::npos) break;
        std::string feat = clean.substr(open + 1, close - open - 1);
        if (!feat.empty()) plugin.optional_features.push_back(feat);
        of_pos = close + 1;
    }

    // Parse ports: scan all blank nodes `[` ... `]` containing "Port"
    size_t pos = 0;
    while (pos < clean.size()) {
        auto bracket_open = clean.find('[', pos);
        if (bracket_open == std::string::npos) break;

        // Find matching closing bracket ']'
        int depth = 1;
        size_t bracket_close = bracket_open + 1;
        while (bracket_close < clean.size() && depth > 0) {
            if (clean[bracket_close] == '[') ++depth;
            else if (clean[bracket_close] == ']') --depth;
            if (depth == 0) break;
            ++bracket_close;
        }
        if (depth != 0) break;

        std::string port_str = clean.substr(bracket_open + 1, bracket_close - bracket_open - 1);
        pos = bracket_close + 1;

        if (port_str.find("Port") == std::string::npos) {
            continue;
        }

        Lv2PortMetadata port;
        // Direction
        if (port_str.find("InputPort") != std::string::npos) {
            port.direction = PortDirection::Input;
        } else if (port_str.find("OutputPort") != std::string::npos) {
            port.direction = PortDirection::Output;
        }

        // Type
        if (port_str.find("AudioPort") != std::string::npos) {
            port.type = PortType::Audio;
        } else if (port_str.find("ControlPort") != std::string::npos) {
            port.type = PortType::Control;
        } else {
            port.type = PortType::Other;
        }

        // Index
        auto idx_pos = port_str.find("lv2:index");
        if (idx_pos == std::string::npos) idx_pos = port_str.find("index");
        if (idx_pos != std::string::npos) {
            auto val_str = port_str.substr(idx_pos + 5);
            auto num_pos = val_str.find_first_of("0123456789");
            if (num_pos != std::string::npos) {
                port.index = static_cast<uint32_t>(std::stoul(val_str.substr(num_pos)));
            }
        }

        // Symbol
        auto sym_pos = port_str.find("lv2:symbol");
        if (sym_pos == std::string::npos) sym_pos = port_str.find("symbol");
        if (sym_pos != std::string::npos) {
            port.symbol = extractQuotedString(port_str.substr(sym_pos));
        }

        // Name
        auto p_name_pos = port_str.find("lv2:name");
        if (p_name_pos == std::string::npos) p_name_pos = port_str.find("name");
        if (p_name_pos != std::string::npos) {
            port.name = extractQuotedString(port_str.substr(p_name_pos));
        }
        if (port.name.empty()) port.name = port.symbol;

        // Default, minimum, maximum
        auto parse_float = [&](const std::string& key, float fallback) -> float {
            auto kpos = port_str.find(key);
            if (kpos == std::string::npos) return fallback;
            auto sub = port_str.substr(kpos + key.size());
            auto num_start = sub.find_first_of("-+0123456789.");
            if (num_start == std::string::npos) return fallback;
            try {
                return std::stof(sub.substr(num_start));
            } catch (...) {
                return fallback;
            }
        };

        port.default_value = parse_float("default", 0.0f);
        port.min_value = parse_float("minimum", 0.0f);
        port.max_value = parse_float("maximum", 1.0f);

        // Properties
        if (port_str.find("toggled") != std::string::npos) port.is_toggled = true;
        if (port_str.find("integer") != std::string::npos) port.is_integer = true;
        if (port_str.find("sampleRate") != std::string::npos) port.is_sample_rate = true;
        if (port_str.find("reportsLatency") != std::string::npos) port.reports_latency = true;

        // Count
        if (port.type == PortType::Audio) {
            if (port.direction == PortDirection::Input) plugin.audio_inputs++;
            else plugin.audio_outputs++;
        } else if (port.type == PortType::Control) {
            if (port.direction == PortDirection::Input) plugin.control_inputs++;
            else plugin.control_outputs++;
        }

        plugin.ports.push_back(port);
    }

    // Sort ports by index
    std::sort(plugin.ports.begin(), plugin.ports.end(),
              [](const Lv2PortMetadata& a, const Lv2PortMetadata& b) {
                  return a.index < b.index;
              });

    return true;
}

} // namespace prismix::lv2