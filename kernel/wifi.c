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
    } else if (network->security == WIFI_SECURITY_WPA2_PSK) {
        size_t i;
        if (credential_len == 64U) {
            for (i = 0U; i < credential_len; ++i) {
                uint8_t ch = credential[i];
                if (!((ch >= '0' && ch <= '9') ||
                      (ch >= 'a' && ch <= 'f') ||
                      (ch >= 'A' && ch <= 'F')))
                    return -2;
            }
        } else if (credential_len < 8U || credential_len > 63U) {
            return -2;
        }
    } else if (network->security == WIFI_SECURITY_WPA3_SAE &&
               credential_len > 63U) {
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


static uint16_t wifi_get_le16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

/* Parse the RSN information element and accept only PSK or SAE AKMs. */
static int wifi_parse_rsn(const uint8_t *p, size_t length,
                          enum wifi_security *security)
{
    size_t pos = 0U;
    uint16_t count;
    uint32_t i;
    int has_psk = 0;
    int has_sae = 0;

    if (p == 0 || security == 0 || length < 2U + 4U + 2U)
        return -1;
    if (wifi_get_le16(p) != 1U)
        return -1;
    pos = 2U;
    if (length - pos < 4U) return -1;
    pos += 4U; /* Group data cipher suite. */

    if (length - pos < 2U) return -1;
    count = wifi_get_le16(p + pos);
    pos += 2U;
    if (count == 0U || (size_t)count > (length - pos) / 4U)
        return -1;
    pos += (size_t)count * 4U;

    if (length - pos < 2U) return -1;
    count = wifi_get_le16(p + pos);
    pos += 2U;
    if (count == 0U || (size_t)count > (length - pos) / 4U)
        return -1;
    for (i = 0U; i < count; ++i) {
        const uint8_t *suite = p + pos + (size_t)i * 4U;
        if (suite[0] == 0x00U && suite[1] == 0x0fU &&
            suite[2] == 0xacU) {
            if (suite[3] == 2U) has_psk = 1;
            if (suite[3] == 8U) has_sae = 1;
        }
    }
    if (!has_psk && !has_sae)
        return -2;
    *security = has_sae ? WIFI_SECURITY_WPA3_SAE : WIFI_SECURITY_WPA2_PSK;
    return 0;
}

int wifi_parse_beacon(const uint8_t *frame, size_t frame_len,
                      int16_t rssi_dbm, struct wifi_network *network)
{
    uint16_t fc;
    uint16_t capability;
    uint32_t i;
    size_t pos;
    int have_ssid = 0;
    int have_channel = 0;
    int have_rsn = 0;
    struct wifi_network parsed;

    if (frame == 0 || network == 0 || frame_len < 36U)
        return -1;

    fc = wifi_get_le16(frame);
    if ((fc & 0x0003U) != 0U || ((fc >> 2) & 0x3U) != 0U ||
        ((((fc >> 4) & 0x0fU) != 8U) && (((fc >> 4) & 0x0fU) != 5U)) ||
        (fc & 0x4000U) != 0U)
        return -2;

    for (i = 0U; i < WIFI_BSSID_LEN; ++i)
        parsed.bssid[i] = frame[16U + i];
    if ((parsed.bssid[0] & 1U) != 0U)
        return -3;

    capability = wifi_get_le16(frame + 34U);
    if ((capability & 0x0010U) != 0U)
        return -4; /* Legacy WEP privacy is deliberately unsupported. */

    parsed.ssid_len = 0U;
    parsed.channel = 0U;
    parsed.rssi_dbm = rssi_dbm;
    parsed.security = WIFI_SECURITY_OPEN;

    pos = 36U;
    while (pos < frame_len) {
        uint8_t id, length;
        const uint8_t *value;
        if (frame_len - pos < 2U) return -5;
        id = frame[pos];
        length = frame[pos + 1U];
        pos += 2U;
        if ((size_t)length > frame_len - pos) return -5;
        value = frame + pos;

        if (id == 0U) {
            if (have_ssid || length == 0U || length > WIFI_SSID_MAX)
                return -6;
            for (i = 0U; i < length; ++i)
                parsed.ssid[i] = value[i];
            parsed.ssid_len = length;
            have_ssid = 1;
        } else if (id == 3U) {
            if (have_channel || length != 1U || value[0] == 0U ||
                value[0] > 196U)
                return -7;
            parsed.channel = value[0];
            have_channel = 1;
        } else if (id == 48U) {
            if (have_rsn || wifi_parse_rsn(value, length,
                                           &parsed.security) != 0)
                return -8;
            have_rsn = 1;
        }
        pos += length;
    }

    if (!have_ssid || !have_channel)
        return -9;
    *network = parsed;
    return 0;
}
