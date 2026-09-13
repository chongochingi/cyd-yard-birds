#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------------------
// Yard-bird display for the ESP32-2432S028 "Cheap Yellow Display", fed by
// BirdNET-Go. Portrait 240x320: a text list by default, full-screen photo on a
// new identification. No SD card — species JPEGs cache in internal LittleFS.
// ---------------------------------------------------------------------------

// ---- BirdNET-Go server -----------------------------------------------------
// These are only DEFAULTS. The real address is entered once in the setup portal
// and stored in NVS, so a downloaded build works against anyone's instance.
#define BIRDNET_DEFAULT_HOST   "birdnet.local"   // or an IP / hostname
#define BIRDNET_DEFAULT_PORT   8085

// ---- Panel geometry (portrait) --------------------------------------------
#define SCREEN_W   240
#define SCREEN_H   320
#define ROTATION   0                 // 0/2 = portrait on ILI9341

// ---- List layout ----------------------------------------------------------
#define HEADER_H      20
#define LIST_ROW_H    25
#define LIST_ROWS     12                 // 20 + 12*25 = 320 exactly
#define LIST_NAME_MAX 20                 // glyphs that fit before the count column

// ---- Hero layout ----------------------------------------------------------
#define HERO_IMG_H    240                // 240x240 centre-cropped photo
#define HERO_HOLD_MS  3000UL             // how long the photo stays up

// ---- Image cache (internal LittleFS) --------------------------------------
#define CACHE_DIR       "/birds"
#define CACHE_MAX_BYTES (700UL * 1024UL)  // leave headroom in the 896 KB partition

// ---- Timings --------------------------------------------------------------
#define WIFI_PORTAL_TIMEOUT_S  180           // captive portal auto-close
#define WIFI_CONNECT_MS        15000UL       // try saved credentials this long first
#define LIST_REFRESH_MS        (5UL * 60UL * 1000UL)   // re-pull counts every 5 min
#define RECONNECT_DELAY_MS     5000UL
#define STREAM_POLL_MS         10000UL       // SSE wait per loop
#define HERO_DEDUPE_MS         45000UL       // ignore repeat IDs of the same bird

// ---- Colours (RGB565) -----------------------------------------------------
#define COL_BG        0x0000
#define COL_HEADER    0x1A4C
#define COL_TEXT      0xFFFF
#define COL_DIM       0xB5B6
#define COL_ACCENT    0x3D8F
#define COL_ROW_ALT   0x10A2
#define COL_HERO_BAND 0x18E3
#define COL_TILE      0x18E3
