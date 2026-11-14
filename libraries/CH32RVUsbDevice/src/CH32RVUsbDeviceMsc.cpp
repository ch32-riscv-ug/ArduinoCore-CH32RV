/* USB mass storage on the device stack: bulk-only transport and the SCSI
 * commands a host uses with a disk, for up to 8 logical units (one
 * CH32RVUsbDeviceMsc each, on one interface). */
#include "internal/CH32RVUsbDeviceInternal.h"

#include <string.h>

#define MSC_MAX_LUNS 8

namespace {
/* The bulk-only transport, the interface's: one command at a time, for the
 * unit it addresses. */
enum class Stage : uint8_t { Command, DataIn, DataOut, Status, Stalled, Reset };
struct Bot {
    CH32RVUsbDeviceMsc *first;       /* LUN 0, the interface's function; the others follow its list */
    CH32RVUsbDeviceMsc *unit;        /* the unit the command in hand addresses; nullptr: none by that number */
    Stage stage;
    bool in, failed;
    uint16_t packet, chunk;
    uint32_t tag, expected, residue, lba, total, done;
    uint8_t csw[13];
    uint8_t cmd[16];
    /* One block, or a command and its answer; a whole packet at any speed. */
    uint8_t buffer[CH32RV_USB_MAX_PACKET > 512 ? CH32RV_USB_MAX_PACKET : 512] __attribute__((aligned(4)));
} bot;                               /* zero: no unit yet, Stage::Command; packet set when configured */
}  // namespace

static void copy_padded(uint8_t *dst, size_t size, const char *value)
{
    memset(dst, 0, size);
    if (value) {
        size_t n = strlen(value);
        memcpy(dst, value, n > size ? size : n);
    }
}

void CH32RVUsbDeviceMsc::vendorID(const char *value)
{
    copy_padded((uint8_t *)_vendor, sizeof _vendor - 1, value);
}

void CH32RVUsbDeviceMsc::productID(const char *value)
{
    copy_padded((uint8_t *)_product, sizeof _product - 1, value);
}

void CH32RVUsbDeviceMsc::productRevision(const char *value)
{
    copy_padded((uint8_t *)_revision, sizeof _revision - 1, value);
}

bool CH32RVUsbDeviceMsc::begin(uint32_t blockCount, uint16_t blockSize)
{
    _blockCount = blockCount;
    _blockSize = blockSize;
    return begin();
}

void CH32RVUsbDeviceMsc::end()
{
    _mediaPresent = _attention = false;
    _senseKey = _asc = _ascq = 0;
}

void CH32RVUsbDeviceMsc::mediaPresent(bool value)
{
    if (value && !_mediaPresent && device().ready()) {
        _attention = true;
    }
    if (!value && _mediaPresent) {
        _removals++;
    }
    _mediaPresent = value;
}

void CH32RVUsbDeviceMsc::inquiry(uint8_t vendor[8], uint8_t product[16], uint8_t revision[4]) const
{
    copy_padded(vendor, 8, _vendor);
    copy_padded(product, 16, _product);
    copy_padded(revision, 4, _revision);
}

void CH32RVUsbDeviceMsc::capacity(uint32_t *blockCount, uint16_t *blockSize) const
{
    *blockCount = _mediaPresent ? _blockCount : 0;
    *blockSize = _mediaPresent ? _blockSize : 0;
}

bool CH32RVUsbDeviceMsc::startStop(uint8_t powerCondition, bool start, bool loadEject)
{
    bool ok = true;
    if (_storage) {
        ok = _storage->startStop(powerCondition, start, loadEject);
    }
    if (_onStartStop) {
        ok = _onStartStop(powerCondition, start, loadEject) && ok;
    }
    return ok;
}

int32_t CH32RVUsbDeviceMsc::read10(uint32_t lba, uint32_t offset, void *buffer, uint32_t size)
{
    if (!_mediaPresent) {
        return -1;
    }
    _ioSense = false;
    if (_onRead) {
        return _onRead(lba, offset, buffer, size);
    }
    return _storage ? _storage->read(lba, offset, buffer, size) : -1;
}

int32_t CH32RVUsbDeviceMsc::write10(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t size)
{
    if (!_mediaPresent || !writable()) {
        return -1;
    }
    _ioSense = false;
    if (_onWrite) {
        return _onWrite(lba, offset, buffer, size);
    }
    return _storage ? _storage->write(lba, offset, buffer, size) : -1;
}

static bool msc_descriptor(const CH32RVUsbDeviceFunction &self, CH32RVUsbDescriptorWriter &w, uint8_t itf, uint8_t *ep,
                               uint8_t str, CH32RVUsbSpeed speed)
{
    CH32RVUsbDeviceMsc &m = const_cast<CH32RVUsbDeviceMsc &>(static_cast<const CH32RVUsbDeviceMsc &>(self));
    const uint8_t n = (*ep)++;
    m._ep = n;
    const uint16_t mps = ch32rv_usb_bulk_size(speed);
    const uint8_t d[] = {
        9, 4, itf, 0, 2, 0x08, 0x06, 0x50, str,             /* mass storage, SCSI, bulk-only */
        7, 5, n, 2, (uint8_t)mps, (uint8_t)(mps >> 8), 0,
        7, 5, (uint8_t)(0x80 | n), 2, (uint8_t)mps, (uint8_t)(mps >> 8), 0,
    };
    w.put(d, sizeof d);
    return true;
}

/* LUN 1 on: a unit of LUN 0's interface, with nothing of its own to describe. */
static bool msc_lun_descriptor(const CH32RVUsbDeviceFunction &, CH32RVUsbDescriptorWriter &, uint8_t, uint8_t *,
                               uint8_t, CH32RVUsbSpeed)
{
    return true;
}

static const CH32RVUsbFunctionType msc_type = {msc_descriptor, 1, 1, nullptr, 0};
static const CH32RVUsbFunctionType msc_lun_type = {msc_lun_descriptor, 0, MSC_MAX_LUNS - 1, nullptr, 0};

CH32RVUsbDeviceMsc::CH32RVUsbDeviceMsc(CH32RVUsbDevice &device)
    : CH32RVUsbDeviceFunction(device, bot.first ? msc_lun_type : msc_type, CH32RV_USB_MSC_NAME)
{
    if (!attached()) {
        return;
    }
    if (!bot.first) {
        bot.first = this;
        return;
    }
    CH32RVUsbDeviceMsc *u = bot.first;
    while (u->_nextLun) {
        u = u->_nextLun;
    }
    u->_nextLun = this;
    _lun = (uint8_t)(u->_lun + 1);
}

uint8_t CH32RVUsbDeviceMsc::maxLun() const
{
    uint8_t n = 0;
    for (const CH32RVUsbDeviceMsc *u = bot.first; u && u->_nextLun; u = u->_nextLun) {
        n++;
    }
    return n;
}

void CH32RVUsbDeviceMsc::setSense(uint8_t senseKey, uint8_t asc, uint8_t ascq)
{
    _ioKey = senseKey;
    _ioAsc = asc;
    _ioAscq = ascq;
    _ioSense = true;
}

#define CBW_SIGNATURE 0x43425355u
#define CSW_SIGNATURE 0x53425355u

#define SCSI_TEST_UNIT_READY          0x00
#define SCSI_REQUEST_SENSE            0x03
#define SCSI_INQUIRY                  0x12
#define SCSI_MODE_SENSE_6             0x1A
#define SCSI_START_STOP_UNIT          0x1B
#define SCSI_PREVENT_ALLOW            0x1E
#define SCSI_READ_FORMAT_CAPACITIES   0x23
#define SCSI_READ_CAPACITY_10         0x25
#define SCSI_READ_10                  0x28
#define SCSI_WRITE_10                 0x2A
#define SCSI_VERIFY_10                0x2F
#define SCSI_SYNCHRONIZE_CACHE_10     0x35
#define SCSI_MODE_SENSE_10            0x5A

#define SENSE_NOT_READY       0x02
#define SENSE_MEDIUM_ERROR    0x03
#define SENSE_ILLEGAL_REQUEST 0x05
#define SENSE_UNIT_ATTENTION  0x06
#define SENSE_DATA_PROTECT    0x07

static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static void put_be32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

/* ---------------------------------------------------------- the stack's side
 * The hooks reach every unit (each is a function of the device); the
 * transport is LUN 0's to run. */
void CH32RVUsbDeviceMsc::usbReset()
{
    if (this == bot.first) {
        bot.stage = Stage::Command;
        bot.unit = nullptr;
    }
    _attention = false;                          /* the host starts afresh (a reset recovery keeps it) */
    _senseKey = _asc = _ascq = 0;
}

void CH32RVUsbDeviceMsc::usbConfigured(CH32RVUsbSpeed speed)
{
    if (this != bot.first) {
        return;
    }
    bot.packet = ch32rv_usb_bulk_size(speed);
    receiveCommand();
}

void CH32RVUsbDeviceMsc::receiveCommand()
{
    bot.stage = Stage::Command;
    if (!device().transferBusy(_ep)) {
        device().transfer(_ep, bot.buffer, sizeof bot.buffer);   /* a whole packet fits, whatever the host sends */
    }
}

bool CH32RVUsbDeviceMsc::usbControl(CH32RVUsbControlStage stage, const CH32RVUsbSetup &s)
{
    if ((s.bmRequestType & 0x60) != 0x20) {
        return false;
    }
    switch (s.bRequest) {
    case 0xFF:                                   /* bulk-only mass storage reset */
        if (stage == CH32RVUsbControlStage::Setup) {
            return device().controlStatus();
        }
        if (stage == CH32RVUsbControlStage::Ack) {
            bot.stage = Stage::Command;          /* the host clears both halts next */
            if (!device().stalled(_ep)) {
                receiveCommand();
            }
        }
        return true;
    case 0xFE: {                                 /* GET_MAX_LUN: the units, less one */
        static uint8_t last;
        last = maxLun();
        return stage != CH32RVUsbControlStage::Setup || device().controlIn(&last, 1);
    }
    }
    return false;
}

void CH32RVUsbDeviceMsc::usbHaltCleared(uint8_t address)
{
    if (bot.stage == Stage::Stalled) {
        status();                                /* the data stage ended early: now the status */
    } else if (bot.stage == Stage::Command && address == _ep) {
        receiveCommand();
    }
}

/* Leave the command failed: the sense data says why (on the unit it
 * addressed); the data stage the host expects is stalled, and the status
 * follows once it clears the halt. */
void CH32RVUsbDeviceMsc::fail(uint8_t key, uint8_t asc, uint8_t ascq)
{
    if (bot.unit) {
        bot.unit->_senseKey = key;
        bot.unit->_asc = asc;
        bot.unit->_ascq = ascq;
    }
    bot.failed = true;
    if (bot.residue) {
        bot.stage = Stage::Stalled;
        device().stall(bot.in ? (uint8_t)(0x80 | _ep) : _ep);
    } else {
        status();
    }
}

void CH32RVUsbDeviceMsc::failIo(uint8_t key, uint8_t asc, uint8_t ascq)
{
    CH32RVUsbDeviceMsc &u = *bot.unit;
    if (u._ioSense) {
        u._ioSense = false;
        fail(u._ioKey, u._ioAsc, u._ioAscq);
    } else {
        fail(key, asc, ascq);
    }
}

void CH32RVUsbDeviceMsc::status()
{
    const uint8_t csw[13] = {
        (uint8_t)CSW_SIGNATURE, (uint8_t)(CSW_SIGNATURE >> 8), (uint8_t)(CSW_SIGNATURE >> 16), (uint8_t)(CSW_SIGNATURE >> 24),
        (uint8_t)bot.tag, (uint8_t)(bot.tag >> 8), (uint8_t)(bot.tag >> 16), (uint8_t)(bot.tag >> 24),
        (uint8_t)bot.residue, (uint8_t)(bot.residue >> 8), (uint8_t)(bot.residue >> 16), (uint8_t)(bot.residue >> 24),
        (uint8_t)(bot.failed ? 1 : 0),
    };
    memcpy(bot.csw, csw, sizeof csw);
    bot.stage = Stage::Status;
    device().transfer((uint8_t)(0x80 | _ep), bot.csw, sizeof bot.csw);
}

/* An answer prepared in the buffer: at most what the host asked for. A
 * shorter answer ending on a full packet is followed by a stalled IN, so the
 * host sees where it ends (bulk-only transport 6.7.2). */
void CH32RVUsbDeviceMsc::sendData(uint16_t length)
{
    if (bot.expected == 0) {
        bot.failed = length != 0;                /* an answer the host did not ask for */
        status();
        return;
    }
    if (!bot.in) {
        bot.stage = Stage::Stalled;              /* the host sends data no command takes */
        device().stall(_ep);
        return;
    }
    const uint16_t n = length < bot.expected ? length : (uint16_t)bot.expected;
    bot.residue = bot.expected - n;
    bot.total = n;
    bot.done = 0;
    bot.stage = Stage::DataIn;
    bot.chunk = n;
    device().transfer((uint8_t)(0x80 | _ep), bot.buffer, n);
}

void CH32RVUsbDeviceMsc::readNext()
{
    CH32RVUsbDeviceMsc &u = *bot.unit;
    const uint32_t left = bot.total - bot.done;
    bot.chunk = (uint16_t)(left < sizeof bot.buffer ? left : sizeof bot.buffer);
    if (!u.mediumHeld()) {
        bot.residue = bot.expected - bot.done;
        fail(SENSE_NOT_READY, 0x3A, 0x00);       /* taken out during the command */
        return;
    }
    const int32_t n = u.read10(bot.lba + bot.done / u._blockSize, bot.done % u._blockSize, bot.buffer, bot.chunk);
    if (n != (int32_t)bot.chunk) {
        bot.residue = bot.expected - bot.done;
        failIo(SENSE_MEDIUM_ERROR, 0x11, 0x00);  /* unrecovered read error */
        return;
    }
    device().transfer((uint8_t)(0x80 | _ep), bot.buffer, bot.chunk);
}

void CH32RVUsbDeviceMsc::writeNext()
{
    const uint32_t left = bot.total - bot.done;
    bot.chunk = (uint16_t)(left < sizeof bot.buffer ? left : sizeof bot.buffer);
    device().transfer(_ep, bot.buffer, bot.chunk);
}

/* The command block in bot.cmd, for the unit in bot.unit. */
void CH32RVUsbDeviceMsc::command()
{
    const uint8_t op = bot.cmd[0];
    uint8_t *const buf = bot.buffer;
    if (!bot.unit) {
        /* No unit by that number (SPC-4: LOGICAL UNIT NOT SUPPORTED). */
        if (op == SCSI_REQUEST_SENSE) {
            memset(buf, 0, 18);
            buf[0] = 0x70;
            buf[2] = SENSE_ILLEGAL_REQUEST;
            buf[7] = 10;
            buf[12] = 0x25;
            sendData(18);
        } else {
            fail(SENSE_ILLEGAL_REQUEST, 0x25, 0x00);
        }
        return;
    }
    CH32RVUsbDeviceMsc &u = *bot.unit;
    if (u._attention && op != SCSI_INQUIRY && op != SCSI_REQUEST_SENSE) {
        u._attention = false;
        fail(SENSE_UNIT_ATTENTION, 0x28, 0x00);  /* not ready to ready change, medium may have changed */
        return;
    }
    switch (op) {
    case SCSI_TEST_UNIT_READY:
        if (!u.testUnitReady()) {
            fail(SENSE_NOT_READY, 0x3A, 0x00);   /* medium not present */
            return;
        }
        sendData(0);
        return;
    case SCSI_REQUEST_SENSE: {
        memset(buf, 0, 18);
        buf[0] = 0x70;
        buf[2] = u._senseKey;
        buf[7] = 10;
        buf[12] = u._asc;
        buf[13] = u._ascq;
        u._senseKey = u._asc = u._ascq = 0;
        sendData(18);
        return;
    }
    case SCSI_INQUIRY:
        memset(buf, 0, 36);
        buf[1] = 0x80;                           /* removable */
        buf[2] = 2;                              /* SPC-2 */
        buf[3] = 2;                              /* response format 2 */
        buf[4] = 31;
        u.inquiry(buf + 8, buf + 16, buf + 32);
        sendData(36);
        return;
    case SCSI_MODE_SENSE_6:
        memset(buf, 0, 4);
        buf[0] = 3;
        buf[2] = u.writable() ? 0 : 0x80;        /* write protect */
        sendData(4);
        return;
    case SCSI_MODE_SENSE_10:
        memset(buf, 0, 8);
        buf[1] = 6;
        buf[3] = u.writable() ? 0 : 0x80;
        sendData(8);
        return;
    case SCSI_START_STOP_UNIT:
        if (!u.startStop((uint8_t)(bot.cmd[4] >> 4), bot.cmd[4] & 1, (bot.cmd[4] & 2) != 0)) {
            fail(SENSE_ILLEGAL_REQUEST, 0x24, 0x00);
            return;
        }
        sendData(0);
        return;
    case SCSI_VERIFY_10: {                       /* reported good without comparing (BYTCHK 0) */
        const uint32_t lba = be32(bot.cmd + 2), blocks = (uint32_t)((bot.cmd[7] << 8) | bot.cmd[8]);
        if (!u.testUnitReady()) {
            fail(SENSE_NOT_READY, 0x3A, 0x00);
            return;
        }
        if (lba > u._blockCount || blocks > u._blockCount - lba) {
            fail(SENSE_ILLEGAL_REQUEST, 0x21, 0x00);
            return;
        }
        sendData(0);
        return;
    }
    case SCSI_PREVENT_ALLOW:
    case SCSI_SYNCHRONIZE_CACHE_10:              /* around an eject, a cache flush: nothing to do */
        sendData(0);
        return;
    case SCSI_READ_FORMAT_CAPACITIES:
    case SCSI_READ_CAPACITY_10: {
        uint32_t blocks = 0;
        uint16_t size = 0;
        u.capacity(&blocks, &size);
        if (!u.testUnitReady() || !blocks || !size) {
            fail(SENSE_NOT_READY, 0x3A, 0x00);
            return;
        }
        if (op == SCSI_READ_CAPACITY_10) {
            put_be32(buf, blocks - 1);
            put_be32(buf + 4, size);
            sendData(8);
        } else {
            memset(buf, 0, 12);
            buf[3] = 8;
            put_be32(buf + 4, blocks);
            buf[8] = 2;                          /* formatted media */
            buf[9] = (uint8_t)(size >> 16);
            buf[10] = (uint8_t)(size >> 8);
            buf[11] = (uint8_t)size;
            sendData(12);
        }
        return;
    }
    case SCSI_READ_10:
    case SCSI_WRITE_10: {
        const bool write = op == SCSI_WRITE_10;
        if (!u.testUnitReady() || !u._blockSize) {
            fail(SENSE_NOT_READY, 0x3A, 0x00);
            return;
        }
        if (write && !u.writable()) {
            fail(SENSE_DATA_PROTECT, 0x27, 0x00);
            return;
        }
        if (bot.in == write) {                   /* the direction disagrees with the command */
            fail(SENSE_ILLEGAL_REQUEST, 0x24, 0x00);
            return;
        }
        bot.lba = be32(bot.cmd + 2);
        const uint32_t blocks = (uint32_t)((bot.cmd[7] << 8) | bot.cmd[8]);
        if (bot.lba > u._blockCount || blocks > u._blockCount - bot.lba) {
            fail(SENSE_ILLEGAL_REQUEST, 0x21, 0x00);   /* LBA out of range */
            return;
        }
        const uint32_t bytes = blocks * u._blockSize;
        bot.total = bytes < bot.expected ? bytes : bot.expected;
        bot.residue = bot.expected - bot.total;
        bot.done = 0;
        u._commandRemovals = u._removals;
        if (bot.total == 0) {
            if (bot.residue) {
                fail(SENSE_ILLEGAL_REQUEST, 0x24, 0x00);
            } else {
                status();
            }
            return;
        }
        if (write) {
            bot.stage = Stage::DataOut;
            writeNext();
        } else {
            bot.stage = Stage::DataIn;
            readNext();
        }
        return;
    }
    }
    fail(SENSE_ILLEGAL_REQUEST, 0x20, 0x00);     /* invalid command operation code */
}

void CH32RVUsbDeviceMsc::usbTransferred(uint8_t address, uint16_t length)
{
    if (this != bot.first) {
        return;
    }
    if (address == _ep) {                        /* OUT */
        if (bot.stage == Stage::Command) {
            const uint8_t *b = bot.buffer;
            if (length != 31 || le32(b) != CBW_SIGNATURE || (b[14] & 0x1F) < 1 || (b[14] & 0x1F) > 16) {
                /* Not a valid command block: stall both ways until a reset
                 * recovery (bulk-only transport 6.6.1). */
                bot.stage = Stage::Reset;
                device().stall(_ep);
                device().stall((uint8_t)(0x80 | _ep));
                return;
            }
            bot.tag = le32(b + 4);
            bot.expected = le32(b + 8);
            bot.in = (b[12] & 0x80) != 0;
            const uint8_t lun = b[13] & 0x0F;
            memcpy(bot.cmd, b + 15, 16);
            bot.residue = bot.expected;
            bot.failed = false;
            bot.unit = this;
            while (bot.unit && bot.unit->_lun != lun) {
                bot.unit = bot.unit->_nextLun;
            }
            command();
        } else if (bot.stage == Stage::DataOut) {
            CH32RVUsbDeviceMsc &u = *bot.unit;
            if (!u.mediumHeld()) {
                bot.residue = bot.expected - bot.done;
                fail(SENSE_NOT_READY, 0x3A, 0x00);   /* taken out during the command: none of this block is written */
                return;
            }
            const int32_t n = u.write10(bot.lba + bot.done / u._blockSize, bot.done % u._blockSize, bot.buffer, length);
            if (n != (int32_t)length) {
                bot.residue = bot.expected - bot.done;
                failIo(SENSE_MEDIUM_ERROR, 0x0C, 0x00);   /* write error */
                return;
            }
            bot.done += length;
            if (bot.done < bot.total && length == bot.chunk) {
                writeNext();
            } else {
                bot.residue = bot.expected - bot.done;
                status();
            }
        }
        return;
    }
    if (address == (uint8_t)(0x80 | _ep)) {      /* IN */
        if (bot.stage == Stage::Status) {
            receiveCommand();
        } else if (bot.stage == Stage::DataIn) {
            bot.done += length;
            if (bot.cmd[0] == SCSI_READ_10 && bot.done < bot.total) {
                readNext();
                return;
            }
            if (bot.residue && (bot.done % bot.packet) == 0) {
                bot.stage = Stage::Stalled;      /* shorter than asked, ending on a full packet */
                device().stall((uint8_t)(0x80 | _ep));
                return;
            }
            status();
        }
    }
}
