#include "SAMVST.h"

#include <algorithm>
#include <array>
#include <cmath>
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
constexpr float kTextPanelPadding = 8.f;

int32_t ClampNonNegativeInt32(int value)
{
  if (value < 0)
    return 0;

  constexpr int32_t kMax = std::numeric_limits<int32_t>::max();
  if (value > kMax)
    return kMax;

  return static_cast<int32_t>(value);
}
} // namespace

#define STB_TEXTEDIT_CHARTYPE char16_t
#define STB_TEXTEDIT_POSITIONTYPE int
#define STB_TEXTEDIT_STRING SAMTextPanelControl
#define STB_TEXTEDIT_KEYTYPE uint32_t
class SAMTextPanelControl;
#include "stb_textedit.h"

#define VIRTUAL_KEY_BIT 0x80000000
#define STB_TEXTEDIT_K_SHIFT 0x40000000
#define STB_TEXTEDIT_K_CONTROL 0x20000000
#define STB_TEXTEDIT_K_ALT 0x10000000
#define STB_TEXTEDIT_K_LEFT (VIRTUAL_KEY_BIT | kVK_LEFT)
#define STB_TEXTEDIT_K_RIGHT (VIRTUAL_KEY_BIT | kVK_RIGHT)
#define STB_TEXTEDIT_K_UP (VIRTUAL_KEY_BIT | kVK_UP)
#define STB_TEXTEDIT_K_DOWN (VIRTUAL_KEY_BIT | kVK_DOWN)
#define STB_TEXTEDIT_K_LINESTART (VIRTUAL_KEY_BIT | kVK_HOME)
#define STB_TEXTEDIT_K_LINEEND (VIRTUAL_KEY_BIT | kVK_END)
#define STB_TEXTEDIT_K_WORDLEFT (STB_TEXTEDIT_K_LEFT | STB_TEXTEDIT_K_CONTROL)
#define STB_TEXTEDIT_K_WORDRIGHT (STB_TEXTEDIT_K_RIGHT | STB_TEXTEDIT_K_CONTROL)
#define STB_TEXTEDIT_K_TEXTSTART (STB_TEXTEDIT_K_LINESTART | STB_TEXTEDIT_K_CONTROL)
#define STB_TEXTEDIT_K_TEXTEND (STB_TEXTEDIT_K_LINEEND | STB_TEXTEDIT_K_CONTROL)
#define STB_TEXTEDIT_K_DELETE (VIRTUAL_KEY_BIT | kVK_DELETE)
#define STB_TEXTEDIT_K_BACKSPACE (VIRTUAL_KEY_BIT | kVK_BACK)
#define STB_TEXTEDIT_K_UNDO (STB_TEXTEDIT_K_CONTROL | 'z')
#define STB_TEXTEDIT_K_REDO (STB_TEXTEDIT_K_CONTROL | STB_TEXTEDIT_K_SHIFT | 'z')
#define STB_TEXTEDIT_K_INSERT (VIRTUAL_KEY_BIT | kVK_INSERT)
#define STB_TEXTEDIT_K_PGUP (VIRTUAL_KEY_BIT | kVK_PRIOR)
#define STB_TEXTEDIT_K_PGDOWN (VIRTUAL_KEY_BIT | kVK_NEXT)
#define STB_TEXTEDIT_STRINGLEN(tc) SAMTextPanelControl::GetLength(tc)
#define STB_TEXTEDIT_LAYOUTROW SAMTextPanelControl::Layout
#define STB_TEXTEDIT_GETWIDTH(tc, n, i) SAMTextPanelControl::GetCharWidth(tc, n, i)
#define STB_TEXTEDIT_KEYTOTEXT(key) ((key & VIRTUAL_KEY_BIT) ? 0 : ((key & STB_TEXTEDIT_K_CONTROL) ? 0 : (key & (~0xF0000000))))
#define STB_TEXTEDIT_GETCHAR(tc, i) SAMTextPanelControl::GetChar(tc, i)
#define STB_TEXTEDIT_NEWLINE '\n'
#define STB_TEXTEDIT_IS_SPACE(ch) ((ch) == u' ' || (ch) == u'\t' || (ch) == u'\n' || (ch) == u'\r')
#define STB_TEXTEDIT_DELETECHARS SAMTextPanelControl::DeleteChars
#define STB_TEXTEDIT_INSERTCHARS SAMTextPanelControl::InsertChars

static void stb_textedit_initialize_state(STB_TexteditState* state, int is_single_line);
static void stb_textedit_click(SAMTextPanelControl* str, STB_TexteditState* state, float x, float y);
static void stb_textedit_drag(SAMTextPanelControl* str, STB_TexteditState* state, float x, float y);
static int stb_textedit_cut(SAMTextPanelControl* str, STB_TexteditState* state);
static int stb_textedit_paste(SAMTextPanelControl* str, STB_TexteditState* state, const STB_TEXTEDIT_CHARTYPE* text, int len);
static void stb_textedit_key(SAMTextPanelControl* str, STB_TexteditState* state, STB_TEXTEDIT_KEYTYPE key);

class SAMTextPanelControl final : public ITextControl
{
public:
  SAMTextPanelControl(const IRECT& bounds, const char* str,
                      std::function<void(const char*)> onTextCommitted,
                      std::function<void(const char*)> onTextEdited)
  : ITextControl(bounds, "",
      DEFAULT_TEXT.WithAlign(EAlign::Near).WithVAlign(EVAlign::Top).WithSize(16.f).WithFont(kUIFontID).WithFGColor(kUiPurpleLight),
      kUiPurpleDark)
  , mOnTextCommitted(std::move(onTextCommitted))
  , mOnTextEdited(std::move(onTextEdited))
  {
    mIgnoreMouse = false;
    SetTextEntryLength(kMaxTextBufferLength);
    stb_textedit_initialize_state(&mEditState, false);
    SetStr(str ? str : "");
    mEditState.cursor = GetLength(this);
    mEditState.select_start = mEditState.cursor;
    mEditState.select_end = mEditState.cursor;
  }

  void SetStr(const char* str) override
  {
    std::u16string text = UTF8ToUTF16String(str ? str : "");
    if (static_cast<int>(text.size()) > kMaxTextBufferLength)
      text.resize(kMaxTextBufferLength);

    mEditString = std::move(text);
    mCharWidths.clear();
    mRows.clear();
    mLayoutDirty = true;
    const std::string utf8 = UTF16ToUTF8String(mEditString);
    mCommittedText = utf8;
    ITextControl::SetStr(utf8.c_str());
  }

  void StartEditing()
  {
    mEditing = true;
    SetDirty(false);
  }

  bool IsEditing() const
  {
    return mEditing;
  }

  bool HandleGlobalKey(const IKeyPress& key, bool isUp)
  {
    if (isUp || !mEditing)
      return false;

    return HandleKeyPress(key);
  }

  void Draw(IGraphics& g) override
  {
    g.FillRect(mBGColor, mRECT, &mBlend);

    const IRECT textRect = GetTextRect();
    EnsureLayout();

    const int selStart = std::min(mEditState.select_start, mEditState.select_end);
    const int selEnd = std::max(mEditState.select_start, mEditState.select_end);
    const float lineHeight = GetLineHeight();
    const IText normalText = GetText().WithFGColor(kUiPurpleLight).WithAlign(EAlign::Near).WithVAlign(EVAlign::Top);
    const IText selectedText = normalText.WithFGColor(kUiPurpleDark);

    for (size_t rowIdx = 0; rowIdx < mRows.size(); ++rowIdx)
    {
      const RowInfo& row = mRows[rowIdx];
      const float top = textRect.T + static_cast<float>(rowIdx) * lineHeight;
      const float bottom = top + lineHeight;

      if (bottom < textRect.T)
        continue;
      if (top > textRect.B)
        break;

      const int rowStart = row.start;
      const int rowEnd = row.start + row.length;
      int textEnd = rowEnd;
      if (textEnd > rowStart && (mEditString[textEnd - 1] == u'\n' || mEditString[textEnd - 1] == u'\r'))
        --textEnd;

      const int rowSelStart = std::clamp(selStart, rowStart, textEnd);
      const int rowSelEnd = std::clamp(selEnd, rowStart, textEnd);
      float xCursor = textRect.L + GetXOffsetInRow(row, rowStart);

      for (int charIdx = rowStart; charIdx < textEnd; ++charIdx)
      {
        const char16_t c = mEditString[static_cast<size_t>(charIdx)];
        const float charWidth = (charIdx >= 0 && charIdx < static_cast<int>(mCharWidths.size()))
          ? mCharWidths[static_cast<size_t>(charIdx)]
          : GetMonospaceAdvance();

        const bool selected = (charIdx >= rowSelStart && charIdx < rowSelEnd);
        if (selected)
          g.FillRect(kUiPurpleLight, IRECT(xCursor, top, xCursor + charWidth, bottom), &mBlend);

        if (c != u' ' && c != u'\t')
        {
          std::u16string glyph(1, c);
          const std::string utf8 = UTF16ToUTF8String(glyph);
          const IText& style = selected ? selectedText : normalText;
          g.DrawText(style, utf8.c_str(), IRECT(xCursor, top, textRect.R, bottom), &mBlend);
        }

        xCursor += charWidth;
      }
    }

    if (mEditing)
    {
      const CursorPixelPos cursorPos = GetCursorPixelPos(mEditState.cursor);
      const float caretWidth = std::max(10.f, cursorPos.width);
      const IRECT caretRect(textRect.L + cursorPos.x,
                            textRect.T + cursorPos.y,
                            textRect.L + cursorPos.x + caretWidth,
                            textRect.T + cursorPos.y + lineHeight);
      g.FillRect(kUiPurpleLight, caretRect, &mBlend);

      if (cursorPos.index < GetLength(this))
      {
        const char16_t ch = mEditString[cursorPos.index];
        if (ch != u'\n' && ch != u'\r')
        {
          std::u16string c(1, ch);
          const std::string utf8 = UTF16ToUTF8String(c);
          const IText caretText = normalText.WithFGColor(kUiPurpleDark);
          g.DrawText(caretText, utf8.c_str(), IRECT(caretRect.L, caretRect.T, textRect.R, caretRect.B), &mBlend);
        }
      }
    }
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    StartEditing();
    const IRECT textRect = GetTextRect();
    const float localX = x - textRect.L;
    const float localY = y - textRect.T;

    if (mod.L)
    {
      if (mod.S)
      {
        if (mEditState.select_start == mEditState.select_end)
          mEditState.select_start = mEditState.cursor;

        stb_textedit_drag(this, &mEditState, localX, localY);
      }
      else
      {
        stb_textedit_click(this, &mEditState, localX, localY);
      }
      SetDirty(false);
    }
  }

  void OnMouseDrag(float x, float y, float dX, float dY, const IMouseMod& mod) override
  {
    (void) dX;
    (void) dY;

    if (!mEditing || !mod.L)
      return;

    const IRECT textRect = GetTextRect();
    const float localX = x - textRect.L;
    const float localY = y - textRect.T;
    stb_textedit_drag(this, &mEditState, localX, localY);
    SetDirty(false);
  }

  void OnMouseDblClick(float x, float y, const IMouseMod& mod) override
  {
    (void) x;
    (void) y;
    (void) mod;

    if (!mEditing)
      StartEditing();

    SelectAll();
    SetDirty(false);
  }

  bool OnKeyDown(float x, float y, const IKeyPress& key) override
  {
    (void) x;
    (void) y;
    return HandleKeyPress(key);
  }

  static int DeleteChars(SAMTextPanelControl* _this, size_t pos, size_t num)
  {
    if (pos >= _this->mEditString.size())
      return false;

    const size_t clamped = std::min(num, _this->mEditString.size() - pos);
    _this->mEditString.erase(pos, clamped);
    _this->OnTextChange();
    return true;
  }

  static int InsertChars(SAMTextPanelControl* _this, size_t pos, const char16_t* text, size_t num)
  {
    if (pos > _this->mEditString.size() || text == nullptr || num == 0)
      return false;

    const int remaining = kMaxTextBufferLength - static_cast<int>(_this->mEditString.size());
    if (remaining <= 0)
      return false;

    const size_t insertCount = std::min(num, static_cast<size_t>(remaining));
    std::u16string sanitized;
    sanitized.reserve(insertCount);

    for (size_t i = 0; i < insertCount; ++i)
    {
      const char16_t c = text[i];
      sanitized.push_back(c);
    }

    if (sanitized.empty())
      return false;

    _this->mEditString.insert(pos, sanitized);
    _this->OnTextChange();
    return true;
  }

  static void Layout(StbTexteditRow* row, SAMTextPanelControl* _this, int start_i)
  {
    _this->EnsureLayout();
    const float lineHeight = _this->GetLineHeight();
    const RowLookup lookup = _this->FindRowForIndex(start_i);
    const RowInfo& info = _this->mRows[lookup.rowIdx];
    const int rowStart = info.start + lookup.offsetInRow;
    const int rowRemaining = std::max(0, info.length - lookup.offsetInRow);
    const float x0 = _this->GetXOffsetInRow(info, rowStart);
    const float x1 = _this->GetXOffsetInRow(info, info.start + info.length);

    row->x0 = x0;
    row->x1 = std::max(x0, x1);
    row->ymin = static_cast<float>(lookup.rowIdx) * lineHeight;
    row->ymax = row->ymin + lineHeight;
    row->baseline_y_delta = lineHeight;
    row->num_chars = rowRemaining;
  }

  static float GetCharWidth(SAMTextPanelControl* _this, int n, int i)
  {
    _this->EnsureCharWidths();
    const int idx = n + i;
    if (idx < 0 || idx >= static_cast<int>(_this->mCharWidths.size()))
      return _this->GetFallbackCharWidth();

    return _this->mCharWidths[idx];
  }

  static char16_t GetChar(SAMTextPanelControl* _this, int pos)
  {
    if (pos < 0 || pos >= static_cast<int>(_this->mEditString.size()))
      return 0;

    return _this->mEditString[static_cast<size_t>(pos)];
  }

  static int GetLength(SAMTextPanelControl* _this)
  {
    return static_cast<int>(_this->mEditString.size());
  }

private:
  struct RowInfo
  {
    int start = 0;
    int length = 0;
  };

  struct RowLookup
  {
    size_t rowIdx = 0;
    int offsetInRow = 0;
  };

  struct CursorPixelPos
  {
    int index = 0;
    float x = 0.f;
    float y = 0.f;
    float width = 0.f;
  };

  template <typename Proc>
  bool CallSTB(Proc&& proc)
  {
    const STB_TexteditState oldState = mEditState;
    const std::u16string oldString = mEditString;
    proc();

    const bool stateChanged = std::memcmp(&oldState, &mEditState, sizeof(STB_TexteditState)) != 0;
    const bool textChanged = oldString != mEditString;
    if (stateChanged || textChanged)
      SetDirty(false);

    return stateChanged || textChanged;
  }

  bool HandleKeyPress(const IKeyPress& key)
  {
    if (!mEditing)
      return false;

    if (key.C)
    {
      switch (key.VK)
      {
        case 'A':
          SelectAll();
          return true;
        case 'X':
          Cut();
          return true;
        case 'C':
          CopySelection();
          return true;
        case 'V':
          Paste();
          return true;
        case 'Z':
          if (key.S)
            CallSTB([&]() { stb_textedit_key(this, &mEditState, STB_TEXTEDIT_K_REDO); });
          else
            CallSTB([&]() { stb_textedit_key(this, &mEditState, STB_TEXTEDIT_K_UNDO); });
          return true;
        default:
          break;
      }
    }

    if (key.VK == kVK_ESCAPE)
    {
      CommitAndStopEditing();
      return true;
    }

    int stbKey = 0;
    wdl_utf8_parsechar(key.utf8, &stbKey);

    switch (key.VK)
    {
      case kVK_SPACE: stbKey = ' '; break;
      case kVK_TAB: return false;
      case kVK_DELETE: stbKey = STB_TEXTEDIT_K_DELETE; break;
      case kVK_BACK: stbKey = STB_TEXTEDIT_K_BACKSPACE; break;
      case kVK_LEFT: stbKey = STB_TEXTEDIT_K_LEFT; break;
      case kVK_RIGHT: stbKey = STB_TEXTEDIT_K_RIGHT; break;
      case kVK_UP: stbKey = STB_TEXTEDIT_K_UP; break;
      case kVK_DOWN: stbKey = STB_TEXTEDIT_K_DOWN; break;
      case kVK_PRIOR: stbKey = STB_TEXTEDIT_K_PGUP; break;
      case kVK_NEXT: stbKey = STB_TEXTEDIT_K_PGDOWN; break;
      case kVK_HOME: stbKey = STB_TEXTEDIT_K_LINESTART; break;
      case kVK_END: stbKey = STB_TEXTEDIT_K_LINEEND; break;
      case kVK_RETURN:
        if (key.S)
          stbKey = '\r';
        else
        {
          CommitAndStopEditing();
          return true;
        }
        break;
      default:
        if (stbKey == 0)
          stbKey = (key.VK) | VIRTUAL_KEY_BIT;
        break;
    }

    if (key.C)
      stbKey |= STB_TEXTEDIT_K_CONTROL;
    if (key.A)
      stbKey |= STB_TEXTEDIT_K_ALT;
    if (key.S)
      stbKey |= STB_TEXTEDIT_K_SHIFT;

    CallSTB([&]() { stb_textedit_key(this, &mEditState, static_cast<uint32_t>(stbKey)); });
    return true;
  }

  void EnsureCharWidths()
  {
    if (!mCharWidths.empty() || mEditString.empty())
      return;

    mCharWidths.resize(mEditString.size(), 0.f);
    for (size_t i = 0; i < mEditString.size(); ++i)
    {
      const char16_t c = mEditString[i];
      if (c == u'\n' || c == u'\r')
      {
        mCharWidths[i] = 0.f;
        continue;
      }

      const char16_t prev = (i == 0) ? 0 : mEditString[i - 1];
      mCharWidths[i] = MeasureCharWidth(c, prev);
    }
  }

  void EnsureLayout()
  {
    if (!mLayoutDirty)
      return;

    EnsureCharWidths();
    mRows.clear();

    const int len = GetLength(this);
    if (len <= 0)
    {
      mRows.push_back({0, 0});
      mLayoutDirty = false;
      return;
    }

    const float maxWidth = std::max(1.f, GetTextRect().W());
    int idx = 0;

    while (idx < len)
    {
      const int rowStart = idx;
      int rowLength = 0;
      float rowWidth = 0.f;
      int breakPos = -1;
      float breakWidth = 0.f;

      while (idx < len)
      {
        const char16_t c = mEditString[static_cast<size_t>(idx)];
        if (c == u'\n' || c == u'\r')
        {
          ++idx;
          ++rowLength;
          break;
        }

        const float charWidth = mCharWidths[static_cast<size_t>(idx)];
        if (rowLength > 0 && (rowWidth + charWidth) > maxWidth)
        {
          if (breakPos >= rowStart)
          {
            rowLength = breakPos - rowStart + 1;
            idx = breakPos + 1;
          }
          break;
        }

        rowWidth += charWidth;
        if (c == u' ' || c == u'\t')
        {
          breakPos = idx;
          breakWidth = rowWidth;
        }
        ++idx;
        ++rowLength;
      }

      if (rowLength <= 0)
      {
        rowLength = 1;
        idx = std::min(len, idx + 1);
      }

      if (breakPos >= rowStart && idx == breakPos + 1 && rowWidth > maxWidth)
        rowWidth = breakWidth;

      mRows.push_back({rowStart, rowLength});
    }

    if (mRows.empty())
      mRows.push_back({0, 0});

    mLayoutDirty = false;
  }

  float MeasureCharWidth(char16_t c, char16_t prev) const
  {
    (void) prev;

    if (c == u'\t')
      return GetMonospaceAdvance() * 2.f;

    return GetMonospaceAdvance();
  }

  float GetFallbackCharWidth() const
  {
    return std::max(8.f, GetText().mSize * 0.62f);
  }

  float GetMonospaceAdvance() const
  {
    if (mMonospaceAdvance > 0.f)
      return mMonospaceAdvance;

    if (!GetUI())
    {
      mMonospaceAdvance = GetFallbackCharWidth();
      return mMonospaceAdvance;
    }

    IRECT bounds;
    const float mWidth = std::max(0.f, GetUI()->MeasureText(GetText(), "M", bounds));
    const float zWidth = std::max(0.f, GetUI()->MeasureText(GetText(), "0", bounds));
    mMonospaceAdvance = std::max({GetFallbackCharWidth(), mWidth, zWidth});
    return mMonospaceAdvance;
  }

  float GetLineHeight() const
  {
    return GetText().mSize * 1.35f;
  }

  IRECT GetTextRect() const
  {
    return mRECT.GetPadded(-kTextPanelPadding);
  }

  RowLookup FindRowForIndex(int index)
  {
    EnsureLayout();

    if (mRows.empty())
      return {};

    int clamped = std::clamp(index, 0, GetLength(this));
    for (size_t i = 0; i < mRows.size(); ++i)
    {
      const RowInfo& row = mRows[i];
      const int rowStart = row.start;
      const int rowEnd = row.start + row.length;
      if (clamped < rowEnd || (i + 1 == mRows.size() && clamped <= rowEnd))
      {
        return {i, clamped - rowStart};
      }
    }

    const RowInfo& last = mRows.back();
    return {mRows.size() - 1, std::max(0, clamped - last.start)};
  }

  float GetXOffsetInRow(const RowInfo& row, int index) const
  {
    const int rowStart = row.start;
    const int rowEnd = row.start + row.length;
    const int clamped = std::clamp(index, rowStart, rowEnd);
    float x = 0.f;

    for (int i = rowStart; i < clamped; ++i)
    {
      if (i >= 0 && i < static_cast<int>(mCharWidths.size()))
        x += mCharWidths[static_cast<size_t>(i)];
    }

    return x;
  }

  CursorPixelPos GetCursorPixelPos(int cursor)
  {
    EnsureLayout();
    EnsureCharWidths();
    const RowLookup lookup = FindRowForIndex(cursor);
    const RowInfo& row = mRows[lookup.rowIdx];
    const int rowCursor = row.start + lookup.offsetInRow;
    const float x = GetXOffsetInRow(row, rowCursor);
    float width = GetFallbackCharWidth();

    if (rowCursor >= 0 && rowCursor < static_cast<int>(mCharWidths.size()))
    {
      width = mCharWidths[static_cast<size_t>(rowCursor)];
      if (width <= 0.f)
        width = GetFallbackCharWidth();
    }

    return {rowCursor, x, static_cast<float>(lookup.rowIdx) * GetLineHeight(), width};
  }

  void OnTextChange()
  {
    mCharWidths.clear();
    mRows.clear();
    mLayoutDirty = true;
    const std::string utf8 = UTF16ToUTF8String(mEditString);
    ITextControl::SetStr(utf8.c_str());

    if (mOnTextEdited)
      mOnTextEdited(utf8.c_str());
  }

  void CommitAndStopEditing()
  {
    mEditing = false;
    const std::string text = UTF16ToUTF8String(mEditString);

    if (text != mCommittedText)
    {
      mCommittedText = text;
      if (mOnTextCommitted)
        mOnTextCommitted(text.c_str());
    }

    SetDirty(false);
  }

  void CopySelection()
  {
    if (mEditState.select_start == mEditState.select_end || !GetUI())
      return;

    const int start = std::min(mEditState.select_start, mEditState.select_end);
    const int end = std::max(mEditState.select_start, mEditState.select_end);
    GetUI()->SetTextInClipboard(UTF16ToUTF8String(mEditString.data() + start, mEditString.data() + end).c_str());
  }

  void Paste()
  {
    if (!GetUI())
      return;

    WDL_String fromClipboard;
    if (!GetUI()->GetTextFromClipboard(fromClipboard))
      return;

    CallSTB([&]() {
      auto uText = UTF8ToUTF16String(fromClipboard.Get());
      stb_textedit_paste(this, &mEditState, uText.data(), static_cast<int>(uText.size()));
    });
  }

  void Cut()
  {
    CopySelection();
    CallSTB([&]() { stb_textedit_cut(this, &mEditState); });
  }

  void SelectAll()
  {
    mEditState.select_start = 0;
    mEditState.select_end = GetLength(this);
    mEditState.cursor = mEditState.select_end;
    SetDirty(false);
  }

  std::function<void(const char*)> mOnTextCommitted;
  std::function<void(const char*)> mOnTextEdited;
  std::u16string mEditString;
  std::vector<float> mCharWidths;
  std::vector<RowInfo> mRows;
  STB_TexteditState mEditState {};
  std::string mCommittedText;
  bool mEditing = false;
  bool mLayoutDirty = true;
  mutable float mMonospaceAdvance = 0.f;
};

#define STB_TEXTEDIT_IMPLEMENTATION
#include "stb_textedit.h"

#undef STB_TEXTEDIT_CHARTYPE
#undef STB_TEXTEDIT_POSITIONTYPE
#undef STB_TEXTEDIT_STRING
#undef STB_TEXTEDIT_KEYTYPE
#undef VIRTUAL_KEY_BIT
#undef STB_TEXTEDIT_K_SHIFT
#undef STB_TEXTEDIT_K_CONTROL
#undef STB_TEXTEDIT_K_ALT
#undef STB_TEXTEDIT_K_LEFT
#undef STB_TEXTEDIT_K_RIGHT
#undef STB_TEXTEDIT_K_UP
#undef STB_TEXTEDIT_K_DOWN
#undef STB_TEXTEDIT_K_LINESTART
#undef STB_TEXTEDIT_K_LINEEND
#undef STB_TEXTEDIT_K_WORDLEFT
#undef STB_TEXTEDIT_K_WORDRIGHT
#undef STB_TEXTEDIT_K_TEXTSTART
#undef STB_TEXTEDIT_K_TEXTEND
#undef STB_TEXTEDIT_K_DELETE
#undef STB_TEXTEDIT_K_BACKSPACE
#undef STB_TEXTEDIT_K_UNDO
#undef STB_TEXTEDIT_K_REDO
#undef STB_TEXTEDIT_K_INSERT
#undef STB_TEXTEDIT_K_PGUP
#undef STB_TEXTEDIT_K_PGDOWN
#undef STB_TEXTEDIT_STRINGLEN
#undef STB_TEXTEDIT_LAYOUTROW
#undef STB_TEXTEDIT_GETWIDTH
#undef STB_TEXTEDIT_KEYTOTEXT
#undef STB_TEXTEDIT_GETCHAR
#undef STB_TEXTEDIT_NEWLINE
#undef STB_TEXTEDIT_IS_SPACE
#undef STB_TEXTEDIT_DELETECHARS
#undef STB_TEXTEDIT_INSERTCHARS

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

    pGraphics->AttachControl(new ITextControl(controlsPane.ReduceFromTop(20.f), "Enter/Esc commit • Shift+Enter inserts CR",
      DEFAULT_TEXT.WithSize(13.f).WithFGColor(kUiPurpleLight).WithAlign(EAlign::Near).WithFont(kUIFontID), COLOR_TRANSPARENT));

    IRECT textTitleRow = textPane.ReduceFromTop(24.f);
    pGraphics->AttachControl(new ITextControl(textTitleRow, "Text Buffer",
      DEFAULT_TEXT.WithSize(14.f).WithFGColor(kUiPurpleLight).WithAlign(EAlign::Near).WithFont(kUIFontID), COLOR_TRANSPARENT));
    textPane.ReduceFromTop(6.f);

    pGraphics->AttachControl(new SAMTextPanelControl(textPane.GetPadded(-2.f), "",
      [this](const char* text) {
        SetTextBuffer(text);
        mNeedsRender.store(true, std::memory_order_release);
        if (!RenderPhraseFromText())
          DBGMSG("SAMVST: failed to render phrase after text commit\n");
        SyncUIState();
      },
      [this](const char* text) {
        // Keep phrase state current while typing; synth render happens on trigger/commit.
        SetTextBuffer(text);
        mNeedsRender.store(true, std::memory_order_release);
      }), kCtrlTagTextPanel);

    pGraphics->SetKeyHandlerFunc([pGraphics](const IKeyPress& key, bool isUp) {
      if (auto* pControl = pGraphics->GetControlWithTag(kCtrlTagTextPanel))
      {
        if (auto* pTextPanel = pControl->As<SAMTextPanelControl>())
          return pTextPanel->HandleGlobalKey(key, isUp);
      }

      return false;
    });

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
      if (auto* pTextControl = pTextPanelControl->As<SAMTextPanelControl>())
      {
        if (!pTextControl->IsEditing())
        {
          std::string text = GetTextBuffer();
          pTextControl->SetStr(text.c_str());
          pTextControl->SetDirty(false);
        }
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
