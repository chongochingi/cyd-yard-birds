#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include "config.h"
#include "birdnet.h"
#include "ui.h"
#include "touch.h"

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

// --- setup portal state -----------------------------------------------------
// The parameters must stay alive for the whole portal session, and the save
// callback needs to reach them, so they are kept at file scope.
static WiFiManagerParameter *g_pHost = nullptr;
static WiFiManagerParameter *g_pPort = nullptr;
static WiFiManagerParameter *g_pUser = nullptr;
static WiFiManagerParameter *g_pPass = nullptr;

// Fired the moment the user taps Save, before the portal decides whether to
// close. This is what makes a server-only change stick even if the portal then
// sits waiting on a WiFi reconnect.
static void onPortalSave() {
  if (g_pHost && g_pPort)
    BirdNet::setServer(g_pHost->getValue(), (uint16_t)atoi(g_pPort->getValue()));
  if (g_pUser && g_pPass)
    BirdNet::setAuth(g_pUser->getValue(), g_pPass->getValue());
  Serial.println("[cfg] portal save — server/credentials persisted");
}

// Opens the captive portal, persists whatever was entered, and returns.
// Callable from setup() or later from the header button.
static void runSetupPortal(const char *title) {
  const bool wifiWasOk = (WiFi.status() == WL_CONNECTED);

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
  g_pHost = &pHost; g_pPort = &pPort; g_pUser = &pUser; g_pPass = &pPass;

  wm.addParameter(&pHost);
  wm.addParameter(&pPort);
  wm.addParameter(&pUser);
  wm.addParameter(&pPass);

  // Persist on Save rather than relying on the portal returning — the portal
  // only returns once WiFi connects, which never happens when WiFi is already
  // fine and only the server address changed.
  wm.setSaveParamsCallback(onPortalSave);

  // If WiFi already works there is nothing to wait for, so close on save.
  // Otherwise keep the default so a new network can be verified.
  if (wifiWasOk) wm.setBreakAfterConfig(true);

  UI::splash(title, "join CYD-Birds-Setup");
  bool ok = wm.startConfigPortal("CYD-Birds-Setup");
  if (!ok) Serial.println("[cfg] portal closed without confirmation");

  // Belt and braces — persist again in case the portal did return normally.
  if (pHost.getValue() && strlen(pHost.getValue()))
    BirdNet::setServer(pHost.getValue(), (uint16_t)atoi(pPort.getValue()));
  BirdNet::setAuth(pUser.getValue(), pPass.getValue());
  g_pHost = g_pPort = g_pUser = g_pPass = nullptr;
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== CYD Yard Birds ===");

  UI::begin();
  UI::panelTest();                    // R/G/B/W flash — proves panel + backlight are alive
  UI::splash("Yard Birds", "starting up");

  // Touch drives only the header SETUP button. First boot runs a two-point
  // calibration and stores it in NVS; every later boot reuses it.
  //
  // A bad calibration makes the panel unusable and you cannot tap your way out
  // of it, so allow forcing a redo: send 'c' on the serial console within 2s of
  // boot. This is the only recovery path that does not depend on touch.
  bool forceCal = false;
  unsigned long calWait = millis();
  while (millis() - calWait < 2000) {
    if (Serial.available()) {
      int ch = Serial.read();
      if (ch == 'c' || ch == 'C') { forceCal = true; break; }
    }
    delay(20);
  }
  if (forceCal) Serial.println("[touch] recalibration triggered from serial");
  touchInit(forceCal);

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
    runSetupPortal(title);
  }

  Serial.printf("[wifi] ip=%s server=%s:%u\n",
                WiFi.localIP().toString().c_str(),
                BirdNet::serverHost().c_str(), (unsigned)BirdNet::serverPort());

  // After a portal save the first request can race the WiFi reconnect, so give
  // it a few tries before declaring there is nothing to show. Without this the
  // screen reads "no detections today" for up to a full refresh interval.
  const int attempts = needPortal ? 6 : 1;
  for (int i = 0; i < attempts; i++) {
    UI::splash("Loading", "today's birds");
    gCount       = BirdNet::fetchDailySpecies(gRows, LIST_ROWS);
    gLastRefresh = millis();
    if (gCount > 0) break;
    delay(2500);
  }
  showList();
}

void loop() {
  // --- header SETUP button: reopen the portal to change the server address ---
  TouchEvent te = touchProcess();
  if (te.type == TouchEvent::TAP && UI::setupButtonHit(te.x, te.y)) {
    Serial.printf("[ui] SETUP tapped at (%d,%d)\n", te.x, te.y);
    UI::drawList(gRows, gCount, "setup...");
    runSetupPortal("Server setup");
    // Whatever was entered is now in NVS; reconnect if WiFi changed.
    if (WiFi.status() != WL_CONNECTED) {
      WiFi.begin();
      delay(2000);
    }
    refreshList();
    showList();
    return;
  }

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
