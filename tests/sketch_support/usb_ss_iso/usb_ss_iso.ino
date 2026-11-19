/* USBSS isochronous bursts (tests/instrumented/usb/test_usb_ss_iso.py): one
 * vendor interface whose alternate settings each have isochronous endpoints
 * of 1024 bytes, one service interval every 125 us (bInterval 1), and a
 * SuperSpeed companion asking for (bMaxBurst + 1) x (Mult + 1) packets an
 * interval - the companion's Mult > 0 is what the test asks of the
 * controller: whether a whole interval of more than one burst goes out and
 * comes in.
 *
 *   alt  bMaxBurst  Mult  packets  endpoints
 *    1       1        0      2     IN, OUT    (one burst: the baseline)
 *    2       1        1      4     IN, OUT
 *    3       1        2      6     IN, OUT
 *    4      15        2     48     IN         (the most USB 3.2 has; 393 MB/s)
 *    5      15        2     48     OUT
 *
 * Content each side checks: packet k of the interval numbered s starts with s
 * (32 bits, little endian) and k (16 bits), and its last byte is (s + k) &
 * 0xFF. IN: the DUT numbers the transfers it submits (one an interval) and
 * writes those 7 bytes a packet; the rest is left as it is. OUT: the DUT
 * checks each transfer that ends (one an interval) the same way.
 *
 * Beside it, a vendor bulk interface (CH32RVUsbDeviceVendor, subclass / protocol
 * 0x56 / 0x42) for the endpoint halt (usb-ss-iso's test_usb_vendor_halt): it
 * echoes what bulk OUT brings back on bulk IN, and two vendor requests to the
 * interface (wIndex its number): 0x51 stalls the endpoint wValue names
 * (CH32RVUsbDevice::stall()), 0x52 answers 1 byte, whether it is halted
 * (stalled()).
 *
 *   ready <nonce>  READY <nonce> <build id> <USB started 0/1> <last error>
 *   iso            ISO <alt> <packets an interval> <IN buffers>
 *                      <IN submitted> <IN refused>
 *                      <OUT intervals> <OUT whole> <OUT short> <OUT bad>
 *                      <IN: the longest gap between two completions, us>
 *                      <IN: gaps over 1.5 intervals (187 us)>
 *                      <IN: the longest from a completion to the next submit, us>
 *                  (counts since the last SET_INTERFACE; OUT short: fewer
 *                  bytes than the interval's packets, bad: a header or last
 *                  byte out of place) */
#include <Arduino.h>
#include <CH32RV.h>
#include <SerialDMSeq.h>
#include <CH32RVUsbDevice.h>
#include <stdlib.h>
#include <string.h>

#ifndef BENCH_BUILD_ID
#error BENCH_BUILD_ID must identify this compilation
#endif

struct Alt {
    uint8_t maxBurst, mult;
    bool in, out;
};
static const Alt ALTS[] = {
    {0, 0, false, false},
    {1, 0, true, true},
    {1, 1, true, true},
    {1, 2, true, true},
    {15, 2, true, false},
    {15, 2, false, true},
};
static const uint8_t ALT_COUNT = sizeof ALTS / sizeof ALTS[0];

class IsoBurst;
static bool iso_descriptor(const CH32RVUsbDeviceFunction &self, CH32RVUsbDescriptorWriter &w, uint8_t itf, uint8_t *ep,
                           uint8_t str, CH32RVUsbSpeed speed);
static const CH32RVUsbFunctionType iso_type = {iso_descriptor, 1, 1, nullptr, 0};

class IsoBurst : public CH32RVUsbDeviceFunction {
public:
    IsoBurst() : CH32RVUsbDeviceFunction(USBDevice, iso_type, "ISO burst") {}

    uint8_t ep = 0;
    CH32RVUsbSpeed speed = CH32RVUsbSpeed::Full;
    uint8_t alt = 0;
    uint16_t mps = 0, packets = 0;
    uint8_t *inBuf[2] = {nullptr, nullptr};
    uint8_t inBuffers = 0, inNext = 0;
    uint8_t *outBuf = nullptr;
    bool inBusy = false, outBusy = false;
    uint32_t inSerial = 0;
    uint32_t inSubmitted = 0, inRefused = 0;
    uint32_t inLastDone = 0, inGapMax = 0, inGapsLong = 0, inResubmitMax = 0;
    uint32_t outIntervals = 0, outWhole = 0, outShort = 0, outBad = 0;

    /* Packets an interval in this speed's form: SuperSpeed's companion, one
     * below it. */
    uint16_t perInterval(uint8_t a) const
    {
        return speed == CH32RVUsbSpeed::Super ? (uint16_t)((ALTS[a].maxBurst + 1u) * (ALTS[a].mult + 1u)) : 1u;
    }

    void release()
    {
        free(inBuf[0]);
        free(inBuf[1]);
        free(outBuf);
        inBuf[0] = inBuf[1] = outBuf = nullptr;
        inBuffers = 0;
        inBusy = outBusy = false;
    }

    void sendNext()
    {
        if (!inBuffers || inBusy) {
            return;
        }
        uint8_t *b = inBuf[inNext];
        const uint32_t s = inSerial;
        for (uint16_t k = 0; k < packets; k++) {
            uint8_t *p = b + (uint32_t)k * mps;
            memcpy(p, &s, 4);
            memcpy(p + 4, &k, 2);
            p[mps - 1] = (uint8_t)(s + k);
        }
        if (device().transfer((uint8_t)(0x80 | ep), b, (uint16_t)(packets * mps))) {
            inBusy = true;
            inSerial++;
            inSubmitted++;
            inNext = (uint8_t)((inNext + 1u) % inBuffers);
        } else {
            inRefused++;
        }
    }

    void receiveNext()
    {
        if (!outBuf || outBusy) {
            return;
        }
        if (device().transfer(ep, outBuf, (uint16_t)(packets * mps))) {
            outBusy = true;
        }
    }

    void checkOut(uint16_t length)
    {
        outIntervals++;
        if (length != packets * mps) {
            outShort++;
            return;
        }
        uint32_t s;
        memcpy(&s, outBuf, 4);
        for (uint16_t k = 0; k < packets; k++) {
            const uint8_t *p = outBuf + (uint32_t)k * mps;
            uint32_t ps;
            uint16_t pk;
            memcpy(&ps, p, 4);
            memcpy(&pk, p + 4, 2);
            if (ps != s || pk != k || p[mps - 1] != (uint8_t)(s + k)) {
                outBad++;
                return;
            }
        }
        outWhole++;
    }

protected:
    void usbReset() override
    {
        alt = 0;
        release();
    }

    void usbConfigured(CH32RVUsbSpeed s) override
    {
        speed = s;
        alt = 0;
        release();
    }

    bool usbSetInterface(uint8_t, uint8_t a) override
    {
        if (a >= ALT_COUNT) {
            return false;
        }
        release();
        alt = a;
        inSubmitted = inRefused = 0;
        inLastDone = inGapMax = inGapsLong = inResubmitMax = 0;
        outIntervals = outWhole = outShort = outBad = 0;
        if (!a) {
            return true;
        }
        mps = speed == CH32RVUsbSpeed::Super ? 1024 : speed == CH32RVUsbSpeed::High ? 512 : 64;
        packets = perInterval(a);
        const size_t bytes = (size_t)packets * mps;
        if (ALTS[a].in) {
            /* Two buffers, one being read by the DMA while the other is
             * numbered (the controller may report an isochronous IN done
             * before its DMA has read it, TODO 2026-11-14); one when the RAM
             * holds no more (CH32X315's 64 KB at 48 KB an interval). */
            for (inBuffers = 0; inBuffers < 2; inBuffers++) {
                inBuf[inBuffers] = (uint8_t *)malloc(bytes);
                if (!inBuf[inBuffers]) {
                    break;
                }
                memset(inBuf[inBuffers], 0xA5, bytes);
            }
            inNext = 0;
            sendNext();
        }
        if (ALTS[a].out) {
            outBuf = (uint8_t *)malloc(bytes);
            receiveNext();
        }
        return true;
    }

    void usbTransferred(uint8_t address, uint16_t length) override
    {
        if (address & 0x80) {
            const uint32_t now = micros();
            if (inLastDone) {
                const uint32_t gap = now - inLastDone;
                inGapMax = gap > inGapMax ? gap : inGapMax;
                inGapsLong += gap > 187u;
            }
            inLastDone = now;
            inBusy = false;
            sendNext();
            const uint32_t took = micros() - now;
            inResubmitMax = took > inResubmitMax ? took : inResubmitMax;
        } else {
            outBusy = false;
            if (outBuf) {
                checkOut(length);
            }
            receiveNext();
        }
    }

    void usbPoll() override
    {
        sendNext();          /* a refused submission is tried again */
        receiveNext();
    }
};

static bool iso_descriptor(const CH32RVUsbDeviceFunction &self, CH32RVUsbDescriptorWriter &w, uint8_t itf, uint8_t *ep,
                           uint8_t str, CH32RVUsbSpeed speed)
{
    IsoBurst &f = const_cast<IsoBurst &>(static_cast<const IsoBurst &>(self));
    const uint8_t n = (*ep)++;
    f.ep = n;
    const uint16_t mps = speed == CH32RVUsbSpeed::Super ? 1024 : speed == CH32RVUsbSpeed::High ? 512 : 64;
    const uint8_t alt0[] = {9, 4, itf, 0, 0, 0xFF, 0x49, 0x53, str};
    w.put(alt0, sizeof alt0);
    for (uint8_t a = 1; a < ALT_COUNT; a++) {
        const Alt &s = ALTS[a];
        const uint8_t head[] = {9, 4, itf, a, (uint8_t)(s.in + s.out), 0xFF, 0x49, 0x53, str};
        w.put(head, sizeof head);
        /* Isochronous, asynchronous, data; every (micro)frame / interval. */
        if (s.in) {
            w.superSpeedBurst(s.maxBurst, s.mult);
            const uint8_t d[] = {7, 5, (uint8_t)(0x80 | n), 0x05, (uint8_t)mps, (uint8_t)(mps >> 8), 1};
            w.put(d, sizeof d);
        }
        if (s.out) {
            w.superSpeedBurst(s.maxBurst, s.mult);
            const uint8_t d[] = {7, 5, n, 0x05, (uint8_t)mps, (uint8_t)(mps >> 8), 1};
            w.put(d, sizeof d);
        }
    }
    return true;
}

static IsoBurst iso;
static CH32RVUsbDeviceVendor bulk(USBDevice, 64);
static CH32RVUsbDeviceConfig config;
static char line[80];
static uint8_t used;

static void reply(const char *command)
{
    if (strncmp(command, "ready ", 6) == 0) {
        SerialDMSeq.print("READY ");
        SerialDMSeq.print(command + 6);
        SerialDMSeq.print(' ');
        SerialDMSeq.print(BENCH_BUILD_ID);
        SerialDMSeq.print(' ');
        SerialDMSeq.print(USBDevice.started() ? 1 : 0);
        SerialDMSeq.print(' ');
        SerialDMSeq.println(USBDevice.lastErrorName());
    } else if (strcmp(command, "iso") == 0) {
        SerialDMSeq.printf("ISO %u %u %u %lu %lu %lu %lu %lu %lu %lu %lu %lu\n", iso.alt, iso.alt ? iso.packets : 0,
                           iso.inBuffers, (unsigned long)iso.inSubmitted, (unsigned long)iso.inRefused,
                           (unsigned long)iso.outIntervals, (unsigned long)iso.outWhole,
                           (unsigned long)iso.outShort, (unsigned long)iso.outBad, (unsigned long)iso.inGapMax,
                           (unsigned long)iso.inGapsLong, (unsigned long)iso.inResubmitMax);
    } else {
        SerialDMSeq.println("ERROR COMMAND");
    }
}

void setup()
{
    SerialDMSeq.begin(115200);
    SerialDMSeq.print("STARTUP ");  // a restart in the middle of a test shows (run_hardware.py)
    SerialDMSeq.println(CH32RV.resetReasonName());
    config.product = "CH32RV usb-ss-iso " BENCH_BUILD_ID;
    config.selfPowered = true;
#ifdef USB_TEST_CONTROLLER
    config.controller = USB_TEST_CONTROLLER;   /* run_hardware.py: the controller the record's usb names */
#endif
    bulk.setInterfaceProtocol(0x56, 0x42);
    bulk.onControlRequest([](const CH32RVUsbDeviceVendorControlRequest &r) {
        if (r.stage != CH32RV_USB_DEVICE_CONTROL_STAGE_SETUP) {
            return true;
        }
        if (r.bRequest == 0x51 && !(r.bmRequestType & 0x80)) {
            USBDevice.stall((uint8_t)r.wValue);
            return bulk.sendControlResponse(r);
        }
        if (r.bRequest == 0x52 && (r.bmRequestType & 0x80)) {
            static uint8_t halted;
            halted = USBDevice.stalled((uint8_t)r.wValue) ? 1 : 0;
            return bulk.sendControlResponse(r, &halted, 1);
        }
        return false;
    });
    USBDevice.begin(config);
}

void loop()
{
    uint8_t echo[256];
    const size_t room = (size_t)bulk.availableForWrite();
    if (room && bulk.available()) {
        bulk.write(echo, bulk.read(echo, room < sizeof echo ? room : sizeof echo));
        bulk.flush();
    }
    while (SerialDMSeq.available()) {
        const int c = SerialDMSeq.read();
        if (c == '\n') {
            line[used] = 0;
            if (used) {
                reply(line);
            }
            used = 0;
        } else if (c != '\r' && used < sizeof line - 1) {
            line[used++] = (char)c;
        }
    }
}
