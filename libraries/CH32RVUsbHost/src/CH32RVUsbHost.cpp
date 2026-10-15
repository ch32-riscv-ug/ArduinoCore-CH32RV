/* CH32RVUsbHost: lifecycle, the device table, control transfers, hubs and
 * printing. The function groups (HID, CDC, ...) are in files of their own. */
#include "CH32RVUsbHostInternal.h"

#include <string.h>

#include "ch32rv_registers.h"

#if CH32RV_TINYUSB_HOST

#include "ch32rv_usbfs_irq.h"
#include "host/hub.h"

/* ------------------------------------------------------------- port */
#if defined(CH32RV_SERIES_CH32X035) || defined(CH32RV_SERIES_CH32X033)
/* CH32X035 / X033: USBFS on PC16 (D-) / PC17 (D+). Host mode wants the 15 kOhm
 * pull-downs (the controller's, UH_PD_DIS clear) and no pull-up, the PHY on
 * the 3.3 V supply, and the pads handed to it (EVT USBFS_Host_Init). */
#define X03X_AFIO_UDM_PUE_MASK 0x00000003u
#define X03X_AFIO_UDP_PUE_MASK 0x0000000Cu
#define X03X_AFIO_USB_PHY_V33  0x00000040u
#define X03X_AFIO_USB_IOEN     0x00000080u
#define USBFS_BASE_CTRL        (*(volatile uint8_t *)0x40023400u)

static bool port_open(void)
{
    ch32rv_clock_enable(AFIO);
    ch32rv_clock_enable(USBFS);
    pinMode(PC16, INPUT_PULLDOWN);
    pinMode(PC17, INPUT_PULLDOWN);
    CH32RV_AFIO_CTLR = (CH32RV_AFIO_CTLR & ~(X03X_AFIO_UDP_PUE_MASK | X03X_AFIO_UDM_PUE_MASK)) |
                       X03X_AFIO_USB_PHY_V33 | X03X_AFIO_USB_IOEN;
    return true;
}

static void port_close(void)
{
    CH32RV_AFIO_CTLR &= ~X03X_AFIO_USB_IOEN;
}

#else /* V20x / V30x OTG_FS */

#define USBFS_BASE_CTRL        (*(volatile uint8_t *)0x50000000u)
/* RCC_CFGR0[23:22] USBPRE: 48 MHz for the PHY from the PLL (as the device). */
#define HOST_USBPRE_SHIFT 22u
#if CH32RV_CLOCK_USE_PLL && CH32RV_CLOCK_SYSCLK_HZ == 48000000
#define HOST_USBPRE 0u
#elif CH32RV_CLOCK_USE_PLL && CH32RV_CLOCK_SYSCLK_HZ == 96000000
#define HOST_USBPRE 1u
#elif CH32RV_CLOCK_USE_PLL && CH32RV_CLOCK_SYSCLK_HZ == 144000000
#define HOST_USBPRE 2u
#endif

static bool port_open(void)
{
#if defined(HOST_USBPRE) && !defined(CH32RV_PART_CH32V203F6P6) && !defined(CH32RV_PART_CH32V203G6U6)
    CH32RV_RCC_CFGR0 = (CH32RV_RCC_CFGR0 & ~(3u << HOST_USBPRE_SHIFT)) | (HOST_USBPRE << HOST_USBPRE_SHIFT);
#if defined(CH32RV_SERIES_CH32V203) || defined(CH32RV_SERIES_CH32V208)
    pinMode(PB6, INPUT);
    pinMode(PB7, INPUT);
#else
    pinMode(PA11, INPUT);
    pinMode(PA12, INPUT);
#endif
    ch32rv_clock_enable(USBFS);
    return true;
#else
    return false;        /* no 48 MHz for the PHY at this clock, or no USBFS on the part */
#endif
}

static void port_close(void)
{
    ch32rv_clock_disable(USBFS);
}

#endif

/* Whether this die has the host half. CH32X035R8T6 / C8T6 / G8U6 / G8R6 do;
 * F8U6, F7P6, D8U6 and CH32X033F8P6 are device-only (datasheet model table),
 * and there BASE_CTRL.HOST_MODE does not stick - the EVT polls for it, and on
 * an F8U6 it reads 0 however long it is given (measured 2026-10-15). So the
 * bit is the test, which also covers a board built for "ANY". */
static bool port_has_host(void)
{
    USBFS_BASE_CTRL = 0x80;
    const uint32_t t0 = micros();
    while (!(USBFS_BASE_CTRL & 0x80) && (uint32_t)(micros() - t0) < 2000u) {
    }
    const bool has = (USBFS_BASE_CTRL & 0x80) != 0;
    USBFS_BASE_CTRL = 0;
    return has;
}

static void host_irq(void)
{
    tuh_int_handler(CH32RV_TUH_RHPORT, true);
}

/* ------------------------------------------------------------- groups */
static CH32RVUsbHostGroup *groups;

void ch32rv_usb_host_join(CH32RVUsbHostGroup &group)
{
    for (CH32RVUsbHostGroup *g = groups; g; g = g->next) {
        if (g == &group) {
            return;
        }
    }
    group.next = groups;
    groups = &group;
}

/* The hub driver comes with the core: a device behind a hub is ordinary. */
static const usbh_class_driver_t hub_driver = {
    "HUB", hub_init, hub_deinit, hub_open, hub_set_config, hub_xfer_cb, hub_close,
};

#define DRIVER_MAX 8
/* Raw storage: the driver struct has const members, so it is copied, not
 * assigned. */
alignas(usbh_class_driver_t) static uint8_t drivers[DRIVER_MAX * sizeof(usbh_class_driver_t)];

extern "C" usbh_class_driver_t const *usbh_app_driver_get_cb(uint8_t *count)
{
    uint8_t n = 0;
    memcpy(drivers, &hub_driver, sizeof hub_driver);
    n++;
    for (CH32RVUsbHostGroup *g = groups; g && n < DRIVER_MAX; g = g->next) {
        if (g->driver) {
            memcpy(drivers + n * sizeof(usbh_class_driver_t), g->driver, sizeof *g->driver);
            n++;
        }
    }
    *count = n;
    return reinterpret_cast<const usbh_class_driver_t *>(drivers);
}

/* ------------------------------------------------------------- devices */
#define SLOTS (CFG_TUH_DEVICE_MAX + CFG_TUH_HUB + 1)

struct DeviceSlot {
    CH32RVUsbHostDeviceInfo info;
    char manufacturer[CH32RV_USB_HOST_STRING_MAX];
    char product[CH32RV_USB_HOST_STRING_MAX];
    char serial[CH32RV_USB_HOST_STRING_MAX];
    uint16_t claimed;       /* interface bitmap */
    bool pending;           /* mounted; info and the callback still due */
    bool known;             /* info filled, onDeviceConnected() called */
};
static DeviceSlot slots[SLOTS];

CH32RVUsbHost *ch32rv_usb_host_active;
TU_ATTR_ALIGNED(4) uint8_t ch32rv_usb_host_scratch[CFG_TUH_ENUMERATION_BUFSIZE];

void ch32rv_usb_host_mark_claimed(uint8_t daddr, uint8_t itf)
{
    if (daddr < SLOTS && itf < 16) {
        slots[daddr].claimed = (uint16_t)(slots[daddr].claimed | (1u << itf));
        slots[daddr].info.supported = true;
    }
}

const CH32RVUsbHostDeviceInfo *ch32rv_usb_host_device(uint8_t daddr)
{
    return daddr < SLOTS && slots[daddr].known ? &slots[daddr].info : nullptr;
}

extern "C" void tuh_mount_cb(uint8_t daddr)
{
    if (daddr < SLOTS) {
        slots[daddr].pending = true;
    }
}

extern "C" void tuh_umount_cb(uint8_t daddr)
{
    if (daddr >= SLOTS) {
        return;
    }
    DeviceSlot &s = slots[daddr];
    const bool was_known = s.known;
    s.pending = false;
    s.known = false;
    for (CH32RVUsbHostGroup *g = groups; g; g = g->next) {
        if (g->detach) {
            g->detach(daddr);
        }
    }
    CH32RVUsbHost *host = ch32rv_usb_host_active;
    if (was_known && host && host->_disconnected) {
        host->_disconnected(s.info);
    }
    s.claimed = 0;
}

/* UTF-16LE string descriptor -> ASCII. */
static void ascii_from_utf16(const uint8_t *desc, char *out, size_t capacity)
{
    size_t n = 0;
    if (desc[0] >= 2 && desc[1] == TUSB_DESC_STRING) {
        for (size_t i = 2; i + 1 < desc[0] && n + 1 < capacity; i += 2) {
            const uint16_t c = (uint16_t)(desc[i] | (desc[i + 1] << 8));
            out[n++] = (c >= 0x20 && c < 0x7F) ? (char)c : '?';
        }
    }
    out[n] = '\0';
}

static void read_string(uint8_t daddr, uint8_t index, char *out)
{
    out[0] = '\0';
    if (!index) {
        return;
    }
    uint8_t *buf = ch32rv_usb_host_scratch;
    if (tuh_descriptor_get_string_sync(daddr, index, 0x0409, buf, 2 * CH32RV_USB_HOST_STRING_MAX + 2) ==
        XFER_RESULT_SUCCESS) {
        ascii_from_utf16(buf, out, CH32RV_USB_HOST_STRING_MAX);
    }
}

/* Fill in a newly mounted device and tell the sketch. From task context. */
static void complete_mount(CH32RVUsbHost &host, uint8_t daddr)
{
    DeviceSlot &s = slots[daddr];
    s.pending = false;
    CH32RVUsbHostDeviceInfo &d = s.info;
    const uint16_t claimed = s.claimed;
    d = CH32RVUsbHostDeviceInfo();
    d.address = daddr;
    d.supported = claimed != 0;
    tusb_desc_device_t dev;
    if (tuh_descriptor_get_device_sync(daddr, &dev, sizeof dev) != XFER_RESULT_SUCCESS) {
        return;
    }
    d.vid = dev.idVendor;
    d.pid = dev.idProduct;
    d.usbVersion = dev.bcdUSB;
    d.deviceVersion = dev.bcdDevice;
    d.deviceClass = dev.bDeviceClass;
    d.deviceSubClass = dev.bDeviceSubClass;
    d.deviceProtocol = dev.bDeviceProtocol;
    d.maxPacketSize0 = dev.bMaxPacketSize0;
    d.isHub = dev.bDeviceClass == TUSB_CLASS_HUB;
    tuh_bus_info_t bus;
    if (tuh_bus_info_get(daddr, &bus)) {
        d.parentAddress = bus.hub_addr;
        d.portId = bus.hub_addr ? (uint8_t)((bus.hub_addr << 4) | (bus.hub_port & 0x0F)) : 0x01;
        d.speed = bus.speed == TUSB_SPEED_LOW ? CH32RV_USB_HOST_SPEED_LOW : CH32RV_USB_HOST_SPEED_FULL;
    }
    tusb_desc_configuration_t cfg;
    if (tuh_descriptor_get_configuration_sync(daddr, 0, &cfg, sizeof cfg) == XFER_RESULT_SUCCESS) {
        d.configurationValue = cfg.bConfigurationValue;
        d.configurationAttributes = cfg.bmAttributes;
        d.configurationMaxPower = cfg.bMaxPower;
        d.configurationInterfaceCount = cfg.bNumInterfaces;
        d.configurationTotalLength = cfg.wTotalLength;
    }
    read_string(daddr, dev.iManufacturer, s.manufacturer);
    read_string(daddr, dev.iProduct, s.product);
    read_string(daddr, dev.iSerialNumber, s.serial);
    d.manufacturer = s.manufacturer;
    d.product = s.product;
    d.serial = s.serial;
    if (!tuh_mounted(daddr)) {
        return;                 /* gone while we asked */
    }
    s.known = true;
    if (host._connected) {
        host._connected(d);
    }
}

/* ------------------------------------------------------------- the task */
extern "C" void ch32rv_usb_host_task(void)
{
    static volatile bool busy;
    CH32RVUsbHost *host = ch32rv_usb_host_active;
    if (!host || !host->_started || busy) {
        return;
    }
    busy = true;
    tuh_task_ext(0, false);
    for (uint8_t a = 1; a < SLOTS; a++) {
        if (slots[a].pending) {
            complete_mount(*host, a);
        }
    }
    for (CH32RVUsbHostGroup *g = groups; g; g = g->next) {
        if (g->poll) {
            g->poll(*host);
        }
    }
    busy = false;
}

bool ch32rv_usb_host_wait(volatile bool &done, uint32_t timeoutMs)
{
    const uint32_t t0 = millis();
    while (!done) {
        tuh_task_ext(0, false);
        if ((uint32_t)(millis() - t0) >= timeoutMs) {
            return false;
        }
    }
    return true;
}

/* ------------------------------------------------------------- control */
CH32RVUsbHostError ch32rv_usb_host_result_error(xfer_result_t result)
{
    switch (result) {
    case XFER_RESULT_SUCCESS: return CH32RVUsbHostError::None;
    case XFER_RESULT_STALLED: return CH32RVUsbHostError::Stalled;
    case XFER_RESULT_TIMEOUT: return CH32RVUsbHostError::Timeout;
    default: return CH32RVUsbHostError::TransferFailed;
    }
}

namespace {
/* One synchronous control transfer at a time; static, so a transfer that
 * outlives its caller's timeout still completes into valid memory. */
struct ControlWait {
    volatile bool done;
    volatile xfer_result_t result;
    volatile uint32_t actual;
    uint32_t generation;
};
ControlWait control_wait;

void control_done(tuh_xfer_t *xfer)
{
    if ((uint32_t)xfer->user_data != control_wait.generation) {
        return;                 /* a transfer that was given up on */
    }
    control_wait.result = xfer->result;
    control_wait.actual = xfer->actual_len;
    control_wait.done = true;
}
}  // namespace

bool ch32rv_usb_host_control(CH32RVUsbHost &host, uint8_t daddr, uint8_t requestType, uint8_t request,
                             uint16_t value, uint16_t index, uint8_t *data, uint16_t length,
                             size_t *actualLength, uint32_t timeoutMs)
{
    if (actualLength) {
        *actualLength = 0;
    }
    if (!host._started) {
        return host.fail(CH32RVUsbHostError::NotStarted);
    }
    if (!daddr || !tuh_connected(daddr)) {
        return host.fail(CH32RVUsbHostError::NotConnected);
    }
    static tusb_control_request_t setup;
    setup.bmRequestType = requestType;
    setup.bRequest = request;
    setup.wValue = value;
    setup.wIndex = index;
    setup.wLength = length;
    control_wait.generation++;
    control_wait.done = false;
    control_wait.result = XFER_RESULT_INVALID;
    control_wait.actual = 0;
    tuh_xfer_t xfer = {};
    xfer.daddr = daddr;
    xfer.ep_addr = 0;
    xfer.setup = &setup;
    xfer.buffer = data;
    xfer.complete_cb = control_done;
    xfer.user_data = control_wait.generation;
    if (!tuh_control_xfer(&xfer)) {
        return host.fail(CH32RVUsbHostError::TransferFailed);
    }
    if (!ch32rv_usb_host_wait(control_wait.done, timeoutMs)) {
        /* Abandoned: stop it before returning, so its data stage cannot
         * land in the caller's buffer afterwards. */
        control_wait.generation++;
        tuh_edpt_abort_xfer(daddr, 0);
        return host.fail(CH32RVUsbHostError::Timeout);
    }
    if (actualLength) {
        *actualLength = control_wait.actual;
    }
    if (control_wait.result != XFER_RESULT_SUCCESS) {
        return host.fail(ch32rv_usb_host_result_error(control_wait.result));
    }
    return host.ok();
}

uint16_t ch32rv_usb_host_config_descriptor(uint8_t daddr)
{
    tusb_desc_configuration_t head;
    if (tuh_descriptor_get_configuration_sync(daddr, 0, &head, sizeof head) != XFER_RESULT_SUCCESS) {
        return 0;
    }
    const uint16_t total = TU_MIN(head.wTotalLength, (uint16_t)sizeof ch32rv_usb_host_scratch);
    if (tuh_descriptor_get_configuration_sync(daddr, 0, ch32rv_usb_host_scratch, total) != XFER_RESULT_SUCCESS) {
        return 0;
    }
    return total;
}

#endif /* CH32RV_TINYUSB_HOST */

/* ------------------------------------------------------------- the class */
CH32RVUsbHost::CH32RVUsbHost() {}

const char *CH32RVUsbHost::lastErrorName() const
{
    switch (_error) {
    case CH32RVUsbHostError::None: return "None";
    case CH32RVUsbHostError::Unsupported: return "Unsupported";
    case CH32RVUsbHostError::Busy: return "Busy";
    case CH32RVUsbHostError::NotStarted: return "NotStarted";
    case CH32RVUsbHostError::NotConnected: return "NotConnected";
    case CH32RVUsbHostError::NotSupported: return "NotSupported";
    case CH32RVUsbHostError::InvalidArgument: return "InvalidArgument";
    case CH32RVUsbHostError::Timeout: return "Timeout";
    case CH32RVUsbHostError::Stalled: return "Stalled";
    case CH32RVUsbHostError::TransferFailed: return "TransferFailed";
    case CH32RVUsbHostError::NoMemory: return "NoMemory";
    case CH32RVUsbHostError::InvalidResponse: return "InvalidResponse";
    }
    return "?";
}

#if CH32RV_TINYUSB_HOST

bool CH32RVUsbHost::begin()
{
    if (_started) {
        return ok();
    }
    if (ch32rv_usbfs_irq_hook && ch32rv_usbfs_irq_hook != host_irq) {
        return fail(CH32RVUsbHostError::Busy);
    }
    memset(slots, 0, sizeof slots);
    if (!port_open()) {
        return fail(CH32RVUsbHostError::Unsupported);
    }
    if (!port_has_host()) {
        port_close();
        return fail(CH32RVUsbHostError::Unsupported);
    }
    ch32rv_usb_host_active = this;
    ch32rv_usbfs_irq_hook = host_irq;
    tusb_rhport_init_t init = {};
    init.role = TUSB_ROLE_HOST;
    init.speed = TUSB_SPEED_FULL;
    if (!tuh_rhport_init(CH32RV_TUH_RHPORT, &init)) {
        ch32rv_usbfs_irq_hook = nullptr;
        port_close();
        return fail(CH32RVUsbHostError::NoMemory);
    }
    _started = true;
    return ok();
}

void CH32RVUsbHost::end()
{
    if (!_started) {
        return;
    }
    for (uint8_t a = 1; a < SLOTS; a++) {
        if (slots[a].known || slots[a].pending) {
            tuh_umount_cb(a);
        }
    }
    tuh_deinit(CH32RV_TUH_RHPORT);
    port_close();
    ch32rv_usbfs_irq_hook = nullptr;
    _started = false;
    if (ch32rv_usb_host_active == this) {
        ch32rv_usb_host_active = nullptr;
    }
}

bool CH32RVUsbHost::ready() const
{
    return _started && tuh_inited();
}

void CH32RVUsbHost::task()
{
    ch32rv_usb_host_task();
}

uint8_t CH32RVUsbHost::resolve(uint8_t address) const
{
    if (address != CH32RV_USB_HOST_ANY_ADDRESS) {
        return address < SLOTS && slots[address].known ? address : 0;
    }
    for (uint8_t a = 1; a < SLOTS; a++) {
        if (slots[a].known && !slots[a].info.isHub) {
            return a;
        }
    }
    return 0;
}

size_t CH32RVUsbHost::deviceCount() const
{
    size_t n = 0;
    for (uint8_t a = 1; a < SLOTS; a++) {
        n += slots[a].known ? 1u : 0u;
    }
    return n;
}

size_t CH32RVUsbHost::getDevices(CH32RVUsbHostDeviceInfo *devices, size_t maxDevices) const
{
    size_t n = 0;
    for (uint8_t a = 1; a < SLOTS && n < maxDevices; a++) {
        if (slots[a].known) {
            devices[n++] = slots[a].info;
        }
    }
    return n;
}

bool CH32RVUsbHost::getDevice(uint8_t address, CH32RVUsbHostDeviceInfo &device) const
{
    if (address >= SLOTS || !slots[address].known) {
        return false;
    }
    device = slots[address].info;
    return true;
}

size_t CH32RVUsbHost::getInterfaces(uint8_t address, CH32RVUsbHostInterfaceInfo *interfaces, size_t maxInterfaces)
{
    const uint8_t daddr = resolve(address);
    if (!daddr) {
        fail(CH32RVUsbHostError::NotConnected);
        return 0;
    }
    const uint16_t len = ch32rv_usb_host_config_descriptor(daddr);
    size_t n = 0;
    for (uint16_t at = 0; at + 2 <= len && n < maxInterfaces; at = (uint16_t)(at + ch32rv_usb_host_scratch[at])) {
        const uint8_t *d = &ch32rv_usb_host_scratch[at];
        if (d[0] < 2 || at + d[0] > len) {
            break;
        }
        if (d[1] == TUSB_DESC_INTERFACE && d[0] >= 9) {
            CH32RVUsbHostInterfaceInfo &i = interfaces[n++];
            i = CH32RVUsbHostInterfaceInfo();
            i.number = d[2];
            i.alternate = d[3];
            i.endpointCount = d[4];
            i.interfaceClass = d[5];
            i.interfaceSubClass = d[6];
            i.interfaceProtocol = d[7];
            i.claimed = d[2] < 16 && (slots[daddr].claimed & (1u << d[2]));
        }
    }
    ok();
    return n;
}

size_t CH32RVUsbHost::getEndpoints(uint8_t address, CH32RVUsbHostEndpointInfo *endpoints, size_t maxEndpoints)
{
    const uint8_t daddr = resolve(address);
    if (!daddr) {
        fail(CH32RVUsbHostError::NotConnected);
        return 0;
    }
    const uint16_t len = ch32rv_usb_host_config_descriptor(daddr);
    size_t n = 0;
    uint8_t itf = 0;
    for (uint16_t at = 0; at + 2 <= len && n < maxEndpoints; at = (uint16_t)(at + ch32rv_usb_host_scratch[at])) {
        const uint8_t *d = &ch32rv_usb_host_scratch[at];
        if (d[0] < 2 || at + d[0] > len) {
            break;
        }
        if (d[1] == TUSB_DESC_INTERFACE && d[0] >= 9) {
            itf = d[2];
        } else if (d[1] == TUSB_DESC_ENDPOINT && d[0] >= 7) {
            CH32RVUsbHostEndpointInfo &e = endpoints[n++];
            e.address = d[2];
            e.interfaceNumber = itf;
            e.attributes = d[3];
            e.maxPacketSize = (uint16_t)((d[4] | (d[5] << 8)) & 0x7FF);
            e.interval = d[6];
        }
    }
    ok();
    return n;
}

bool CH32RVUsbHost::getHubInfo(uint8_t hubAddress, CH32RVUsbHostHubInfo &hub)
{
    uint8_t desc[9] = {};
    size_t got = 0;
    if (!ch32rv_usb_host_control(*this, hubAddress, 0xA0, TUSB_REQ_GET_DESCRIPTOR, 0x2900, 0, desc,
                                 sizeof desc, &got, 1000)) {
        return false;
    }
    if (got < 7) {
        return fail(CH32RVUsbHostError::InvalidResponse);
    }
    hub = CH32RVUsbHostHubInfo();
    hub.address = hubAddress;
    hub.portCount = desc[2];
    hub.characteristics = (uint16_t)(desc[3] | (desc[4] << 8));
    hub.powerOnToPowerGoodMs = (uint16_t)(desc[5] * 2u);
    hub.controllerCurrentMa = desc[6];
    return ok();
}

bool CH32RVUsbHost::getHubPortStatus(uint8_t hubAddress, uint8_t port, uint16_t &status, uint16_t &change)
{
    uint8_t r[4] = {};
    size_t got = 0;
    if (!ch32rv_usb_host_control(*this, hubAddress, 0xA3, TUSB_REQ_GET_STATUS, 0, port, r, sizeof r, &got,
                                 1000)) {
        return false;
    }
    status = (uint16_t)(r[0] | (r[1] << 8));
    change = (uint16_t)(r[2] | (r[3] << 8));
    return ok();
}

bool CH32RVUsbHost::setHubPortPower(uint8_t hubAddress, uint8_t port, bool enable)
{
    return ch32rv_usb_host_control(*this, hubAddress, 0x23,
                                   enable ? TUSB_REQ_SET_FEATURE : TUSB_REQ_CLEAR_FEATURE,
                                   HUB_FEATURE_PORT_POWER, port, nullptr, 0, nullptr, 1000);
}

bool CH32RVUsbHost::vendorControlTransfer(uint8_t requestType, uint8_t request, uint16_t value, uint16_t index,
                                          uint8_t *data, size_t length, size_t *actualLength, uint8_t address,
                                          uint32_t timeoutMs)
{
    if (length > 0xFFFFu || (length && !data)) {
        return fail(CH32RVUsbHostError::InvalidArgument);
    }
    const uint8_t daddr = resolve(address);
    if (!daddr) {
        return fail(_started ? CH32RVUsbHostError::NotConnected : CH32RVUsbHostError::NotStarted);
    }
    return ch32rv_usb_host_control(*this, daddr, requestType, request, value, index, data, (uint16_t)length,
                                   actualLength, timeoutMs);
}

bool CH32RVUsbHost::vendorControlIn(uint8_t request, uint16_t value, uint16_t index, uint8_t *data,
                                    size_t length, size_t *actualLength, uint8_t address, uint32_t timeoutMs)
{
    /* Device-to-host, vendor, recipient device. */
    return vendorControlTransfer(0xC0, request, value, index, data, length, actualLength, address, timeoutMs);
}

bool CH32RVUsbHost::vendorControlOut(uint8_t request, uint16_t value, uint16_t index, const uint8_t *data,
                                     size_t length, uint8_t address, uint32_t timeoutMs)
{
    return vendorControlTransfer(0x40, request, value, index, const_cast<uint8_t *>(data), length, nullptr,
                                 address, timeoutMs);
}

bool CH32RVUsbHost::getStringDescriptor(uint8_t address, uint8_t index, char *out, size_t capacity)
{
    if (!out || !capacity) {
        return fail(CH32RVUsbHostError::InvalidArgument);
    }
    out[0] = '\0';
    const uint8_t daddr = resolve(address);
    if (!daddr) {
        return fail(CH32RVUsbHostError::NotConnected);
    }
    uint8_t *buf = ch32rv_usb_host_scratch;
    const xfer_result_t r = tuh_descriptor_get_string_sync(daddr, index, 0x0409, buf,
                                                           (uint16_t)TU_MIN(sizeof ch32rv_usb_host_scratch, 255u));
    if (r != XFER_RESULT_SUCCESS) {
        return fail(ch32rv_usb_host_result_error(r));
    }
    ascii_from_utf16(buf, out, capacity);
    return ok();
}

#else /* no host on this series */

bool CH32RVUsbHost::begin() { return fail(CH32RVUsbHostError::Unsupported); }
void CH32RVUsbHost::end() {}
bool CH32RVUsbHost::ready() const { return false; }
void CH32RVUsbHost::task() {}
uint8_t CH32RVUsbHost::resolve(uint8_t) const { return 0; }
size_t CH32RVUsbHost::deviceCount() const { return 0; }
size_t CH32RVUsbHost::getDevices(CH32RVUsbHostDeviceInfo *, size_t) const { return 0; }
bool CH32RVUsbHost::getDevice(uint8_t, CH32RVUsbHostDeviceInfo &) const { return false; }
size_t CH32RVUsbHost::getInterfaces(uint8_t, CH32RVUsbHostInterfaceInfo *, size_t) { return 0; }
size_t CH32RVUsbHost::getEndpoints(uint8_t, CH32RVUsbHostEndpointInfo *, size_t) { return 0; }
bool CH32RVUsbHost::getHubInfo(uint8_t, CH32RVUsbHostHubInfo &) { return fail(CH32RVUsbHostError::Unsupported); }
bool CH32RVUsbHost::getHubPortStatus(uint8_t, uint8_t, uint16_t &, uint16_t &)
{
    return fail(CH32RVUsbHostError::Unsupported);
}
bool CH32RVUsbHost::setHubPortPower(uint8_t, uint8_t, bool) { return fail(CH32RVUsbHostError::Unsupported); }
bool CH32RVUsbHost::vendorControlTransfer(uint8_t, uint8_t, uint16_t, uint16_t, uint8_t *, size_t, size_t *,
                                          uint8_t, uint32_t)
{
    return fail(CH32RVUsbHostError::Unsupported);
}
bool CH32RVUsbHost::vendorControlIn(uint8_t, uint16_t, uint16_t, uint8_t *, size_t, size_t *, uint8_t, uint32_t)
{
    return fail(CH32RVUsbHostError::Unsupported);
}
bool CH32RVUsbHost::vendorControlOut(uint8_t, uint16_t, uint16_t, const uint8_t *, size_t, uint8_t, uint32_t)
{
    return fail(CH32RVUsbHostError::Unsupported);
}
bool CH32RVUsbHost::getStringDescriptor(uint8_t, uint8_t, char *out, size_t capacity)
{
    if (out && capacity) {
        out[0] = '\0';
    }
    return fail(CH32RVUsbHostError::Unsupported);
}

#endif /* CH32RV_TINYUSB_HOST */

/* ------------------------------------------------------------- printing */
void ch32rvUsbHostPrintHex(const uint8_t *data, size_t length, Print &out)
{
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < length; i++) {
        if (i) {
            out.print(' ');
        }
        out.print(hex[data[i] >> 4]);
        out.print(hex[data[i] & 15]);
    }
}

static void print_hex16(Print &out, uint16_t v)
{
    static const char hex[] = "0123456789abcdef";
    for (int s = 12; s >= 0; s -= 4) {
        out.print(hex[(v >> s) & 15]);
    }
}

void ch32rvUsbHostPrint(const CH32RVUsbHostDeviceInfo &d, Print &out)
{
    out.print("device: address=");
    out.print(d.address);
    out.print(" vid=");
    print_hex16(out, d.vid);
    out.print(" pid=");
    print_hex16(out, d.pid);
    out.print(" class=");
    out.print(d.deviceClass, HEX);
    out.print(d.speed == CH32RV_USB_HOST_SPEED_LOW ? " speed=low" : " speed=full");
    out.print(" manufacturer=\"");
    out.print(d.manufacturer);
    out.print("\" product=\"");
    out.print(d.product);
    out.print("\" serial=\"");
    out.print(d.serial);
    out.print("\" interfaces=");
    out.print(d.configurationInterfaceCount);
    if (d.isHub) {
        out.print(" hub");
    }
    out.println();
}

void ch32rvUsbHostPrint(const CH32RVUsbHostInterfaceInfo &i, Print &out)
{
    out.print("  interface ");
    out.print(i.number);
    out.print(" alt=");
    out.print(i.alternate);
    out.print(" class=");
    out.print(i.interfaceClass, HEX);
    out.print('/');
    out.print(i.interfaceSubClass, HEX);
    out.print('/');
    out.print(i.interfaceProtocol, HEX);
    out.print(" endpoints=");
    out.print(i.endpointCount);
    out.println(i.claimed ? " claimed" : "");
}

void ch32rvUsbHostPrint(const CH32RVUsbHostEndpointInfo &e, Print &out)
{
    static const char *const types[] = {"control", "isochronous", "bulk", "interrupt"};
    out.print("    endpoint 0x");
    out.print(e.address, HEX);
    out.print(" interface=");
    out.print(e.interfaceNumber);
    out.print(' ');
    out.print(types[e.attributes & 3]);
    out.print(" max_packet=");
    out.print(e.maxPacketSize);
    out.print(" interval=");
    out.println(e.interval);
}

void CH32RVUsbHost::printDeviceInfo(uint8_t address, Print &out)
{
    CH32RVUsbHostDeviceInfo d;
    if (!getDevice(address, d)) {
        out.println("device: none");
        return;
    }
    ch32rvUsbHostPrint(d, out);
    CH32RVUsbHostInterfaceInfo itfs[8];
    const size_t ni = getInterfaces(address, itfs, 8);
    for (size_t i = 0; i < ni; i++) {
        ch32rvUsbHostPrint(itfs[i], out);
    }
    CH32RVUsbHostEndpointInfo eps[12];
    const size_t ne = getEndpoints(address, eps, 12);
    for (size_t i = 0; i < ne; i++) {
        ch32rvUsbHostPrint(eps[i], out);
    }
}

void CH32RVUsbHost::printAllDeviceInfo(Print &out)
{
    CH32RVUsbHostDeviceInfo list[4];
    const size_t n = getDevices(list, 4);
    if (!n) {
        out.println("devices: none");
    }
    for (size_t i = 0; i < n; i++) {
        printDeviceInfo(list[i].address, out);
    }
}
