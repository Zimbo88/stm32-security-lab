# Security Policy

STM32 Security Lab is maintained by Mathias Zimmermann.

If you discover a security issue, use GitHub Private Vulnerability Reporting:
<https://github.com/Zimbo88/stm32-security-lab/security/advisories/new> when
the feature is enabled. If it is unavailable, open a minimal public issue
requesting private coordination without exploit details. Do not invent or use
an unverified email address for this project.

## Project scope

STM32 Security Lab is a defensive embedded-security research platform. It is
intended for controlled evaluation of secure boot, firmware authenticity,
integrity verification, update handling, recovery behaviour, debug access,
memory protection and hardware fault responses on devices owned by or
explicitly entrusted to the researcher.

The project is not a production security product and has not undergone a
third-party security certification.

## Supported versions

Security fixes are applied to the current development branch and to the most
recent tagged release where practical.

| Version | Supported |
|---|---|
| Current development branch | Yes |
| Latest tagged release | Yes |
| Older releases | Best effort |

## Reporting a vulnerability

Do not publish an unconfirmed vulnerability in a public issue.

Provide:

- the affected component and revision;
- hardware and toolchain details;
- reproduction steps;
- observed and expected behaviour;
- potential security impact;
- relevant logs or traces with private information removed.

Until a dedicated private reporting channel is published, create a minimal
public issue requesting a private coordination channel. Do not include exploit
details, signing material, device secrets or sensitive target information in
that issue.

Include the affected commit, component, threat model, safe reproduction steps,
expected and observed behavior, impact assessment and sanitized logs. State
whether RDP, WRP or Option Bytes were changed. Do not perform invasive tests,
fault injection or destructive hardware actions without explicit authorization.

Reports are acknowledged when maintainers can review them. Investigation,
fix, coordination and disclosure timing depend on severity, reproducibility,
maintainer availability and affected hardware. No CVE assignment or fixed
response deadline is guaranteed. Coordinate an embargo before any public
disclosure.

## Research safety

Only perform invasive testing, fault injection, option-byte modification,
read-out-protection experiments or destructive flash operations on hardware
for which you have explicit authorization.

Back up recoverable device state before modifying protection settings.
RDP Level 2 may be irreversible on supported STM32 devices and can permanently
disable normal debug access. Consult the authoritative device documentation
before changing option bytes.

## Cryptographic material

Private signing keys and signing seeds must never be committed to the
repository. Repository examples use placeholders or explicitly designated
development-only material.

Loss of the active signing key can make trusted updates impossible; compromise
requires treating the key as revoked and following the documented key-loss
response. Do not work around key loss by weakening signature verification.

## Third-party components

Third-party software remains subject to its own license and security policy.
See the notices contained in the corresponding third-party directories.
