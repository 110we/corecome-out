# CORECOME Security Plugin

## Purpose

All user-facing permission policy is owned by one independent system component: the **CORECOME Security Plugin**. Tools, drivers, compatibility personalities and assessment workers must not implement separate consent systems or grant themselves permissions.

The plugin is the policy broker and consent authority. The kernel remains the reference monitor: it validates every privileged operation and rejects missing, invalid, expired, revoked or out-of-scope grants. This split centralizes policy without making enforcement optional or bypassable.

## Required request flow

1. A tool declares its identity, requested operation set, target/resource, and why it needs access.
2. The system routes the request to the Security Plugin. The tool receives no privileged access while the request is pending.
3. The plugin checks its policy and presents the request to the local user. A missing plugin, unavailable consent UI, timeout, or user rejection means deny.
4. The user approves the request. The plugin issues a bounded grant containing a grant ID, requester/tool identity, target/resource scope, permitted operations, expiry and optional resource/action quota.
5. The tool presents that grant when invoking a privileged kernel interface. The kernel validates it on every operation.
6. The plugin can revoke a grant. Revocation must take effect at the kernel enforcement point, not merely hide the grant in the UI.

Approval is explicit and tied to the displayed request. Approval for one tool, target, operation or time window does not imply approval for another.

## Responsibilities

### Security Plugin

- Own the single permission request and consent workflow for the whole OS.
- Maintain tool identities, permission policies, user decisions and grant lifecycle.
- Provide one place to inspect, approve, deny and revoke permissions.
- Support per-request and carefully defined remembered decisions; never silently broaden an earlier grant.
- Report why a request was denied and which permission is missing.
- Manage optional audit presentation and retention policy, subject to kernel minimum security events.

### Kernel

- Provide a small, stable security reference-monitor interface.
- Verify grant authenticity, issuer, requester identity, target, operation, expiry, quotas and revocation on every privileged path.
- Enforce process isolation and object/resource boundaries independently of tool claims.
- Deny by default if the plugin is absent, unavailable, compromised or cannot provide a valid grant.
- Keep a minimal emergency stop/revocation path that cannot be disabled by an ordinary tool.
- Never let an ABI compatibility layer, driver or plugin bypass the reference monitor.

### Tools and plugins other than Security Plugin

- Declare requested permissions; do not display their own competing system authorization prompts.
- Cannot mint, edit, extend or forge grants.
- Receive only the capabilities explicitly granted for the current request.

## Plugin installation and bootstrap

The Security Plugin is a trusted system plugin, not an ordinary third-party tool. On a complete OS, a tool requiring privileged access must cause the system to locate/install or enable the Security Plugin, verify its trusted package signature and version, and then route the request to it. No tool operation proceeds while installation or approval is pending.

The OS must avoid a bootstrap deadlock: installation and recovery of the Security Plugin are handled by a minimal trusted system path, not by asking the missing plugin to authorize its own installation. An untrusted package must never replace the trusted Security Plugin.

For the current M0/M1 kernel, there is no module loader, package manager, process identity system or consent UI. Therefore the first implementation stage is a built-in trusted Security Plugin interface and kernel reference-monitor contract. Dynamic loading, signed installation, user prompts and per-tool identities are later milestones and must be tested before being described as operational.

## Current implementation boundary

The current `security.c` is an early single-core reference-monitor prototype with a bounded target scope, operation allow-list, expiry, action quota, audit ring and emergency stop. It is not yet the full Security Plugin. It has no authenticated tool identities, cryptographic grants, concurrency-safe locking, dynamic plugin loading, package verification or user-consent UI. Those are explicit implementation gaps.

## Acceptance tests

- A tool cannot execute a privileged operation while consent is pending.
- Missing, disabled, invalid or compromised Security Plugin results in deny-by-default.
- User denial prevents the requested operation.
- Approval grants only the displayed tool, target, operations, duration and quota.
- A different tool or target cannot reuse another tool's grant.
- Expiry and revocation are enforced by the kernel on the next operation.
- Compatibility layers and drivers cannot bypass the same enforcement path.
- Security Plugin update/recovery cannot be hijacked by an untrusted package.
