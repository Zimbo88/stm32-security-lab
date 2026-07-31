# Write-protection evaluation

No Option Byte was read-modified-written by Part 2 and no WRP setting was
activated. This file is an evaluation only.

For `stm32f429_1m`, the useful future WRP boundary would be sectors 0-1 (Stage
0). Sectors 2-3 contain metadata and must remain writable by the reviewed
commit path; sectors 5-10 contain update slots; sector 11 is the recovery
reserve. Protecting metadata or slots would break normal updates or
confirmation.

WRP is an Option-Byte policy, not a software flag. A wrong mask can prevent
Stage-0 maintenance, and recovery differs between RDP0 and RDP2. A later
procedure must record chip ID, current Option Bytes, sector mapping, and a
known-good board before any write, and include an unprotected reference board.

The software rejects Stage-0/metadata/recovery update targets and the MPU adds
a privileged-read-only application barrier. Neither means the flash is
physically write-protected. No programmer command, Make target, CI step, or
hidden Option-Byte write exists.

Status: layout analysis `DOCUMENTED ONLY`; WRP activation `NOT IMPLEMENTED`.
