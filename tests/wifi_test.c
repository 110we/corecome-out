#include "wifi.h"
#include <assert.h>
#include <string.h>

struct mock { int starts, stops, scans, connects, disconnects; };
static int start(void *p) { ((struct mock *)p)->starts++; return 0; }
static int stop(void *p) { ((struct mock *)p)->stops++; return 0; }
static int scan(void *p) { ((struct mock *)p)->scans++; return 0; }
static int connect(void *p, const struct wifi_network *n,
                   const uint8_t *key, size_t len)
{
    struct mock *m = p;
    assert(n != 0);
    (void)key; (void)len;
    m->connects++;
    return 0;
}
static int disconnect(void *p) { ((struct mock *)p)->disconnects++; return 0; }

int main(void)
{
    static const struct wifi_device_ops ops = {
        start, stop, scan, connect, disconnect
    };
    struct mock mock = {0};
    struct wifi_device dev;
    struct wifi_network net;
    const uint8_t key[] = "correct-horse";
    uint32_t i;

    memset(&net, 0, sizeof(net));
    memcpy(net.ssid, "COXVBN-test", 11U);
    net.ssid_len = 11U;
    net.bssid[0] = 0x02U;
    net.bssid[5] = 0x01U;
    net.channel = 6U;
    net.rssi_dbm = -42;
    net.security = WIFI_SECURITY_WPA2_PSK;

    {
        uint8_t beacon[64] = {0};
        struct wifi_network parsed;
        size_t n = 36U;
        /* Management beacon: subtype 8, BSSID at addr3, no WEP privacy. */
        beacon[0] = 0x80U;
        beacon[16] = 0x02U;
        beacon[21] = 0x01U;
        beacon[36] = 0U; beacon[37] = 4U;
        memcpy(beacon + 38U, "test", 4U); n = 42U;
        beacon[n++] = 3U; beacon[n++] = 1U; beacon[n++] = 11U;
        assert(wifi_parse_beacon(beacon, n, -55, &parsed) == 0);
        assert(parsed.ssid_len == 4U && memcmp(parsed.ssid, "test", 4U) == 0);
        assert(parsed.channel == 11U && parsed.rssi_dbm == -55);
        assert(parsed.security == WIFI_SECURITY_OPEN);
        beacon[0] = 0x40U; /* Probe request is not a scan result. */
        assert(wifi_parse_beacon(beacon, n, -55, &parsed) == -2);
        beacon[0] = 0x80U;
        beacon[34] = 0x10U; /* WEP privacy bit. */
        assert(wifi_parse_beacon(beacon, n, -55, &parsed) == -4);
        beacon[34] = 0U;
        assert(wifi_parse_beacon(beacon, 37U, -55, &parsed) == -5);
    }

    assert(wifi_device_init(&dev, &ops, &mock) == 0);
    assert(wifi_scan(&dev) == -1);
    assert(wifi_start(&dev) == 0 && dev.state == WIFI_STATE_READY);
    assert(wifi_scan(&dev) == 0 && dev.state == WIFI_STATE_SCANNING);
    assert(wifi_scan_report(&dev, &net) == 0);
    assert(wifi_scan_report(&dev, &net) == 0);
    assert(dev.scan_count == 1U);
    assert(wifi_scan_complete(&dev, 0) == 0);
    assert(wifi_connect(&dev, &net, key, sizeof(key) - 1U) == 0);
    assert(dev.state == WIFI_STATE_CONNECTING);
    assert(wifi_connection_complete(&dev, 0) == 0);
    assert(dev.state == WIFI_STATE_CONNECTED);
    assert(wifi_disconnect(&dev) == 0);
    assert(wifi_stop(&dev) == 0);

    assert(wifi_start(&dev) == 0);
    assert(wifi_scan(&dev) == 0);
    for (i = 0U; i < WIFI_SCAN_MAX; ++i) {
        net.bssid[4] = (uint8_t)(i + 1U);
        assert(wifi_scan_report(&dev, &net) == 0);
    }
    net.bssid[4] = 0xf0U;
    assert(wifi_scan_report(&dev, &net) == -2);
    assert(dev.scan_count == WIFI_SCAN_MAX && dev.scan_dropped == 1U);
    assert(wifi_scan_complete(&dev, -7) == -7);
    assert(dev.state == WIFI_STATE_ERROR && dev.last_error == -7);

    assert(wifi_start(&dev) == 0);
    assert(wifi_connect(&dev, &net, key, 3U) == -2);
    assert(wifi_stop(&dev) == 0);
    assert(mock.starts == 3 && mock.stops == 2);
    assert(mock.scans == 2 && mock.connects == 1 && mock.disconnects == 1);
    return 0;
}
