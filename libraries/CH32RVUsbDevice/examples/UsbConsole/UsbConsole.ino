// en: Drive HID and vendor traffic by hand from the hardware UART, and log everything the host sends down (HID output
// en: and protocol, vendor control requests, bulk OUT): for when a host application does not react as expected.
// ja: ハードウェアの UART から HID とベンダーの転送を 1 つずつ手で送り、ホストから来るもの（HID の出力と protocol、
// ja: ベンダーの制御の要求、bulk OUT）を全部表示する。ホストのアプリが思ったように反応しないときに使う。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。
/* requires: USB_DEVICE */

#include <CH32RVUsbDevice.h>

CH32RVUsbDevice device;
CH32RVUsbDeviceHidKeyboard keyboard(device);
CH32RVUsbDeviceHidMouse mouse(device);
CH32RVUsbDeviceVendor vendor(device);

static bool beginOk, lastReady;
static String line;
static uint8_t controlOutBuffer[64];   // en: vendor control OUT data, valid until the ACK / ja: ACK までの制御 OUT のデータ

static void printHex(const uint8_t *data, size_t length)
{
  for (size_t i = 0; i < length; i++) {
    Serial.printf(i ? " %02x" : "%02x", data[i]);
  }
}

// en: The first space-delimited token; `text hello world` keeps the rest.
// ja: 空白で区切った最初の語。`text hello world` は残りをそのまま持つ。
static String nextToken(String &rest)
{
  rest.trim();
  const int space = rest.indexOf(' ');
  if (space < 0) {
    const String token = rest;
    rest = "";
    return token;
  }
  const String token = rest.substring(0, space);
  rest = rest.substring(space + 1);
  rest.trim();
  return token;
}

// en: `0x04` and `4` alike, as captures and datasheets mix both.
// ja: `0x04` も `4` も受ける（取り込みとデータシートで両方が混じる）。
static bool parseNumber(const String &text, long &value)
{
  if (text.length() == 0) {
    return false;
  }
  char *end = nullptr;
  const bool hex = text.startsWith("0x") || text.startsWith("0X");
  value = strtol(text.c_str() + (hex ? 2 : 0), &end, hex ? 16 : 10);
  return end && *end == '\0';
}

static size_t parseHexBytes(String rest, uint8_t *buffer, size_t capacity)
{
  size_t count = 0;
  while (count < capacity) {
    const String token = nextToken(rest);
    if (token.length() == 0) {
      break;
    }
    long value = 0;
    const String withPrefix = (token.startsWith("0x") || token.startsWith("0X")) ? token : "0x" + token;
    if (!parseNumber(withPrefix, value) || value < 0 || value > 0xff) {
      Serial.printf("ERROR not a byte: %s\n", token.c_str());
      return 0;
    }
    buffer[count++] = uint8_t(value);
  }
  return count;
}

static void printHelp()
{
  Serial.println("commands:");
  Serial.println("  help | state");
  Serial.println("  text <string>                    keystrokes to the host's focused window");
  Serial.println("  key <usage> [modifiers]          tap a key (HID usage, 0x04 = a)");
  Serial.println("  hold <usage> [modifiers] | release");
  Serial.println("  mouse <dx> <dy> [buttons] [wheel] | click <1|2|4>");
  Serial.println("  report <id> <hex...>             a raw report on HID instance 0");
  Serial.println("  vendor <hex...> | vendortext <string>");
}

static void printState()
{
  Serial.printf("STATE mounted=%u suspended=%u leds=0x%02x hid_protocol=%s vendor_mounted=%u vendor_tx_free=%u\n",
                device.ready(), device.suspended(), keyboard.ledState().leds, keyboard.protocol() ? "report" : "boot",
                vendor.mounted(), unsigned(vendor.writeAvailable()));
}

static void handleCommand(String input)
{
  String rest = input;
  const String verb = nextToken(rest);
  if (verb.length() == 0) {
    return;
  }
  if (verb == "help") {
    return printHelp();
  }
  if (verb == "state") {
    return printState();
  }
  // en: Everything below sends something; nothing is queued for a host that has not configured the device.
  // ja: ここから下は送るもの。設定していないホストへは何も貯めない。
  if (!device.ready()) {
    Serial.println("ERROR not mounted - the host has not enumerated this device");
    return;
  }
  if (verb == "text") {
    Serial.printf("SEND text \"%s\" ok=%u\n", rest.c_str(), keyboard.write(rest.c_str()));
    return;
  }
  if (verb == "key" || verb == "hold") {
    long usage = 0, modifiers = 0;
    if (!parseNumber(nextToken(rest), usage)) {
      Serial.println("ERROR usage: key <usage> [modifiers]");
      return;
    }
    const String modifierToken = nextToken(rest);
    if (modifierToken.length() > 0 && !parseNumber(modifierToken, modifiers)) {
      Serial.println("ERROR modifiers must be a number");
      return;
    }
    const bool ok = verb == "key" ? keyboard.tapUsage(uint8_t(usage), uint8_t(modifiers))
                                  : keyboard.pressUsage(uint8_t(usage), uint8_t(modifiers));
    Serial.printf("SEND %s usage=0x%02x modifiers=0x%02x ok=%u\n", verb.c_str(), unsigned(usage), unsigned(modifiers), ok);
    return;
  }
  if (verb == "release") {
    Serial.printf("SEND release ok=%u\n", keyboard.releaseAll());
    return;
  }
  if (verb == "mouse") {
    long dx = 0, dy = 0, buttons = 0, wheel = 0;
    if (!parseNumber(nextToken(rest), dx) || !parseNumber(nextToken(rest), dy)) {
      Serial.println("ERROR usage: mouse <dx> <dy> [buttons] [wheel]");
      return;
    }
    const String buttonToken = nextToken(rest);
    if (buttonToken.length() > 0) {
      parseNumber(buttonToken, buttons);
    }
    const String wheelToken = nextToken(rest);
    if (wheelToken.length() > 0) {
      parseNumber(wheelToken, wheel);
    }
    const bool ok = mouse.move(int8_t(dx), int8_t(dy), int8_t(wheel), uint8_t(buttons));
    Serial.printf("SEND mouse dx=%ld dy=%ld buttons=0x%02x wheel=%ld ok=%u\n", dx, dy, unsigned(buttons), wheel, ok);
    return;
  }
  if (verb == "click") {
    long button = 1;
    const String buttonToken = nextToken(rest);
    if (buttonToken.length() > 0 && !parseNumber(buttonToken, button)) {
      Serial.println("ERROR usage: click <1|2|4>");
      return;
    }
    Serial.printf("SEND click buttons=0x%02x ok=%u\n", unsigned(button), mouse.click(uint8_t(button)));
    return;
  }
  if (verb == "report") {
    long reportId = 0;
    if (!parseNumber(nextToken(rest), reportId)) {
      Serial.println("ERROR usage: report <id> <hex...>");
      return;
    }
    uint8_t payload[64];
    const size_t length = parseHexBytes(rest, payload, sizeof(payload));
    if (length == 0) {
      Serial.println("ERROR no payload bytes");
      return;
    }
    // en: The keyboard and the mouse share HID instance 0 and differ by report ID; the payload leaves the ID out.
    // ja: キーボードとマウスは HID の instance 0 を共有し、報告の ID で分かれる。payload に ID は含めない。
    const bool ok = device.sendHidReport(0, uint8_t(reportId), payload, length);
    Serial.printf("SEND report id=%u length=%u ok=%u error=%s data=", unsigned(reportId), unsigned(length), ok,
                  ok ? "none" : device.lastErrorName());
    printHex(payload, length);
    Serial.println();
    return;
  }
  if (verb == "vendor" || verb == "vendortext") {
    if (!vendor.mounted()) {
      Serial.println("ERROR the vendor interface's endpoints are not open");
      return;
    }
    uint8_t payload[64];
    size_t length = 0;
    if (verb == "vendortext") {
      length = min(size_t(rest.length()), sizeof(payload));
      memcpy(payload, rest.c_str(), length);
    } else {
      length = parseHexBytes(rest, payload, sizeof(payload));
    }
    if (length == 0) {
      Serial.println("ERROR no payload bytes");
      return;
    }
    // en: The bytes accepted: fewer when the host is not reading bulk IN.
    // ja: 受け付けたバイト数。ホストが bulk IN を読んでいないと少なくなる。
    const size_t written = vendor.write(payload, length);
    vendor.flush();
    Serial.printf("SEND vendor length=%u written=%u data=", unsigned(length), unsigned(written));
    printHex(payload, length);
    Serial.println();
    return;
  }
  Serial.printf("ERROR unknown command: %s (try `help`)\n", verb.c_str());
}

static bool handleVendorControl(const CH32RVUsbDeviceVendorControlRequest &r)
{
  Serial.printf("HOST_VENDOR_CONTROL stage=%u type=0x%02x request=0x%02x value=0x%04x index=0x%04x length=%u actual=%u\n",
                r.stage, r.bmRequestType, r.bRequest, r.wValue, r.wIndex, r.wLength, r.actualBytes);
  // en: Every request is answered, so the host sees success instead of a stall; change this to replay the protocol
  // en: you are studying.
  // ja: どの要求にも答えるので、ホストには stall でなく成功に見える。調べるプロトコルを再現するならここを変える。
  if (r.stage != CH32RV_USB_DEVICE_CONTROL_STAGE_SETUP) {
    if (r.stage == CH32RV_USB_DEVICE_CONTROL_STAGE_ACK && !(r.bmRequestType & 0x80) && r.actualBytes) {
      Serial.print("HOST_VENDOR_CONTROL_DATA ");
      printHex(controlOutBuffer, r.actualBytes);
      Serial.println();
    }
    return true;
  }
  static const char banner[] = "CH32RV UsbConsole";
  if (r.bmRequestType & 0x80) {
    return vendor.sendControlResponse(r, banner, min(size_t(r.wLength), sizeof(banner) - 1));
  }
  if (r.wLength > sizeof(controlOutBuffer)) {
    return false;                    // en: too long: stall / ja: 長すぎる: stall
  }
  return vendor.sendControlResponse(r, controlOutBuffer, r.wLength);
}

void setup()
{
  // en: The command console is the hardware UART; the USB connector goes to the host under test.
  // ja: コマンドの口はハードウェアの UART。USB のコネクタは試すホストへ。
  Serial.begin(115200, SERIAL_8N1, RX, TX);
  delay(1500);
  Serial.println();
  Serial.println("=== CH32RVUsbDevice console ===");

  keyboard.onOutputReport([](const CH32RVUsbDeviceHidKeyboardOutputReport &report) {
    Serial.printf("HOST_HID_OUTPUT leds=0x%02x num=%u caps=%u scroll=%u\n", report.leds, report.numLock,
                  report.capsLock, report.scrollLock);
  });
  // en: A BIOS / UEFI selects the boot protocol; an OS driver switches back to report.
  // ja: BIOS / UEFI は boot protocol を選び、OS のドライバーは report に戻す。
  keyboard.onProtocol([](const CH32RVUsbDeviceHidProtocolEvent &event) {
    Serial.printf("HOST_HID_PROTOCOL instance=%u protocol=%s\n", event.instance, event.protocol ? "report" : "boot");
  });
  // en: Bulk OUT straight to this function; the data is valid during the call only.
  // ja: bulk OUT はこの関数へそのまま渡る。データは呼ばれている間だけ有効。
  vendor.onRxData([](const uint8_t *data, size_t length) {
    Serial.printf("HOST_VENDOR_OUT %u ", unsigned(length));
    printHex(data, length);
    Serial.println();
  });
  vendor.onControlRequest(handleVendorControl);

  CH32RVUsbDeviceConfig config;
  config.vid = 0x1209;
  config.pid = 0x0001;
  config.manufacturer = "ch32-riscv-ug";
  config.product = "CH32RV UsbConsole";
  config.serialNumber = "ch32rv-console";
  beginOk = device.begin(config);
  if (!beginOk) {
    Serial.printf("BEGIN failed error=%s\n", device.lastErrorName());
    return;
  }
  Serial.println("BEGIN ok - connect the USB connector to the host under test");
  printHelp();
}

void loop()
{
  if (beginOk && device.ready() != lastReady) {
    lastReady = device.ready();
    Serial.println(lastReady ? "MOUNTED" : "UNMOUNTED");
  }
  while (Serial.available()) {
    const char c = char(Serial.read());
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      handleCommand(line);
      line = "";
      continue;
    }
    if (line.length() < 200) {
      line += c;
    }
  }
  deliverEvents();
}
