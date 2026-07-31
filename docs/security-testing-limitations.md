# Security-testing limitations

The new evidence is host evidence. It does not prove behaviour of the target
under RDP2, WRP, brownout, power loss, clock faults, voltage glitches, fault
injection, invasive analysis or side-channel observation.

The portable deterministic engine is not equivalent to a long-running
coverage-guided libFuzzer/AFL++ campaign. The current environment lacks
`clang`, `cppcheck`, `clang-tidy`, `scan-build`, Valgrind and AFL++. No external
cryptographic audit was performed. Monocypher remains a vendored third-party
implementation; this part tests its repository wrappers and known call paths,
not every internal branch as a project quality target.

Coverage is an aid to test quality, not a proof of absence of undefined
behaviour or security flaws. The policy model can itself be wrong, which is
why it is intentionally small and paired with transition tests and code
review. Hardware regression is safe and reversible only; no physical power
loss campaign was performed.
