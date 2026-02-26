#include "SAMVST.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>

#include "IPlug_include_in_plug_src.h"
#include "IControls.h"
#include "SAMBridge.h"

namespace
{
static const char* kUIFontID = "Roboto-Regular";
static const IColor kUiPurpleDark = IColor(255, 58, 45, 145);
static const IColor kUiPurpleLight = IColor(255, 122, 110, 208);

int32_t ClampNonNegativeInt32(int value)
{
  if (value < 0)
    return 0;

  constexpr int32_t kMax = std::numeric_limits<int32_t>::max();
  if (value > kMax)
    return kMax;

  return static_cast<int32_t>(value);
}

class SAMTextPanelControl final : public IMultiLineTextControl
{
public:
  SAMTextPanelControl(const IRECT& bounds, const char* str, std::function<void(const char*)> onTextChanged)
  : IMultiLineTextControl(bounds, str,
      DEFAULT_TEXT.WithAlign(EAlign::Near).WithSize(16.f).WithFont(kUIFontID).WithFGColor(kUiPurpleLight),
      kUiPurpleDark)
  , mOnTextChanged(std::move(onTextChanged))
  {
    mIgnoreMouse = false;
  }

  void StartEditing()
  {
    if (GetUI())
      GetUI()->CreateTextEntry(*this,
        DEFAULT_TEXT.WithAlign(EAlign::Near).WithSize(15.f).WithFont(kUIFontID).WithFGColor(kUiPurpleLight).WithTEColors(kUiPurpleDark, kUiPurpleLight),
        mRECT.GetPadded(-8.f), GetStr());
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    (void) x;
    (void) y;
    (void) mod;
    StartEditing();
  }

  void OnTextEntryCompletion(const char* str, int valIdx) override
  {
    (void) valIdx;
    SetStr(str ? str : "");

    if (mOnTextChanged)
      mOnTextChanged(str ? str : "");

    SetDirty(false);
  }

private:
  std::function<void(const char*)> mOnTextChanged;
};
} // namespace

SAMVST::SAMVST(const InstanceInfo& info)
: Plugin(info, MakeConfig(kNumParams, kNumPresets))
{
  GetParam(kOutputGain)->InitDouble("Output Gain", 100., 0., 200.0, 0.01, "%");
  GetParam(kSpeed)->InitInt("Speed", kDefaultSpeed, kSAMParamMin, kSAMParamMax, "");
  GetParam(kPitch)->InitInt("Pitch", kDefaultPitch, kSAMParamMin, kSAMParamMax, "");
  GetParam(kThroat)->InitInt("Throat", kDefaultThroat, kSAMParamMin, kSAMParamMax, "");
  GetParam(kMouth)->InitInt("Mouth", kDefaultMouth, kSAMParamMin, kSAMParamMax, "");

#if IPLUG_EDITOR
  mMakeGraphicsFunc = [&]() {
    return MakeGraphics(*this, PLUG_WIDTH, PLUG_HEIGHT, PLUG_FPS, GetScaleForScreen(PLUG_WIDTH, PLUG_HEIGHT));
  };

  mLayoutFunc = [&](IGraphics* pGraphics) {
    pGraphics->AttachPanelBackground(kUiPurpleDark);
    bool loadedFont = pGraphics->LoadFont(kUIFontID, UI_FONT_FN);

    if (!loadedFont)
    {
      loadedFont = pGraphics->LoadFont(kUIFontID, "Helvetica", ETextStyle::Normal);
      DBGMSG("SAMVST: UI font resource missing, attempting system font fallback\n");
    }

    if (!loadedFont)
    {
      DBGMSG("SAMVST: failed to load any font for UI controls\n");
      return;
    }

    const IVStyle style = DEFAULT_STYLE.WithDrawShadows(false).WithEmboss(false).WithRoundness(4.f)
      .WithColor(kBG, kUiPurpleDark)
      .WithColor(kFG, kUiPurpleLight)
      .WithColor(kPR, kUiPurpleLight)
      .WithColor(kFR, kUiPurpleLight)
      .WithColor(kHL, kUiPurpleLight)
      .WithColor(kSH, kUiPurpleDark)
      .WithLabelText(DEFAULT_LABEL_TEXT.WithFont(kUIFontID).WithFGColor(kUiPurpleLight))
      .WithValueText(DEFAULT_VALUE_TEXT.WithFont(kUIFontID).WithFGColor(kUiPurpleLight));

    IRECT bounds = pGraphics->GetBounds().GetPadded(-12.f);
    IRECT titleRow = bounds.ReduceFromTop(30.f);
    pGraphics->AttachControl(new ITextControl(titleRow, "SAM-VST",
      DEFAULT_TEXT.WithSize(20.f).WithAlign(EAlign::Near).WithFont(kUIFontID).WithFGColor(kUiPurpleLight), COLOR_TRANSPARENT));
    bounds.ReduceFromTop(8.f);

    IRECT controlsPane = bounds.FracRectHorizontal(0.42f).GetPadded(-2.f);
    IRECT textPane = bounds.FracRectHorizontal(0.58f, true).GetPadded(-2.f);

    IRECT sliderArea = controlsPane.ReduceFromTop(210.f);
    const std::array<int, 5> sliderParams = {kOutputGain, kSpeed, kPitch, kThroat, kMouth};
    const std::array<const char*, 5> sliderLabels = {"Output Gain", "Speed", "Pitch", "Throat", "Mouth"};

    for (size_t i = 0; i < sliderParams.size(); ++i)
    {
      const IRECT row = sliderArea.SubRectVertical(static_cast<int>(sliderParams.size()), static_cast<int>(i)).GetPadded(-2.f);
      pGraphics->AttachControl(new IVSliderControl(row, sliderParams[i], sliderLabels[i], style, false, EDirection::Horizontal));
    }

    controlsPane.ReduceFromTop(10.f);
    IRECT actionRow = controlsPane.ReduceFromTop(38.f);
    pGraphics->AttachControl(new IVButtonControl(actionRow.FracRectHorizontal(0.6f).GetPadded(-2.f), [this](IControl*) {
      RequestPlaybackTrigger();
    }, "Playback", style));

    pGraphics->AttachControl(new IVButtonControl(actionRow.FracRectHorizontal(0.38f, true).GetPadded(-2.f), [pGraphics](IControl*) {
      if (auto* pControl = pGraphics->GetControlWithTag(kCtrlTagTextPanel))
      {
        if (auto* pTextPanel = pControl->As<SAMTextPanelControl>())
          pTextPanel->StartEditing();
      }
    }, "Edit Text", style));

    controlsPane.ReduceFromTop(8.f);
    IRECT statusRow = controlsPane.ReduceFromTop(22.f);
    pGraphics->AttachControl(new ITextControl(statusRow, "",
      DEFAULT_TEXT.WithSize(14.f).WithAlign(EAlign::Near).WithFont(kUIFontID).WithFGColor(kUiPurpleLight), COLOR_TRANSPARENT), kCtrlTagPlaybackStatus);

    pGraphics->AttachControl(new ITextControl(controlsPane.ReduceFromTop(20.f), "MIDI note-on retriggers phrase",
      DEFAULT_TEXT.WithSize(13.f).WithFGColor(kUiPurpleLight).WithAlign(EAlign::Near).WithFont(kUIFontID), COLOR_TRANSPARENT));

    IRECT textTitleRow = textPane.ReduceFromTop(24.f);
    pGraphics->AttachControl(new ITextControl(textTitleRow, "Text Buffer",
      DEFAULT_TEXT.WithSize(14.f).WithFGColor(kUiPurpleLight).WithAlign(EAlign::Near).WithFont(kUIFontID), COLOR_TRANSPARENT));
    textPane.ReduceFromTop(6.f);

    pGraphics->AttachControl(new SAMTextPanelControl(textPane.GetPadded(-2.f), "", [this](const char* text) {
      SetTextBuffer(text);
      mNeedsRender.store(true, std::memory_order_release);
      if (!RenderPhraseFromText())
        DBGMSG("SAMVST: failed to render phrase after text edit\n");
      SyncUIState();
    }), kCtrlTagTextPanel);

    SyncUIState();
  };
#endif

  OnReset();
}

bool SAMVST::SerializeState(IByteChunk& chunk) const
{
  const bool triggerPending = mPlaybackTriggerPending.load(std::memory_order_acquire);
  const int32_t triggerRequests = ClampNonNegativeInt32(mPlaybackTriggerRequests.load(std::memory_order_acquire));
  const uint32_t flags = triggerPending ? kStateFlagPlaybackPending : 0u;

  std::string textCopy;
  {
    std::lock_guard<std::mutex> lock(mTextMutex);
    textCopy = mTextBuffer;
  }

  chunk.Put(&kStateMagic);
  chunk.Put(&kStateVersion);
  chunk.Put(&flags);
  chunk.Put(&triggerRequests);
  chunk.PutStr(textCopy.c_str());

  return SerializeParams(chunk);
}

int SAMVST::UnserializeState(const IByteChunk& chunk, int startPos)
{
  const int startPosForParams = startPos;
  uint32_t stateMagic = 0;
  uint32_t stateVersion = 0;
  uint32_t flags = 0;
  int32_t triggerRequests = 0;
  WDL_String text;

  int pos = chunk.Get(&stateMagic, startPos);
  if (pos >= 0)
    pos = chunk.Get(&stateVersion, pos);
  if (pos >= 0)
    pos = chunk.Get(&flags, pos);
  if (pos >= 0)
    pos = chunk.Get(&triggerRequests, pos);
  if (pos >= 0)
    pos = chunk.GetStr(text, pos);

  if (pos >= 0 && stateMagic == kStateMagic && stateVersion == kStateVersion)
  {
    const bool triggerPending = (flags & kStateFlagPlaybackPending) != 0u;
    const int requestCount = std::max(0, static_cast<int>(triggerRequests));

    mPlaybackTriggerPending.store(triggerPending, std::memory_order_release);
    mPlaybackTriggerRequests.store(requestCount, std::memory_order_release);
    mPlaybackTriggerAcks.store(std::max(0, requestCount - (triggerPending ? 1 : 0)), std::memory_order_release);

    SetTextBuffer(text.Get());

    mLastPlaybackAckSeen = -1;
#if IPLUG_EDITOR
    SyncUIState();
#endif
    startPos = pos;
  }
  else
  {
    bool legacyTriggerPending = false;
    int legacyTriggerRequests = 0;
    WDL_String legacyText;
    int legacyPos = chunk.Get(&legacyTriggerPending, startPos);
    if (legacyPos >= 0)
      legacyPos = chunk.Get(&legacyTriggerRequests, legacyPos);
    if (legacyPos >= 0)
      legacyPos = chunk.GetStr(legacyText, legacyPos);

    if (legacyPos >= 0)
    {
      const int requestCount = std::max(0, legacyTriggerRequests);

      mPlaybackTriggerPending.store(legacyTriggerPending, std::memory_order_release);
      mPlaybackTriggerRequests.store(requestCount, std::memory_order_release);
      mPlaybackTriggerAcks.store(std::max(0, requestCount - (legacyTriggerPending ? 1 : 0)), std::memory_order_release);
      SetTextBuffer(legacyText.Get());

      mLastPlaybackAckSeen = -1;
#if IPLUG_EDITOR
      SyncUIState();
#endif
      startPos = legacyPos;
      DBGMSG("SAMVST: migrated legacy state chunk format\n");
    }
    else
    {
      if (pos >= 0)
        DBGMSG("SAMVST: ignoring unknown state chunk magic/version (%u/%u)\n", stateMagic, stateVersion);

      startPos = startPosForParams;
    }
  }

  const int paramPos = UnserializeParams(chunk, startPos);

  mNeedsRender.store(true, std::memory_order_release);
  if (!RenderPhraseFromText())
    DBGMSG("SAMVST: failed to re-render phrase after state load\n");

  return paramPos;
}

void SAMVST::OnReset()
{
  const double hostSampleRate = (GetSampleRate() > 1.0) ? GetSampleRate() : 44100.0;
  mSAMReadIncrement = sam_bridge::kSAMSourceSampleRate / hostSampleRate;
  if (mSAMReadIncrement <= 0.0)
    mSAMReadIncrement = sam_bridge::kSAMSourceSampleRate / 44100.0;

  mNeedsRender.store(true, std::memory_order_release);
  if (!RenderPhraseFromText())
  {
    mRenderedPCM.clear();
    ResetPlaybackPosition();
    DBGMSG("SAMVST: failed to render phrase on reset\n");
  }
}

void SAMVST::OnParamChange(int paramIdx)
{
  if (paramIdx == kSpeed || paramIdx == kPitch || paramIdx == kThroat || paramIdx == kMouth)
    mNeedsRender.store(true, std::memory_order_release);
}

#if IPLUG_EDITOR
void SAMVST::OnUIOpen()
{
  Plugin::OnUIOpen();
  SyncUIState();
}
#endif

void SAMVST::OnIdle()
{
  const int ackCount = mPlaybackTriggerAcks.load(std::memory_order_acquire);
  if (ackCount != mLastPlaybackAckSeen)
  {
    mLastPlaybackAckSeen = ackCount;
    UpdatePlaybackStatusText(true);
  }
}

void SAMVST::RequestPlaybackTrigger()
{
  const int requestCount = mPlaybackTriggerRequests.fetch_add(1, std::memory_order_acq_rel) + 1;
  mPlaybackTriggerPending.store(true, std::memory_order_release);

  const bool needRender = mNeedsRender.load(std::memory_order_acquire) || mRenderedPCM.empty();
  if (needRender && !RenderPhraseFromText())
  {
    mNeedsRender.store(true, std::memory_order_release);
    return;
  }
  ResetPlaybackPosition();

  DBGMSG("SAMVST: playback trigger request #%d queued\n", requestCount);
  UpdatePlaybackStatusText(false);
}

bool SAMVST::RenderPhraseFromText()
{
  std::string phrase = GetTextBuffer();
  if (phrase.empty())
    phrase = mFallbackPhrase;

  std::vector<uint8_t> rendered;
  const int speed = static_cast<int>(GetParam(kSpeed)->Value());
  const int pitch = static_cast<int>(GetParam(kPitch)->Value());
  const int throat = static_cast<int>(GetParam(kThroat)->Value());
  const int mouth = static_cast<int>(GetParam(kMouth)->Value());

  if (!sam_bridge::RenderTextToPCM(phrase, speed, pitch, throat, mouth, rendered))
  {
    mNeedsRender.store(true, std::memory_order_release);
    return false;
  }

  mRenderedPCM = std::move(rendered);

  double sum = 0.0;
  for (uint8_t sample : mRenderedPCM)
    sum += U8ToFloat(sample);

  mRenderedDCBias = mRenderedPCM.empty() ? 0.f : static_cast<float>(sum / static_cast<double>(mRenderedPCM.size()));
  ResetPlaybackPosition();
  mNeedsRender.store(false, std::memory_order_release);

  DBGMSG("SAMVST: rendered phrase \"%s\" with %d samples @ %.0fHz source\n",
         phrase.c_str(), static_cast<int>(mRenderedPCM.size()), sam_bridge::kSAMSourceSampleRate);
  return !mRenderedPCM.empty();
}

void SAMVST::ResetPlaybackPosition()
{
  mSAMReadPos = 0.0;
  mIsPlaying = !mRenderedPCM.empty();
}

float SAMVST::U8ToFloat(uint8_t v)
{
  return (static_cast<float>(v) - 128.f) * (1.f / 128.f);
}

float SAMVST::ReadSAMSample()
{
  if (!mIsPlaying || mRenderedPCM.empty())
    return 0.f;

  const size_t size = mRenderedPCM.size();
  const size_t idx = static_cast<size_t>(mSAMReadPos);

  if (idx >= size)
  {
    mIsPlaying = false;
    return 0.f;
  }

  const size_t nextIdx = (idx + 1 < size) ? idx + 1 : idx;
  const double frac = mSAMReadPos - static_cast<double>(idx);

  const float s0 = U8ToFloat(mRenderedPCM[idx]);
  const float s1 = U8ToFloat(mRenderedPCM[nextIdx]);
  const float out = s0 + static_cast<float>((s1 - s0) * frac);
  const float centeredOut = std::clamp(out - mRenderedDCBias, -1.f, 1.f);

  mSAMReadPos += mSAMReadIncrement;
  if (mSAMReadPos >= static_cast<double>(size))
    mIsPlaying = false;

  return centeredOut;
}

void SAMVST::SetTextBuffer(const char* text)
{
  std::lock_guard<std::mutex> lock(mTextMutex);
  mTextBuffer = text ? text : "";

  if (mTextBuffer.size() > kMaxTextBufferLength)
    mTextBuffer.resize(kMaxTextBufferLength);

  mNeedsRender.store(true, std::memory_order_release);
}

std::string SAMVST::GetTextBuffer() const
{
  std::lock_guard<std::mutex> lock(mTextMutex);
  return mTextBuffer;
}

void SAMVST::UpdatePlaybackStatusText(bool acknowledged)
{
#if IPLUG_EDITOR
  if (auto* pUI = GetUI())
  {
    if (auto* pStatusControl = pUI->GetControlWithTag(kCtrlTagPlaybackStatus))
    {
      if (auto* pTextControl = pStatusControl->As<ITextControl>())
      {
        const int requestCount = mPlaybackTriggerRequests.load(std::memory_order_acquire);
        const int ackCount = mPlaybackTriggerAcks.load(std::memory_order_acquire);
        WDL_String text;

        if (requestCount == 0)
        {
          text.Set("Playback idle");
        }
        else if (acknowledged && ackCount >= requestCount)
        {
          text.SetFormatted(128, "Playback request #%d acknowledged", ackCount);
        }
        else
        {
          text.SetFormatted(128, "Playback request #%d pending DSP ack", requestCount);
        }

        pTextControl->SetStr(text.Get());
        pTextControl->SetDirty(false);
      }
    }
  }
#else
  (void) acknowledged;
#endif
}

#if IPLUG_EDITOR
void SAMVST::SyncUIState()
{
  if (auto* pUI = GetUI())
  {
    if (auto* pTextPanelControl = pUI->GetControlWithTag(kCtrlTagTextPanel))
    {
      if (auto* pTextControl = pTextPanelControl->As<ITextControl>())
      {
        std::string text = GetTextBuffer();
        if (text.empty())
          text = "Click Edit Text to enter a phrase.";

        pTextControl->SetStr(text.c_str());
        pTextControl->SetDirty(false);
      }
    }

    UpdatePlaybackStatusText(false);
    pUI->SetAllControlsDirty();
  }
}
#endif

#if IPLUG_DSP
void SAMVST::ProcessMidiMsg(const IMidiMsg& msg)
{
  if (msg.StatusMsg() == IMidiMsg::kNoteOn && msg.Velocity() > 0)
  {
    const bool needRender = mNeedsRender.load(std::memory_order_acquire) || mRenderedPCM.empty();
    if (needRender && !RenderPhraseFromText())
    {
      mNeedsRender.store(true, std::memory_order_release);
      return;
    }

    const int requestCount = mPlaybackTriggerRequests.fetch_add(1, std::memory_order_acq_rel) + 1;
    mPlaybackTriggerPending.store(true, std::memory_order_release);
    ResetPlaybackPosition();
    DBGMSG("SAMVST: MIDI note-on retrigger #%d note=%d velocity=%d\n",
           requestCount, msg.NoteNumber(), msg.Velocity());
  }
}

void SAMVST::ProcessBlock(sample** inputs, sample** outputs, int nFrames)
{
  (void) inputs;

  if (mPlaybackTriggerPending.exchange(false, std::memory_order_acq_rel))
    mPlaybackTriggerAcks.fetch_add(1, std::memory_order_acq_rel);

  const double gain = GetParam(kOutputGain)->Value() * 0.01;
  const int nOutChans = NOutChansConnected();

  for (int s = 0; s < nFrames; ++s)
  {
    const sample mono = static_cast<sample>(ReadSAMSample() * static_cast<float>(gain));

    for (int c = 0; c < nOutChans; ++c)
      outputs[c][s] = mono;
  }
}
#endif
