// SPDX-FileCopyrightText: 2025-2026 JIN (GNBD)
// SPDX-License-Identifier: MIT
// Part of ats-mini-recovery-theme. Included by the main .ino in this order.

// ---------------------------------------------------------------------------
// On-screen text input (shared miniradio keypad)
// ---------------------------------------------------------------------------

// Returns true with `out` filled when OK is selected. A blank result is
// rejected unless allowEmpty is set.
static bool runTextInput(const char *title, const String &initial, String &out,
                        const char *suffix, bool allowEmpty)
{
  String value = initial;
  String tail = suffix ? suffix : "";
  kbShift = false;
  kbSym = false;
  kbCursor = 0;

  auto redraw = [&]()
  {
    gSprite.fillScreen(COL_BG);
    drawHeader(title);

    gSprite.fillRoundRect(8, 22, 304, 20, 8, COL_PANEL);
    gSprite.drawRoundRect(8, 22, 304, 20, 8, COL_ACC);
    gSprite.setTextDatum(TL_DATUM);
    gSprite.setTextColor(COL_TEXT, COL_PANEL);
    String shown = value;
    if((int)shown.length() > 30) shown = shown.substring(shown.length() - 30);
    gSprite.drawString((shown + "_").c_str(), 14, 27, FONT_SMALL);
    if(tail.length())
    {
      gSprite.setTextDatum(TR_DATUM);
      gSprite.setTextColor(COL_MUTED, COL_PANEL);
      gSprite.drawString(tail.c_str(), 306, 27, FONT_TINY);
      gSprite.setTextDatum(TL_DATUM);
    }

    kbBuild(56);
    drawKb();
    gSprite.pushSprite(0, 0);
  };

  redraw();
  while(true)
  {
    int8_t d = readEncoder(false);
    if(kbHandleEncoder(d)) redraw();

    BtnEvent ev = readButtonEvent();
    if(ev == BTN_HOLD) return false;
    if(ev == BTN_CLICK)
    {
      const KbKey &k = kbKeys[kbCursor];
      if(k.type == KB_OK)
      {
        if(value.length() == 0 && !allowEmpty) { redraw(); continue; }
        out = value + tail;
        return true;
      }
      if(k.type == KB_CHAR)
      {
        char c = kbChar(kbCursor);
        if(c && value.length() < 28) value += c;
      }
      else if(k.type == KB_DEL) { if(value.length()) value.remove(value.length() - 1); }
      else if(k.type == KB_SHIFT) kbShift = !kbShift;
      else if(k.type == KB_SYM) kbSym = !kbSym;
      else if(k.type == KB_SPACE) { if(value.length() < 28) value += ' '; }
      redraw();
    }
    delay(10);
  }
}
