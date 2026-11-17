# COXVBN Wi-Fi subsystem

The current implementation provides a bounded, hardware-independent Wi-Fi
control core in `kernel/wifi.c`. It validates power, scan, association, and
disconnect state transitions. Scan results are capped at 32 entries and
deduplicated by BSSID.

## Scan frame ingestion

Drivers may pass received 802.11 Beacon and Probe Response management frames
to `wifi_scan_report_frame()`. The helper parses the frame with
`wifi_parse_beacon()`, validates the resulting network, and adds or updates
the bounded scan cache. It is valid only while a scan is active. A driver
still owns RX buffers and must pass a complete, unencrypted management frame
plus its measured RSSI.

The parser currently recognizes open networks and RSN AKM suites for WPA2-PSK
and WPA3-SAE. It rejects malformed/duplicate SSID, DS channel, and RSN
elements, unsupported privacy modes, invalid BSSIDs, and channel conflicts.
Hidden SSIDs are not yet represented in scan results.

## Driver boundary

A hardware driver must implement `struct wifi_device_ops` and report scan
results through `wifi_scan_report()` or `wifi_scan_report_frame()`, then
complete scans and associations through the corresponding completion
functions. The driver is responsible for bus discovery (for example PCIe,
USB, or SDIO), firmware loading, radio configuration, interrupts/DMA, and
translating hardware events into this API.

## Security boundary

The core validates credential presence and WPA2-PSK length/hex bounds. It does
not implement WPA2 four-way handshake, WPA3-SAE, key derivation, cryptographic
primitives, or secure credential storage. Those require a reviewed 802.11
security implementation before protected networks can be joined. Credentials
are passed to the device backend and must not be logged or retained in
plaintext by a driver.

## Current limitations

This is not yet a usable wireless driver and does not make QEMU's virt machine
appear as a Wi-Fi adapter. No chipset, bus driver, firmware, 802.11 MAC/PHY,
authentication implementation, DHCP, or IP networking integration is present.
A concrete target chipset and its firmware/licensing constraints are required
for end-to-end hardware support. Host tests validate parser and control-core
behavior only; they do not claim over-the-air connectivity.
