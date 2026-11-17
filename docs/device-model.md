# COXVBN device registry

The current device-model slice provides a bounded registry for kernel-owned
device objects and a minimal lifecycle contract.

## Implemented

- Up to 32 registered devices with unique names.
- Device classes for platform, network, storage, input, and console devices.
- Probe on registration; failed probes are not published as active devices.
- Generation-bound references prevent stale references from resolving after
  unregister.
- Explicit remove, suspend, resume, lookup, and state-query operations.
- Host tests cover duplicate names, probe failure, capacity exhaustion,
  lifecycle transitions, and stale references.

## Not yet implemented

This registry does not discover PCI/PCIe, USB, SDIO, or virtio buses; match
hardware IDs; load firmware; allocate DMA buffers; register interrupts; or
bind a driver automatically. Callers must serialize registry operations.
There is no IRQ/preemption/SMP-safe locking yet. A concrete hardware driver
must still implement and validate its own bus and device protocol.
