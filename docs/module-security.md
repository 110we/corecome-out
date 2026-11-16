# Module receiver-key lifecycle

## Enrollment

A module may enroll a receiver public key under its own module identity. The
caller identity passed to the Key Vault must be obtained by trusted kernel
code from the current execution context; it must never be accepted from an
untrusted module payload. Enrollment requires a proof-of-possession check
against the candidate public key.

The Key Vault stores a public key and a reference tuple
(module ID, receiver ID, key ID, key version). It does not expose a key-read
API. A platform cryptographic backend supplies the proof verifier through
`coxvbn_key_vault_set_receiver_verifier`.

## Rotation and revocation

Rotation creates a new key ID and increments the receiver key version. The
old record is revoked as part of the same bounded in-memory operation. A
revoked record is never reactivated or reused, and attempts to rotate from an
old reference fail. Explicit revocation is also terminal.

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
The API is not yet exposed to user mode; kernel integration must bind
`caller_module_id` to an authenticated module execution context.
