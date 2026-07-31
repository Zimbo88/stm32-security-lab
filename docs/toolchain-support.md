# Toolchain Support

The following versions were used for the current local evidence on Ubuntu
24.04:

| Tool | Version | Status |
|---|---|---|
| Python | 3.12.3 | `TESTED` |
| Host GCC | 13.3.0 | `TESTED` |
| ARM GNU GCC | 13.2.1 | `TESTED` |
| GNU Make | 4.3 | `TESTED` |
| Git | 2.43.0 | `TESTED` |
| OpenSSL | 3.0.13 | `TESTED` for local key/tool workflows |
| Clang/libFuzzer | unavailable in the recorded environment | `NOT TESTED` |
| cppcheck, clang-tidy, scan-build, Valgrind | unavailable in the recorded environment | `NOT TESTED` |

Python versions are constrained by the project requirements and HIL package;
the normal host suite targets Python 3.12. Security dependencies are pinned
in `requirements-security.txt`. ARM GCC and host tool versions are environment
inputs, so release provenance records their reported version strings.

CI uses Ubuntu 24.04, Python 3.12 and the Ubuntu ARM GNU package. CI is
defined and locally inspected in this branch but has not yet executed on
GitHub for this branch.
