/* CH32RVUsbDeviceNet's EspUsbDevice-style IP calls (declared in
 * CH32RVUsbDeviceNet.h), on NetworkUsbNcm. */
#include "NetworkUsbNcm.h"

namespace {
/* What the calls set before beginNetwork(), kept beside the interface. */
struct UsbNetSettings {
    bool server = false, client = false, gateway = false, defaultRoute = false, configured = false;
};
UsbNetSettings settings;
}

static NetworkUsbNcm &interface_of(CH32RVUsbDeviceNet &ncm, NetworkUsbNcm *&slot)
{
    if (!slot) {
        slot = new NetworkUsbNcm(ncm);
        slot->config(IPAddress(192, 168, 7, 1), IPAddress(192, 168, 7, 1), IPAddress(255, 255, 255, 0));
    }
    return *slot;
}

void CH32RVUsbDeviceNet::ipConfig(IPAddress local, IPAddress gateway, IPAddress subnet)
{
    settings.configured = true;
    NetworkUsbNcm &ifc = interface_of(*this, _network);
    if (!settings.client) {
        ifc.config(local, gateway, subnet);
    }
}

void CH32RVUsbDeviceNet::dhcpServer(bool enable)
{
    settings.server = enable;
    if (enable) {
        settings.client = false;
    }
    interface_of(*this, _network);
}

void CH32RVUsbDeviceNet::dhcpClient(bool enable)
{
    settings.client = enable;
    if (enable) {
        settings.server = false;
    }
    interface_of(*this, _network);
}

void CH32RVUsbDeviceNet::dhcpAdvertiseGateway(bool enable)
{
    interface_of(*this, _network).dhcpAdvertiseGateway(enable);
}

void CH32RVUsbDeviceNet::dhcpDns(IPAddress dns)
{
    interface_of(*this, _network).dhcpDns(dns);
}

void CH32RVUsbDeviceNet::defaultRoute(bool enable)
{
    settings.defaultRoute = enable;
    interface_of(*this, _network);
}

bool CH32RVUsbDeviceNet::beginNetwork()
{
    NetworkUsbNcm &ifc = interface_of(*this, _network);
    if (settings.client) {
        ifc.config();                       /* all zero: DHCP */
    }
    if (!ifc.begin(settings.defaultRoute)) {
        return false;
    }
    if (settings.defaultRoute) {
        ifc.setDefault();
    }
    ifc.dhcpServer(settings.server);
    return true;
}

bool CH32RVUsbDeviceNet::networkUp() const
{
    return _network && _network->hasIP();
}

IPAddress CH32RVUsbDeviceNet::localIP() const
{
    return _network ? _network->localIP() : IPAddress((uint32_t)0);
}
