// en: Set the voltage of a USB PD charger's PPS range from the UART: type a voltage in millivolts and Enter.
// en: Handles a charger without PPS, a voltage outside the range, a refusal and unplugging.
// ja: USB PD 充電器の PPS の範囲の電圧を UART から決める。mV で数字を打って Enter。
// ja: PPS の無い充電器、範囲の外の電圧、断られたとき、抜いたときを扱う。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

#include <CH32RVUsbPd.h>

// en: The current asked for with each voltage: keep it within what the board and its load take.
// ja: 電圧と一緒に求める電流。基板と負荷が受けられる範囲にする。
static const uint16_t REQUEST_MA = 1000;

static int8_t ppsIndex = -1;
static bool wasReady;
static char line[16];
static uint8_t used;

static int8_t findPps()
{
  for (uint8_t i = 0; i < USBPD.profileCount(); i++) {
    const CH32RVUsbPdProfile p = USBPD.profile(i);
    if (p.kind == CH32RVUsbPdSupply::Pps) {
      Serial.printf("PPS profile %u: %u..%u mV, up to %u mA\n", i, p.min_mv, p.max_mv, p.max_ma);
      return (int8_t)i;
    }
  }
  return -1;
}

static void ask(uint16_t mv)
{
  if (ppsIndex < 0) {
    Serial.println("this charger has no PPS range");
    return;
  }
  const CH32RVUsbPdProfile p = USBPD.profile((uint8_t)ppsIndex);
  if (mv < p.min_mv || mv > p.max_mv) {
    Serial.printf("%u mV is outside %u..%u mV\n", mv, p.min_mv, p.max_mv);
    return;
  }
  // en: requestProfile() waits for the charger's answer (up to about 1.5 s); a refusal keeps the old contract.
  // ja: requestProfile() は充電器の答えを待つ（最大約 1.5 秒）。断られたら前の契約のまま。
  if (USBPD.requestProfile((uint8_t)ppsIndex, mv, REQUEST_MA)) {
    Serial.printf("contract: %u mV, up to %u mA (PPS)\n", USBPD.voltage(), USBPD.current());
  } else {
    Serial.printf("refused; still %u mV\n", USBPD.voltage());
  }
}

void setup()
{
  Serial.begin(115200, SERIAL_8N1, RX, TX);
  if (!USBPD.begin()) {
    Serial.println("USB PD is not available on this board");
  }
}

void loop()
{
  // en: The driver keeps the PPS contract alive by itself; the sketch only follows attach and detach.
  // ja: PPS の契約の維持はドライバがする。スケッチは抜き差しを追うだけ。
  const bool ready = USBPD.ready();
  if (ready && !wasReady) {
    Serial.printf("charger ready at %u mV\n", USBPD.voltage());
    ppsIndex = findPps();
    if (ppsIndex < 0) {
      Serial.println("this charger has no PPS range (fixed voltages only)");
    } else {
      Serial.println("type a voltage in mV and Enter");
    }
  } else if (!ready && wasReady) {
    Serial.println(USBPD.connected() ? "renegotiating" : "charger unplugged");
    ppsIndex = -1;
  }
  wasReady = ready;

  while (Serial.available()) {
    const char c = (char)Serial.read();
    if (c == '\r' || c == '\n') {
      if (used) {
        line[used] = '\0';
        used = 0;
        ask((uint16_t)strtoul(line, nullptr, 10));
      }
    } else if (used < sizeof line - 1) {
      line[used++] = c;
    }
  }
}
