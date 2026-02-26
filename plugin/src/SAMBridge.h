#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sam_bridge {

constexpr double kSAMSourceSampleRate = 22050.0;

// Render text via the SAM C core and return copied unsigned 8-bit PCM at 22.05kHz.
bool RenderTextToPCM(const std::string& text,
                     int speed,
                     int pitch,
                     int throat,
                     int mouth,
                     std::vector<uint8_t>& pcmOut);

} // namespace sam_bridge

