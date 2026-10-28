// Keep a few settings in the internal flash across resets: one page, overwritten in place.
//
// Advanced use. An SD card or an I2C EEPROM is the recommended place for data that must last. Here:
// - each save erases the page; the flash wears out (10K erases on some series), so save on a user action, not on
//   a timer;
// - re-uploading the sketch may erase the page (depends on the route and the tool version);
// - a power cut while saving loses the settings (the sketch then starts from the defaults);
// - on V003, V103, X035 and L103 the CPU stops for a few ms while saving (UART bytes are lost, millis() falls
//   behind).
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

union Page {
  uint32_t words[CH32RV_FLASH_PAGE_SIZE / 4];
  struct {
    uint16_t version, size;
    Settings settings;
  } f;
};
static_assert(sizeof(Page) == CH32RV_FLASH_PAGE_SIZE, "Settings does not fit in one page");

CH32RV_FLASH_STORE(store, 1);
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
  for (uint32_t i = 0; i < CH32RV_FLASH_PAGE_SIZE / 4; i++) {
    if (p.words[i] != saved.words[i]) return ch32rv_flash_write_page(store, p.words);
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
