#include "birdnet.h"
#include "config.h"

#include <LittleFS.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>

namespace BirdNet {

static WiFiClient s_stream;
static bool       s_streamUp = false;

// Server address lives in NVS so a downloaded build can be pointed at any
// BirdNET-Go instance from the setup portal — no recompile.
static String   s_host = BIRDNET_DEFAULT_HOST;
static uint16_t s_port = BIRDNET_DEFAULT_PORT;

static const char *NVS_NS = "bnet";

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

// "Cyanocitta cristata" -> "/birds/cyanocitta_cristata.jpg"
static String cachePathFor(const String &sciName) {
  String s = sciName;
  s.toLowerCase();
  s.replace(" ", "_");
  s.replace("/", "-");
  s.replace("'", "");
  s.replace(".", "");
  return String(CACHE_DIR) + "/" + s + ".jpg";
}

// "Cyanocitta cristata" -> "Cyanocitta%20cristata"
static String urlEncode(const String &in) {
  String out;
  char buf[8];
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += c;
    } else {
      snprintf(buf, sizeof(buf), "%%%02X", (unsigned char)c);
      out += buf;
    }
  }
  return out;
}

String serverHost() { return s_host; }
uint16_t serverPort() { return s_port; }

void setServer(const String &host, uint16_t port) {
  if (host.length()) s_host = host;
  if (port)          s_port = port;
  Preferences p;
  if (p.begin(NVS_NS, false)) {
    p.putString("host", s_host);
    p.putUShort("port", s_port);
    p.end();
  }
  Serial.printf("[cfg] server = %s:%u\n", s_host.c_str(), (unsigned)s_port);
}

String apiBase() {
  return String("http://") + s_host + ":" + String(s_port) + "/api/v2";
}

// ---------------------------------------------------------------------------
// lifecycle
// ---------------------------------------------------------------------------
void begin() {
  if (!LittleFS.begin(true)) {
    Serial.println("[cache] LittleFS mount FAILED");
  } else {
    Serial.printf("[cache] LittleFS ok, used %u / %u bytes\n",
                  (unsigned)LittleFS.usedBytes(), (unsigned)LittleFS.totalBytes());
  }
  if (!LittleFS.exists(CACHE_DIR)) LittleFS.mkdir(CACHE_DIR);

  // load a previously configured server, if any
  // (read-write: opens read-only would fail with NOT_FOUND before first save)
  Preferences p;
  if (p.begin(NVS_NS, false)) {
    s_host = p.getString("host", BIRDNET_DEFAULT_HOST);
    s_port = p.getUShort("port", BIRDNET_DEFAULT_PORT);
    p.end();
  }
  Serial.printf("[cfg] server = %s:%u\n", s_host.c_str(), (unsigned)s_port);
}

// ---------------------------------------------------------------------------
// today's species list
//
// /analytics/species/daily returns an array already sorted by count descending:
//   [{scientific_name, common_name, species_code, count, high_confidence,
//     max_confidence, first_heard, latest_heard, days_this_year, ...}]
// ---------------------------------------------------------------------------
int fetchDailySpecies(Species *out, int maxCount) {
  if (WiFi.status() != WL_CONNECTED) return 0;

  HTTPClient http;
  String url = apiBase() + "/analytics/species/daily";
  http.begin(url);
  http.setTimeout(8000);
  int code = http.GET();
  if (code != 200) {
    Serial.printf("[list] GET %s -> %d\n", url.c_str(), code);
    http.end();
    return 0;
  }
  String body = http.getString();
  http.end();

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    Serial.printf("[list] json error: %s\n", err.c_str());
    return 0;
  }

  JsonArray arr;
  if (doc.is<JsonArray>())            arr = doc.as<JsonArray>();
  else if (doc["data"].is<JsonArray>()) arr = doc["data"].as<JsonArray>();
  else return 0;

  int n = 0;
  for (JsonObject o : arr) {
    if (n >= maxCount) break;
    const char *cn = o["common_name"]     | (const char *)nullptr;
    const char *sn = o["scientific_name"] | (const char *)nullptr;
    if (!cn && !sn) continue;

    Species &s = out[n];
    s.commonName     = cn ? cn : sn;
    s.scientificName = sn ? sn : cn;
    s.count          = o["count"]          | 0;
    s.maxConfidence  = o["max_confidence"] | 0.0f;
    s.latestHeard    = (const char *)(o["latest_heard"] | "");
    n++;
  }
  Serial.printf("[list] %d species today\n", n);
  return n;
}

// ---------------------------------------------------------------------------
// image cache
// ---------------------------------------------------------------------------
String ensureImage(const String &scientificName) {
  if (scientificName.isEmpty()) return String();

  String path = cachePathFor(scientificName);

  if (LittleFS.exists(path)) {
    File f = LittleFS.open(path, "r");
    if (f && f.size() > 512) { f.close(); return path; }
    if (f) f.close();
    LittleFS.remove(path);              // truncated entry, refetch
  }

  if (WiFi.status() != WL_CONNECTED) return String();

  String url = apiBase() + "/media/image/" + urlEncode(scientificName);
  HTTPClient http;
  http.begin(url);
  http.setTimeout(8000);
  int code = http.GET();
  if (code != 200) {
    Serial.printf("[img] GET %s -> %d\n", url.c_str(), code);
    http.end();
    return String();
  }

  File f = LittleFS.open(path, "w");
  if (!f) { http.end(); return String(); }
  int written = http.writeToStream(&f);
  f.close();
  http.end();

  if (written <= 512) { LittleFS.remove(path); return String(); }
  Serial.printf("[img] cached %s (%d bytes)\n", path.c_str(), written);
  return path;
}

int cachedImageCount() {
  int n = 0;
  File dir = LittleFS.open(CACHE_DIR);
  if (!dir || !dir.isDirectory()) return 0;
  File f = dir.openNextFile();
  while (f) { if (!f.isDirectory()) n++; f = dir.openNextFile(); }
  return n;
}

size_t cacheBytesUsed() {
  size_t total = 0;
  File dir = LittleFS.open(CACHE_DIR);
  if (!dir || !dir.isDirectory()) return 0;
  File f = dir.openNextFile();
  while (f) { if (!f.isDirectory()) total += f.size(); f = dir.openNextFile(); }
  return total;
}

// ---------------------------------------------------------------------------
// SSE live stream
// ---------------------------------------------------------------------------
bool streamConnected() { return s_streamUp && s_stream.connected(); }

void streamClose() {
  s_stream.stop();
  s_streamUp = false;
}

bool streamOpen() {
  streamClose();
  if (WiFi.status() != WL_CONNECTED) return false;
  if (!s_stream.connect(s_host.c_str(), s_port)) {
    Serial.println("[sse] connect failed");
    return false;
  }
  String req = String("GET /api/v2/detections/stream HTTP/1.1\r\n") +
               "Host: " + s_host + ":" + String(s_port) + "\r\n" +
               "Accept: text/event-stream\r\n" +
               "Cache-Control: no-cache\r\n" +
               "Connection: keep-alive\r\n\r\n";
  s_stream.print(req);

  // drain headers (blank line terminates)
  unsigned long t0 = millis();
  while (millis() - t0 < 5000) {
    if (s_stream.available() && s_stream.readStringUntil('\n') == "\r") break;
    delay(1);
  }
  s_streamUp = true;
  Serial.println("[sse] stream open");
  return true;
}

static bool readLine(String &out, unsigned long deadline) {
  out = "";
  while (millis() < deadline) {
    if (!s_stream.connected() && !s_stream.available()) return false;
    while (s_stream.available()) {
      char c = (char)s_stream.read();
      if (c == '\n') { out.trim(); return true; }
      out += c;
      if (out.length() > 4096) out = "";
    }
    delay(2);
  }
  return false;
}

bool streamNext(String &scientificName, String &commonName, unsigned long timeoutMs) {
  if (!streamConnected()) return false;

  unsigned long deadline = millis() + timeoutMs;
  String eventName;

  while (millis() < deadline) {
    if (!s_stream.connected() && !s_stream.available()) { s_streamUp = false; return false; }

    String line;
    if (!readLine(line, deadline)) break;
    if (line.length() == 0) continue;

    if (line.startsWith("event:")) { eventName = line.substring(6); eventName.trim(); continue; }
    if (!line.startsWith("data:")) continue;

    String payload = line.substring(5);
    payload.trim();
    if (payload.isEmpty() || eventName == "connected") continue;

    // Payload is an array of pending detections:
    // [{species, scientificName, thumbnail, status, hitCount, firstDetected, ...}]
    JsonDocument doc;
    if (deserializeJson(doc, payload)) continue;

    for (JsonObject o : doc.as<JsonArray>()) {
      const char *sn = o["scientificName"] | (const char *)nullptr;
      const char *sp = o["species"]        | (const char *)nullptr;
      if (!sn && !sp) continue;
      scientificName = sn ? sn : sp;
      commonName     = sp ? sp : sn;
      return true;
    }
  }
  return false;
}

}  // namespace BirdNet
