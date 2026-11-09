/* The RP2040 as USB host for tests/peer/usb/device: the DUT (a CH32 running
 * CH32RVUsbDevice) is the device. TinyUSB's host stack on the RP2040's own
 * controller (Adafruit TinyUSB Library, patched by the runner) is independent
 * of the DUT's stack, so a pass here is interoperability.
 *
 * The same commands as peer_s3 (EspUsbHost), on the RTT console (SEGGER RTT
 * over the probe's SWD); every answer and event is a line starting "P ". The
 * TinyUSB callbacks only record or print: the commands run from loop(), and
 * every wait keeps the host stack running (pump()).
 *
 *   ping                 P PONG
 *   dev                  P DEV ... / P ITF ... per interface / P DEVEND <n>
 *   cdc <hex>            P CDC <hex echoed back within 2 s>
 *   cdcbig <n>           P CDCBIG <n> <ok> <bytes back>  (patterned echo)
 *   baud <n>             P BAUD <ok>
 *   hidv <hex>           P HIDV_SENT <ok>     (vendor output report; the first byte is its ID)
 *   leds <bits>          P LEDS <ok>
 *   midi <ch> <n> <v>    P MIDI_SENT <ok>
 *   net                  P NETOPEN <ok>
 *   netsend              P NETSENT <ok>       (broadcast, EtherType 0x88B5)
 *   netstat              P NETRX <frames> <frames of 0x88B5>
 *   vend <hex>           P VEND <hex echoed back within 2 s>
 *   vreq                 P VREQ <ok> <answer as text>
 *   msc                  P MSC <ok> <vendor> <product> <blocks> <block size>
 *   mscraw <lba>         P MSCRAW <ok>        (write a pattern, read it back, restore)
 *   mscwrite <text>      P MSCWRITE <ok>      (/HOST.TXT on the FAT volume)
 *   mscread              P MSCREAD <ok> <first line of /README.TXT>
 *   mscblk <lba> <n>     P MSCBLK <ok> <sense key> <asc> <ascq>  (one READ(10) of n blocks, n <= 4)
 *   msctur               P MSCTUR <ok> <sense key> <asc> <ascq>  (TEST UNIT READY; the sense when it fails)
 *   ccid                 P CCID <ok> <ATR hex>
 *   apdu <hex>           P APDU <ok> <response hex> <SW hex>
 *   dfu                  P DFU <found> <ok>   (DFU_DETACH to the runtime interface; ok 0 when stalled)
 *   hididle <d> <ms>     P HIDIDLE <ok> <vendor input reports in <ms>>  (SET_IDLE duration d x 4 ms,
 *                        report ID 0, to the HID interface)
 *   reset                P RESET              (the network counters start over)
 *
 * Events: P CONNECTED <addr> <vid> <pid> <product> / P DISCONNECTED <addr>
 * <vid> <pid> / P KEY <p|r> <keycode> <ascii> / P HIDVIN <hex> /
 * P MIDI <cin> <status> <d1> <d2>.
 *
 * Not here: the speaker, microphone and camera (peer_s3's audioin / audioout /
 * video). The RP2040's host controller driver has no isochronous transfers. */
#include <Adafruit_TinyUSB.h>
#include <RTTStream.h>
#include "peer_fat.h"
#include "peer_pico.h"

RTTStream rtt;

/* printf to the console. RTTStream's own printf writes hex digits in upper case; the lines are lower case. */
static void say(const char *format, ...)
{
  char text[256];
  va_list ap;
  va_start(ap, format);
  vsnprintf(text, sizeof text, format, ap);
  va_end(ap);
  rtt.print(text);
}
Adafruit_USBH_Host USBHost;
Adafruit_USBH_CDC cdc;

static void pump() { USBHost.task(); }

static void pause(uint32_t ms)
{
  const uint32_t t0 = millis();
  while (millis() - t0 < ms) pump();
}

/* ------------------------------------------------------------ hex */
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
  for (size_t i = 0; i < n; i++) say("%02x", d[i]);
}

/* ------------------------------------------------------------ the device */
static Device dev;
static volatile uint8_t mounted;    /* set by tuh_mount_cb, announced from loop() */
static uint8_t config[1024];
static uint16_t configLength;

static void utf16(const uint16_t *s, char *out, size_t cap)
{
  size_t n = 0;
  const size_t chars = ((s[0] & 0xFF) - 2) / 2;
  for (size_t i = 0; i < chars && n + 1 < cap; i++) out[n++] = (char)(s[1 + i] < 128 ? s[1 + i] : '?');
  out[n] = '\0';
}

/* The interface (alternate 0) of class / subclass, or -1. */
static int findInterface(uint8_t cls, int sub)
{
  int found = -1;
  walkConfig(config, configLength, [&](const uint8_t *d)
       {
         if (d[1] == TUSB_DESC_INTERFACE && d[3] == 0 && d[5] == cls && (sub < 0 || d[6] == sub)) { found = d[2]; return false; }
         return true; });
  return found;
}

/* The bulk (or interrupt) endpoints of interface `itf` at `alt`: their descriptors. */
static void endpointsOf(int itf, uint8_t alt, const tusb_desc_endpoint_t **in, const tusb_desc_endpoint_t **out)
{
  bool inside = false;
  *in = *out = nullptr;
  walkConfig(config, configLength, [&](const uint8_t *d)
       {
         if (d[1] == TUSB_DESC_INTERFACE) inside = d[2] == itf && d[3] == alt;
         else if (inside && d[1] == TUSB_DESC_ENDPOINT && (d[3] & 3) == TUSB_XFER_BULK)
         {
           if (d[2] & 0x80) { if (!*in) *in = (const tusb_desc_endpoint_t *)d; }
           else if (!*out) *out = (const tusb_desc_endpoint_t *)d;
         }
         return true; });
}

static bool claimedClass(uint8_t cls, uint8_t sub)
{
  return cls == 0x02 ? sub == 0x02 : cls == 0x0A || cls == 0x03 || cls == 0x08 || (cls == 0x01 && sub == 0x03);
}

static void cmdDev()
{
  unsigned n = 0;
  if (dev.address && dev.announced)
  {
    n = 1;
    say("P DEV %u %04x %04x %u %04x %02x %s|%s|%s\n", dev.address, dev.vid, dev.pid,
               tuh_speed_get(dev.address) == TUSB_SPEED_LOW ? 0 : 1, dev.usb, dev.deviceClass, dev.manufacturer,
               dev.product, dev.serial);
    walkConfig(config, configLength, [&](const uint8_t *d)
         {
           if (d[1] == TUSB_DESC_INTERFACE)
             say("P ITF %u %u %u %02x %02x %02x %u\n", dev.address, d[2], d[3], d[5], d[6], d[7],
                        claimedClass(d[5], d[6]) ? 1 : 0);
           return true; });
  }
  say("P DEVEND %u\n", n);
}

/* ------------------------------------------------------------ transfers */

static void waitDone(tuh_xfer_t *x)
{
  Wait *w = (Wait *)x->user_data;
  w->result = x->result;
  w->actual = x->actual_len;
  w->done = true;
}

/* Until done, the device gone or `ms` up; false when not done. */
static bool waitFor(Wait &w, uint32_t ms)
{
  const uint32_t t0 = millis();
  while (!w.done && dev.address && millis() - t0 < ms) pump();
  return w.done;
}

/* A control transfer; the result (XFER_RESULT_INVALID when it never finished). */
static xfer_result_t control(uint8_t type, uint8_t request, uint16_t value, uint16_t index, void *data, uint16_t length,
                             uint32_t *actual = nullptr, uint32_t ms = 2000)
{
  static tusb_control_request_t setup;
  setup.bmRequestType = type;
  setup.bRequest = request;
  setup.wValue = value;
  setup.wIndex = index;
  setup.wLength = length;
  static Wait w;
  w = Wait{false, XFER_RESULT_INVALID, 0};
  tuh_xfer_t x = {};
  x.daddr = dev.address;
  x.ep_addr = 0;
  x.setup = &setup;
  x.buffer = (uint8_t *)data;
  x.complete_cb = waitDone;
  x.user_data = (uintptr_t)&w;
  if (!dev.address || !tuh_control_xfer(&x)) return XFER_RESULT_FAILED;
  waitFor(w, ms);
  if (actual) *actual = w.actual;
  return w.done ? w.result : XFER_RESULT_INVALID;
}

/* GET_DESCRIPTOR through control(): every request of this sketch has a time limit (TinyUSB's _sync calls wait
 * without one, and a device that leaves in the middle left the RP2040 waiting). */
static bool descriptor(uint8_t type, uint8_t index, uint16_t language, void *buffer, uint16_t length)
{
  return control(0x80, 6, (uint16_t)(type << 8 | index), language, buffer, length) == XFER_RESULT_SUCCESS;
}

static void fetchString(uint8_t index, char *out, size_t cap)
{
  static uint16_t buf[64];
  out[0] = '\0';
  if (index && descriptor(TUSB_DESC_STRING, index, 0x0409, buf, sizeof buf)) utf16(buf, out, cap);
}

static void announce()
{
  static tusb_desc_device_t d;
  dev.address = mounted;
  if (!descriptor(TUSB_DESC_DEVICE, 0, 0, &d, sizeof d)) return;
  dev.vid = d.idVendor;
  dev.pid = d.idProduct;
  dev.usb = d.bcdUSB;
  dev.deviceClass = d.bDeviceClass;
  fetchString(d.iManufacturer, dev.manufacturer, sizeof dev.manufacturer);
  fetchString(d.iProduct, dev.product, sizeof dev.product);
  fetchString(d.iSerialNumber, dev.serial, sizeof dev.serial);
  configLength = 0;
  if (descriptor(TUSB_DESC_CONFIGURATION, 0, 0, config, 9))
  {
    uint16_t total = (uint16_t)(config[2] | config[3] << 8);
    if (total > sizeof config) total = sizeof config;
    if (descriptor(TUSB_DESC_CONFIGURATION, 0, 0, config, total)) configLength = total;
  }
  if (!dev.address) return;
  dev.announced = true;
  say("P CONNECTED %u %04x %04x %s\n", dev.address, dev.vid, dev.pid, dev.product);
}

/* One transfer on a bulk endpoint this sketch opened; bytes moved, -1 on failure or time out. */
static int bulk(uint8_t ep, uint8_t *data, uint16_t length, uint32_t ms)
{
  static Wait w;
  w = Wait{false, XFER_RESULT_INVALID, 0};
  tuh_xfer_t x = {};
  x.daddr = dev.address;
  x.ep_addr = ep;
  x.buffer = data;
  x.buflen = length;
  x.complete_cb = waitDone;
  x.user_data = (uintptr_t)&w;
  if (!dev.address || !tuh_edpt_xfer(&x)) return -1;
  if (!waitFor(w, ms))
  {
    if (dev.address) tuh_edpt_abort_xfer(dev.address, ep);
    return -1;
  }
  return w.result == XFER_RESULT_SUCCESS ? (int)w.actual : -1;
}

/* ------------------------------------------------------------ CDC */
static int cdcIndex = -1;

static size_t collectCdc(uint8_t *out, size_t want, uint32_t ms)
{
  size_t got = 0;
  const uint32_t t0 = millis();
  while (got < want && millis() - t0 < ms)
  {
    pump();
    while (got < want && cdc.available()) out[got++] = (uint8_t)cdc.read();
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
  const size_t got = collectCdc(rx, n, 2000);
  rtt.print("P CDC ");
  printHex(rx, got);
  rtt.println();
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
      sent += cdc.write(tx + sent, min((size_t)64, n - sent));
      cdc.flush();
    }
    pump();
    while (got < n && cdc.available()) rx[got++] = (uint8_t)cdc.read();
  }
  say("P CDCBIG %u %u %u\n", (unsigned)n, (got == n && memcmp(tx, rx, n) == 0) ? 1 : 0, (unsigned)got);
}

static void baudDone(tuh_xfer_t *x) { waitDone(x); }

static void cmdBaud(uint32_t baud)
{
  static Wait w;
  w = Wait{false, XFER_RESULT_INVALID, 0};
  const bool ok = cdcIndex >= 0 && tuh_cdc_set_baudrate((uint8_t)cdcIndex, baud, baudDone, (uintptr_t)&w) &&
                  waitFor(w, 2000) && w.result == XFER_RESULT_SUCCESS;
  say("P BAUD %u\n", ok ? 1 : 0);
}

/* ------------------------------------------------------------ HID */
static HidInstance hids[CFG_TUH_HID];
static volatile uint32_t hidVendorInputs;
static volatile bool setReportDone;
static volatile uint16_t setReportLength;

static const tuh_hid_report_info_t *reportOf(uint8_t idx, uint8_t id)
{
  for (uint8_t i = 0; i < hids[idx].count; i++)
    if (hids[idx].info[i].report_id == id) return &hids[idx].info[i];
  return nullptr;
}

/* The HID instance and report ID of the first report with usage page `page` (and usage, unless < 0). */
static bool findReport(bool vendor, uint8_t *idx, uint8_t *id)
{
  for (uint8_t i = 0; i < CFG_TUH_HID; i++)
    for (uint8_t k = 0; hids[i].used && k < hids[i].count; k++)
    {
      const tuh_hid_report_info_t &r = hids[i].info[k];
      if (vendor ? r.usage_page >= 0xFF00 : (r.usage_page == HID_USAGE_PAGE_DESKTOP && r.usage == HID_USAGE_DESKTOP_KEYBOARD))
      {
        *idx = i;
        *id = r.report_id;
        return true;
      }
    }
  return false;
}

static void keyboard(HidInstance &h, const uint8_t *r, uint16_t len)
{
  static const uint8_t ascii[128][2] = {HID_KEYCODE_TO_ASCII};
  if (len < 8) return;
  const uint8_t *now = r + 2;
  for (int i = 0; i < 6; i++)
    if (h.keys[i] && !memchr(now, h.keys[i], 6))
      say("P KEY r %u %u\n", h.keys[i], h.keys[i] < 128 ? ascii[h.keys[i]][0] : 0);
  for (int i = 0; i < 6; i++)
    if (now[i] && !memchr(h.keys, now[i], 6)) say("P KEY p %u %u\n", now[i], now[i] < 128 ? ascii[now[i]][0] : 0);
  memcpy(h.keys, now, 6);
}

static void cmdHidv(const char *arg)
{
  static uint8_t d[64];
  const size_t n = parseHex(arg, d, sizeof d);
  uint8_t idx, id;
  bool ok = n >= 1 && findReport(true, &idx, &id);
  if (ok && !tuh_hid_send_report(dev.address, idx, d[0], d + 1, (uint16_t)(n - 1)))
  {
    setReportDone = false;  /* no interrupt OUT: SET_REPORT */
    ok = tuh_hid_set_report(dev.address, idx, d[0], HID_REPORT_TYPE_OUTPUT, d + 1, (uint16_t)(n - 1));
    const uint32_t t0 = millis();
    while (ok && !setReportDone && millis() - t0 < 2000) pump();
    ok = ok && setReportDone && setReportLength;
  }
  say("P HIDV_SENT %u\n", ok ? 1 : 0);
}

static void cmdLeds(unsigned v)
{
  static uint8_t leds;
  uint8_t idx, id;
  leds = (uint8_t)v;
  setReportDone = false;
  bool ok = findReport(false, &idx, &id) && tuh_hid_set_report(dev.address, idx, id, HID_REPORT_TYPE_OUTPUT, &leds, 1);
  const uint32_t t0 = millis();
  while (ok && !setReportDone && millis() - t0 < 2000) pump();
  say("P LEDS %u\n", ok && setReportDone && setReportLength ? 1 : 0);
}

static void cmdHidIdle(const char *arg)
{
  unsigned duration = 0, ms = 0;
  sscanf(arg, "%u %u", &duration, &ms);
  const int itf = findInterface(0x03, -1);
  if (itf < 0)
  {
    rtt.println("P HIDIDLE 0 0");
    return;
  }
  const bool ok = control(0x21, 0x0A, (uint16_t)(duration << 8), (uint16_t)itf, nullptr, 0) == XFER_RESULT_SUCCESS;
  const uint32_t before = hidVendorInputs;
  pause(ms);
  say("P HIDIDLE %u %lu\n", ok ? 1 : 0, (unsigned long)(hidVendorInputs - before));
}

/* ------------------------------------------------------------ MIDI */
static int midiIndex = -1;

/* ------------------------------------------------------------ vendor bulk */
static uint8_t vendorIn, vendorOut;

static bool openPair(int itf, uint8_t alt, uint8_t *in, uint8_t *out)
{
  const tusb_desc_endpoint_t *i, *o;
  endpointsOf(itf, alt, &i, &o);
  if (!i || !o || !tuh_edpt_open(dev.address, i) || !tuh_edpt_open(dev.address, o)) return false;
  *in = i->bEndpointAddress;
  *out = o->bEndpointAddress;
  return true;
}

static void cmdVend(const char *arg)
{
  static uint8_t tx[128], rx[128];
  const size_t n = parseHex(arg, tx, sizeof tx);
  if (!vendorIn)
  {
    const int itf = findInterface(0xFF, -1);
    if (itf < 0 || !openPair(itf, 0, &vendorIn, &vendorOut))
    {
      vendorIn = 0;
      rtt.println("P VEND ERR no vendor interface");
      return;
    }
  }
  if (bulk(vendorOut, tx, (uint16_t)n, 2000) != (int)n)
  {
    rtt.println("P VEND ERR write");
    return;
  }
  size_t got = 0;
  const uint32_t t0 = millis();
  while (got < n && millis() - t0 < 2000)
  {
    const int k = bulk(vendorIn, rx + got, 64, 2000 - (millis() - t0));
    if (k < 0) break;
    got += (size_t)k;
  }
  rtt.print("P VEND ");
  printHex(rx, got);
  rtt.println();
}

static void cmdVreq()
{
  char buf[16] = {};
  const bool ok = control(0xC0, 0x10, 0, 0, buf, sizeof buf - 1) == XFER_RESULT_SUCCESS;
  say("P VREQ %u %s\n", ok ? 1 : 0, buf);
}

/* ------------------------------------------------------------ mass storage */
static Wait mscWait;
static uint8_t mscStatus;   /* the CSW's bCSWStatus */

/* TinyUSB's MSC host does not look at a stage's result: after a stalled data stage it reads no CSW and reports
 * the one before, and its tag is the same for every command. So the CSW's signature is wiped once read: a
 * command whose CSW never came finds it wiped, and fails. */
static bool mscDone(uint8_t, const tuh_msc_complete_data_t *data)
{
  msc_csw_t *csw = (msc_csw_t *)data->csw;
  mscStatus = csw && csw->signature == MSC_CSW_SIGNATURE ? csw->status : 0xFF;
  if (csw) csw->signature = 0;
  mscWait.done = true;
  return true;
}

static void mscStart() { mscWait = Wait{false, XFER_RESULT_INVALID, 0}; }

/* The last command: true when it ended with a good CSW. */
static bool mscFinish(bool started, uint32_t ms = 5000)
{
  if (!started) return false;
  const uint32_t t0 = millis();
  while (!mscWait.done && dev.address && millis() - t0 < ms) pump();
  return mscWait.done && mscStatus == MSC_CSW_STATUS_PASSED;
}

static bool mscIdle(uint32_t ms = 2000)
{
  const uint32_t t0 = millis();
  while (dev.address && !tuh_msc_ready(dev.address) && millis() - t0 < ms) pump();
  return dev.address && tuh_msc_ready(dev.address);
}

static bool mscTur()
{
  mscStart();
  return mscFinish(mscIdle() && tuh_msc_test_unit_ready(dev.address, 0, mscDone, 0));
}

static void mscSense(uint8_t *key, uint8_t *asc, uint8_t *ascq)
{
  static scsi_sense_fixed_resp_t sense;
  memset(&sense, 0, sizeof sense);
  mscStart();
  if (mscFinish(mscIdle() && tuh_msc_request_sense(dev.address, 0, &sense, mscDone, 0)))
  {
    *key = sense.sense_key;
    *asc = sense.add_sense_code;
    *ascq = sense.add_sense_qualifier;
  }
}

static bool mscRead(uint32_t lba, uint8_t *buffer, uint16_t count)
{
  mscStart();
  return mscFinish(mscIdle() && tuh_msc_read10(dev.address, 0, buffer, lba, count, mscDone, 0));
}

static bool mscWrite(uint32_t lba, const uint8_t *buffer, uint16_t count)
{
  mscStart();
  return mscFinish(mscIdle() && tuh_msc_write10(dev.address, 0, buffer, lba, count, mscDone, 0));
}

static void trimCopy(char *out, const uint8_t *in, size_t n)
{
  memcpy(out, in, n);
  out[n] = '\0';
  for (int i = (int)n - 1; i >= 0 && out[i] == ' '; i--) out[i] = '\0';
}

static void cmdMsc()
{
  static scsi_inquiry_resp_t inq;
  static scsi_read_capacity10_resp_t cap;
  bool ok = false;
  for (int i = 0; i < 10 && !(ok = mscTur()); i++) pause(200);   /* ready, through UNIT ATTENTION */
  mscStart();
  ok = ok && mscFinish(tuh_msc_inquiry(dev.address, 0, &inq, mscDone, 0));
  mscStart();
  ok = ok && mscIdle() && mscFinish(tuh_msc_read_capacity(dev.address, 0, &cap, mscDone, 0));
  char vendor[9], product[17];
  trimCopy(vendor, inq.vendor_id, 8);
  trimCopy(product, inq.product_id, 16);
  const uint32_t blocks = ok ? tu_ntohl(cap.last_lba) + 1 : 0, size = ok ? tu_ntohl(cap.block_size) : 0;
  say("P MSC %u %s %s %lu %lu\n", ok ? 1 : 0, ok ? vendor : "-", ok ? product : "-", (unsigned long)blocks,
             (unsigned long)size);
}

static void cmdMscRaw(uint32_t lba)
{
  static uint8_t keep[512], pattern[512], back[512];
  for (size_t i = 0; i < 512; i++) pattern[i] = (uint8_t)(i ^ (lba * 31));
  bool ok = mscRead(lba, keep, 1);
  ok = ok && mscWrite(lba, pattern, 1);
  ok = ok && mscRead(lba, back, 1) && memcmp(back, pattern, 512) == 0;
  ok = mscWrite(lba, keep, 1) && ok;
  say("P MSCRAW %u\n", ok ? 1 : 0);
}

static void cmdMscBlocks(const char *arg)
{
  static uint8_t blocks[4 * 512];
  char *end = nullptr;
  const uint32_t lba = strtoul(arg, &end, 10);
  uint32_t n = strtoul(end, nullptr, 10);
  n = n < 1 ? 1 : n > 4 ? 4 : n;
  uint8_t key = 0, asc = 0, ascq = 0;
  const bool ok = mscRead(lba, blocks, (uint16_t)n);
  if (!ok && mscWait.done) mscSense(&key, &asc, &ascq);
  say("P MSCBLK %u %02x %02x %02x\n", ok ? 1 : 0, key, asc, ascq);
}

static void cmdMscTur()
{
  uint8_t key = 0, asc = 0, ascq = 0;
  const bool ok = mscTur();
  if (!ok) mscSense(&key, &asc, &ascq);
  say("P MSCTUR %u %02x %02x %02x\n", ok ? 1 : 0, key, asc, ascq);
}

static bool fatRead(uint32_t lba, uint8_t *buffer, uint16_t count) { return mscRead(lba, buffer, count); }
static bool fatWrite(uint32_t lba, const uint8_t *buffer, uint16_t count) { return mscWrite(lba, buffer, count); }
static PeerFat fat(fatRead, fatWrite);

static bool mountFat()
{
  if (!mscTur() && !mscTur()) return false;  /* UNIT ATTENTION once after a change */
  return fat.mount();
}

static void cmdMscWrite(const char *text)
{
  static char body[200];
  const int n = snprintf(body, sizeof body, "%s\r\n", text);
  const bool ok = mountFat() && fat.writeFile("HOST.TXT", (const uint8_t *)body, (uint32_t)n);
  say("P MSCWRITE %u\n", ok ? 1 : 0);
}

static void cmdMscRead()
{
  char first[96] = {};
  const bool ok = mountFat() && fat.firstLine("README.TXT", first, sizeof first);
  say("P MSCREAD %u %s\n", ok ? 1 : 0, first);
}

/* ------------------------------------------------------------ smart card (CCID) */
static uint8_t ccidIn, ccidOut, ccidSeq;
static uint8_t ccidMessage[300];

/* One command and its response: the response's length, -1 on failure. */
static int ccidExchange(uint8_t type, const uint8_t *data, uint32_t n)
{
  static uint8_t tx[300];
  if (!ccidIn)
  {
    const int itf = findInterface(0x0B, -1);
    if (itf < 0 || !openPair(itf, 0, &ccidIn, &ccidOut))
    {
      ccidIn = 0;
      return -1;
    }
  }
  memset(tx, 0, 10);
  tx[0] = type;
  tx[1] = (uint8_t)n;
  tx[2] = (uint8_t)(n >> 8);
  tx[6] = ccidSeq++;
  if (n) memcpy(tx + 10, data, n);
  if (bulk(ccidOut, tx, (uint16_t)(10 + n), 2000) != (int)(10 + n)) return -1;
  for (int tries = 0; tries < 4; tries++)
  {
    const int k = bulk(ccidIn, ccidMessage, sizeof ccidMessage, 5000);
    if (k < 10) return -1;
    if ((ccidMessage[7] & 0xC0) != 0x80) return k;   /* not "time extension": the answer */
  }
  return -1;
}

static void cmdCcid()
{
  const int k = ccidExchange(0x62, nullptr, 0);   /* IccPowerOn */
  const bool ok = k >= 10 && ccidMessage[0] == 0x80 && (ccidMessage[7] & 0x40) == 0;
  say("P CCID %u ", ok ? 1 : 0);
  if (ok) printHex(ccidMessage + 10, (size_t)(k - 10));
  rtt.println();
}

static void cmdApdu(const char *arg)
{
  uint8_t apdu[64];
  const size_t n = parseHex(arg, apdu, sizeof apdu);
  const int k = ccidExchange(0x6F, apdu, n);      /* XfrBlock */
  const bool ok = k >= 12 && ccidMessage[0] == 0x80 && (ccidMessage[7] & 0x40) == 0;
  say("P APDU %u ", ok ? 1 : 0);
  if (ok) printHex(ccidMessage + 10, (size_t)(k - 12));
  say(" %02x%02x\n", ok ? ccidMessage[k - 2] : 0, ok ? ccidMessage[k - 1] : 0);
}

/* ------------------------------------------------------------ network (CDC NCM) */
static uint8_t netIn, netOut;
static volatile uint32_t netFrames, netOurs;
static uint8_t ntbIn[2048];
static uint16_t netSeq;

static void netReceived(tuh_xfer_t *x);

static void netArm()
{
  tuh_xfer_t x = {};
  x.daddr = dev.address;
  x.ep_addr = netIn;
  x.buffer = ntbIn;
  x.buflen = sizeof ntbIn;
  x.complete_cb = netReceived;
  if (dev.address && netIn) tuh_edpt_xfer(&x);
}

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

static void netReceived(tuh_xfer_t *x)
{
  if (x->result == XFER_RESULT_SUCCESS && x->actual_len >= 12 && !memcmp(ntbIn, "NCMH", 4))
  {
    const uint16_t length = (uint16_t)x->actual_len;
    uint16_t ndp = le16(ntbIn + 10);
    for (int guard = 0; ndp && ndp + 8 <= length && !memcmp(ntbIn + ndp, "NCM", 3) && guard < 8; guard++)
    {
      const uint16_t ndpLength = le16(ntbIn + ndp + 4);
      for (uint16_t at = ndp + 8; at + 4 <= ndp + ndpLength && at + 4 <= length; at += 4)
      {
        const uint16_t index = le16(ntbIn + at), size = le16(ntbIn + at + 2);
        if (!index || !size) break;
        netFrames++;
        if (size >= 14 && index + 14 <= length && ntbIn[index + 12] == 0x88 && ntbIn[index + 13] == 0xB5) netOurs++;
      }
      ndp = le16(ntbIn + ndp + 6);
    }
  }
  if (x->result != XFER_RESULT_STALLED) netArm();
}

static void cmdNet()
{
  const int comm = findInterface(0x02, 0x0D);
  int data = -1;
  bool afterComm = false;
  walkConfig(config, configLength, [&](const uint8_t *d)
       {
         if (d[1] == TUSB_DESC_INTERFACE && d[3] == 0)
         {
           if (afterComm && d[5] == 0x0A) { data = d[2]; return false; }
           afterComm = d[2] == comm;
         }
         return true; });
  bool ok = comm >= 0 && data >= 0 && control(0x01, 0x0B, 1, (uint16_t)data, nullptr, 0) == XFER_RESULT_SUCCESS;  /* SET_INTERFACE alt 1 */
  ok = ok && openPair(data, 1, &netIn, &netOut);
  if (ok) netArm();
  say("P NETOPEN %u\n", ok ? 1 : 0);
}

static void cmdNetSend()
{
  static uint8_t ntb[12 + 16 + 4 + 60];
  memset(ntb, 0, sizeof ntb);
  const uint16_t total = sizeof ntb;
  memcpy(ntb, "NCMH", 4);
  ntb[4] = 12;
  ntb[6] = (uint8_t)netSeq;
  ntb[7] = (uint8_t)(netSeq++ >> 8);
  ntb[8] = (uint8_t)total;
  ntb[9] = (uint8_t)(total >> 8);
  ntb[10] = 12;                                   /* the NDP */
  memcpy(ntb + 12, "NCM0", 4);
  ntb[16] = 16;                                   /* its length: one datagram and the terminator */
  ntb[20] = 32;                                   /* the datagram's index and length */
  ntb[22] = 60;
  uint8_t *f = ntb + 32;
  memset(f, 0xFF, 6);
  const uint8_t src[6] = {0x02, 0x00, 0x00, 0x00, 0x50, 0x49};
  memcpy(f + 6, src, 6);
  f[12] = 0x88;
  f[13] = 0xB5;
  memcpy(f + 14, "Pico peer frame", 15);
  const bool ok = netOut && bulk(netOut, ntb, total, 2000) == total;
  say("P NETSENT %u\n", ok ? 1 : 0);
}

/* ------------------------------------------------------------ DFU */
static void cmdDfu()
{
  const int itf = findInterface(0xFE, 0x01);
  if (itf < 0)
  {
    rtt.println("P DFU 0 0");
    return;
  }
  /* DFU_DETACH, wTimeout 1000 ms. The device may leave before the status stage completes. */
  const xfer_result_t r = control(0x21, 0x00, 1000, (uint16_t)itf, nullptr, 0);
  say("P DFU 1 %u\n", r == XFER_RESULT_SUCCESS || r == XFER_RESULT_INVALID ? 1 : 0);
}

/* ------------------------------------------------------------ commands */
static char line[200];
static size_t used;

static void run(char *cmd)
{
  char *arg = strchr(cmd, ' ');
  if (arg) *arg++ = '\0';
  else arg = cmd + strlen(cmd);
  if (!strcmp(cmd, "ping")) rtt.println("P PONG");
  else if (!strcmp(cmd, "dev")) cmdDev();
  else if (!strcmp(cmd, "cdc")) cmdCdc(arg);
  else if (!strcmp(cmd, "cdcbig")) cmdCdcBig(strtoul(arg, nullptr, 10));
  else if (!strcmp(cmd, "baud")) cmdBaud(strtoul(arg, nullptr, 10));
  else if (!strcmp(cmd, "hidv")) cmdHidv(arg);
  else if (!strcmp(cmd, "leds")) cmdLeds(strtoul(arg, nullptr, 0));
  else if (!strcmp(cmd, "midi"))
  {
    unsigned ch = 0, n = 0, v = 0;
    sscanf(arg, "%u %u %u", &ch, &n, &v);
    const uint8_t p[4] = {0x09, (uint8_t)(0x90 | (ch & 15)), (uint8_t)n, (uint8_t)v};
    const bool ok = midiIndex >= 0 && tuh_midi_packet_write_n((uint8_t)midiIndex, p, 4) == 4;
    if (ok) tuh_midi_write_flush((uint8_t)midiIndex);
    say("P MIDI_SENT %u\n", ok ? 1 : 0);
  }
  else if (!strcmp(cmd, "net")) cmdNet();
  else if (!strcmp(cmd, "netsend")) cmdNetSend();
  else if (!strcmp(cmd, "netstat")) say("P NETRX %lu %lu\n", (unsigned long)netFrames, (unsigned long)netOurs);
  else if (!strcmp(cmd, "vend")) cmdVend(arg);
  else if (!strcmp(cmd, "vreq")) cmdVreq();
  else if (!strcmp(cmd, "hididle")) cmdHidIdle(arg);
  else if (!strcmp(cmd, "msc")) cmdMsc();
  else if (!strcmp(cmd, "mscraw")) cmdMscRaw(strtoul(arg, nullptr, 10));
  else if (!strcmp(cmd, "mscwrite")) cmdMscWrite(arg);
  else if (!strcmp(cmd, "mscread")) cmdMscRead();
  else if (!strcmp(cmd, "msctur")) cmdMscTur();
  else if (!strcmp(cmd, "mscblk")) cmdMscBlocks(arg);
  else if (!strcmp(cmd, "ccid")) cmdCcid();
  else if (!strcmp(cmd, "apdu")) cmdApdu(arg);
  else if (!strcmp(cmd, "dfu")) cmdDfu();
  else if (!strcmp(cmd, "reset"))
  {
    netFrames = netOurs = 0;
    rtt.println("P RESET");
  }
  else say("P UNKNOWN %s\n", cmd);
}

void setup()
{
  USBHost.begin(0);
  cdc.begin(115200);
  rtt.println("P READY");
}

void loop()
{
  pump();
  if (!dev.announced)
  {
    /* Configured is enough: TinyUSB's MSC driver repeats TEST UNIT READY until the medium is ready, and holds
     * tuh_mount_cb back until then. */
    for (uint8_t a = 1; !mounted && a <= CFG_TUH_DEVICE_MAX; a++)
      if (tuh_mounted(a)) mounted = a;
    if (mounted) announce();
  }
  while (rtt.available())
  {
    const char c = (char)rtt.read();
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
}

/* ------------------------------------------------------------ TinyUSB callbacks */
extern "C" {
void tuh_mount_cb(uint8_t daddr)
{
  if (!mounted) mounted = daddr;
}

void tuh_umount_cb(uint8_t daddr)
{
  if (daddr != dev.address && daddr != mounted) return;
  say("P DISCONNECTED %u %04x %04x\n", daddr, dev.vid, dev.pid);
  mounted = 0;
  dev = Device{};
  configLength = 0;
  vendorIn = vendorOut = ccidIn = ccidOut = netIn = netOut = 0;
  midiIndex = cdcIndex = -1;
  for (auto &h : hids) h = HidInstance{};
}

void tuh_cdc_mount_cb(uint8_t idx)
{
  cdc.mount(idx);
  cdcIndex = idx;
}

void tuh_cdc_umount_cb(uint8_t idx)
{
  cdc.umount(idx);
  cdcIndex = -1;
}

void tuh_hid_mount_cb(uint8_t daddr, uint8_t idx, const uint8_t *desc, uint16_t len)
{
  if (idx >= CFG_TUH_HID) return;
  hids[idx] = HidInstance{};
  hids[idx].used = true;
  hids[idx].count = tuh_hid_parse_report_descriptor(hids[idx].info, 8, desc, len);
  tuh_hid_receive_report(daddr, idx);
}

void tuh_hid_umount_cb(uint8_t, uint8_t idx)
{
  if (idx < CFG_TUH_HID) hids[idx] = HidInstance{};
}

void tuh_hid_report_received_cb(uint8_t daddr, uint8_t idx, const uint8_t *report, uint16_t len)
{
  if (idx < CFG_TUH_HID && len)
  {
    HidInstance &h = hids[idx];
    uint8_t id = 0;
    if (h.count > 1 || (h.count == 1 && h.info[0].report_id))
    {
      id = report[0];
      report++;
      len--;
    }
    const tuh_hid_report_info_t *r = reportOf(idx, id);
    if (r && r->usage_page == HID_USAGE_PAGE_DESKTOP && r->usage == HID_USAGE_DESKTOP_KEYBOARD) keyboard(h, report, len);
    else if (r && r->usage_page >= 0xFF00)
    {
      hidVendorInputs++;
      rtt.print("P HIDVIN ");
      printHex(report, len);
      rtt.println();
    }
  }
  tuh_hid_receive_report(daddr, idx);
}

void tuh_hid_set_report_complete_cb(uint8_t, uint8_t, uint8_t, uint8_t, uint16_t len)
{
  setReportLength = len;
  setReportDone = true;
}

void tuh_midi_mount_cb(uint8_t idx, const tuh_midi_mount_cb_t *) { midiIndex = idx; }

void tuh_midi_umount_cb(uint8_t) { midiIndex = -1; }

void tuh_midi_rx_cb(uint8_t idx, uint32_t)
{
  uint8_t p[4];
  while (tuh_midi_packet_read_n(idx, p, 4) == 4)
    say("P MIDI %u %02x %u %u\n", p[0] & 0x0F, p[1], p[2], p[3]);
}
}
