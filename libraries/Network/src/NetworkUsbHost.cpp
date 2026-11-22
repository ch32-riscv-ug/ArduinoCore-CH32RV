/* NetworkUsbHost, and CH32RVUsbHost's EspUsbHost-style IP calls (declared in
 * CH32RVUsbHost.h). */
#include "NetworkUsbHost.h"

#include <stdlib.h>
#include <string.h>

#include "lwip/netif.h"

NetworkUsbHost::NetworkUsbHost(CH32RVUsbHost &host, uint8_t address) : _host(host), _address(address) {}

NetworkUsbHost::~NetworkUsbHost()
{
    end();
}

bool NetworkUsbHost::begin()
{
    if (!_host.networkReady(_address) && !_host.networkOpen(_address)) {
        return false;
    }
    CH32RVUsbHostNetworkInterfaceInfo info;
    if (!_host.getNetworkInterface(info)) {
        return false;
    }
    _address = info.address;
    if (!_frame) {
        _frame = static_cast<uint8_t *>(malloc(CH32RV_USB_HOST_NETWORK_MAX_FRAME));
        if (!_frame) {
            return false;
        }
    }
    /* The device has its MAC; this end takes the same with the last bit
     * flipped (as NetworkUsbNcm does from the other side). */
    uint8_t mac[6];
    memcpy(mac, info.macAddress, 6);
    mac[0] = (uint8_t)((mac[0] | 0x02) & ~0x01);   /* locally administered, unicast */
    mac[5] ^= 1;
    if (!netifStart(mac)) {
        return false;
    }
    setLinkUp(_host.networkLinkUp(_address));
    return true;
}

void NetworkUsbHost::end()
{
    netifStop();
    free(_frame);
    _frame = nullptr;
}

void NetworkUsbHost::poll()
{
    if (!_frame) {
        return;
    }
    if (!_host.networkReady(_address)) {
        setLinkUp(false);
        return;
    }
    setLinkUp(_host.networkLinkUp(_address));
    size_t n;
    while ((n = _host.networkReadFrame(_frame, CH32RV_USB_HOST_NETWORK_MAX_FRAME, _address)) != 0) {
        frameReceived(_frame, n);
    }
}

bool NetworkUsbHost::transmitFrame(const uint8_t *frame, size_t length)
{
    return _host.networkWriteFrame(frame, length, _address);
}

size_t NetworkUsbHost::printDriverInfo(Print &out) const
{
    CH32RVUsbHostNetworkInterfaceInfo info;
    if (!_host.getNetworkInterface(info)) {
        return out.print("USB host network (closed)");
    }
    return out.print(info.protocol == CH32RV_USB_HOST_NETWORK_PROTOCOL_CDC_NCM ? "USB host NCM" : "USB host ECM");
}

bool CH32RVUsbHost::networkAttachNetif(const CH32RVUsbHostNetworkConfig &config, uint8_t address)
{
    if (_networkIf) {
        return true;
    }
    NetworkUsbHost *ifc = new NetworkUsbHost(*this, address);
    if (!ifc) {
        return fail(CH32RVUsbHostError::NoMemory);
    }
    if (!config.dhcpClient) {
        ifc->config(config.ip, config.gateway, config.subnet, config.dns1, config.dns2);
    } else if ((uint32_t)config.dns1 || (uint32_t)config.dns2) {
        ifc->config((uint32_t)0, (uint32_t)0, (uint32_t)0, config.dns1, config.dns2);
    }
    if (!ifc->begin()) {
        delete ifc;
        return false;   /* begin() left the error (networkOpen()'s) */
    }
    _networkIf = ifc;
    return ok();
}

bool CH32RVUsbHost::networkDetachNetif(uint8_t address)
{
    if (!_networkIf || (address != CH32RV_USB_HOST_ANY_ADDRESS && address != _networkIf->address())) {
        return false;
    }
    delete _networkIf;
    _networkIf = nullptr;
    return true;
}

IPAddress CH32RVUsbHost::networkLocalIP(uint8_t address) const
{
    if (!_networkIf || (address != CH32RV_USB_HOST_ANY_ADDRESS && address != _networkIf->address())) {
        return IPAddress((uint32_t)0);
    }
    return _networkIf->localIP();
}
