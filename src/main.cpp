#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include "config.h"
#include "birdnet.h"
#include "ui.h"

// ---------------------------------------------------------------------------
// Yard-bird display driven by BirdNET-Go.
//
//   default view : list of today's species — name, detections today, ID %
//   new ID       : full-screen photo for HERO_HOLD_MS, then back to the list
//
// First boot (or an unreachable server) opens a captive portal that collects
// BOTH the WiFi network and the BirdNET-Go address, so a downloaded build can be
// pointed at anyone's instance without recompiling.
// ---------------------------------------------------------------------------

static Species gRows[LIST_ROWS];
static int     gCount       = 0;
static ulong   gLastRefresh = 0;
static ulong   gLastHeroAt  = 0;
static String  gLastHeroSci;

static void refreshList() {
  gCount = BirdNet::fetchDailySpecies(gRows, LIST_ROWS);
  gLastRefresh = millis();
}

static const char *statusText() {
  if (WiFi.status() != WL_CONNECTED) return "no wifi";
  if (!BirdNet::streamConnected())   return "linking";
  return "live";
}

static void showList() { UI::drawList(gRows, gCount, statusText()); }

static Species resolveSpecies(const String &sci, const String &common) {
  for (int i = 0; i < gCount; i++)
    if (gRows[i].scientificName == sci) return gRows[i];
  Species s;
  s.scientificName = sci;
  s.commonName     = common;
  return s;
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== CYD Yard Birds ===");

  UI::begin();
  UI::panelTest();
  UI::splash("Yard Birds", "starting up");

  BirdNet::begin();                    // mounts LittleFS, loads saved server

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  // --- 1. Saved credentials first. A normal boot must NOT raise the setup AP. ---
  UI::splash("Yard Birds", "connecting...");
  WiFi.begin();                        // credentials stored in NVS
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_CONNECT_MS) delay(250);

  bool needPortal = (WiFi.status() != WL_CONNECTED);

  if (!needPortal) {
    Serial.printf("[wifi] connected, ip=%s\n", WiFi.localIP().toString().c_str());

    // --- 2. Can we actually reach BirdNET-Go? If not, the address is wrong. ---
    UI::splash("Loading", "today's birds");
    if (BirdNet::fetchDailySpecies(gRows, LIST_ROWS) == 0) {
      Serial.printf("[cfg] %s:%u unreachable, opening setup\n",
                    BirdNet::serverHost().c_str(), (unsigned)BirdNet::serverPort());
      UI::message("Server unreachable", BirdNet::serverHost().c_str(), COL_ACCENT);
      delay(2500);
      needPortal = true;
    } else {
      gLastRefresh = millis();
      showList();
    }
  }

  // --- 3. Setup portal: WiFi network + BirdNET-Go address ---------------------
  if (needPortal) {
    WiFiManager wm;
    wm.setConfigPortalTimeout(WIFI_PORTAL_TIMEOUT_S);
    wm.setDebugOutput(false);

    WiFiManagerParameter pHost("host", "BirdNET-Go host (IP or hostname)",
                               BirdNet::serverHost().c_str(), 40);
    WiFiManagerParameter pPort("port", "BirdNET-Go port",
                               String(BirdNet::serverPort()).c_str(), 6);
    wm.addParameter(&pHost);
    wm.addParameter(&pPort);

    UI::splash("WiFi setup", "join CYD-Birds-Setup");
    if (!wm.startConfigPortal("CYD-Birds-Setup")) {
      UI::message("Setup failed", "restarting...", COL_ACCENT);
      delay(8000);
      ESP.restart();
    }
    // portal closed with values saved — persist the server address
    BirdNet::setServer(pHost.getValue(), (uint16_t)atoi(pPort.getValue()));
  }

  Serial.printf("[wifi] ip=%s server=%s:%u\n",
                WiFi.localIP().toString().c_str(),
                BirdNet::serverHost().c_str(), (unsigned)BirdNet::serverPort());

  UI::splash("Loading", "today's birds");
  refreshList();
  showList();
}

void loop() {
  if (!BirdNet::streamConnected()) {
    showList();                                  // status reads "linking"
    if (!BirdNet::streamOpen()) {
      delay(RECONNECT_DELAY_MS);
      return;
    }
    showList();
  }

  String sci, common;
  if (BirdNet::streamNext(sci, common, STREAM_POLL_MS)) {
    bool dup = (sci == gLastHeroSci) && (millis() - gLastHeroAt < HERO_DEDUPE_MS);
    if (!dup && sci.length()) {
      refreshList();                             // pick up the bumped count
      Species s = resolveSpecies(sci, common);
      UI::drawHero(s);
      gLastHeroSci = sci;
      gLastHeroAt  = millis();
      delay(HERO_HOLD_MS);                       // hold the photo
      showList();
    }
  }

  if (millis() - gLastRefresh > LIST_REFRESH_MS) {
    refreshList();
    showList();
  }

  if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
    delay(RECONNECT_DELAY_MS);
  }
}
