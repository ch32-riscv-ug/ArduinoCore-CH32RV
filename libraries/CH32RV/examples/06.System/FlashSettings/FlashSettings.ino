// Keep a few settings in the internal flash across resets: one page, rewritten in place.
//
// Advanced use. An SD card or an I2C EEPROM is the recommended place for data that must last. Here:
// - each save erases the page; the flash wears out (10K erases on some series), so save on a user action, not on
//   a timer;
// - re-uploading the sketch may erase the page (depends on the route and the tool version);
// - a power cut while saving loses the settings (the sketch then starts from the defaults);
// - on V003, V00x, M030, V103, L103, X035 and V205 the CPU stops for a few ms while saving (UART bytes are lost,
//   millis() falls behind).
//
// Send a number to set the interval and save it; reset the board to see it come back.
#include <ch32rv_flash.h>

struct Settings {
  uint16_t interval_ms;
  uint8_t mode;
  uint8_t level;
};
const Settings kDefaults = {500, 1, 3};

// Change kVersion whenever Settings changes. A page written by another layout, or by another sketch, then reads as
// "nothing saved" instead of as wrong values.
const uint16_t kVersion = 1;

// One write unit; on series that erase by 4 KiB or more the rest of the erase unit stays erased.
union Page {
  uint32_t words[CH32RV_FLASH_WRITE_SIZE / 4];
  struct {
    uint16_t version, size;
    Settings settings;
  } f;
};
static_assert(sizeof(Page) == CH32RV_FLASH_WRITE_SIZE, "Settings does not fit in one page");

CH32RV_FLASH_STORE(store, CH32RV_FLASH_ERASE_SIZE);
const Page &saved = *(const Page *)store;

bool loadSettings(Settings &s) {
  if (saved.f.version != kVersion || saved.f.size != sizeof(Settings)) return false;  // never saved, or another layout
  s = saved.f.settings;
  return true;
}

bool saveSettings(const Settings &s) {
  Page p;
  for (uint32_t &w : p.words) w = 0xFFFFFFFF;
  p.f.version = kVersion;
  p.f.size = sizeof(Settings);
  p.f.settings = s;
  for (uint32_t i = 0; i < CH32RV_FLASH_WRITE_SIZE / 4; i++) {
    if (p.words[i] != saved.words[i]) return ch32rv_flash_rewrite_page(CH32RV_FLASH_OFFSET(store), p.words);
  }
  return true;  // unchanged: spare the flash
}

Settings settings;

void setup() {
  Serial.begin(115200);
  delay(300);
  const bool loaded = loadSettings(settings);
  if (!loaded) settings = kDefaults;
  Serial.print(loaded ? "loaded" : "defaults");
  Serial.print(" interval ");
  Serial.println(settings.interval_ms);
}

void loop() {
  if (Serial.available()) {
    const long v = Serial.parseInt();
    if (v > 0 && v < 65536) {
      settings.interval_ms = v;
      Serial.print(saveSettings(settings) ? "saved interval " : "save failed, interval ");
      Serial.println(settings.interval_ms);
    }
  }
}
