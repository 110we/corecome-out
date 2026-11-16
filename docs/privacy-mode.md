# COXVBN Daily Privacy Profile

## Purpose and scope

The Daily Privacy Profile is an ordinary, user-controlled operating profile for reducing incidental disclosure during everyday use. It is not an evasion mode, covert mode, or identity-spoofing feature. It must remain compatible with platform security controls, endpoint protection, audit requirements, and applicable organizational policy.

This document defines a design contract. The module registry now has a host-tested domain boundary that prevents Daily Profile modules from requesting or receiving RedLab assessment capabilities. The current kernel does not yet implement user sessions, a desktop, application sandboxing, notification services, or profile switching.

## Intended controls

1. **Profile separation:** keep daily, development, and assessment app settings/data in separate user-owned profiles or sandbox containers when the future user-space platform supports them.
2. **Visible profile switching:** show the active profile clearly; require an authenticated user action to switch; provide a straightforward return to the prior profile.
3. **Sensitive display controls:** allow the user to suppress message contents and other sensitive previews on lock screens or notifications, without suppressing security alerts or audit events.
4. **Permission review:** provide a user-readable view of app permissions and recent access; allow revocation through normal platform permission controls.
5. **Telemetry choice:** minimize nonessential telemetry and make collection choices explicit. Required security and reliability events must not be silently disabled.
6. **Data lifecycle:** document what is stored per profile, how it is backed up, and how the user can export or delete their own profile data.
7. **Safe defaults:** new profiles receive no elevated capabilities, no assessment network permissions, and no implicit access to another profile's private data.

## Separation from RedLab

The Daily Privacy Profile and RedLab are separate security domains:

- Daily mode receives no discovery, passive-capture, or active-probe capability; the domain-aware module registration API rejects any Daily Profile request or grant containing those capability bits.
- Switching into RedLab requires an explicit confirmation and a separate authorization/scope workflow.
- Switching back to daily mode revokes RedLab job handles and assessment capabilities before the profile transition completes.
- A failed revocation or uncertain transition fails closed and leaves the system in a clearly indicated restricted state.
- Evidence and audit records remain available to authorized reviewers; daily mode cannot erase or rewrite them.

## Non-goals and prohibited behavior

The profile must not:
- conceal processes, services, files, or network activity from the device owner or authorized administrators;
- bypass, disable, tamper with, or evade EDR, antivirus, firewall, logging, or audit controls;
- create covert persistence or hide unauthorized activity;
- impersonate a real person, organization, device, or trusted service;
- silently falsify device identity, location, or security posture.

Legitimate privacy measures such as reducing lock-screen previews, separating personal/work data, limiting unnecessary app permissions, and choosing a less revealing user-facing theme are in scope.

## State model

```text
DAILY
  └─ explicit authenticated switch + RedLab authorization → REDLAB_PENDING
       ├─ scope and capability checks pass → REDLAB_ACTIVE
       └─ any check fails → DAILY (no assessment capability granted)

REDLAB_ACTIVE
  └─ stop jobs + revoke assessment capabilities + audit transition
       ├─ all steps confirmed → DAILY
       └─ uncertain/failure → RESTRICTED (visible, no new assessment work)
```

The state indicator is always visible to the local user. A profile transition is not complete until the required revocations and audit event have succeeded.

## Acceptance criteria

Implementation is not complete until tests prove:

- daily profile cannot obtain RedLab capabilities, even by requesting them directly;
- profile data and app permissions are isolated across profiles;
- switching requires authenticated user intent and produces an audit event;
- lock-screen preview settings do not suppress security-critical notifications;
- switching out of RedLab stops queued work and revokes capabilities before returning to daily mode;
- failure during transition enters a visible fail-closed state;
- privacy settings do not disable or bypass endpoint protection, audit, or administrator controls;
- profile export/deletion affects only the selected user-owned profile and follows documented retention rules.

## Delivery order

1. Define profile identity and data ownership after user/session foundations exist.
2. Add capability separation and profile-scoped handles.
3. Add visible profile manager and authenticated transition state machine.
4. Add notification-preview and permission-review settings in user space.
5. Add telemetry preferences with explicit mandatory-security-event exceptions.
6. Add cross-profile isolation, transition-failure, and audit regression tests.

Until those stages are implemented and tested, this remains a design contract, not a working privacy feature.
