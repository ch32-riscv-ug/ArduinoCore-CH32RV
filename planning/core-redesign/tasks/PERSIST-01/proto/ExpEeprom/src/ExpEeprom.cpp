#include "ExpEeprom.h"
#include "ch32rv_registers.h"

extern "C" {
__attribute__((weak, section(".ch32rv_store"), aligned(CH32RV_EEPROM_UNIT))) uint8_t
    ch32rv_eeprom_area[2u * CH32RV_EEPROM_BANK(CH32RV_EEPROM_DEFAULT)];
__attribute__((weak)) uint8_t ch32rv_eeprom_ram[CH32RV_EEPROM_DEFAULT];
__attribute__((weak)) extern const uint16_t ch32rv_eeprom_capacity = CH32RV_EEPROM_DEFAULT;
void ch32rv_millis_advance(uint32_t ms);
}

ExpEepromClass EEPROM;

namespace {
#define FL(o) CH32RV_REG32(0x40022000u + (o))
constexpr uint32_t KEYR = 0x04, STATR = 0x0C, CTLR = 0x10, ADDR = 0x14, MODEKEYR = 0x24;
constexpr uint32_t STRT = 1u << 6, LOCK = 1u << 7, FLOCK = 1u << 15, PAGE_PG = 1u << 16, PAGE_ER = 1u << 17,
                   BUF_LOAD = 1u << 18, BUF_RST = 1u << 19, BSY = 1u, WRPRTERR = 1u << 4, EOP = 1u << 5;
constexpr uint32_t MAGIC = 0xC32EE901u;
constexpr uint32_t U = CH32RV_EEPROM_UNIT;

struct Header {
  uint32_t magic, gen;
  uint16_t len, ver;
  uint32_t crc;
};

// The area itself is referenced (not the linker's start / end), or --gc-sections drops it.
uint32_t bankSize() { return CH32RV_EEPROM_BANK((uint32_t)ch32rv_eeprom_capacity); }
const uint8_t *bank(uint8_t b) { return ch32rv_eeprom_area + b * bankSize(); }

uint32_t crc32(uint32_t c, const uint8_t *p, uint32_t n) {
  while (n--) {
    c ^= *p++;
    for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
  }
  return c;
}
uint32_t crcOf(const uint8_t *b, const uint8_t *data, uint32_t len) {
  return ~crc32(crc32(~0u, b, 12), data, len);
}

// A bank counts only if its CRC matches twice (a word cut mid-erase can read differently each time).
bool valid(const uint8_t *b, Header &h) {
  memcpy(&h, b, sizeof h);
  if (h.magic != MAGIC || h.len > bankSize() - CH32RV_EEPROM_HEADER) return false;
  const uint8_t *d = b + CH32RV_EEPROM_HEADER;
  return crcOf(b, d, h.len) == h.crc && crcOf(b, d, h.len) == h.crc;
}

// Waits for BSY with a bound; a write-protected page keeps it set (PERSIST-01 M10).
bool wait() {
  for (uint32_t n = 0; n < 0x400000u; n++) {
    const uint32_t s = FL(STATR);
    if (s & WRPRTERR) return false;
    if (!(s & BSY)) return true;
  }
  return false;
}

bool erasePage(uint32_t a) {
  FL(CTLR) |= PAGE_ER; FL(ADDR) = a; FL(CTLR) |= STRT;
  const bool ok = wait();
  FL(CTLR) &= ~PAGE_ER;
  return ok;
}

bool programPage(uint32_t a, const uint32_t *w) {
  FL(CTLR) |= PAGE_PG; FL(CTLR) |= BUF_RST;
  bool ok = wait();
  FL(CTLR) &= ~PAGE_PG;
  for (uint32_t i = 0; ok && i < U / 4u; i++) {
    ((volatile uint32_t *)a)[i] = w[i];
    FL(CTLR) |= PAGE_PG; FL(CTLR) |= BUF_LOAD;
    ok = wait();
    FL(CTLR) &= ~PAGE_PG;
  }
  if (ok) { FL(CTLR) |= PAGE_PG; FL(ADDR) = a; FL(CTLR) |= STRT; ok = wait(); }
  return ok;
}
}  // namespace

uint8_t *ExpEepromClass::_ram() { return ch32rv_eeprom_ram; }

bool ExpEepromClass::begin(size_t size) {
  _active = 0xFF; _gen = 0;
  if (size > ch32rv_eeprom_capacity || size + CH32RV_EEPROM_HEADER > bankSize()) { _status = TooLarge; return false; }
  _size = (uint16_t)size;
  Header h[2];
  const bool v0 = valid(bank(0), h[0]), v1 = valid(bank(1), h[1]);
  memset(_ram(), 0xFF, _size);
  if (v0 && v1 && h[0].gen == h[1].gen) { _status = Corrupt; return false; }
  if (v0 || v1) {
    _active = (v0 && (!v1 || (int32_t)(h[0].gen - h[1].gen) > 0)) ? 0 : 1;
    _gen = h[_active].gen;
    memcpy(_ram(), bank(_active) + CH32RV_EEPROM_HEADER, h[_active].len < _size ? h[_active].len : _size);
    _status = Loaded;
    return true;
  }
  _status = Empty;
  for (uint32_t i = 0; i < 2u * bankSize(); i++)
    if (bank(0)[i] != 0xFF) { _status = Corrupt; break; }
  return _status == Empty;
}

bool ExpEepromClass::commit() {
  if (!_size) return false;
  if (_active != 0xFF) {
    const Header *h = (const Header *)bank(_active);
    if (h->len == _size && !memcmp(bank(_active) + CH32RV_EEPROM_HEADER, _ram(), _size)) return true;
  }
  const uint8_t to = _active == 0 ? 1 : 0;
  // The flash controller takes the 0x08000000 address; the link address of the area is the alias at 0.
  const uint32_t base = 0x08000000u | (uint32_t)bank(to), pages = bankSize() / U;
  Header h = {MAGIC, _gen + 1u, _size, 1, 0};
  h.crc = crcOf((const uint8_t *)&h, _ram(), _size);
  CH32RV_REG32(0x40003000u) = 0xAAAAu;  // feed the IWDG if it runs: it counts through the erase
  const uint32_t irq = ch32rv_irq_save();
  const uint32_t t0 = CH32RV_SYSTICK_CNT;
  FL(KEYR) = 0x45670123u; FL(KEYR) = 0xCDEF89ABu; FL(MODEKEYR) = 0x45670123u; FL(MODEKEYR) = 0xCDEF89ABu;
  FL(STATR) = WRPRTERR | EOP;
  bool ok = true;
  for (uint32_t p = 0; ok && p < pages; p++) ok = erasePage(base + p * U);
  // Last page first: the header's page goes last, so a cut leaves no valid header over a partial body.
  for (uint32_t p = pages; ok && p-- > 0;) {
    uint32_t w[U / 4u];
    uint8_t *b = (uint8_t *)w;
    for (uint32_t i = 0; i < U; i++) {
      const uint32_t o = p * U + i;
      b[i] = o < CH32RV_EEPROM_HEADER ? ((const uint8_t *)&h)[o]
             : o - CH32RV_EEPROM_HEADER < _size ? _ram()[o - CH32RV_EEPROM_HEADER] : 0xFF;
    }
    ok = programPage(base + p * U, w);
  }
  FL(CTLR) = 0; FL(STATR) = WRPRTERR | EOP;  // leave nothing set (PERSIST-01 M10)
  FL(CTLR) = LOCK | FLOCK;
  const uint32_t ticks = CH32RV_SYSTICK_CNT - t0;
  ch32rv_irq_restore(irq);
  _us = ticks / (F_CPU / 1000000u);
  if (_us > 1000u) ch32rv_millis_advance(_us / 1000u - 1u);
  Header c;
  if (!ok || !valid((const uint8_t *)base, c) || c.gen != h.gen) return false;
  _active = to; _gen = h.gen;
  return true;
}
