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

  bool needPortal   = false;
  const char *title = "Setup";

  if (WiFi.status() != WL_CONNECTED) {
    // WiFi itself is the problem.
    needPortal = true;
    title      = "WiFi setup";
  } else {
    Serial.printf("[wifi] connected, ip=%s\n", WiFi.localIP().toString().c_str());
    UI::splash("Loading", "today's birds");

    bool reachable = (BirdNet::fetchDailySpecies(gRows, LIST_ROWS) > 0);

    if (reachable) {
      gLastRefresh = millis();
      showList();
    } else if (!BirdNet::serverConfigured()) {
      // Never been set up — this is genuine first-time configuration, so the
      // screen should say "server", not "WiFi".
      Serial.println("[cfg] no server configured yet — first-time setup");
      needPortal = true;
      title      = "Server setup";
    } else {
      // Configured but temporarily unreachable. Retry quietly rather than
      // throwing a setup screen at a device that is already configured.
      Serial.printf("[cfg] %s:%u unreachable — retrying %lus before setup\n",
                    BirdNet::serverHost().c_str(), (unsigned)BirdNet::serverPort(),
                    SERVER_GRACE_MS / 1000);
      unsigned long graceStart = millis();
      while (millis() - graceStart < SERVER_GRACE_MS) {
        UI::message("Can't reach server", BirdNet::serverHost().c_str(), COL_ACCENT);
        delay(5000);
        if (BirdNet::fetchDailySpecies(gRows, LIST_ROWS) > 0) {
          reachable    = true;
          gLastRefresh = millis();
          showList();
          break;
        }
      }
      if (!reachable) {
        Serial.println("[cfg] still unreachable — offering setup to correct it");
        needPortal = true;
        title      = "Server setup";
      }
    }
  }

  // --- 2. Setup portal: WiFi network + BirdNET-Go address + optional auth ------
  if (needPortal) {
    WiFiManager wm;
    wm.setConfigPortalTimeout(WIFI_PORTAL_TIMEOUT_S);
    wm.setDebugOutput(false);

    WiFiManagerParameter pHost("host", "BirdNET-Go host (IP or hostname)",
                               BirdNet::serverHost().c_str(), 40);
    WiFiManagerParameter pPort("port", "BirdNET-Go port",
                               String(BirdNet::serverPort()).c_str(), 6);
    WiFiManagerParameter pUser("user", "Basic auth user (leave blank if none)",
                               BirdNet::authUser().c_str(), 32);
    WiFiManagerParameter pPass("pass", "Basic auth password", "", 64);
    wm.addParameter(&pHost);
    wm.addParameter(&pPort);
    wm.addParameter(&pUser);
    wm.addParameter(&pPass);

    UI::splash(title, "join CYD-Birds-Setup");
    if (!wm.startConfigPortal("CYD-Birds-Setup")) {
      UI::message("Setup failed", "restarting...", COL_ACCENT);
      delay(8000);
      ESP.restart();
    }
    // portal closed with values saved — persist server + credentials
    BirdNet::setServer(pHost.getValue(), (uint16_t)atoi(pPort.getValue()));
    BirdNet::setAuth(pUser.getValue(), pPass.getValue());
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
