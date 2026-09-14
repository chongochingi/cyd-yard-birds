#include "ui.h"
#include "config.h"

#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>
#include <LittleFS.h>

// Shared with the touch driver (declared extern in ui.h).
TFT_eSPI tft;

// ---------------------------------------------------------------------------
// TJpg_Decoder output callback.
//
// this fork's tjpgdcnf.h sets JD_FORMAT 0 == RGB888 (3 bytes/pixel) — the
// opposite of upstream, where 0 is RGB565. Convert by hand and byte-swap.
// ---------------------------------------------------------------------------
static uint16_t s_block[16 * 16];      // max MCU at full scale

static bool jpgOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *data) {
  if (y >= (int16_t)SCREEN_H) return false;

  const uint8_t *src = (const uint8_t *)data;
  for (uint16_t row = 0; row < h; row++) {
    if (y + (int16_t)row >= (int16_t)SCREEN_H) break;
    for (uint16_t col = 0; col < w; col++) {
      uint32_t i = ((uint32_t)row * w + col) * 3;
      uint16_t px = ((uint16_t)(src[i    ] >> 3) << 11)
                  | ((uint16_t)(src[i + 1] >> 2) <<  5)
                  |  (uint16_t)(src[i + 2] >> 3);
      s_block[row * w + col] = (px >> 8) | (px << 8);
    }
  }
  tft.pushImage(x, y, w, h, s_block);
  return true;
}

namespace UI {

static int rowY(int i) { return HEADER_H + i * LIST_ROW_H; }

void begin() {
#ifdef TFT_BACKLIGHT_PIN
  pinMode(TFT_BACKLIGHT_PIN, OUTPUT);
  digitalWrite(TFT_BACKLIGHT_PIN, HIGH);
  Serial.printf("[ui] backlight on (gpio %d)\n", TFT_BACKLIGHT_PIN);
#endif
  tft.init();
  tft.setRotation(ROTATION);
  tft.fillScreen(COL_BG);
  TJpgDec.setCallback(jpgOutput);   // no setSwapBytes(): the callback swaps by hand
}

void panelTest() {
  const uint16_t seq[] = {TFT_RED, TFT_GREEN, TFT_BLUE, TFT_WHITE, TFT_BLACK};
  for (uint8_t i = 0; i < sizeof(seq) / sizeof(seq[0]); i++) {
    tft.fillScreen(seq[i]);
    delay(350);
  }
  tft.fillScreen(COL_BG);
}

void splash(const char *line1, const char *line2) {
  tft.fillScreen(COL_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COL_ACCENT, COL_BG);
  tft.drawString(line1, SCREEN_W / 2, SCREEN_H / 2 - 12, 4);
  if (line2) {
    tft.setTextColor(COL_DIM, COL_BG);
    tft.drawString(line2, SCREEN_W / 2, SCREEN_H / 2 + 18, 2);
  }
  tft.setTextDatum(TL_DATUM);
}

void message(const char *line1, const char *line2, uint16_t colour) {
  tft.fillScreen(COL_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(colour, COL_BG);
  tft.drawString(line1, SCREEN_W / 2, SCREEN_H / 2 - 10, 4);
  if (line2) {
    tft.setTextColor(COL_DIM, COL_BG);
    tft.drawString(line2, SCREEN_W / 2, SCREEN_H / 2 + 18, 2);
  }
  tft.setTextDatum(TL_DATUM);
}

// ---------------------------------------------------------------------------
// The list. One row:  NAME ............  379  |  93%
// ---------------------------------------------------------------------------
void drawList(const Species *rows, int count, const char *status) {
  tft.fillScreen(COL_BG);

  // header: title, status, and the setup button
  tft.fillRect(0, 0, SCREEN_W, HEADER_H, COL_HEADER);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COL_TEXT, COL_HEADER);
  tft.drawString("Yard Birds", 4, 3, 2);

  if (status) {
    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(COL_DIM, COL_HEADER);
    tft.drawString(status, BTN_X - 4, 5, 1);
    tft.setTextDatum(TL_DATUM);
  }

  // Tap target: reopens the WiFiManager portal so the server address can be
  // changed without erasing flash.
  tft.fillRoundRect(BTN_X, BTN_Y, BTN_W, BTN_H, 3, COL_ACCENT);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_BLACK, COL_ACCENT);
  tft.drawString("SETUP", BTN_X + BTN_W / 2, BTN_Y + BTN_H / 2, 1);
  tft.setTextDatum(TL_DATUM);

  // column captions
  tft.setTextColor(COL_DIM, COL_BG);
  tft.drawString("species", 4, HEADER_H - 2, 1);
  tft.setTextDatum(TR_DATUM);
  tft.drawString("today", 196, HEADER_H - 2, 1);
  tft.drawString("id", 236, HEADER_H - 2, 1);
  tft.setTextDatum(TL_DATUM);

  int shown = count;
  if (shown > LIST_ROWS) shown = LIST_ROWS;

  for (int i = 0; i < shown; i++) {
    const Species &s = rows[i];
    int y = rowY(i);

    if (i & 1) tft.fillRect(0, y, SCREEN_W, LIST_ROW_H, COL_ROW_ALT);

    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(COL_TEXT, (i & 1) ? COL_ROW_ALT : COL_BG);
    tft.drawString(s.commonName, 4, y + 5, 2);

    char cnt[10];
    snprintf(cnt, sizeof(cnt), "%d", s.count);
    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(COL_ACCENT, (i & 1) ? COL_ROW_ALT : COL_BG);
    tft.drawString(cnt, 196, y + 5, 2);

    char pct[8];
    snprintf(pct, sizeof(pct), "%d%%", (int)(s.maxConfidence * 100.0f + 0.5f));
    tft.setTextColor(s.maxConfidence >= 0.7f ? COL_ACCENT : COL_DIM,
                     (i & 1) ? COL_ROW_ALT : COL_BG);
    tft.drawString(pct, 236, y + 5, 2);
    tft.setTextDatum(TL_DATUM);
  }

  if (shown == 0) {
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(COL_DIM, COL_BG);
    tft.drawString("no detections today", SCREEN_W / 2, SCREEN_H / 2, 2);
    tft.setTextDatum(TL_DATUM);
  }
}

// ---------------------------------------------------------------------------
// The hero. Species JPEGs are 320x240; portrait is 240 wide, so decode at full
// scale into a 240x240 viewport offset -40px to centre-crop. Caption below.
// ---------------------------------------------------------------------------
void drawHero(const Species &s) {
  String path = BirdNet::ensureImage(s.scientificName);

  tft.fillScreen(COL_BG);

  if (path.length()) {
    const int srcW = 320;
    int xoff = -(srcW - SCREEN_W) / 2;          // -40, centre-crop
    tft.setViewport(0, 0, SCREEN_W, HERO_IMG_H, false);
    TJpgDec.setJpgScale(0);                      // 1/1
    TJpgDec.drawFsJpg(xoff, 0, path, LittleFS);
    tft.resetViewport();
  } else {
    tft.fillRect(0, 0, SCREEN_W, HERO_IMG_H, COL_TILE);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(COL_DIM, COL_TILE);
    tft.drawString("no image", SCREEN_W / 2, HERO_IMG_H / 2, 2);
    tft.setTextDatum(TL_DATUM);
  }

  // caption band
  int by = HERO_IMG_H;
  tft.fillRect(0, by, SCREEN_W, SCREEN_H - by, COL_HERO_BAND);

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COL_TEXT, COL_HERO_BAND);
  tft.drawString(s.commonName, SCREEN_W / 2, by + 22, 4);

  char line[48];
  snprintf(line, sizeof(line), "%d today  -  %d%% id",
           s.count, (int)(s.maxConfidence * 100.0f + 0.5f));
  tft.setTextColor(COL_ACCENT, COL_HERO_BAND);
  tft.drawString(line, SCREEN_W / 2, by + 54, 2);
  tft.setTextDatum(TL_DATUM);
}

// Generous hit box — the resistive panel needs a firm, imprecise tap.
bool setupButtonHit(int16_t x, int16_t y) {
  return x >= BTN_X - 4 && x < BTN_X + BTN_W + 4 &&
         y >= BTN_Y - 2 && y < BTN_Y + BTN_H + 4;
}

}  // namespace UI
