#include "NetworkMdns.h"
#include "NetworkManager.h"
#include "internal/NetworkCore.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "lwip/apps/mdns.h"
#include "lwip/apps/mdns_out.h"
#include "lwip/prot/dns.h"
#include "lwip/netif.h"
#include "lwip/timeouts.h"

NetworkMdns MDNS;

namespace {
struct Service {
    char service[17];      /* "_http" */
    uint8_t proto;         /* DNSSD_PROTO_TCP / UDP */
    uint16_t port;
    uint8_t txtLength;
    uint8_t txt[96];       /* items, each after a length byte */
    bool used;
};

bool running;
char host[MDNS_LABEL_MAXLEN + 1];
char instance[MDNS_LABEL_MAXLEN + 1];
Service services[MDNS_MAX_SERVICES];
}

static void txt_items(struct mdns_service *service, void *userdata)
{
    const Service *s = static_cast<const Service *>(userdata);
    for (uint8_t at = 0; at < s->txtLength; at = (uint8_t)(at + 1 + s->txt[at])) {
        mdns_resp_add_service_txtitem(service, reinterpret_cast<const char *>(s->txt + at + 1), s->txt[at]);
    }
}

static void add_services(struct netif *n)
{
    for (auto &s : services) {
        if (s.used) {
            mdns_resp_add_service(n, instance, s.service, (enum mdns_sd_proto)s.proto, s.port, txt_items, &s);
        }
    }
}

/* mdns_resp_remove_netif() stops the probe timer only: the delayed answers and the multicast holds it set with
 * mdns_set_timeout() (MDNS_RESPONSE_DELAY, 20 to 120 ms after a question) stay queued with the netif as their
 * argument, and run on it after the interface is gone - a netif deleted by NetworkInterface::netifStop() (a driver's
 * end()) was read again (heap-use-after-free in netif_mdns_data). Cancel them first. */
static void remove_netif(struct netif *n)
{
    sys_untimeout(mdns_send_multicast_msg_delayed_ipv4, n);
    sys_untimeout(mdns_send_unicast_msg_delayed_ipv4, n);
    sys_untimeout(mdns_multicast_timeout_reset_ipv4, n);
    sys_untimeout(mdns_multicast_probe_timeout_reset_ipv4, n);
    sys_untimeout(mdns_multicast_timeout_25ttl_reset_ipv4, n);
#if LWIP_IPV6
    sys_untimeout(mdns_send_multicast_msg_delayed_ipv6, n);
    sys_untimeout(mdns_send_unicast_msg_delayed_ipv6, n);
    sys_untimeout(mdns_multicast_timeout_reset_ipv6, n);
    sys_untimeout(mdns_multicast_probe_timeout_reset_ipv6, n);
    sys_untimeout(mdns_multicast_timeout_25ttl_reset_ipv6, n);
#endif
    mdns_resp_remove_netif(n);
}

/* Interfaces starting and stopping, from the Network library. */
static void netif_changed(struct netif *n, bool up)
{
    if (!n->state) {
        return;   /* the loopback interface: no NetworkInterface, nothing to announce */
    }
    if (up) {
        if (mdns_resp_add_netif(n, host) == ERR_OK) {
            add_services(n);
            mdns_resp_announce(n);
        }
    } else if (mdns_resp_netif_active(n)) {
        remove_netif(n);
    }
}

bool NetworkMdns::begin(const char *hostName)
{
    if (!hostName || !*hostName || strlen(hostName) > MDNS_LABEL_MAXLEN) {
        return false;
    }
    ch32rv_net::start();
    end();
    strcpy(host, hostName);
    if (!instance[0]) {
        strcpy(instance, hostName);
    }
    static bool initialised;
    if (!initialised) {
        initialised = true;
        mdns_resp_init();
    }
    running = true;
    ch32rv_net::setNetifListener(netif_changed);
    struct netif *n;
    NETIF_FOREACH(n) {
        netif_changed(n, true);
    }
    return true;
}

void NetworkMdns::end()
{
    if (!running) {
        return;
    }
    running = false;
    ch32rv_net::setNetifListener(nullptr);
    struct netif *n;
    NETIF_FOREACH(n) {
        if (mdns_resp_netif_active(n)) {
            remove_netif(n);
        }
    }
}

bool NetworkMdns::setInstanceName(const char *name)
{
    if (!name || strlen(name) > MDNS_LABEL_MAXLEN) {
        return false;
    }
    strcpy(instance, name);
    return true;
}

/* "http" or "_http" -> "_http". */
static bool label(char *out, size_t size, const char *in)
{
    if (!in || !*in) {
        return false;
    }
    const char *bare = in[0] == '_' ? in + 1 : in;
    if (strlen(bare) + 2 > size) {
        return false;
    }
    out[0] = '_';
    strcpy(out + 1, bare);
    return true;
}

static Service *find(const char *service, const char *proto)
{
    char name[sizeof services[0].service];
    if (!label(name, sizeof name, service)) {
        return nullptr;
    }
    const uint8_t p = (proto && strstr(proto, "udp")) ? DNSSD_PROTO_UDP : DNSSD_PROTO_TCP;
    for (auto &s : services) {
        if (s.used && s.proto == p && !strcmp(s.service, name)) {
            return &s;
        }
    }
    return nullptr;
}

bool NetworkMdns::addService(const char *service, const char *proto, uint16_t port)
{
    if (find(service, proto)) {
        return true;
    }
    for (auto &s : services) {
        if (!s.used) {
            if (!label(s.service, sizeof s.service, service)) {
                return false;
            }
            s.proto = (proto && strstr(proto, "udp")) ? DNSSD_PROTO_UDP : DNSSD_PROTO_TCP;
            s.port = port;
            s.txtLength = 0;
            s.used = true;
            if (running) {
                struct netif *n;
                NETIF_FOREACH(n) {
                    if (mdns_resp_netif_active(n)) {
                        mdns_resp_add_service(n, instance, s.service, (enum mdns_sd_proto)s.proto, s.port, txt_items, &s);
                        mdns_resp_announce(n);
                    }
                }
            }
            return true;
        }
    }
    return false;
}

bool NetworkMdns::addServiceTxt(const char *service, const char *proto, const char *key, const char *value)
{
    Service *s = find(service, proto);
    if (!s || !key) {
        return false;
    }
    const size_t k = strlen(key), v = value ? strlen(value) : 0;
    const size_t item = k + (value ? 1 + v : 0);
    if (item > 63 || s->txtLength + 1 + item > sizeof s->txt) {
        return false;
    }
    uint8_t *at = s->txt + s->txtLength;
    *at++ = (uint8_t)item;
    memcpy(at, key, k);
    if (value) {
        at[k] = '=';
        memcpy(at + k + 1, value, v);
    }
    s->txtLength = (uint8_t)(s->txtLength + 1 + item);
    if (running) {
        struct netif *n;
        NETIF_FOREACH(n) {
            if (mdns_resp_netif_active(n)) {
                mdns_resp_announce(n);
            }
        }
    }
    return true;
}

IPAddress NetworkMdns::queryHost(const char *host, uint32_t timeout)
{
    char name[MDNS_LABEL_MAXLEN + 8];
    if (!host || !*host) {
        return IPAddress((uint32_t)0);
    }
    const size_t n = strlen(host);
    const bool local = n > 6 && strcmp(host + n - 6, ".local") == 0;
    if (n + (local ? 1 : 7) > sizeof name) {
        return IPAddress((uint32_t)0);
    }
    strcpy(name, host);
    if (!local) {
        strcat(name, ".local");
    }
    IPAddress ip;
    return Network.hostByName(name, ip, timeout) ? ip : IPAddress((uint32_t)0);
}

/* ---- queryService() ---- */

namespace {
constexpr uint8_t RESULTS = 4;
struct Result {
    struct mdns_domain instance, host;
    uint16_t port;
    uint32_t ip;
    uint8_t txtLength;
    uint8_t txt[96];
    bool srv;
};
Result results[RESULTS];
uint8_t resultCount;
struct mdns_domain wanted;          /* _service._proto.local */

bool same(const struct mdns_domain &a, const struct mdns_domain &b)
{
    if (a.length != b.length) {
        return false;
    }
    for (uint16_t i = 0; i < a.length; i++) {
        uint8_t x = a.name[i], y = b.name[i];
        if (x >= 'A' && x <= 'Z') x = (uint8_t)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (uint8_t)(y + 32);
        if (x != y) {
            return false;
        }
    }
    return true;
}

/* A domain from a record's variable part (labels, then the zero). */
bool domain_from(const char *p, int length, struct mdns_domain &d)
{
    if (length <= 0 || length > MDNS_DOMAIN_MAXLEN) {
        return false;
    }
    memset(&d, 0, sizeof d);
    memcpy(d.name, p, (size_t)length);
    d.length = (uint16_t)length;
    return true;
}

Result *find(const struct mdns_domain &instance, bool add)
{
    for (uint8_t i = 0; i < resultCount; i++) {
        if (same(results[i].instance, instance)) {
            return &results[i];
        }
    }
    if (!add || resultCount >= RESULTS) {
        return nullptr;
    }
    Result &r = results[resultCount++];
    memset(&r, 0, sizeof r);
    r.instance = instance;
    return &r;
}

void on_answer(struct mdns_answer *answer, const char *varpart, int varlen, int flags, void *arg)
{
    (void)flags;
    (void)arg;
    struct mdns_domain d;
    switch (answer->info.type) {
    case DNS_RRTYPE_PTR:
        if (same(answer->info.domain, wanted) && domain_from(varpart, varlen, d)) {
            find(d, true);
        }
        break;
    case DNS_RRTYPE_SRV:
        if (varlen > 6) {
            Result *r = find(answer->info.domain, true);
            if (r && domain_from(varpart + 6, varlen - 6, r->host)) {
                r->port = (uint16_t)((uint8_t)varpart[4] << 8 | (uint8_t)varpart[5]);
                r->srv = true;
            }
        }
        break;
    case DNS_RRTYPE_TXT: {
        Result *r = find(answer->info.domain, true);
        if (r) {
            r->txtLength = (uint8_t)(varlen < (int)sizeof r->txt ? varlen : (int)sizeof r->txt);
            memcpy(r->txt, varpart, r->txtLength);
        }
        break;
    }
    case DNS_RRTYPE_A:
        if (varlen == 4) {
            uint32_t ip;
            memcpy(&ip, varpart, 4);
            for (uint8_t i = 0; i < resultCount; i++) {
                if (results[i].srv && same(results[i].host, answer->info.domain)) {
                    results[i].ip = ip;
                }
            }
        }
        break;
    default:
        break;
    }
}

/* The first label of a domain as text. */
String first_label(const struct mdns_domain &d)
{
    if (!d.length || d.name[0] == 0 || d.name[0] >= d.length) {
        return String();
    }
    char out[MDNS_LABEL_MAXLEN + 1];
    const uint8_t n = d.name[0] > MDNS_LABEL_MAXLEN ? MDNS_LABEL_MAXLEN : d.name[0];
    memcpy(out, d.name + 1, n);
    out[n] = 0;
    return String(out);
}

/* The TXT item `i` (or the one named `key`): its key and value. */
bool txt_item(const Result &r, int index, const char *key, String *k, String *v)
{
    int i = 0;
    for (uint8_t at = 0; at < r.txtLength; at = (uint8_t)(at + 1 + r.txt[at]), i++) {
        const uint8_t n = r.txt[at];
        if (!n || at + 1 + n > r.txtLength) {
            continue;
        }
        const char *item = reinterpret_cast<const char *>(r.txt + at + 1);
        const char *eq = static_cast<const char *>(memchr(item, '=', n));
        const size_t klen = eq ? (size_t)(eq - item) : n;
        const bool hit = key ? (strlen(key) == klen && strncasecmp(item, key, klen) == 0) : i == index;
        if (hit) {
            char buf[97];
            if (k) {
                memcpy(buf, item, klen);
                buf[klen] = 0;
                *k = String(buf);
            }
            if (v) {
                const size_t vlen = eq ? (size_t)(n - klen - 1) : 0;
                memcpy(buf, eq ? eq + 1 : item, vlen);
                buf[vlen] = 0;
                *v = String(buf);
            }
            return true;
        }
    }
    return false;
}

const Result *at(int idx)
{
    return idx >= 0 && idx < resultCount && results[idx].srv ? &results[idx] : nullptr;
}
}  // namespace

int NetworkMdns::queryService(const char *service, const char *proto, uint32_t timeout)
{
    resultCount = 0;
    if (!running || !service || !proto) {
        return 0;
    }
    char svc[MDNS_LABEL_MAXLEN + 2];
    snprintf(svc, sizeof svc, "%s%s", service[0] == '_' ? "" : "_", service);
    const char *p = proto[0] == '_' ? proto + 1 : proto;
    const enum mdns_sd_proto sd = strcasecmp(p, "udp") == 0 ? DNSSD_PROTO_UDP : DNSSD_PROTO_TCP;
    /* What the PTR answers are for: _service._proto.local. */
    memset(&wanted, 0, sizeof wanted);
    const char *labels[3] = {svc, sd == DNSSD_PROTO_UDP ? "_udp" : "_tcp", "local"};
    for (const char *l : labels) {
        const size_t n = strlen(l);
        if (wanted.length + 1 + n + 1 > MDNS_DOMAIN_MAXLEN) {
            return 0;
        }
        wanted.name[wanted.length++] = (uint8_t)n;
        memcpy(wanted.name + wanted.length, l, n);
        wanted.length = (uint16_t)(wanted.length + n);
    }
    wanted.name[wanted.length++] = 0;
    uint8_t ids[4];
    uint8_t asked = 0;
    struct netif *n;
    NETIF_FOREACH(n) {
        if (asked < sizeof ids && mdns_resp_netif_active(n) &&
            mdns_search_service(nullptr, svc, sd, n, on_answer, nullptr, &ids[asked]) == ERR_OK) {
            asked++;
        }
    }
    if (!asked) {
        return 0;
    }
    /* Answers come for as long as the responders take: the whole timeout. */
    ch32rv_net::waitFor([](void *) { return false; }, nullptr, timeout);
    for (uint8_t i = 0; i < asked; i++) {
        mdns_search_stop(ids[i]);
    }
    /* An instance whose address did not come along: asked by name. */
    int count = 0;
    for (uint8_t i = 0; i < resultCount; i++) {
        Result &r = results[i];
        if (!r.srv) {
            continue;
        }
        if (!r.ip) {
            r.ip = (uint32_t)queryHost(first_label(r.host).c_str(), 1000);
        }
        if (i != count) {
            results[count] = r;
        }
        count++;
    }
    resultCount = (uint8_t)count;
    return count;
}

String NetworkMdns::hostname(int idx) { const Result *r = at(idx); return r ? first_label(r->host) : String(); }
String NetworkMdns::instanceName(int idx) { const Result *r = at(idx); return r ? first_label(r->instance) : String(); }
IPAddress NetworkMdns::IP(int idx) { const Result *r = at(idx); return IPAddress(r ? r->ip : 0u); }
uint16_t NetworkMdns::port(int idx) { const Result *r = at(idx); return r ? r->port : 0; }

int NetworkMdns::numTxt(int idx)
{
    const Result *r = at(idx);
    int n = 0;
    for (uint8_t a = 0; r && a < r->txtLength; a = (uint8_t)(a + 1 + r->txt[a])) {
        n += r->txt[a] ? 1 : 0;
    }
    return n;
}

bool NetworkMdns::hasTxt(int idx, const char *key)
{
    const Result *r = at(idx);
    return r && key && txt_item(*r, 0, key, nullptr, nullptr);
}

String NetworkMdns::txt(int idx, const char *key)
{
    String v;
    const Result *r = at(idx);
    if (r && key) {
        txt_item(*r, 0, key, nullptr, &v);
    }
    return v;
}

String NetworkMdns::txtKey(int idx, int txtIdx)
{
    String k;
    const Result *r = at(idx);
    if (r) {
        txt_item(*r, txtIdx, nullptr, &k, nullptr);
    }
    return k;
}

String NetworkMdns::txt(int idx, int txtIdx)
{
    String v;
    const Result *r = at(idx);
    if (r) {
        txt_item(*r, txtIdx, nullptr, nullptr, &v);
    }
    return v;
}
