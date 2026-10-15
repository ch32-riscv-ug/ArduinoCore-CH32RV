/* The ESP32-S3 as USB host for tests/peer/usb/device: the DUT (a CH32 running
 * CH32RVUsbDevice) is the device. EspUsbHost is an independent stack (ESP-IDF
 * USB host), so a pass here is interoperability, not a stack talking to
 * itself.
 *
 * Commands arrive one per line on Serial (UART0, the board's USB-serial
 * bridge); every answer and event is a line starting "S3 ". Blocking EspUsbHost
 * calls must not run on its USB task, so all commands run from loop() and the
 * callbacks only record or print.
 *
 *   ping                 S3 PONG
 *   dev                  S3 DEV ... / S3 ITF ... per interface / S3 DEVEND <n>
 *   cdc <hex>            S3 CDC <hex echoed back within 2 s>
 *   cdcbig <n>           S3 CDCBIG <n> <ok> <bytes back>  (patterned echo)
 *   baud <n>             S3 BAUD <ok>
 *   hidv <hex>           S3 HIDV_SENT <ok>    (vendor output report)
 *   leds <bits>          S3 LEDS <ok>
 *   midi <ch> <n> <v>    S3 MIDI_SENT <ok>
 *   net                  S3 NETOPEN <ok>
 *   netsend              S3 NETSENT <ok>      (broadcast, EtherType 0x88B5)
 *   netstat              S3 NETRX <frames> <frames of 0x88B5>
 *   vend <hex>           S3 VEND <hex echoed back within 2 s>
 *   vreq                 S3 VREQ <ok> <answer as text>
 *   msc                  S3 MSC <ok> <vendor> <product> <blocks> <block size>
 *   mscraw <lba>         S3 MSCRAW <ok>       (write a pattern, read it back, restore)
 *   mscwrite <text>      S3 MSCWRITE <ok>     (/HOST.TXT on the FAT volume)
 *   mscread              S3 MSCREAD <ok> <first line of /README.TXT>
 *   ccid                 S3 CCID <ok> <ATR hex>
 *   apdu <hex>           S3 APDU <ok> <response hex> <SW hex>
 *   audioin <ms>         S3 AUDIOIN <bytes> <ramp breaks>
 *   audioout <ms>        S3 AUDIOOUT <ok> <bytes> <byte sum>
 *   video <ms>           S3 VIDEO <frames> <pattern errors> <width> <height>
 *   dfu                  S3 DFU <ok>          (DFU_DETACH to the runtime interface)
 *   reset                S3 RESET             (the counters below start over)
 *
 * Events: S3 CONNECTED <addr> <vid> <pid> <product> / S3 DISCONNECTED <addr>
 * <vid> <pid> / S3 KEY <p|r> <keycode> <ascii> / S3 HIDVIN <hex> /
 * S3 MIDI <cin> <status> <d1> <d2>. */
#include <Arduino.h>
#include <EspUsbHost.h>

/* Before any function: the IDE hoists prototypes to here. */
typedef size_t (*ReadFn)(uint8_t *out, size_t length);

EspUsbHost usb;
EspUsbHostCdcSerial cdc(usb);
EspUsbHostMscFS msc;

static char line[200];
static size_t used;

static volatile uint32_t netFrames, netOurs;
static volatile uint32_t audioBytes, audioBreaks;
static volatile int32_t audioLast = -1;
static volatile uint32_t videoFrames, videoErrors;
static volatile uint16_t videoW, videoH;

static int hexval(char c)
{
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static size_t parseHex(const char *s, uint8_t *out, size_t cap)
{
  size_t n = 0;
  while (s[0] && s[1] && n < cap)
  {
    const int h = hexval(s[0]), l = hexval(s[1]);
    if (h < 0 || l < 0) break;
    out[n++] = (uint8_t)(h << 4 | l);
    s += 2;
  }
  return n;
}

static void printHex(const uint8_t *d, size_t n)
{
  for (size_t i = 0; i < n; i++) Serial.printf("%02x", d[i]);
}

static uint8_t firstAddress()
{
  EspUsbHostDeviceInfo list[4];
  const size_t n = usb.getDevices(list, 4);
  for (size_t i = 0; i < n; i++)
    if (!list[i].isHub) return list[i].address;
  return 0;
}

static void cmdDev()
{
  EspUsbHostDeviceInfo list[4];
  const size_t n = usb.getDevices(list, 4);
  for (size_t i = 0; i < n; i++)
  {
    const EspUsbHostDeviceInfo &d = list[i];
    Serial.printf("S3 DEV %u %04x %04x %u %04x %02x %s|%s|%s\n", d.address, d.vid, d.pid,
                  d.speed == USB_SPEED_LOW ? 0 : 1, d.usbVersion, d.deviceClass,
                  d.manufacturer, d.product, d.serial);
    EspUsbHostInterfaceInfo itfs[16];
    const size_t ni = usb.getInterfaces(d.address, itfs, 16);
    for (size_t k = 0; k < ni; k++)
      Serial.printf("S3 ITF %u %u %u %02x %02x %02x %u\n", d.address, itfs[k].number, itfs[k].alternate,
                    itfs[k].interfaceClass, itfs[k].interfaceSubClass, itfs[k].interfaceProtocol,
                    itfs[k].claimed ? 1 : 0);
  }
  Serial.printf("S3 DEVEND %u\n", (unsigned)n);
}

/* Read what comes back on `read` until `want` bytes or the time is up. */
static uint8_t vendorAddress;
static bool vendorOpened;

static size_t readCdc(uint8_t *o, size_t k)
{
  size_t m = 0;
  while (m < k && cdc.available()) o[m++] = (uint8_t)cdc.read();
  return m;
}

static size_t readVendor(uint8_t *o, size_t k)
{
  return usb.vendorRead(o, k, vendorAddress);
}

static size_t collect(ReadFn read, uint8_t *out, size_t want, uint32_t ms)
{
  size_t got = 0;
  const uint32_t t0 = millis();
  while (got < want && millis() - t0 < ms)
  {
    const size_t n = read(out + got, want - got);
    got += n;
    if (!n) delay(1);
  }
  return got;
}

static void cmdCdc(const char *arg)
{
  uint8_t tx[96], rx[96];
  const size_t n = parseHex(arg, tx, sizeof tx);
  while (cdc.available()) cdc.read();
  cdc.write(tx, n);
  cdc.flush();
  const size_t got = collect(readCdc, rx, n, 2000);
  Serial.print("S3 CDC ");
  printHex(rx, got);
  Serial.println();
}

static void cmdCdcBig(size_t n)
{
  static uint8_t tx[4096], rx[4096];
  if (n > sizeof tx) n = sizeof tx;
  for (size_t i = 0; i < n; i++) tx[i] = (uint8_t)(i * 13 + 7);
  while (cdc.available()) cdc.read();
  size_t sent = 0, got = 0;
  const uint32_t t0 = millis();
  while ((sent < n || got < n) && millis() - t0 < 10000)
  {
    if (sent < n)
    {
      const size_t chunk = min((size_t)64, n - sent);
      sent += cdc.write(tx + sent, chunk);
    }
    while (got < n && cdc.available()) rx[got++] = (uint8_t)cdc.read();
    delay(1);
  }
  Serial.printf("S3 CDCBIG %u %u %u\n", (unsigned)n, (got == n && memcmp(tx, rx, n) == 0) ? 1 : 0,
                (unsigned)got);
}

static void cmdVend(const char *arg)
{
  uint8_t tx[128], rx[128];
  const size_t n = parseHex(arg, tx, sizeof tx);
  const uint8_t a = firstAddress();
  vendorAddress = a;
  if (!usb.vendorWrite(tx, n, a))
  {
    Serial.printf("S3 VEND ERR %s\n", usb.lastErrorName());
    return;
  }
  const size_t got = collect(readVendor, rx, n, 2000);
  Serial.print("S3 VEND ");
  printHex(rx, got);
  Serial.println();
}

static void cmdMsc()
{
  const uint8_t a = firstAddress();
  EspUsbHostMscInquiry inq;
  uint32_t blocks = 0, size = 0;
  const bool ok = usb.mscWaitReady(a, 5000) && usb.mscInquiry(inq, a) && usb.mscCapacity(blocks, size, a);
  Serial.printf("S3 MSC %u %s %s %lu %lu\n", ok ? 1 : 0, ok ? inq.vendor : "-", ok ? inq.product : "-",
                (unsigned long)blocks, (unsigned long)size);
}

static void cmdMscRaw(uint32_t lba)
{
  static uint8_t keep[512], pattern[512], back[512];
  const uint8_t a = firstAddress();
  for (size_t i = 0; i < 512; i++) pattern[i] = (uint8_t)(i ^ (lba * 31));
  bool ok = usb.mscReadBlocks(lba, keep, 1, a);
  ok = ok && usb.mscWriteBlocks(lba, pattern, 1, a);
  ok = ok && usb.mscReadBlocks(lba, back, 1, a) && memcmp(back, pattern, 512) == 0;
  ok = usb.mscWriteBlocks(lba, keep, 1, a) && ok;
  Serial.printf("S3 MSCRAW %u\n", ok ? 1 : 0);
}

static void cmdMscWrite(const char *text)
{
  bool ok = msc.begin(usb, "/usb", firstAddress());
  if (ok)
  {
    File f = msc.open("/HOST.TXT", FILE_WRITE);
    ok = f && f.print(text) == strlen(text) && f.print("\r\n") == 2;
    if (f) f.close();
    msc.end();
  }
  Serial.printf("S3 MSCWRITE %u\n", ok ? 1 : 0);
}

static void cmdMscRead()
{
  String first;
  bool ok = msc.begin(usb, "/usb", firstAddress());
  if (ok)
  {
    File f = msc.open("/README.TXT", FILE_READ);
    ok = (bool)f;
    if (f)
    {
      first = f.readStringUntil('\n');
      f.close();
    }
    msc.end();
  }
  first.trim();
  Serial.printf("S3 MSCREAD %u %s\n", ok ? 1 : 0, first.c_str());
}

static void cmdCcid()
{
  const uint8_t a = firstAddress();
  uint8_t atr[40];
  size_t n = 0;
  const bool ok = usb.ccidOpen(a) && usb.ccidPowerOn(atr, sizeof atr, &n, ESP_USB_HOST_CCID_VOLTAGE_AUTO, 0, a);
  Serial.printf("S3 CCID %u ", ok ? 1 : 0);
  printHex(atr, n);
  Serial.println();
}

static void cmdApdu(const char *arg)
{
  uint8_t tx[64], rx[80];
  const size_t n = parseHex(arg, tx, sizeof tx);
  size_t got = 0;
  uint16_t sw = 0;
  const bool ok = usb.ccidApdu(tx, n, rx, sizeof rx, &got, &sw, 0, firstAddress());
  Serial.printf("S3 APDU %u ", ok ? 1 : 0);
  printHex(rx, got);
  Serial.printf(" %04x\n", sw);
}

static void cmdAudioIn(uint32_t ms)
{
  const uint8_t a = firstAddress();
  audioBytes = 0;
  audioBreaks = 0;
  audioLast = -1;
  if (!usb.audioInputStart(1, 16, 32000, a))
  {
    Serial.printf("S3 AUDIOIN 0 0 %s\n", usb.lastErrorName());
    return;
  }
  delay(ms);
  Serial.printf("S3 AUDIOIN %lu %lu\n", (unsigned long)audioBytes, (unsigned long)audioBreaks);
}

/* Speaker: EspUsbHost asks for each packet's frames (onAudioOutputRequest)
 * and pads what it is not given with silence, so the stream never stalls.
 * `out*` hold what is left to give and what was given. */
static volatile uint32_t outLeft, outGiven, outSum;
static volatile uint16_t outSample;

static void cmdAudioOut(uint32_t ms)
{
  const uint8_t a = firstAddress();
  outGiven = 0;
  outSum = 0;
  outSample = 0;
  outLeft = 32000u * ms / 1000u;          /* frames: mono 16-bit */
  if (!usb.audioOutputStart(1, 16, 32000, a))
  {
    outLeft = 0;
    Serial.printf("S3 AUDIOOUT 0 0 0 %s\n", usb.lastErrorName());
    return;
  }
  const uint32_t t0 = millis();
  while (outLeft && millis() - t0 < ms + 3000)
    delay(5);
  delay(400);                             /* the queued packets leave */
  usb.audioOutputStop(a);
  Serial.printf("S3 AUDIOOUT %u %lu %lu\n", outLeft ? 0 : 1, (unsigned long)(outGiven * 2u), (unsigned long)outSum);
}

static void cmdVideoInfo();

static void cmdVideo(uint32_t ms)
{
  const uint8_t a = firstAddress();
  videoFrames = 0;
  videoErrors = 0;
  if (!usb.videoStart(ESP_USB_HOST_VIDEO_FORMAT_YUY2, 80, 60, 0, a))
  {
    Serial.printf("S3 VIDEO 0 0 0 0 %s\n", usb.lastErrorName());
    return;
  }
  delay(ms);
  cmdVideoInfo();
  usb.videoStop(a);
  Serial.printf("S3 VIDEO %lu %lu %u %u\n", (unsigned long)videoFrames, (unsigned long)videoErrors, videoW, videoH);
}

static void cmdVideoInfo()
{
  EspUsbHostVideoStreamInfo streams[8];
  const size_t n = usb.getVideoStreams(firstAddress(), streams, 8);
  for (size_t i = 0; i < n; i++)
    Serial.printf("S3 VSTREAM %u itf=%u alt=%u ep=%02x iso=%u fmt=%u %ux%u interval=%lu maxpayload=%lu\n", (unsigned)i,
                  streams[i].interfaceNumber, streams[i].alternate, streams[i].endpointAddress,
                  streams[i].isochronous ? 1 : 0, streams[i].format, streams[i].width, streams[i].height,
                  (unsigned long)streams[i].frameInterval, (unsigned long)streams[i].maxPayloadSize);
  EspUsbHostVideoStats st;
  if (usb.videoStats(st, firstAddress()))
    Serial.printf("S3 VSTATS frames=%lu incomplete=%lu payloads=%lu hdr=%lu payloaderr=%lu pkterr=%lu ovf=%lu bytes=%lu\n",
                  (unsigned long)st.frames, (unsigned long)st.framesIncomplete, (unsigned long)st.payloads,
                  (unsigned long)st.headerErrors, (unsigned long)st.payloadErrors, (unsigned long)st.packetErrors,
                  (unsigned long)st.overflows, (unsigned long)st.bytes);
  Serial.printf("S3 VEND_INFO %u\n", (unsigned)n);
}

static void cmdDfu()
{
  const uint8_t a = firstAddress();
  EspUsbHostInterfaceInfo itfs[16];
  const size_t n = usb.getInterfaces(a, itfs, 16);
  for (size_t i = 0; i < n; i++)
  {
    if (itfs[i].interfaceClass == 0xFE && itfs[i].interfaceSubClass == 0x01)
    {
      /* DFU_DETACH: class request to the interface, wTimeout 1000 ms. The
       * device may leave before the status stage completes. */
      usb.vendorControlTransfer(0x21, 0x00, 1000, itfs[i].number, nullptr, 0, nullptr, a);
      Serial.println("S3 DFU 1");
      return;
    }
  }
  Serial.println("S3 DFU 0");
}

static void run(char *cmd)
{
  char *arg = strchr(cmd, ' ');
  if (arg) *arg++ = '\0';
  else arg = cmd + strlen(cmd);
  const uint8_t a = firstAddress();
  if (!strcmp(cmd, "ping")) Serial.println("S3 PONG");
  else if (!strcmp(cmd, "dev")) cmdDev();
  else if (!strcmp(cmd, "cdc")) cmdCdc(arg);
  else if (!strcmp(cmd, "cdcbig")) cmdCdcBig(strtoul(arg, nullptr, 10));
  else if (!strcmp(cmd, "baud")) Serial.printf("S3 BAUD %u\n", cdc.setBaudRate(strtoul(arg, nullptr, 10)) ? 1 : 0);
  else if (!strcmp(cmd, "hidv"))
  {
    uint8_t d[64];
    const size_t n = parseHex(arg, d, sizeof d);
    Serial.printf("S3 HIDV_SENT %u\n", usb.sendHIDVendorOutput(d, n, a) ? 1 : 0);
  }
  else if (!strcmp(cmd, "leds"))
  {
    const unsigned v = strtoul(arg, nullptr, 0);
    Serial.printf("S3 LEDS %u\n", usb.setKeyboardLeds(v & 1, v & 2, v & 4, a) ? 1 : 0);
  }
  else if (!strcmp(cmd, "midi"))
  {
    unsigned ch = 0, n = 0, v = 0;
    sscanf(arg, "%u %u %u", &ch, &n, &v);
    Serial.printf("S3 MIDI_SENT %u\n", usb.midiSendNoteOn(ch, n, v, a) ? 1 : 0);
  }
  else if (!strcmp(cmd, "net")) Serial.printf("S3 NETOPEN %u\n", usb.networkOpen(a) ? 1 : 0);
  else if (!strcmp(cmd, "netsend"))
  {
    uint8_t f[60] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x02, 0x00, 0x00, 0x00, 0x53, 0x33, 0x88, 0xb5};
    memcpy(f + 14, "S3 peer frame", 13);
    Serial.printf("S3 NETSENT %u\n", usb.networkWriteFrame(f, sizeof f, a) ? 1 : 0);
  }
  else if (!strcmp(cmd, "netstat")) Serial.printf("S3 NETRX %lu %lu\n", (unsigned long)netFrames, (unsigned long)netOurs);
  else if (!strcmp(cmd, "vend"))
  {
    if (!vendorOpened) vendorOpened = usb.vendorOpen(a);
    cmdVend(arg);
  }
  else if (!strcmp(cmd, "vreq"))
  {
    char buf[16] = {};
    size_t got = 0;
    const bool ok = usb.vendorControlIn(0x10, 0, 0, (uint8_t *)buf, sizeof buf - 1, &got, a);
    Serial.printf("S3 VREQ %u %s\n", ok ? 1 : 0, buf);
  }
  else if (!strcmp(cmd, "msc")) cmdMsc();
  else if (!strcmp(cmd, "mscraw")) cmdMscRaw(strtoul(arg, nullptr, 10));
  else if (!strcmp(cmd, "mscwrite")) cmdMscWrite(arg);
  else if (!strcmp(cmd, "mscread")) cmdMscRead();
  else if (!strcmp(cmd, "ccid")) cmdCcid();
  else if (!strcmp(cmd, "apdu")) cmdApdu(arg);
  else if (!strcmp(cmd, "audioin")) cmdAudioIn(strtoul(arg, nullptr, 10));
  else if (!strcmp(cmd, "audioout")) cmdAudioOut(strtoul(arg, nullptr, 10));
  else if (!strcmp(cmd, "video")) cmdVideo(strtoul(arg, nullptr, 10));
  else if (!strcmp(cmd, "dfu")) cmdDfu();
  else if (!strcmp(cmd, "vinfo")) cmdVideoInfo();
  else if (!strcmp(cmd, "reset"))
  {
    netFrames = netOurs = 0;
    Serial.println("S3 RESET");
  }
  else Serial.printf("S3 UNKNOWN %s\n", cmd);
}

void setup()
{
  Serial.begin(115200);
  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &d)
                        { Serial.printf("S3 CONNECTED %u %04x %04x %s\n", d.address, d.vid, d.pid, d.product); });
  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &d)
                           {
                             vendorOpened = false;
                             Serial.printf("S3 DISCONNECTED %u %04x %04x\n", d.address, d.vid, d.pid); });
  usb.onKeyboard([](const EspUsbHostKeyboardEvent &e)
                 { Serial.printf("S3 KEY %c %u %u\n", e.pressed ? 'p' : 'r', e.keycode, e.ascii); });
  usb.onHIDVendorInput([](const EspUsbHostHIDVendorInput &e)
                       {
                         Serial.print("S3 HIDVIN ");
                         printHex(e.reportData, e.reportLength);
                         Serial.println(); });
  usb.onMidiMessage([](const EspUsbHostMidiMessage &m)
                    { Serial.printf("S3 MIDI %u %02x %u %u\n", m.codeIndex, m.status, m.data1, m.data2); });
  usb.onNetworkFrame([](const EspUsbHostNetworkFrame &f)
                     {
                       netFrames++;
                       if (f.length >= 14 && f.data[12] == 0x88 && f.data[13] == 0xb5) netOurs++; });
  usb.onAudioData([](const EspUsbHostAudioData &d)
                  {
                    for (size_t i = 0; i + 1 < d.length; i += 2)
                    {
                      const uint16_t s = (uint16_t)(d.data[i] | d.data[i + 1] << 8);
                      /* The first 10 ms may still hold what the device queued
                       * before an earlier stream stopped; judge from there. */
                      if (audioLast >= 0 && s != (uint16_t)(audioLast + 1) && audioBytes >= 640) audioBreaks++;
                      audioLast = s;
                    }
                    audioBytes += d.length; });
  usb.onVideoFrame([](const EspUsbHostVideoFrame &f)
                   {
                     videoFrames++;
                     videoW = f.width;
                     videoH = f.height;
                     for (size_t i = 1; i < f.length; i++)
                       if ((uint8_t)(f.data[i] - f.data[0]) != (uint8_t)(i * 7)) { videoErrors++; break; } });
  usb.onAudioOutputRequest([](EspUsbHostAudioOutputRequest &r)
                           {
                             size_t n = 0;
                             while (n < r.frameCount && outLeft)
                             {
                               const uint16_t v = outSample++;
                               r.data[2 * n] = (uint8_t)v;
                               r.data[2 * n + 1] = (uint8_t)(v >> 8);
                               outSum += (uint8_t)v + (uint8_t)(v >> 8);
                               n++;
                               outLeft--;
                               outGiven++;
                             }
                             r.writtenFrames = n; });
  cdc.begin(115200);
  if (!usb.begin())
    Serial.printf("S3 BEGIN_FAILED %s\n", usb.lastErrorName());
  Serial.println("S3 READY");
}

void loop()
{
  while (Serial.available())
  {
    const char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n')
    {
      line[used] = '\0';
      if (used) run(line);
      used = 0;
    }
    else if (used + 1 < sizeof line)
      line[used++] = c;
  }
  delay(1);
}
