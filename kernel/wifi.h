#ifndef COXVBN_WIFI_H
#define COXVBN_WIFI_H

#include <stddef.h>
#include <stdint.h>

#define WIFI_SSID_MAX 32U
#define WIFI_BSSID_LEN 6U
#define WIFI_SCAN_MAX 32U
#define WIFI_KEY_MAX 128U

enum wifi_state {
    WIFI_STATE_DOWN = 0,
    WIFI_STATE_READY,
    WIFI_STATE_SCANNING,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_ERROR
};

enum wifi_security {
    WIFI_SECURITY_OPEN = 0,
    WIFI_SECURITY_WPA2_PSK,
    WIFI_SECURITY_WPA3_SAE
};

struct wifi_network {
    uint8_t ssid[WIFI_SSID_MAX];
    uint8_t ssid_len;
    uint8_t bssid[WIFI_BSSID_LEN];
    uint16_t channel;
    int16_t rssi_dbm;
    enum wifi_security security;
};

struct wifi_device_ops {
    int (*start)(void *context);
    int (*stop)(void *context);
    int (*scan)(void *context);
    int (*connect)(void *context, const struct wifi_network *network,
                   const uint8_t *credential, size_t credential_len);
    int (*disconnect)(void *context);
};

struct wifi_device {
    const struct wifi_device_ops *ops;
    void *context;
    enum wifi_state state;
    struct wifi_network scan_results[WIFI_SCAN_MAX];
    uint32_t scan_count;
    uint32_t scan_dropped;
    int last_error;
};

int wifi_device_init(struct wifi_device *device,
                     const struct wifi_device_ops *ops, void *context);
int wifi_start(struct wifi_device *device);
int wifi_stop(struct wifi_device *device);
int wifi_scan(struct wifi_device *device);
int wifi_scan_report(struct wifi_device *device,
                     const struct wifi_network *network);
int wifi_scan_report_frame(struct wifi_device *device, const uint8_t *frame,
                           size_t frame_len, int16_t rssi_dbm);
int wifi_scan_complete(struct wifi_device *device, int status);
int wifi_connect(struct wifi_device *device, const struct wifi_network *network,
                 const uint8_t *credential, size_t credential_len);
int wifi_connection_complete(struct wifi_device *device, int status);
int wifi_disconnect(struct wifi_device *device);

/* Parse an unencrypted 802.11 beacon or probe response management frame. */
int wifi_parse_beacon(const uint8_t *frame, size_t frame_len,
                      int16_t rssi_dbm, struct wifi_network *network);

#endif
