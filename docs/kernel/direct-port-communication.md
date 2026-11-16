# COXVBN Direct Port Communication

## Goal

COXVBN will provide a native kernel communication primitive in which two
independent endpoints can exchange messages directly, without a bridge process,
broker process, relay device, or user-space forwarding hop.

This is an internal COXVBN IPC facility. It is not a claim about Ethernet,
TCP/UDP, USB, UART, or PCIe ports.

## Core model

- A **port** is a kernel-managed communication endpoint, not a numeric TCP/UDP
  port and not a hardware connector.
- A **connection** joins exactly two endpoint objects.
- Each endpoint is represented to its owner by an opaque capability handle.
- The kernel performs endpoint creation, peer authorization, connection setup,
  resource accounting, revocation, and teardown.
- Once connected, payload transfer uses a shared channel mapped to the two
  authorized endpoints. The kernel does not copy each message through a
  central forwarding queue.

## Direct data path

The first implementation should use a bounded, bidirectional pair of
single-producer/single-consumer ring buffers:

`Endpoint A -> ring A_to_B -> Endpoint B`
`Endpoint B -> ring B_to_A -> Endpoint A`

The two rings are the channel storage shared by the peers. They are not a
third endpoint and do not forward traffic to another destination. A small
kernel notification primitive may wake a sleeping receiver, but it must not
become a payload relay.

The initial version should prefer correctness and bounded resource use over
lock-free cleverness. Atomic memory ordering, cache coherency, and mapping
rules must be specified for the selected AArch64 execution environment before
the rings are exposed to user mode.

## Connection lifecycle

1. An owner creates a port and receives an opaque capability.
2. The owner explicitly authorizes a peer or transfers a one-time invitation
   capability.
3. The kernel validates both capabilities and creates a paired connection.
4. Each side receives only its own endpoint handle and permitted operations.
5. Peers exchange data directly through their shared channel.
6. Either side may close; the kernel revokes mappings and releases channel
   resources after outstanding users are safely quiesced.

There is no global public port namespace in the first version. This avoids
accidental discovery and name collisions. A later naming service, if ever
needed, must remain optional and must not sit in the data path.

## Security and failure behavior

- Possession of a valid capability is required to connect, send, receive, or
  administer an endpoint.
- Capabilities are unforgeable kernel objects; handles are not physical
  addresses or pointers.
- The kernel enforces per-process and global limits on ports, connections,
  and buffer sizes.
- Invalid handles, revoked capabilities, malformed messages, and exhausted
  buffers fail closed.
- Closing or revoking one peer must not grant the other peer additional rights.
- User processes must never be allowed to map arbitrary kernel memory as a
  shortcut to direct communication.

## Initial scope and non-goals

Version 1 is local IPC between COXVBN execution contexts on one machine.
It does not promise communication between physical hardware connectors,
different computers, or network hosts. Those require separate transports and
must not be confused with this endpoint abstraction.

The first implementation is intentionally deferred until the kernel has safe
physical-page allocation, address-space isolation, and mapping/unmapping
primitives. Until then this document is a design commitment, not an implemented
feature.

## Implementation sequence

1. Finish and test physical memory discovery and page allocation.
2. Establish MMU mappings and address-space isolation.
3. Add kernel object and capability lifetime rules.
4. Implement kernel-only paired endpoints and ring buffers.
5. Add wait/notification semantics and bounded backpressure.
6. Expose a small system-call API and test revocation, peer isolation, and
   malformed or full-buffer cases.
7. Consider optional naming/discovery only after the direct pair API is stable.
