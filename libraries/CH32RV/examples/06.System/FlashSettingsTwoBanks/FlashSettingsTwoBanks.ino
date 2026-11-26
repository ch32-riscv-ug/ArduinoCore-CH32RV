// Keep a few settings in the internal flash across resets: two pages written in turn, so a power cut while saving
// leaves the previous settings.
//
// Advanced use. An SD card or an I2C EEPROM is the recommended place for data that must last. Here:
// - each save erases a page; the flash wears out (10K erases on some series), so save on a user action, not on
//   a timer (two pages halve the wear of each);
// - re-uploading the sketch may erase the pages (depends on the route and the tool version);
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

// One write unit per bank; on series that erase by 4 KiB or more the rest of each erase unit stays erased.
union Page {
  uint32_t words[CH32RV_FLASH_WRITE_SIZE / 4];
  struct {
    uint16_t crc;  // CRC-16 of the rest of the page
    uint16_t gen;  // one more than the other page's at each save
    uint16_t version, size;
    Settings settings;
  } f;
};
static_assert(sizeof(Page) == CH32RV_FLASH_WRITE_SIZE, "Settings does not fit in one page");

CH32RV_FLASH_STORE(store, 2 * CH32RV_FLASH_ERASE_SIZE);
const Page *bank(int i) { return (const Page *)(store + i * CH32RV_FLASH_ERASE_SIZE); }

uint16_t crc16(const Page &p) {
  const uint8_t *b = (const uint8_t *)&p;
  uint16_t c = 0xFFFF;
  for (uint32_t i = 2; i < CH32RV_FLASH_WRITE_SIZE; i++) {
    c ^= b[i];
    for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xA001 & -(c & 1));
  }
  return c;
}

// The page saved last: -1 when neither is whole (never saved, or both damaged).
int newest() {
  const bool a = crc16(*bank(0)) == bank(0)->f.crc, b = crc16(*bank(1)) == bank(1)->f.crc;
  if (a && b) return (int16_t)(bank(1)->f.gen - bank(0)->f.gen) > 0;
  return a ? 0 : b ? 1 : -1;
}

bool loadSettings(Settings &s) {
  const int k = newest();
  if (k < 0 || bank(k)->f.version != kVersion || bank(k)->f.size != sizeof(Settings)) return false;
  s = bank(k)->f.settings;
  return true;
}

bool saveSettings(const Settings &s) {
  const int k = newest();
  Page p;
  for (uint32_t &w : p.words) w = 0xFFFFFFFF;
  p.f.version = kVersion;
  p.f.size = sizeof(Settings);
  p.f.settings = s;
  p.f.gen = k < 0 ? 0 : bank(k)->f.gen;
  if (k >= 0) {
    uint32_t i = 1;  // word 0 holds crc and gen
    while (i < CH32RV_FLASH_WRITE_SIZE / 4 && p.words[i] == bank(k)->words[i]) i++;
    if (i == CH32RV_FLASH_WRITE_SIZE / 4) return true;  // unchanged: spare the flash
  }
  p.f.gen++;
  p.f.crc = crc16(p);
  // Over the older page; the newer one stays until this one is whole.
  return ch32rv_flash_rewrite_page(CH32RV_FLASH_OFFSET(bank(k == 0)), p.words);
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
