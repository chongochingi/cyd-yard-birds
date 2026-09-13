#pragma once
#include <Arduino.h>
#include "birdnet.h"

// ---------------------------------------------------------------------------
// Portrait 240x320 rendering: a text list, plus a full-screen hero on new IDs.
// ---------------------------------------------------------------------------

namespace UI {

  void begin();                                        // panel init + backlight
  void panelTest();                                    // R/G/B/W flash (diagnostic)

  // The list: one row per species -> name, detections today, ID confidence.
  void drawList(const Species *rows, int count, const char *status);

  // Full-screen bird photo for a few seconds. `species` supplies the caption.
  void drawHero(const Species &species);

  void message(const char *line1, const char *line2, uint16_t colour);
  void splash(const char *line1, const char *line2 = nullptr);

}  // namespace UI
