// en: Advertise PD source capabilities using placeholder supply callbacks.
// ja: 電源制御callbackの雛形を使い、PD Sourceの能力を広告する。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

#include <CH32RVUsbPd.h>

// en: Placeholder only: replace with real switching and report the physical result.
// ja: 雛形のみ。実際の電源切替へ置き換え、実回路の成否を返す。
bool vbus(bool on) {
  Serial.println(on ? "VBUS on (5 V)" : "VBUS off");
  return true;
}

// en: Return true only after the requested voltage is reached, not merely scheduled.
// ja: 要求電圧に到達してからtrueを返す。予約だけで成功としない。
bool setVoltage(uint16_t mv, uint16_t ma, bool pps) {
  Serial.print(pps ? "PPS " : "fixed ");
  Serial.print(mv);
  Serial.print(" mV, up to ");
  Serial.print(ma);
  Serial.println(" mA");
  return true;
}

void setup() {
  Serial.begin(115200, SERIAL_8N1, RX, TX);

  USBPDSource.setRpCurrent(PD_RP_3A0);
  // en: The first advertised PDO must be fixed 5 V.
  // ja: 広告する先頭のPDOは固定5 Vにする。
  USBPDSource.addFixed(5000, 3000);
  USBPDSource.addFixed(9000, 2000);
  USBPDSource.addPps(3300, 11000, 2000);
  // en: Register the physical supply handlers before starting; the driver relies on their results.
  // ja: 起動前に実電源を制御する関数を登録する。ドライバはその成否を信頼して動く。
  USBPDSource.onVbus(vbus);
  USBPDSource.onSetVoltage(setVoltage);
  if (!USBPDSource.begin()) {
    Serial.print("USB PD source not started: ");
    Serial.println(USBPDSource.lastErrorName());
    return;
  }
  Serial.println("waiting for a sink");
}

void loop() {
  static int8_t shown = -2;
  const int8_t now = USBPDSource.ready() ? USBPDSource.contractProfile() : -1;
  if (now != shown) {
    shown = now;
    if (now >= 0) {
      Serial.print("contract: profile ");
      Serial.println(now + 1);
    } else if (USBPDSource.attached()) {
      Serial.println("sink attached, no contract");
    }
  }
}
