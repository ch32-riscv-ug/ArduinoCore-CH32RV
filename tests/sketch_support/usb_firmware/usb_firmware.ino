/* Firmware update over USB (tests/instrumented/usb,
 * test_usb_firmware_contract.py): a DFU interface in DFU mode
 * (CH32RVUsbDeviceDfu) and a firmware drive (CH32RVUsbDeviceMscFirmwareDisk)
 * beside the default DFU runtime. The host sends this same image back with
 * its generation mark changed, and reads the mark after the restart: a
 * changed mark is an image that was installed and runs.
 *
 *   ready <nonce>   READY <nonce> <build id> <USB started 0/1> <last error>
 *   fw              FW <generation> <staging capacity> <UF2 family ID, hex>
 *                   <drive RAM sectors> <drive blocks> <DFU bStatus> <DFU bState>
 *                   <drive lastError> <DFU errors seen> <bytes the drive took>
 *
 * Restarts on purpose: a DFU download or a copied file that passes its
 * checks installs the image and resets (STARTUP in the log, then the new
 * generation). An image that fails leaves this one running.
 *
 * Writes the whole program flash: build it with CH32RV_BOOT_HOLD_MS
 * (docs/upload-routes.ja.md) on the bench, so a part-written image is still
 * reachable from the probe. */
#include <Arduino.h>
#include <CH32RV.h>
#include <SerialDMSeq.h>
#include <CH32RVUsbDevice.h>
#include <ch32rv_flash.h>
#include <string.h>

#ifndef BENCH_BUILD_ID
#error BENCH_BUILD_ID must identify this compilation
#endif

/* What the host rewrites in the .bin before sending it back: the 16 hex
 * digits after the tag. Read through a volatile pointer, so the build keeps
 * the bytes in flash and reads them at run time. */
extern "C" __attribute__((used)) const char ch32_generation[] = "CH32GEN:0000000000000000";

CH32RVUsbDeviceDfu dfu;
CH32RVUsbDeviceMsc msc;
static uint8_t storage[4 * 1024];   /* the least: V103 ANY has 10 KB of RAM; the host side takes the drive off usb-storage */
CH32RVUsbDeviceMscFirmwareDisk disk(storage, sizeof storage);

static CH32RVUsbDeviceConfig config;
static volatile uint32_t dfuErrors;
static char line[40];
static uint8_t used;

static void print_generation()
{
    const volatile char *g = ch32_generation + 8;
    for (int i = 0; i < 16; i++) {
        SerialDMSeq.print((char)g[i]);
    }
}

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
    } else if (!strcmp(command, "fw")) {
        SerialDMSeq.print("FW ");
        print_generation();
        SerialDMSeq.print(' ');
        SerialDMSeq.print(ch32rv_flash_staging_size());
        SerialDMSeq.print(' ');
        SerialDMSeq.print(CH32RVUsbDeviceMscFirmwareDisk::uf2FamilyId(), HEX);
        SerialDMSeq.print(' ');
        SerialDMSeq.print(disk.ramSectorCount());
        SerialDMSeq.print(' ');
        SerialDMSeq.print(disk.blockCount());
        SerialDMSeq.print(' ');
        SerialDMSeq.print((unsigned)dfu.status());
        SerialDMSeq.print(' ');
        SerialDMSeq.print(dfu.state());
        SerialDMSeq.print(' ');
        SerialDMSeq.print(disk.lastError());
        SerialDMSeq.print(' ');
        SerialDMSeq.print(dfuErrors);
        SerialDMSeq.print(' ');
        SerialDMSeq.println((unsigned long)disk.written());
    }
}

void setup()
{
    SerialDMSeq.begin(115200);
    SerialDMSeq.print("STARTUP ");  // a restart in the middle of a test shows (run_hardware.py)
    SerialDMSeq.println(CH32RV.resetReasonName());
    dfu.onError([](CH32RVUsbDfuStatus) { dfuErrors++; });
    if (disk.begin("FIRMWARE")) {
        disk.addTextFile("README.TXT", "CH32RV usb-firmware " BENCH_BUILD_ID "\r\n");
        msc.vendorID("CH32RV");
        msc.productID("usb-firmware");
        disk.attach(msc);
    }
    config.product = "CH32RV usb-firmware " BENCH_BUILD_ID;
#ifdef USB_TEST_CONTROLLER
    config.controller = USB_TEST_CONTROLLER;   /* run_hardware.py: the controller the record's usb names */
#endif
    USBDevice.begin(config);
}

void loop()
{
    deliverEvents();
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
