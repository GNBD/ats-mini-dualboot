// SPDX-FileCopyrightText: 2025-2026 JIN (GNBD)
// SPDX-License-Identifier: MIT
// Part of ats-mini-recovery-theme. Included by the main .ino in this order.

// ---------------------------------------------------------------------------
// Input helpers
// ---------------------------------------------------------------------------

static int16_t accelerateEncoder(int8_t dir)
{
  const uint32_t speedThresholds[] = {350, 60, 45, 35, 25};
  const uint16_t accelFactors[] =      {1,  2,  4,  8, 16};
  static uint32_t lastEncoderTime = 0;
  static uint32_t lastSpeed = speedThresholds[0];
  static uint16_t lastAccelFactor = accelFactors[0];
  static int8_t lastEncoderDir = 0;

  uint32_t currentTime = millis();
  lastSpeed = ((currentTime - lastEncoderTime) * 7 + lastSpeed * 3) / 10;

  if(lastSpeed > speedThresholds[0] || lastEncoderDir != dir)
  {
    lastSpeed = speedThresholds[0];
    lastAccelFactor = accelFactors[0];
  }
  else
  {
    for(int8_t i = 4; i >= 0; i--)
    {
      if(lastSpeed <= speedThresholds[i] && lastAccelFactor < accelFactors[i])
      {
        lastAccelFactor = accelFactors[i];
        break;
      }
    }
  }
  lastEncoderTime = currentTime;
  lastEncoderDir = dir;
  return dir * lastAccelFactor;
}

// PCNT counts detents in hardware. Draining them here and running each step
// through accelerateEncoder() keeps the old ISR feel, including the five-step
// burst cap.
static int16_t rawEncoderCount = 0;
static int16_t accelEncoderCount = 0;

// accel=true: menus (main feel). accel=false: keyboard (raw steps).
static int8_t readEncoder(bool accel = true)
{
  int16_t detents = (int16_t)encoder_get_delta();
  int8_t step = (detents > 0) ? 1 : -1;
  for(int16_t i = 0; i < abs((int)detents); i++)
  {
    if(abs(rawEncoderCount) >= 5) break;   // same burst cap as the old ISR
    rawEncoderCount += step;
    accelEncoderCount += accelerateEncoder(step);
  }

  int16_t c = accel ? accelEncoderCount : rawEncoderCount;
  rawEncoderCount = 0;
  accelEncoderCount = 0;
  if(c > 127) c = 127;
  if(c < -127) c = -127;
  return (int8_t)c;
}

// DBFM input rule: a short click opens/selects, a hold goes back. The hold
// fires at 450 ms without waiting for release, and is consumed until the
// button is released so one long press never fires twice.
static bool gBtnConsumed = false;
enum BtnEvent : uint8_t { BTN_NONE = 0, BTN_CLICK, BTN_HOLD };

// Hold ring: a small circular progress with a back chevron, shown only once
// the press is clearly turning into a hold. The covered pixels are snapshotted
// so a short click leaves the screen untouched.
#define HOLD_RING_R   26
#define HOLD_RING_X   160
#define HOLD_RING_Y   84
static uint16_t *holdRingBackup = nullptr;
static bool holdRingBackupOk = false;

static void holdRingSave()
{
  if(!holdRingBackup)
    holdRingBackup = (uint16_t *)ps_malloc(HOLD_RING_R * 2 * HOLD_RING_R * 2 * 2);
  if(!holdRingBackup) { holdRingBackupOk = false; return; }
  gSprite.readRect(HOLD_RING_X - HOLD_RING_R,
                   HOLD_RING_Y - HOLD_RING_R,
                   HOLD_RING_R * 2, HOLD_RING_R * 2,
                   holdRingBackup);
  holdRingBackupOk = true;
}

static void holdRingRestore()
{
  if(holdRingBackupOk)
  {
    gSprite.pushImage(HOLD_RING_X - HOLD_RING_R, HOLD_RING_Y - HOLD_RING_R,
                      HOLD_RING_R * 2, HOLD_RING_R * 2, holdRingBackup);
    gSprite.pushSprite(0, 0);
  }
  holdRingBackupOk = false;
}

static void holdRingDraw(float p)
{
  if(p < 0.0f) p = 0.0f;
  if(p > 1.0f) p = 1.0f;

  gSprite.fillCircle(HOLD_RING_X, HOLD_RING_Y, 24, COL_PANEL);
  gSprite.drawCircle(HOLD_RING_X, HOLD_RING_Y, 24, COL_LINE);
  gSprite.drawArc(HOLD_RING_X, HOLD_RING_Y, 23, 20, 0, (int)(p * 359), COL_GOLD);

  int col = (p > 0.5f) ? COL_GOLD : COL_MUTED;
  gSprite.drawLine(HOLD_RING_X + 5, HOLD_RING_Y - 7,
                   HOLD_RING_X - 2, HOLD_RING_Y, col);
  gSprite.drawLine(HOLD_RING_X - 2, HOLD_RING_Y,
                   HOLD_RING_X + 5, HOLD_RING_Y + 7, col);
  gSprite.setTextDatum(MC_DATUM);
  gSprite.setTextColor(col, COL_PANEL);
  gSprite.drawString("BACK", HOLD_RING_X, HOLD_RING_Y + 14, FONT_TINY);
  gSprite.setTextDatum(TL_DATUM);
  gSprite.pushSprite(0, 0);
}

static BtnEvent readButtonEvent()
{
  batteryTick();

  if(digitalRead(ENCODER_PUSH_BUTTON) != LOW)
  {
    gBtnConsumed = false;
    return BTN_NONE;
  }
  if(gBtnConsumed) return BTN_NONE;

  delay(30);
  if(digitalRead(ENCODER_PUSH_BUTTON) != LOW)
  {
    gBtnConsumed = false;
    return BTN_NONE;
  }

  uint32_t start = millis();
  bool ringShown = false;
  while(digitalRead(ENCODER_PUSH_BUTTON) == LOW)
  {
    uint32_t elapsed = millis() - start;
    if(elapsed >= 120 && !ringShown)
    {
      holdRingSave();
      ringShown = true;
    }
    if(ringShown)
      holdRingDraw((float)(elapsed - 120) / 330.0f);

    if(elapsed >= 450)
    {
      gBtnConsumed = true;
      holdRingDraw(1.0f);
      delay(40);
      holdRingRestore();
      return BTN_HOLD;
    }
    delay(10);
  }

  if(ringShown) holdRingRestore();
  return BTN_CLICK;
}

// Redrawn once a second from readButton() so the header voltage tracks the
// supply without any menu having to own a timer.
static void batteryTick();

static uint32_t readButton()
{
  batteryTick();

  if(digitalRead(ENCODER_PUSH_BUTTON) != LOW)
  {
    gBtnConsumed = false;
    return 0;
  }
  if(gBtnConsumed) return 0;
  delay(30);
  if(digitalRead(ENCODER_PUSH_BUTTON) != LOW)
  {
    gBtnConsumed = false;
    return 0;
  }

  uint32_t start = millis();
  while(digitalRead(ENCODER_PUSH_BUTTON) == LOW)
  {
    uint32_t held = millis() - start;
    if(held >= 300)
    {
      gBtnConsumed = true;
      return held;                          // auto-enter while still held
    }
    delay(10);
  }
  return millis() - start;                  // released early = click
}

