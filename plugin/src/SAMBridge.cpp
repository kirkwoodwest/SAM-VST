#include "SAMBridge.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <mutex>

extern "C" {
#include "reciter.h"
#include "sam.h"
}

extern "C" {
int debug = 0;
}

namespace sam_bridge {

namespace {
constexpr size_t kSAMInputBytes = 256;
constexpr unsigned char kReciterEndMarker = '[';

int ClampSAMParam(int value)
{
  return std::clamp(value, 0, 255);
}
} // namespace

bool RenderTextToPCM(const std::string& text,
                     int speed,
                     int pitch,
                     int throat,
                     int mouth,
                     std::vector<uint8_t>& pcmOut)
{
  static std::mutex sSAMMutex;
  std::lock_guard<std::mutex> lock(sSAMMutex);

  unsigned char input[kSAMInputBytes] = {};
  size_t n = std::min(text.size(), kSAMInputBytes - 2);

  for (size_t i = 0; i < n; ++i)
    input[i] = static_cast<unsigned char>(std::toupper(static_cast<unsigned char>(text[i])));

  input[n] = kReciterEndMarker;
  input[n + 1] = 0;

  if (!TextToPhonemes(input))
  {
    pcmOut.clear();
    return false;
  }

  SetSpeed(static_cast<unsigned char>(ClampSAMParam(speed)));
  SetPitch(static_cast<unsigned char>(ClampSAMParam(pitch)));
  SetThroat(static_cast<unsigned char>(ClampSAMParam(throat)));
  SetMouth(static_cast<unsigned char>(ClampSAMParam(mouth)));

  SetInput(input);

  if (!SAMMain())
  {
    pcmOut.clear();
    return false;
  }

  const int rawLength = GetBufferLength();
  const int sampleCount = rawLength > 0 ? (rawLength / 50) : 0;
  const char* rawBuffer = GetBuffer();

  if (sampleCount <= 0 || rawBuffer == nullptr)
  {
    pcmOut.clear();
    return false;
  }

  pcmOut.resize(static_cast<size_t>(sampleCount));
  std::memcpy(pcmOut.data(), rawBuffer, static_cast<size_t>(sampleCount));
  return true;
}

} // namespace sam_bridge
