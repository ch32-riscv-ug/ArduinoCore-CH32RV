/* Update: replace the running sketch with a new image (arduino-esp32's
 * Update, the same names and error codes).
 *
 *     if (!Update.begin(imageSize)) { Update.printError(Serial); return; }
 *     Update.write(chunk, n);           // in order, as often as needed
 *     if (Update.end()) {
 *         CH32RV.restart();              // or Update.restart(): installs it
 *     }
 *
 * Where the image goes: the chips boot the code flash at 0 and have no second
 * slot to boot from, so the image is staged in the flash after the sketch
 * (up to any CH32RV_FLASH_STORE area, which is kept) and copied over the
 * sketch at the restart, from RAM, with interrupts off. The engine is the
 * core's (ch32rv_update.h, which says what is checked); this is its
 * arduino-esp32 face. capacity() is the largest image this sketch can take:
 * about the flash less the sketch, so a sketch that receives updates should
 * stay under half the flash.
 *
 * A failed or abandoned update leaves the sketch as it was: nothing touches
 * the program until the restart, and a reset of any other kind (power, the
 * watchdog, NRST) forgets the staged image. Losing power during the copy
 * itself (about a second for 64 KB) leaves a part-written sketch; the
 * factory bootloader and the debug interface still reach the chip.
 *
 * Not here (arduino-esp32 has them): setMD5() / md5String(), U_SPIFFS and the
 * other commands, rollback (canRollBack() is false: no previous image is
 * kept). Added: capacity(), beginRandomAccess() / writeAt() (UF2), crc32(),
 * isPending() / cancelPending(), restart(). */
#pragma once

#include <Arduino.h>
#include <CH32RVCallback.h>
#include <ch32rv_flash.h>
#include <ch32rv_update.h>

#define UPDATE_ERROR_OK           (0)
#define UPDATE_ERROR_WRITE        (1)
#define UPDATE_ERROR_ERASE        (2)
#define UPDATE_ERROR_READ         (3)
#define UPDATE_ERROR_SPACE        (4)
#define UPDATE_ERROR_SIZE         (5)
#define UPDATE_ERROR_STREAM       (6)
#define UPDATE_ERROR_MD5          (7)
#define UPDATE_ERROR_MAGIC_BYTE   (8)
#define UPDATE_ERROR_ACTIVATE     (9)
#define UPDATE_ERROR_NO_PARTITION (10)
#define UPDATE_ERROR_BAD_ARGUMENT (11)
#define UPDATE_ERROR_ABORT        (12)
/* This core's: the staged image does not read back as what was written. */
#define UPDATE_ERROR_VERIFY       (20)

#define UPDATE_SIZE_UNKNOWN 0xFFFFFFFF

#define U_FLASH 0

class UpdateClass {
public:
    /* (bytes written so far, the image size) after each write. */
    typedef CH32RVCallback<void(size_t, size_t)> THandlerFunction_Progress;

    constexpr UpdateClass() {}

    UpdateClass &onProgress(THandlerFunction_Progress fn)
    {
        _onProgress = fn;
        return *this;
    }

    /* Start an update of `size` bytes, written in order. UPDATE_SIZE_UNKNOWN:
     * up to capacity(), and end(true) takes what came. command: U_FLASH only
     * (anything else is UPDATE_ERROR_BAD_ARGUMENT). ledPin, when >= 0, is
     * driven to ledOn while the update runs. label: no partitions here;
     * ignored. */
    bool begin(size_t size = UPDATE_SIZE_UNKNOWN, int command = U_FLASH, int ledPin = -1, uint8_t ledOn = LOW,
               const char *label = NULL);
    /* Start an update whose pieces may come in any order (writeAt()): the
     * whole size is erased now. What UF2 needs. */
    bool beginRandomAccess(size_t size);

    /* The next bytes, in order. Returns len, or 0 on an error (getError()). */
    size_t write(uint8_t *data, size_t len);
    /* Read the image from a stream until size() bytes came; a stream that
     * stays silent for its timeout is UPDATE_ERROR_STREAM. */
    size_t writeStream(Stream &data);
    /* After beginRandomAccess(): `len` bytes at `offset` of the image, both
     * multiples of CH32RV_FLASH_WRITE_SIZE (64 to 256 bytes by series; a UF2
     * block's 256 always is). The same bytes again are accepted; different
     * bytes over written ones are UPDATE_ERROR_WRITE. The caller knows when
     * all pieces came (end() cannot). */
    bool writeAt(size_t offset, const uint8_t *data, size_t len);

    /* Check the staged image and make it the one restart() installs.
     * evenIfRemaining: end here even if fewer than begin()'s size came. */
    bool end(bool evenIfRemaining = false);
    void abort();

    void printError(Print &out);
    const char *errorString();
    uint8_t getError();
    void clearError();
    bool hasError() { return getError() != UPDATE_ERROR_OK; }
    bool isRunning();
    bool isFinished() { return isRunning() && progress() == size(); }
    size_t size();
    size_t progress();
    size_t remaining() { return size() - progress(); }

    /* No previous image is kept: false. */
    bool canRollBack() { return false; }
    bool rollBack() { return false; }

    /* The largest image this sketch can stage. */
    static size_t capacity();
    /* end() succeeded: restart() installs the image. */
    bool isPending();
    /* Forget a successful end(): restart() is a plain reset again. */
    void cancelPending();
    /* The staged image's CRC-32 (ch32rv_crc32()), once end() succeeded. */
    uint32_t crc32();
    /* Install the image end() accepted and reset into it - or, with none
     * pending, just reset. CH32RV.restart() does the same in a sketch with
     * this library. End a USB device first (USBDevice.end()): during the
     * copy it does not answer. */
    [[noreturn]] void restart();

private:
    void led(bool on);
    THandlerFunction_Progress _onProgress = nullptr;
    int8_t _ledPin = -1;
    uint8_t _ledOn = LOW;
    uint8_t _error = UPDATE_ERROR_OK;     /* this wrapper's own (BAD_ARGUMENT, STREAM) */
};

extern UpdateClass Update;
