/* USB suspend, resume and remote wakeup: the device side of a peer test to
 * come (TODO: a CH32RVUsbHost peer that suspends the bus; the PC case was
 * dropped - only real transfers go against the PC). The DUT is a CDC port
 * that declares remote wakeup; the host suspends it, resumes it, or is woken
 * by the DUT.
 *
 *   ready <nonce>   READY <nonce> <build id> <USB started 0/1> <last error>
 *   pm              PM <Suspended events> <Resumed events> <suspended() 0/1>
 *                   <remoteWakeupEnabled() 0/1> <Configured events>
 *   wake            WAKE <remoteWakeup() 0/1> <ms waited for the host to allow it>:
 *                   waits up to 5 s for the bus to be suspended with remote
 *                   wakeup enabled, then signals resume once
 *
 * The CDC port echoes what the host writes: the host's write is what resumes
 * a suspended device from the host side. The counts come from onBusEvent(),
 * delivered by deliverEvents() in loop(). */
#include <Arduino.h>
#include <CH32RV.h>
#include <SerialDMSeq.h>
#include <CH32RVUsbDevice.h>
#include <string.h>

#ifndef BENCH_BUILD_ID
#error BENCH_BUILD_ID must identify this compilation
#endif

static CH32RVUsbDeviceConfig config;
static uint32_t suspends, resumes, configured;
static char line[40];
static uint8_t used;

static void reply(const char *command)
{
    if (!strncmp(command, "ready ", 6)) {
        SerialDMSeq.print("READY ");
        SerialDMSeq.print(command + 6);
        SerialDMSeq.print(' ');
        SerialDMSeq.print(BENCH_BUILD_ID);
        SerialDMSeq.print(' ');
        SerialDMSeq.print(USBDevice.started() ? 1 : 0);
        SerialDMSeq.print(' ');
        SerialDMSeq.println(USBDevice.lastErrorName());
    } else if (!strcmp(command, "pm")) {
        deliverEvents();
        SerialDMSeq.print("PM ");
        SerialDMSeq.print(suspends);
        SerialDMSeq.print(' ');
        SerialDMSeq.print(resumes);
        SerialDMSeq.print(' ');
        SerialDMSeq.print(USBDevice.suspended() ? 1 : 0);
        SerialDMSeq.print(' ');
        SerialDMSeq.print(USBDevice.remoteWakeupEnabled() ? 1 : 0);
        SerialDMSeq.print(' ');
        SerialDMSeq.println(configured);
    } else if (!strcmp(command, "wake")) {
        const uint32_t t0 = millis();
        while (!(USBDevice.suspended() && USBDevice.remoteWakeupEnabled()) && millis() - t0 < 5000) {
            delay(1);
        }
        const uint32_t waited = millis() - t0;
        const bool ok = USBDevice.remoteWakeup();
        SerialDMSeq.print("WAKE ");
        SerialDMSeq.print(ok ? 1 : 0);
        SerialDMSeq.print(' ');
        SerialDMSeq.println(waited);
    }
}

void setup()
{
    SerialDMSeq.begin(115200);
    SerialDMSeq.print("STARTUP ");  // a restart in the middle of a test shows (run_hardware.py)
    SerialDMSeq.println(CH32RV.resetReasonName());
    config.product = "CH32RV usb-power " BENCH_BUILD_ID;
    config.remoteWakeupSupported = true;
#ifdef USB_TEST_CONTROLLER
    config.controller = USB_TEST_CONTROLLER;   /* run_hardware.py: the controller the record's usb names */
#endif
    USBDevice.onBusEvent([](CH32RVUsbBusEvent e) {
        if (e == CH32RVUsbBusEvent::Suspended) {
            suspends++;
        } else if (e == CH32RVUsbBusEvent::Resumed) {
            resumes++;
        } else if (e == CH32RVUsbBusEvent::Configured) {
            configured++;
        }
    });
    SerialUSB.begin(115200);
    USBDevice.begin(config);
}

void loop()
{
    deliverEvents();
    while (SerialUSB.available()) {
        SerialUSB.write(SerialUSB.read());
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
