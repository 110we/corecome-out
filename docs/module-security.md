# Module receiver-key lifecycle

## Enrollment and activation

A module registration begins in PENDING state. It receives no effective
capabilities and cannot be treated as runnable. The trusted kernel loader
activates it only after the Key Vault accepts receiver public-key enrollment
and proof of possession. The module ID comes from the registry record, not
from an untrusted module request. A failed proof leaves the module pending.

The Key Vault stores a public key and a reference tuple
(module ID, receiver ID, key ID, key version). It does not expose a key-read
API. A platform cryptographic backend supplies the proof verifier through
`coxvbn_key_vault_set_receiver_verifier`.

## Rotation and revocation

Rotation creates a new key ID and increments the receiver key version. The
old record is revoked as part of the same bounded in-memory operation. A
revoked record is never reactivated or reused, and attempts to rotate from an
old reference fail. Explicit module revocation revokes its enrolled receiver
key before making the module terminally revoked and clearing capabilities.

The current implementation is an in-memory contract with a fixed record
capacity. It does not yet provide persistent audit storage, crash-atomic
durability, secure-element storage, cryptographic algorithms, secure erasure,
or anti-rollback protection across reboot. Those require an audited crypto
backend and trusted monotonic/persistent storage. In particular, retaining a
revocation record in RAM is not proof that an old key can never be restored
from an external snapshot or backup.

## Security boundary

The proof callback in host tests is intentionally a deterministic test stub,
not production cryptography. Until a platform backend is integrated, receiver
enrollment and verification must not be treated as production-authenticated.
The loader-authority pointer is an internal software guardrail, not a hardware
security boundary. The API is not yet exposed to user mode; a future loader
must bind its authority to a protected kernel execution context.
