/* The RP2040 as a USB device for tests/peer/usb/host/classes: MIDI, a vendor
 * bulk interface with a vendor request, and a smart card reader (CCID). The DUT
 * (CH32RVUsbHost) is the host; the RP2040 runs TinyUSB's device stack (Adafruit
 * TinyUSB Library). TinyUSB has no CCID device class: the reader is a class
 * driver of this sketch's own (usbd_app_driver_get_cb).
 *
 * Commands, one per line on the RTT console; answers start "PD ":
 *   ?                      PD READY <mounted 0/1>
 *   midisend <ch> <n> <v>  PD MIDISENT <ok>
 *   card                   PD CARD <APDUs answered>
 *
 * Without a command: a MIDI packet comes back with its note raised by 12, the
 * vendor interface echoes every byte, vendor request 0x10 (IN) answers
 * "RP2040", and the card answers an APDU with its bytes reversed, then 90 00. */
#include <Adafruit_TinyUSB.h>
#include <RTTStream.h>
#include <device/usbd_pvt.h>

RTTStream rtt;

/* Adafruit TinyUSB's RP2040 port runs the USB task in an interrupt, and only when it can take __usb_mutex.
 * Every TinyUSB call from loop() holds it (UsbLock), or the task comes in while loop() holds a FIFO's own
 * lock and waits for it - a second at a bus reset, long enough for the host to give up on the device. */
extern mutex_t __usb_mutex;
struct UsbLock
{
  UsbLock() { mutex_enter_blocking(&__usb_mutex); }
  ~UsbLock() { mutex_exit(&__usb_mutex); }
};

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
Adafruit_USBD_MIDI midi;
Adafruit_USBD_WebUSB vendor;   /* only for its vendor interface: the bytes go through tud_vendor_* */

/* ------------------------------------------------------------ the card reader */
static const uint8_t atr[] = {0x3B, 0x88, 0x80, 0x01, 'R', 'P', '2', '0', '4', '0', 'C', 'C', 0x00};
static volatile uint32_t apdus;

enum { CCID_FUNCTIONAL_LEN = 54, CCID_MAX_MESSAGE = 271 };

class CcidInterface : public Adafruit_USBD_Interface
{
public:
  bool begin() { return TinyUSBDevice.addInterface(*this); }
  uint16_t getInterfaceDescriptor(uint8_t, uint8_t *buf, uint16_t bufsize) override
  {
    const uint16_t len = 9 + CCID_FUNCTIONAL_LEN + 3 * 7;
    if (!buf) return len;
    if (bufsize < len) return 0;
    const uint8_t itf = TinyUSBDevice.allocInterface(1);
    const uint8_t out = TinyUSBDevice.allocEndpoint(TUSB_DIR_OUT);
    const uint8_t in = TinyUSBDevice.allocEndpoint(TUSB_DIR_IN);
    const uint8_t irq = TinyUSBDevice.allocEndpoint(TUSB_DIR_IN);
    const uint8_t d[] = {
        9, TUSB_DESC_INTERFACE, itf, 0, 3, 0x0B, 0, 0, 0,
        CCID_FUNCTIONAL_LEN, 0x21, 0x10, 0x01,         /* CCID 1.10 */
        0,                                             /* bMaxSlotIndex */
        0x07,                                          /* 5 V, 3 V, 1.8 V */
        0x03, 0, 0, 0,                                 /* T=0, T=1 */
        0xA0, 0x0F, 0, 0,                              /* default clock 4000 kHz */
        0xA0, 0x0F, 0, 0,                              /* maximum clock */
        0,                                             /* clocks listed */
        0x80, 0x25, 0, 0,                              /* 9600 bps */
        0x80, 0x25, 0, 0,
        0,                                             /* rates listed */
        0xFE, 0, 0, 0,                                 /* IFSD */
        0, 0, 0, 0,                                    /* synchronous protocols */
        0, 0, 0, 0,                                    /* mechanical */
        0xBE, 0x00, 0x02, 0x00,                        /* automatic parameters, short APDU exchange */
        (uint8_t)CCID_MAX_MESSAGE, (uint8_t)(CCID_MAX_MESSAGE >> 8), 0, 0,
        0xFF, 0xFF,                                    /* GET RESPONSE / ENVELOPE class */
        0, 0,                                          /* no LCD */
        0,                                             /* no PIN pad */
        1,                                             /* one busy slot */
        7, TUSB_DESC_ENDPOINT, out, TUSB_XFER_BULK, 64, 0, 0,
        7, TUSB_DESC_ENDPOINT, in, TUSB_XFER_BULK, 64, 0, 0,
        7, TUSB_DESC_ENDPOINT, irq, TUSB_XFER_INTERRUPT, 8, 0, 16,
    };
    static_assert(sizeof d == len, "CCID descriptor length");
    memcpy(buf, d, len);
    return len;
  }
};

CcidInterface ccid;

static uint8_t ccid_out, ccid_in;
static bool powered;
CFG_TUSB_MEM_ALIGN static uint8_t rx[CCID_MAX_MESSAGE + 1], tx[CCID_MAX_MESSAGE + 1];

static void ccid_arm(uint8_t rhport) { usbd_edpt_xfer(rhport, ccid_out, rx, sizeof rx, false); }

static void ccid_reply(uint8_t rhport, uint8_t type, const uint8_t *data, uint32_t n, uint8_t status, uint8_t error,
                       uint8_t third)
{
  tx[0] = type;
  tx[1] = (uint8_t)n;
  tx[2] = (uint8_t)(n >> 8);
  tx[3] = tx[4] = 0;
  tx[5] = rx[5];  /* slot */
  tx[6] = rx[6];  /* sequence */
  tx[7] = status;
  tx[8] = error;
  tx[9] = third;
  if (n) memcpy(tx + 10, data, n);
  usbd_edpt_xfer(rhport, ccid_in, tx, (uint16_t)(10 + n), false);
}

static void ccid_message(uint8_t rhport, uint32_t length)
{
  const uint8_t present = powered ? 0x00 : 0x01;     /* bStatus: ICC present and active / inactive */
  const uint32_t n = length >= 10 ? (uint32_t)(rx[1] | rx[2] << 8) : 0;
  switch (length >= 10 ? rx[0] : 0)
  {
  case 0x62:                                          /* IccPowerOn */
    powered = true;
    ccid_reply(rhport, 0x80, atr, sizeof atr, 0, 0, 0);
    break;
  case 0x63:                                          /* IccPowerOff */
    powered = false;
    ccid_reply(rhport, 0x81, nullptr, 0, 0x01, 0, 0);
    break;
  case 0x65:                                          /* GetSlotStatus */
    ccid_reply(rhport, 0x81, nullptr, 0, present, 0, 0);
    break;
  case 0x6F:                                          /* XfrBlock: the APDU reversed, then 90 00 */
  {
    static uint8_t answer[CCID_MAX_MESSAGE];
    if (!powered || n + 2 > sizeof answer || n + 10 > length)
    {
      ccid_reply(rhport, 0x80, nullptr, 0, 0x40 | present, 0xFE, 0);  /* ICC mute */
      break;
    }
    for (uint32_t i = 0; i < n; i++) answer[i] = rx[10 + n - 1 - i];
    answer[n] = 0x90;
    answer[n + 1] = 0x00;
    apdus++;
    ccid_reply(rhport, 0x80, answer, n + 2, 0, 0, 0);
    break;
  }
  case 0x6B:                                          /* Escape */
    ccid_reply(rhport, 0x83, nullptr, 0, present, 0, 0);
    break;
  case 0x61:                                          /* SetParameters, GetParameters, ResetParameters: T=0 */
  case 0x6C:
  case 0x6D:
  {
    static const uint8_t t0[] = {0x11, 0x00, 0x00, 0x0A, 0x00};
    ccid_reply(rhport, 0x82, t0, sizeof t0, present, 0, 0);
    break;
  }
  default:                                            /* command not supported */
    ccid_reply(rhport, 0x81, nullptr, 0, 0x40 | present, 0x00, 0);
    break;
  }
}

static void ccid_init() {}
static bool ccid_deinit() { return true; }
static void ccid_reset(uint8_t) { powered = false; }

static uint16_t ccid_open(uint8_t rhport, const tusb_desc_interface_t *itf, uint16_t max_len)
{
  if (itf->bInterfaceClass != 0x0B) return 0;
  const uint16_t len = 9 + CCID_FUNCTIONAL_LEN + 3 * 7;
  if (max_len < len) return 0;
  const uint8_t *p = (const uint8_t *)itf + 9 + CCID_FUNCTIONAL_LEN;
  if (!usbd_open_edpt_pair(rhport, p, 2, TUSB_XFER_BULK, &ccid_out, &ccid_in)) return 0;
  if (!usbd_edpt_open(rhport, (const tusb_desc_endpoint_t *)(p + 14))) return 0;
  ccid_arm(rhport);
  return len;
}

static bool ccid_control(uint8_t, uint8_t, const tusb_control_request_t *) { return false; }

static bool ccid_xfer(uint8_t rhport, uint8_t ep, xfer_result_t result, uint32_t n)
{
  if (ep == ccid_out)
  {
    if (result == XFER_RESULT_SUCCESS) ccid_message(rhport, n);
    ccid_arm(rhport);
  }
  return true;
}

static const usbd_class_driver_t ccid_driver = {
    "CCID", ccid_init, ccid_deinit, ccid_reset, ccid_open, ccid_control, ccid_xfer, nullptr, nullptr,
};

extern "C" const usbd_class_driver_t *usbd_app_driver_get_cb(uint8_t *count)
{
  *count = 1;
  return &ccid_driver;
}

/* ------------------------------------------------------------ vendor request 0x10 */
extern "C" bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, const tusb_control_request_t *r)
{
  static const char answer[] = "RP2040";
  if (r->bmRequestType_bit.type != TUSB_REQ_TYPE_VENDOR || r->bRequest != 0x10 ||
      r->bmRequestType_bit.direction != TUSB_DIR_IN)
    return false;
  if (stage != CONTROL_STAGE_SETUP) return true;
  return tud_control_xfer(rhport, r, (void *)answer, (uint16_t)min((size_t)r->wLength, sizeof answer - 1));
}

/* ------------------------------------------------------------ the console */
static char line[120];
static size_t used;

static void run(char *cmd)
{
  char *arg = strchr(cmd, ' ');
  if (arg) *arg++ = '\0';
  else arg = cmd + strlen(cmd);
  if (!strcmp(cmd, "?")) say("PD READY %u\n", TinyUSBDevice.mounted() ? 1 : 0);
  else if (!strcmp(cmd, "midisend"))
  {
    unsigned ch = 0, n = 0, v = 0;
    sscanf(arg, "%u %u %u", &ch, &n, &v);
    const uint8_t p[4] = {0x09, (uint8_t)(0x90 | (ch & 15)), (uint8_t)n, (uint8_t)v};
    bool ok;
    {
      UsbLock lock;
      ok = tud_midi_packet_write(p);
    }
    say("PD MIDISENT %u\n", ok ? 1 : 0);
  }
  else if (!strcmp(cmd, "card")) say("PD CARD %lu\n", (unsigned long)apdus);
  else say("PD UNKNOWN %s\n", cmd);
}

void setup()
{
  if (!TinyUSBDevice.isInitialized()) TinyUSBDevice.begin(0);
  TinyUSBDevice.clearConfiguration();   /* without the core's CDC serial, like the S3's configuration */
  TinyUSBDevice.setManufacturerDescriptor("ch32rv-tests");
  TinyUSBDevice.setProductDescriptor("Pico peer classes");
  TinyUSBDevice.setSerialDescriptor("PICOPEERCLS");
  midi.begin();
  vendor.begin();
  ccid.begin();
  /* The core enumerated with its CDC before setup(): again, with these interfaces. */
  if (TinyUSBDevice.mounted())
  {
    TinyUSBDevice.detach();
    delay(10);
    TinyUSBDevice.attach();
  }
  rtt.println("PD BOOTED");
}

void loop()
{
  {
    UsbLock lock;
    uint8_t p[4];
    while (tud_midi_packet_read(p))
    {
      p[2] = (uint8_t)(p[2] + 12);
      tud_midi_packet_write(p);
    }
    uint8_t buf[64];
    while (tud_vendor_available())
    {
      const uint32_t n = tud_vendor_read(buf, sizeof buf);
      tud_vendor_write(buf, n);
      tud_vendor_write_flush();
    }
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
  TinyUSB_Device_Task();   /* what the interrupt skipped while loop() held the lock */
}
