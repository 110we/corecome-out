# Recovery and isolated stores

This document defines three separate kernel subsystems. The current code is an
early, in-memory contract and does not claim persistent storage, cryptography,
networking, watchdog support, or a working recovery server.

## Key Vault

The Key Vault is the only interface allowed to verify recovery credentials.
Callers provide a proof; key material is never returned by the API. The
cryptographic and secure-storage backend is intentionally injected and must
fail closed when absent. A production backend still needs reviewed
cryptography, protected key storage, constant-time verification, rate limits,
and key rotation.

## Information Store

The Information Store is a separate bounded in-memory ring for structured
event metadata. It must never contain plaintext keys, passwords, or recovery
proofs. The current implementation is volatile and is not a filesystem or
persistent partition.

## Emergency Recovery

- TCP port 3000 is a reserved recovery-channel port, not a running listener.
- The channel is disabled after initialization and only becomes logically
  enabled after an explicit fault-mode transition.
- Recovery commands are an allowlist: reboot, diagnostics, read logs, and
  system reset. Unknown commands are rejected.
- Every request requires Key Vault verification. Missing verifier or invalid
  proof is denied.
- Destructive reset/format operations require a separately confirmed target
  device and are not implemented by this interface.

A kernel that has fully crashed cannot safely execute its own recovery code.
A future independent watchdog/recovery environment must detect the failure,
take control, initialize the network stack, and expose the channel. Until then,
the fault transition is only a testable state contract; it does not open a
network socket.

## Isolation and initialization

The three modules have separate headers, implementation files, and APIs.
Persistent partitioning, MMU-backed isolation, hardware entropy, cryptographic
implementation, network transport, watchdog takeover, and command execution
remain future milestones.
