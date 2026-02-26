#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "IPlug_include_in_plug_hdr.h"

const int kNumPresets = 1;
constexpr int kMaxTextBufferLength = 512;
constexpr int kSAMParamMin = 0;
constexpr int kSAMParamMax = 255;
constexpr int kDefaultSpeed = 72;
constexpr int kDefaultPitch = 64;
constexpr int kDefaultThroat = 128;
constexpr int kDefaultMouth = 128;

constexpr uint32_t kStateMagic = 0x53414D53; // SAMS
constexpr uint32_t kStateVersion = 1;
constexpr uint32_t kStateFlagPlaybackPending = 1u << 0;

enum EParams
{
  kOutputGain = 0,
  kSpeed,
  kPitch,
  kThroat,
  kMouth,
  kNumParams
};

enum ECtrlTags
{
  kCtrlTagPlaybackStatus = 0,
  kCtrlTagTextPanel
};

using namespace iplug;
using namespace igraphics;

class SAMVST final : public Plugin
{
public:
  explicit SAMVST(const InstanceInfo& info);

  bool SerializeState(IByteChunk& chunk) const override;
  int UnserializeState(const IByteChunk& chunk, int startPos) override;
  void OnReset() override;
  void OnParamChange(int paramIdx) override;

#if IPLUG_EDITOR
  void OnUIOpen() override;
#endif

  void OnIdle() override;

#if IPLUG_DSP
  void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;
  void ProcessMidiMsg(const IMidiMsg& msg) override;
#endif

private:
  void RequestPlaybackTrigger();
  bool RenderPhraseFromText();
  void ResetPlaybackPosition();
  float ReadSAMSample();
  void SetTextBuffer(const char* text);
  std::string GetTextBuffer() const;
  void UpdatePlaybackStatusText(bool acknowledged);
  static float U8ToFloat(uint8_t v);

#if IPLUG_EDITOR
  void SyncUIState();
#endif

  std::atomic<bool> mPlaybackTriggerPending{false};
  std::atomic<bool> mNeedsRender{true};
  std::atomic<int> mPlaybackTriggerRequests{0};
  std::atomic<int> mPlaybackTriggerAcks{0};
  int mLastPlaybackAckSeen = -1;

  mutable std::mutex mTextMutex;
  std::string mTextBuffer = "HELLO FROM SAM VST";

  std::vector<uint8_t> mRenderedPCM;
  float mRenderedDCBias = 0.f;
  double mSAMReadPos = 0.0;
  double mSAMReadIncrement = 0.5;
  bool mIsPlaying = false;
  const std::string mFallbackPhrase = "HELLO FROM SAM VST";
};
