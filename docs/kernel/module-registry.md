# Module registry and capability model

The module registry is a bounded kernel-internal table. The kernel loader
allocates a monotonically increasing module ID and creates a PENDING record.
A pending module is not active and cannot pass capability checks.

Activation is loader-only. It enrolls a receiver public key in the Key Vault
using the registry-assigned module ID and requires the configured
proof-of-possession verifier to accept the key and proof. Only after successful
enrollment does the registry transition the module to ACTIVE and make granted
capabilities usable. Failed enrollment leaves the module PENDING; duplicate
activation is rejected.

The loader authority is represented by the identity of a kernel-owned object.
The pointer must never be exposed through a module ABI or copied from an
untrusted request. This is an in-kernel guardrail, not a substitute for a
hardware privilege boundary. The current tree has no module loader or user
mode yet, so these APIs are not exposed to untrusted callers.

Capability checks require every requested bit to be granted and the module to
be ACTIVE. A zero or unknown capability mask is rejected. Revocation of an
active module first revokes its receiver key; only then does the module become
terminally REVOKED and lose all granted capabilities. Pending modules can be
revoked without a key. Revoked slots are not reused, so an old module
reference cannot become valid for a later module.

The registry currently provides fixed-capacity, in-memory bookkeeping only.
It does not load executable images, verify image signatures, isolate address
spaces, schedule modules, persist revocation, or bind the current CPU
execution context to a module reference. Key Vault enrollment also depends
on an injected verifier and in-memory storage. Before user mode or dynamic
module loading is enabled, those boundaries must be implemented and tested.
