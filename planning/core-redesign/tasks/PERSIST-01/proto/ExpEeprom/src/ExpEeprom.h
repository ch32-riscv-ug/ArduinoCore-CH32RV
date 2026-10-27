// PERSIST-01 experiment: Arduino EEPROM API (begin/commit) over two banks at the end of flash.
#pragma once
#include <Arduino.h>
#include <string.h>

#if defined(CH32RV_SERIES_CH32V003)
#define CH32RV_EEPROM_UNIT 64u
#define CH32RV_EEPROM_DEFAULT 48u
#else
#define CH32RV_EEPROM_UNIT 256u
#define CH32RV_EEPROM_DEFAULT 240u
#endif
#define CH32RV_EEPROM_HEADER 16u
#define CH32RV_EEPROM_BANK(n) ((((n) + CH32RV_EEPROM_HEADER) + CH32RV_EEPROM_UNIT - 1u) / CH32RV_EEPROM_UNIT * CH32RV_EEPROM_UNIT)

// Put once in the sketch to choose the size at build time: reserves two banks at the end of flash and the RAM
// copy. Without it the library's default (CH32RV_EEPROM_DEFAULT) is used.
#define CH32RV_EEPROM_SIZE(n)                                                                                     \
  extern "C" {                                                                                                    \
  __attribute__((section(".ch32rv_store"), aligned(CH32RV_EEPROM_UNIT))) uint8_t                                 \
      ch32rv_eeprom_area[2u * CH32RV_EEPROM_BANK(n)];                                                             \
  uint8_t ch32rv_eeprom_ram[n];                                                                                   \
  extern const uint16_t ch32rv_eeprom_capacity = (n);                                                             \
  }

class ExpEepromClass {
public:
  enum Status : uint8_t { NotBegun, Loaded, Empty, Corrupt, TooLarge };
  bool begin(size_t size);
  uint8_t read(int a) const { return _ram()[a]; }
  void write(int a, uint8_t v) { _ram()[a] = v; }
  void update(int a, uint8_t v) { _ram()[a] = v; }
  uint8_t &operator[](int a) { return _ram()[a]; }
  template <class T> T &get(int a, T &t) const { memcpy(&t, _ram() + a, sizeof t); return t; }
  template <class T> const T &put(int a, const T &t) { memcpy(_ram() + a, &t, sizeof t); return t; }
  bool commit();
  bool end() { const bool ok = commit(); _size = 0; return ok; }
  uint16_t length() const { return _size; }
  Status status() const { return _status; }
  uint32_t lastCommitUs() const { return _us; }

private:
  static uint8_t *_ram();
  uint16_t _size;
  Status _status;
  uint8_t _active;  // bank holding the current copy, 0xFF none
  uint32_t _gen;
  uint32_t _us;
};
extern ExpEepromClass EEPROM;
