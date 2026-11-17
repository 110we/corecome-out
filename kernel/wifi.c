#include "wifi.h"

static int network_valid(const struct wifi_network *network)
{
    uint32_t i;
    int nonzero_bssid = 0;

    if (network == 0 || network->ssid_len == 0U ||
        network->ssid_len > WIFI_SSID_MAX || network->channel == 0U ||
        network->channel > 196U ||
        network->security > WIFI_SECURITY_WPA3_SAE)
        return 0;

    for (i = 0U; i < WIFI_BSSID_LEN; ++i)
        nonzero_bssid |= network->bssid[i];
    return nonzero_bssid != 0;
}

int wifi_device_init(struct wifi_device *device,
                     const struct wifi_device_ops *ops, void *context)
{
    if (device == 0 || ops == 0 || ops->start == 0 || ops->stop == 0 ||
        ops->scan == 0 || ops->connect == 0 || ops->disconnect == 0)
        return -1;

    device->ops = ops;
    device->context = context;
    device->state = WIFI_STATE_DOWN;
    device->scan_count = 0U;
    device->scan_dropped = 0U;
    device->last_error = 0;
    return 0;
}

int wifi_start(struct wifi_device *device)
{
    int rc;
    if (device == 0 || device->ops == 0 ||
        (device->state != WIFI_STATE_DOWN &&
         device->state != WIFI_STATE_ERROR))
        return -1;
    rc = device->ops->start(device->context);
    if (rc != 0) {
        device->state = WIFI_STATE_ERROR;
        device->last_error = rc;
        return rc;
    }
    device->state = WIFI_STATE_READY;
    device->last_error = 0;
    return 0;
}

int wifi_stop(struct wifi_device *device)
{
    int rc;
    if (device == 0 || device->ops == 0 ||
        device->state == WIFI_STATE_DOWN)
        return -1;
    rc = device->ops->stop(device->context);
    if (rc != 0) {
        device->state = WIFI_STATE_ERROR;
        device->last_error = rc;
        return rc;
    }
    device->state = WIFI_STATE_DOWN;
    device->scan_count = 0U;
    device->last_error = 0;
    return 0;
}

int wifi_scan(struct wifi_device *device)
{
    int rc;
    if (device == 0 || device->ops == 0 ||
        device->state != WIFI_STATE_READY)
        return -1;
    device->scan_count = 0U;
    device->scan_dropped = 0U;
    device->state = WIFI_STATE_SCANNING;
    rc = device->ops->scan(device->context);
    if (rc != 0) {
        device->state = WIFI_STATE_READY;
        device->last_error = rc;
        return rc;
    }
    return 0;
}

int wifi_scan_report(struct wifi_device *device,
                     const struct wifi_network *network)
{
    uint32_t i;
    if (device == 0 || device->state != WIFI_STATE_SCANNING ||
        !network_valid(network))
        return -1;

    for (i = 0U; i < device->scan_count; ++i) {
        uint32_t j;
        int same = 1;
        for (j = 0U; j < WIFI_BSSID_LEN; ++j)
            if (device->scan_results[i].bssid[j] != network->bssid[j])
                same = 0;
        if (same) {
            device->scan_results[i] = *network;
            return 0;
        }
    }
    if (device->scan_count >= WIFI_SCAN_MAX) {
        ++device->scan_dropped;
        return -2;
    }
    device->scan_results[device->scan_count++] = *network;
    return 0;
}

int wifi_scan_complete(struct wifi_device *device, int status)
{
    if (device == 0 || device->state != WIFI_STATE_SCANNING)
        return -1;
    device->last_error = status;
    device->state = status == 0 ? WIFI_STATE_READY : WIFI_STATE_ERROR;
    return status;
}

int wifi_connect(struct wifi_device *device, const struct wifi_network *network,
                 const uint8_t *credential, size_t credential_len)
{
    int rc;
    if (device == 0 || device->ops == 0 ||
        device->state != WIFI_STATE_READY || !network_valid(network))
        return -1;
    if (network->security == WIFI_SECURITY_OPEN) {
        if (credential_len != 0U || credential != 0)
            return -2;
    } else if (credential == 0 || credential_len == 0U ||
               credential_len > WIFI_KEY_MAX) {
        return -2;
    } else if (network->security == WIFI_SECURITY_WPA2_PSK &&
               credential_len != 64U &&
               (credential_len < 8U || credential_len > 63U)) {
        return -2;
    }

    device->state = WIFI_STATE_CONNECTING;
    rc = device->ops->connect(device->context, network,
                              credential, credential_len);
    if (rc != 0) {
        device->state = WIFI_STATE_READY;
        device->last_error = rc;
        return rc;
    }
    return 0;
}

int wifi_connection_complete(struct wifi_device *device, int status)
{
    if (device == 0 || device->state != WIFI_STATE_CONNECTING)
        return -1;
    device->last_error = status;
    device->state = status == 0 ? WIFI_STATE_CONNECTED : WIFI_STATE_READY;
    return status;
}

int wifi_disconnect(struct wifi_device *device)
{
    int rc;
    if (device == 0 || device->ops == 0 ||
        device->state != WIFI_STATE_CONNECTED)
        return -1;
    rc = device->ops->disconnect(device->context);
    if (rc != 0) {
        device->state = WIFI_STATE_ERROR;
        device->last_error = rc;
        return rc;
    }
    device->state = WIFI_STATE_READY;
    device->last_error = 0;
    return 0;
}
