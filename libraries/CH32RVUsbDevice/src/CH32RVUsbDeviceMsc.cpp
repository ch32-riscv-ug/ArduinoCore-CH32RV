/* USB mass storage on the device stack: bulk-only transport and the SCSI
 * commands a host uses with a disk, one logical unit. */
#include "CH32RVUsbDeviceInternal.h"

#include <string.h>


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

static const CH32RVUsbFunctionType msc_type = {msc_descriptor, 1, 1, nullptr, 0};

CH32RVUsbDeviceMsc::CH32RVUsbDeviceMsc(CH32RVUsbDevice &device)
    : CH32RVUsbDeviceFunction(device, msc_type, CH32RV_USB_MSC_NAME)
{
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

/* ---------------------------------------------------------- the stack's side */
void CH32RVUsbDeviceMsc::usbReset()
{
    _stage = Stage::Command;
    _attention = false;                          /* the host starts afresh (a reset recovery keeps it) */
    _senseKey = _asc = _ascq = 0;
}

void CH32RVUsbDeviceMsc::usbConfigured(CH32RVUsbSpeed speed)
{
    _packet = ch32rv_usb_bulk_size(speed);
    _stage = Stage::Command;
    receiveCommand();
}

void CH32RVUsbDeviceMsc::receiveCommand()
{
    _stage = Stage::Command;
    if (!device().transferBusy(_ep)) {
        device().transfer(_ep, _buffer, sizeof _buffer);   /* a whole packet fits, whatever the host sends */
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
            _stage = Stage::Command;             /* the host clears both halts next */
            if (!device().stalled(_ep)) {
                receiveCommand();
            }
        }
        return true;
    case 0xFE: {                                 /* GET_MAX_LUN: one unit */
        static const uint8_t zero = 0;
        return stage != CH32RVUsbControlStage::Setup || device().controlIn(&zero, 1);
    }
    }
    return false;
}

void CH32RVUsbDeviceMsc::usbHaltCleared(uint8_t address)
{
    if (_stage == Stage::Stalled) {
        status();                                /* the data stage ended early: now the status */
    } else if (_stage == Stage::Command && address == _ep) {
        receiveCommand();
    }
}

/* Leave the command failed: the sense data says why; the data stage the host
 * expects is stalled, and the status follows once it clears the halt. */
void CH32RVUsbDeviceMsc::fail(uint8_t key, uint8_t asc, uint8_t ascq)
{
    _senseKey = key;
    _asc = asc;
    _ascq = ascq;
    _failed = true;
    if (_residue) {
        _stage = Stage::Stalled;
        device().stall(_in ? (uint8_t)(0x80 | _ep) : _ep);
    } else {
        status();
    }
}

void CH32RVUsbDeviceMsc::status()
{
    const uint8_t csw[13] = {
        (uint8_t)CSW_SIGNATURE, (uint8_t)(CSW_SIGNATURE >> 8), (uint8_t)(CSW_SIGNATURE >> 16), (uint8_t)(CSW_SIGNATURE >> 24),
        (uint8_t)_tag, (uint8_t)(_tag >> 8), (uint8_t)(_tag >> 16), (uint8_t)(_tag >> 24),
        (uint8_t)_residue, (uint8_t)(_residue >> 8), (uint8_t)(_residue >> 16), (uint8_t)(_residue >> 24),
        (uint8_t)(_failed ? 1 : 0),
    };
    memcpy(_csw, csw, sizeof csw);
    _stage = Stage::Status;
    device().transfer((uint8_t)(0x80 | _ep), _csw, sizeof _csw);
}

/* An answer prepared in _buffer: at most what the host asked for. A shorter
 * answer ending on a full packet is followed by a stalled IN, so the host
 * sees where it ends (bulk-only transport 6.7.2). */
void CH32RVUsbDeviceMsc::sendData(uint16_t length)
{
    if (_expected == 0) {
        _failed = length != 0;                   /* an answer the host did not ask for */
        status();
        return;
    }
    if (!_in) {
        _stage = Stage::Stalled;                 /* the host sends data no command takes */
        device().stall(_ep);
        return;
    }
    const uint16_t n = length < _expected ? length : (uint16_t)_expected;
    _residue = _expected - n;
    _total = n;
    _done = 0;
    _stage = Stage::DataIn;
    _chunk = n;
    device().transfer((uint8_t)(0x80 | _ep), _buffer, n);
}

void CH32RVUsbDeviceMsc::readNext()
{
    const uint32_t left = _total - _done;
    _chunk = (uint16_t)(left < sizeof _buffer ? left : sizeof _buffer);
    const int32_t n = read10(_lba + _done / _blockSize, _done % _blockSize, _buffer, _chunk);
    if (n != (int32_t)_chunk) {
        fail(SENSE_MEDIUM_ERROR, 0x11, 0x00);    /* unrecovered read error */
        return;
    }
    device().transfer((uint8_t)(0x80 | _ep), _buffer, _chunk);
}

void CH32RVUsbDeviceMsc::writeNext()
{
    const uint32_t left = _total - _done;
    _chunk = (uint16_t)(left < sizeof _buffer ? left : sizeof _buffer);
    device().transfer(_ep, _buffer, _chunk);
}

void CH32RVUsbDeviceMsc::command()
{
    const uint8_t op = _cmd[0];
    if (_attention && op != SCSI_INQUIRY && op != SCSI_REQUEST_SENSE) {
        _attention = false;
        fail(SENSE_UNIT_ATTENTION, 0x28, 0x00);  /* not ready to ready change, medium may have changed */
        return;
    }
    switch (op) {
    case SCSI_TEST_UNIT_READY:
        if (!testUnitReady()) {
            fail(SENSE_NOT_READY, 0x3A, 0x00);   /* medium not present */
            return;
        }
        sendData(0);
        return;
    case SCSI_REQUEST_SENSE: {
        memset(_buffer, 0, 18);
        _buffer[0] = 0x70;
        _buffer[2] = _senseKey;
        _buffer[7] = 10;
        _buffer[12] = _asc;
        _buffer[13] = _ascq;
        _senseKey = _asc = _ascq = 0;
        sendData(18);
        return;
    }
    case SCSI_INQUIRY:
        memset(_buffer, 0, 36);
        _buffer[1] = 0x80;                       /* removable */
        _buffer[2] = 2;                          /* SPC-2 */
        _buffer[3] = 2;                          /* response format 2 */
        _buffer[4] = 31;
        inquiry(_buffer + 8, _buffer + 16, _buffer + 32);
        sendData(36);
        return;
    case SCSI_MODE_SENSE_6:
        memset(_buffer, 0, 4);
        _buffer[0] = 3;
        _buffer[2] = writable() ? 0 : 0x80;      /* write protect */
        sendData(4);
        return;
    case SCSI_MODE_SENSE_10:
        memset(_buffer, 0, 8);
        _buffer[1] = 6;
        _buffer[3] = writable() ? 0 : 0x80;
        sendData(8);
        return;
    case SCSI_START_STOP_UNIT:
        if (!startStop((uint8_t)(_cmd[4] >> 4), _cmd[4] & 1, (_cmd[4] & 2) != 0)) {
            fail(SENSE_ILLEGAL_REQUEST, 0x24, 0x00);
            return;
        }
        sendData(0);
        return;
    case SCSI_VERIFY_10: {                       /* reported good without comparing (BYTCHK 0) */
        const uint32_t lba = be32(_cmd + 2), blocks = (uint32_t)((_cmd[7] << 8) | _cmd[8]);
        if (!testUnitReady()) {
            fail(SENSE_NOT_READY, 0x3A, 0x00);
            return;
        }
        if (lba > _blockCount || blocks > _blockCount - lba) {
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
        capacity(&blocks, &size);
        if (!testUnitReady() || !blocks || !size) {
            fail(SENSE_NOT_READY, 0x3A, 0x00);
            return;
        }
        if (op == SCSI_READ_CAPACITY_10) {
            put_be32(_buffer, blocks - 1);
            put_be32(_buffer + 4, size);
            sendData(8);
        } else {
            memset(_buffer, 0, 12);
            _buffer[3] = 8;
            put_be32(_buffer + 4, blocks);
            _buffer[8] = 2;                      /* formatted media */
            _buffer[9] = (uint8_t)(size >> 16);
            _buffer[10] = (uint8_t)(size >> 8);
            _buffer[11] = (uint8_t)size;
            sendData(12);
        }
        return;
    }
    case SCSI_READ_10:
    case SCSI_WRITE_10: {
        const bool write = op == SCSI_WRITE_10;
        if (!testUnitReady() || !_blockSize) {
            fail(SENSE_NOT_READY, 0x3A, 0x00);
            return;
        }
        if (write && !writable()) {
            fail(SENSE_DATA_PROTECT, 0x27, 0x00);
            return;
        }
        if (_in == write) {                      /* the direction disagrees with the command */
            fail(SENSE_ILLEGAL_REQUEST, 0x24, 0x00);
            return;
        }
        _lba = be32(_cmd + 2);
        const uint32_t blocks = (uint32_t)((_cmd[7] << 8) | _cmd[8]);
        if (_lba > _blockCount || blocks > _blockCount - _lba) {
            fail(SENSE_ILLEGAL_REQUEST, 0x21, 0x00);   /* LBA out of range */
            return;
        }
        const uint32_t bytes = blocks * _blockSize;
        _total = bytes < _expected ? bytes : _expected;
        _residue = _expected - _total;
        _done = 0;
        if (_total == 0) {
            if (_residue) {
                fail(SENSE_ILLEGAL_REQUEST, 0x24, 0x00);
            } else {
                status();
            }
            return;
        }
        if (write) {
            _stage = Stage::DataOut;
            writeNext();
        } else {
            _stage = Stage::DataIn;
            readNext();
        }
        return;
    }
    }
    fail(SENSE_ILLEGAL_REQUEST, 0x20, 0x00);     /* invalid command operation code */
}

void CH32RVUsbDeviceMsc::usbTransferred(uint8_t address, uint16_t length)
{
    if (address == _ep) {                        /* OUT */
        if (_stage == Stage::Command) {
            if (length != 31 || le32(_buffer) != CBW_SIGNATURE || (_buffer[14] & 0x1F) < 1 ||
                (_buffer[14] & 0x1F) > 16) {
                /* Not a valid command block: stall both ways until a reset
                 * recovery (bulk-only transport 6.6.1). */
                _stage = Stage::Reset;
                device().stall(_ep);
                device().stall((uint8_t)(0x80 | _ep));
                return;
            }
            _tag = le32(_buffer + 4);
            _expected = le32(_buffer + 8);
            _in = (_buffer[12] & 0x80) != 0;
            memcpy(_cmd, _buffer + 15, 16);
            _residue = _expected;
            _failed = false;
            command();
        } else if (_stage == Stage::DataOut) {
            const int32_t n = write10(_lba + _done / _blockSize, _done % _blockSize, _buffer, length);
            if (n != (int32_t)length) {
                _residue = _expected - _done;
                fail(SENSE_MEDIUM_ERROR, 0x0C, 0x00);   /* write error */
                return;
            }
            _done += length;
            if (_done < _total && length == _chunk) {
                writeNext();
            } else {
                _residue = _expected - _done;
                status();
            }
        }
        return;
    }
    if (address == (uint8_t)(0x80 | _ep)) {      /* IN */
        if (_stage == Stage::Status) {
            receiveCommand();
        } else if (_stage == Stage::DataIn) {
            _done += length;
            if (_cmd[0] == SCSI_READ_10 && _done < _total) {
                readNext();
                return;
            }
            if (_residue && (_done % _packet) == 0) {
                _stage = Stage::Stalled;         /* shorter than asked, ending on a full packet */
                device().stall((uint8_t)(0x80 | _ep));
                return;
            }
            status();
        }
    }
}
