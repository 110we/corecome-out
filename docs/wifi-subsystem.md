# COXVBN Wi-Fi subsystem

The current implementation adds a bounded, hardware-independent Wi-Fi control
core in `kernel/wifi.c`. It defines a device-operations boundary and validates
state transitions for power, scan, association, and disconnect operations.
Scan results are capped at 32 entries and deduplicated by BSSID.

## Driver boundary

A hardware driver must implement `struct wifi_device_ops` and report scan
results through `wifi_scan_report()`, then complete scans and associations
through the corresponding completion functions. The driver is responsible for
bus discovery (for example PCIe, USB, or SDIO), firmware loading, radio
configuration, interrupts/DMA, and translating hardware events into this API.

## Security boundary

The core validates credential presence and WPA2-PSK length bounds. It does not
implement WPA2 four-way handshake, WPA3-SAE, key derivation, cryptographic
primitives, or secure credential storage. Those must be supplied by a reviewed
802.11 security implementation before real protected networks can be joined.
Credentials are passed to the device backend and must not be logged or retained
in plaintext by a driver.

## Current limitations

This is not yet a usable wireless driver and does not make QEMU's virt machine
appear as a Wi-Fi adapter. No chipset, bus driver, firmware, 802.11 MAC/PHY,
authentication implementation, DHCP, or IP networking integration is present.
A concrete target chipset and its firmware/licensing constraints are required
for end-to-end hardware support. Host tests cover the control-core state machine
only; they do not claim over-the-air connectivity.
