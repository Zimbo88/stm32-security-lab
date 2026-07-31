# Cortex-M4 MPU policy

The application enables five MPU regions after C runtime initialization and
before normal operation:

| Region | Base | Size | Policy |
|---:|---:|---:|---|
| 0 | `0x08000000` | 1 MiB | privileged read-only flash, executable |
| 1 | `0x20000000` | 128 KiB | full-access SRAM, execute-never |
| 2 | `0x08000000` | 128 KiB | privileged read-only, execute-never; Stage 0/metadata |
| 3 | `0x00000000` | 32 B | no access, execute-never |
| 4 | linker `_stack_guard_start` (`0x2001E000`) | 256 B | no access, execute-never |

Region priority makes region 2 override the general flash mapping. `PRIVDEFENA`
remains enabled so required peripherals use the privileged default map. The
stack has an 8 KiB reserved budget; its lowest 256 bytes are the guard region.
The linker asserts that static data cannot enter that reservation.

The existing confirmation service is the only code path needing a metadata
write. It temporarily suspends the MPU around the existing atomic commit and
immediately restores the prior control value. This is a privileged software
exception, not a claim that a compromised application cannot call it.

The policy is not TrustZone. Cortex-M4 MPU regions have power-of-two alignment,
there are few regions, privileged code can reconfigure the MPU, and flash
control registers are not an isolation boundary. The policy primarily reduces
accidental writes, null dereferences, SRAM execution, and stack-overflow damage.
MemManage faults are captured by the retained fault record and the existing
IWDG/recovery policy remains responsible for eventual progress.

Test builds use `TEST_SCENARIO=mpu_*` explicitly and are not normal releases.
Host descriptor tests are in `tests/mpu_policy`.
