#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>
#include "birdnet.h"

// ---------------------------------------------------------------------------
// Portrait 240x320 rendering: a text list, plus a full-screen hero on new IDs.
// ---------------------------------------------------------------------------

// The single panel instance, shared with the touch driver.
extern TFT_eSPI tft;

namespace UI {

  void begin();                                        // panel init + backlight
  void panelTest();                                    // R/G/B/W flash (diagnostic)

  // The list: one row per species -> name, detections today, ID confidence.
  void drawList(const Species *rows, int count, const char *status);

  // Full-screen bird photo for a few seconds. `species` supplies the caption.
  void drawHero(const Species &species);

  void message(const char *line1, const char *line2, uint16_t colour);
  void splash(const char *line1, const char *line2 = nullptr);

  // Geometry of the header "setup" button, for hit testing.
  bool setupButtonHit(int16_t x, int16_t y);

}  // namespace UI
