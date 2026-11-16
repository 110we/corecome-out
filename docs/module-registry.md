# Module registry and capability model

The module registry is a bounded kernel-internal table. The kernel loader
registers modules and receives monotonically allocated module IDs. A module
requests capabilities, but the loader supplies the approved grant set;
registration rejects unknown bits and any grant not present in the request.

The loader authority is represented by the identity of a kernel-owned object.
The pointer must never be exposed through a module ABI or copied from an
untrusted request. This is an in-kernel guardrail, not a substitute for a
hardware privilege boundary. The current tree has no module loader or user
mode yet, so registration is not exposed to untrusted callers.

Capability checks require every requested bit to be granted. A zero or
unknown capability mask is rejected. Revocation is terminal for that module
record and clears its granted capabilities. Revoked slots are not reused,
so an old module reference cannot become valid for a later module.

The registry currently provides fixed-capacity, in-memory bookkeeping only.
It does not load executable images, verify signatures, isolate address
spaces, schedule modules, persist revocation, or bind the current CPU
execution context to a module reference. Before user mode or dynamic module
loading is enabled, those boundaries must be implemented and tested.
