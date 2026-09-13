#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------------------
// BirdNET-Go client.
//
//   list   -> GET /api/v2/analytics/species/daily   (name, count today, confidence)
//   stream -> GET /api/v2/detections/stream         (SSE, fires on new ID)
//   image  -> GET /api/v2/media/image/<scientificName>   (320x240 JPEG)
// ---------------------------------------------------------------------------

struct Species {
  String commonName;
  String scientificName;
  int    count          = 0;      // detections today
  float  maxConfidence  = 0.0f;   // best ID confidence today
  String latestHeard;             // "17:45:20"
};

namespace BirdNet {

  void begin();                                        // mount LittleFS + cache dir

  // Server address, persisted in NVS. Defaults to BIRDNET_DEFAULT_HOST/PORT on
  // first boot, then whatever the user entered in the setup portal.
  void   setServer(const String &host, uint16_t port);
  String serverHost();
  uint16_t serverPort();
  String apiBase();                                    // exposed for logging

  // Optional HTTP Basic Auth — matches BirdNET-Go's security.basicauth block.
  // Empty username means no Authorization header is sent.
  void   setAuth(const String &user, const String &password);
  String authUser();
  bool   authEnabled();

  // Today's species, already sorted by count descending by the server.
  int fetchDailySpecies(Species *out, int maxCount);

  // Cache the species JPEG on LittleFS; returns its path (empty on failure).
  String ensureImage(const String &scientificName);

  // ---- SSE ----
  bool streamOpen();
  void streamClose();
  bool streamConnected();
  // Wait up to timeoutMs for a new detection. Returns true and fills sci/common.
  bool streamNext(String &scientificName, String &commonName, unsigned long timeoutMs);

  int    cachedImageCount();
  size_t cacheBytesUsed();

}  // namespace BirdNet
