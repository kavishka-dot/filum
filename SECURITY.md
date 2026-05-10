# Security Policy

## Supported versions

| Version | Supported |
|---------|-----------|
| 0.2.x   | ✅        |
| 0.1.x   | ❌        |

## Reporting a vulnerability

**Do not open a public GitHub issue for security vulnerabilities.**

Report security issues by emailing the maintainer directly (see GitHub profile).
Include:
- Description of the vulnerability
- Steps to reproduce
- Potential impact
- Suggested fix (if any)

You will receive acknowledgment within 72 hours and a status update within 7 days.

## Cryptography notice

Filum's cryptographic primitives (Curve25519, ChaCha20, Poly1305) are implemented
in pure C99 for portability to MCU targets without OS or external library support.
These implementations have **not been independently audited**.

For production deployments where a vetted cryptographic library is available
(libsodium, mbedTLS), we recommend replacing `fl_crypto.c` with a thin wrapper
around the audited library. The `FLCryptoCtx` interface is designed to support
this substitution without changes to calling code.

## Scope

The following are in scope for security reports:
- Cryptographic flaws in `fl_crypto.c` (ECDH, ChaCha20, Poly1305)
- Privacy accounting errors in `fl_dp.c`
- Buffer overflows or memory safety issues in any module
- Wire protocol vulnerabilities enabling replay, forgery, or injection
