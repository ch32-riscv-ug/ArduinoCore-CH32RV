/* IPv4 networking on lwIP, with ESP32's Network API (Arduino core 3.x):
 * Network, NetworkInterface, NetworkClient, NetworkServer, NetworkUDP, and
 * configTime() / getLocalTime() (SNTP) and MDNS (an mDNS responder).
 * A link driver (Ethernet MAC, USB NCM) provides the NetworkInterface.
 * docs/network-stack.ja.md */
#pragma once
#include "NetworkManager.h"
#include "NetworkInterface.h"
#include "NetworkClient.h"
#include "NetworkServer.h"
#include "NetworkUdp.h"
#include "NetworkTime.h"
#include "NetworkMdns.h"
