// en: The first sketch for a new board or cable: tells "the device started" from "the host configured it", follows
// en: suspend and resume, checks the host -> device direction (the keyboard's lock LEDs), and prints a checklist when
// en: no host has enumerated it after 10 s. A HID keyboard is registered but never types.
// ja: 新しい板やケーブルで最初に動かすスケッチ。「デバイスが始まった」と「ホストが設定した」を分けて示し、休止と再開を
// ja: 追い、ホストからデバイスへの向き（キーボードのロックの LED）を確かめ、10 秒たっても列挙されなければ確認項目を出す。
// ja: HID のキーボードを登録するが、キーは打たない。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。
/* requires: USB_DEVICE */

#include <CH32RVUsbDevice.h>

CH32RVUsbDevice device;
CH32RVUsbDeviceHidKeyboard keyboard(device);

static bool beginOk, lastReady, lastSuspended, checklistPrinted;
static uint32_t beginMs, lastStatusMs;

static void printChecklist()
{
  Serial.println("CHECKLIST no host has enumerated this board yet:");
  Serial.println("  1. Cable      - a data cable? A charge-only one has no D+ / D-.");
  Serial.println("  2. Connector  - the chip's own USB connector, not a WCH-Link or USB-UART one.");
  Serial.println("                  Check the schematic: the silkscreen is not reliable.");
  Serial.println("  3. Clock      - a CPU Clock menu item that gives USB its 48 MHz: not one marked");
  Serial.println("                  'no USB'. High speed (USBHS) needs the HSE crystal.");
  Serial.println("  4. Power      - a bus-powered board that lost VBUS when the cable moved is not running.");
  Serial.println("  5. Host side  - Linux: 'dmesg -w' while plugging in. Windows: Device Manager.");
  Serial.println("                  If the host logs nothing at all, it is electrical.");
  Serial.println("  6. Descriptor - if the host logs an error instead, run DescriptorDump and compare.");
}

static const char *speedName(CH32RVUsbSpeed speed)
{
  switch (speed) {
  case CH32RVUsbSpeed::High: return "high";
  case CH32RVUsbSpeed::Super: return "super";
  default: return "full";
  }
}

void setup()
{
  // en: The log goes to the hardware UART, apart from the USB connector being checked.
  // ja: 表示はハードウェアの UART。確かめる USB のコネクタとは別。
  Serial.begin(115200, SERIAL_8N1, RX, TX);
  delay(1500);
  Serial.println();
  Serial.println("=== CH32RVUsbDevice bring-up check ===");
  Serial.printf("CORE ch32rv %d.%d.%d  F_CPU %lu Hz\n", CH32RV_VERSION_MAJOR, CH32RV_VERSION_MINOR,
                CH32RV_VERSION_PATCH, (unsigned long)F_CPU);

  // en: An LED output report proves the host -> device direction works.
  // ja: LED の出力の報告が届けば、ホストからデバイスへの向きが動いている。
  keyboard.onOutputReport([](const CH32RVUsbDeviceHidKeyboardOutputReport &report) {
    Serial.printf("HOST_OUTPUT_REPORT leds=0x%02x num=%u caps=%u scroll=%u\n", report.leds, report.numLock,
                  report.capsLock, report.scrollLock);
  });

  CH32RVUsbDeviceConfig config;
  config.vid = 0x1209;
  config.pid = 0x0001;
  config.manufacturer = "ch32-riscv-ug";
  config.product = "CH32RV BringUpCheck";
  config.serialNumber = "ch32rv-bringup";
  // en: Where the part has more than one controller, name the one the cable goes to:
  // ja: コントローラーが複数ある石では、線をつないだ方を指定する:
  // config.controller = CH32RVUsbController::HighSpeed;

  beginOk = device.begin(config);
  beginMs = millis();
  if (!beginOk) {
    Serial.printf("BEGIN failed error=%s\n", device.lastErrorName());
    Serial.println("  A failure here is the board's own - no host is involved yet.");
    Serial.println("  Unsupported / PortUnavailable : no such USB device controller on this part.");
    Serial.println("  OutOfEndpoints / DescriptorTooLarge : the functions do not fit; remove one.");
    Serial.println("  InvalidArgument : a config value is malformed.");
    Serial.println("  Busy : the controller is a USB host right now.");
    return;
  }
  const uint8_t *configuration = device.configurationDescriptor();
  Serial.println("BEGIN ok");
  Serial.printf("DESCRIPTOR config_bytes=%u\n", configuration ? unsigned(configuration[2] | (configuration[3] << 8)) : 0u);
  Serial.printf("VID_PID %04x:%04x\n", config.vid, config.pid);
  Serial.println("Now connect the USB connector to a host and watch for MOUNTED.");
}

void loop()
{
  if (!beginOk) {
    delay(1000);
    return;
  }
  const uint32_t now = millis();
  const bool ready = device.ready();
  if (ready != lastReady) {
    lastReady = ready;
    if (ready) {
      Serial.printf("MOUNTED t=%lums speed=%s\n", (unsigned long)(now - beginMs), speedName(device.speed()));
      Serial.println("  The host enumerated the device and chose its configuration.");
      Serial.println("  Press CapsLock on the host's keyboard to check host -> device.");
      checklistPrinted = true;      // en: enumerated once: no checklist / ja: 一度列挙された: 確認項目は出さない
    } else {
      Serial.printf("UNMOUNTED t=%lums\n", (unsigned long)(now - beginMs));
      Serial.println("  The cable came out, the host reset the bus, or it deselected the configuration.");
    }
  }
  // en: A suspend keeps the configuration: ready() stays true meanwhile.
  // ja: 休止は設定を残す: その間も ready() は true。
  const bool suspended = device.suspended();
  if (suspended != lastSuspended) {
    lastSuspended = suspended;
    Serial.printf("%s t=%lums\n", suspended ? "SUSPENDED" : "RESUMED", (unsigned long)(now - beginMs));
  }
  if (!ready && !checklistPrinted && now - beginMs > 10000) {
    checklistPrinted = true;
    printChecklist();
  }
  if (now - lastStatusMs >= 5000) {
    lastStatusMs = now;
    Serial.printf("STATUS mounted=%u suspended=%u t=%lums leds=0x%02x\n", ready, suspended,
                  (unsigned long)(now - beginMs), keyboard.ledState().leds);
  }
  delay(10);
}
