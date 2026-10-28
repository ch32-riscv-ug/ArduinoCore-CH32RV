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

// --- power-cut test driver (the store code above is FlashSettingsTwoBanks' own, copied by pc.sh) ---
// Commands (one line each): "i" first save of 1000; "r" report what the last boot found; "g D" wait D ms, then three
// saves of value+1.. back to back (the host cuts the power meanwhile).
static bool bootLoaded, bootA, bootB;
static uint16_t bootGenA, bootGenB;
static Settings cur;

void setup() {
  Serial.begin(115200);
  bootA = crc16(*bank(0)) == bank(0)->f.crc;
  bootB = crc16(*bank(1)) == bank(1)->f.crc;
  bootGenA = bank(0)->f.gen;
  bootGenB = bank(1)->f.gen;
  bootLoaded = loadSettings(cur);
  if (!bootLoaded) cur = kDefaults;
}

void loop() {
  if (!Serial.available()) return;
  const int c = Serial.read();
  if (c == 'i') {
    cur.interval_ms = 1000;
    Serial.print(saveSettings(cur) ? "init ok\n" : "init fail\n");
  } else if (c == 'r') {
    Serial.print("pc loaded "); Serial.print(bootLoaded);
    Serial.print(" value "); Serial.print(cur.interval_ms);
    Serial.print(" a "); Serial.print(bootA); Serial.print(' '); Serial.print(bootGenA);
    Serial.print(" b "); Serial.print(bootB); Serial.print(' '); Serial.println(bootGenB);
  } else if (c == 'g') {
    const long d = Serial.parseInt();
    delay(d);
    for (int k = 0; k < 3; k++) {
      cur.interval_ms++;
      saveSettings(cur);
    }
    Serial.print("done "); Serial.println(cur.interval_ms);
  }
}
