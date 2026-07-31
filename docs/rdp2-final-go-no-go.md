# RDP2 final go/no-go

This is a read-only decision record for the Part 5 campaign. RDP2 was not
activated and no Option Byte write was executed.

| Prerequisite | Status | Evidence | Blocker |
| --- | --- | --- | --- |
| Stage 0 stable | PASS | Stage-0 hash remained `679e88fbdc215e42c0c9637be5730b0cd04d744d2ce3ecc5cf1b5cab2fa1ba9a` after UART updates and trial-fault testing | Long-term operation remains open |
| Secure Boot | PASS / LIMITED | Signed live updates and prior HIL signature/hash/manifest rejection | External review absent |
| Signed update path | PASS | Device-key A/B updates v47/v48, v52/v54 over USART1 | Board-specific UART reset limits |
| Rollback | PASS | Older and same-version packages rejected with `ROLLBACK`; flash unchanged | Software-backed floor |
| Trial Boot | PASS / LIMITED | IWDG candidate v49/v53 bounded and rejected; fallback metadata observed | Full reset-cause matrix not repeated after every variant |
| Watchdog | PASS | IWDG reset evidence `raw=0x34000000` in prior campaign and current trial fallback | LSI tolerance not characterized across temperature |
| Automatic fallback | PASS | Rejected trial candidate returned to confirmed slot B/A | Power-loss timing open |
| Recovery | LIMITED | Existing HIL recovery/invalid-state observations and UART update path | Complete UART-only recovery after physical power loss not proven |
| UART-only update | LIMITED | Update data path used USART1; `stm32ctl reset` is now fire-and-forget | NRST is not wired; reset entry used software ST-Link reset |
| Metadata corruption | LIMITED | Prior HIL runs covered erased/zeroed copies and restore verification | Current Part 5 HIL rerun stopped before tests due flash-restore interaction |
| Slot corruption | LIMITED / PASS | Prior HIL signature, hash, header, and slot cases passed or were observed | Evidence is not a new full matrix on this final branch |
| Power-loss campaign | NOT PASSED | No controlled relay/programmable supply was available | Mandatory blocker |
| Key backup | PARTIAL | Local device-key fingerprint matches Stage 0 and packages v47-v54 verified | Independent backup custody not audited here |
| Key-compromise plan | DOCUMENTED | `docs/key-loss-and-compromise-response.md` | Operational rehearsal open |
| Release artifacts | PASS locally | Existing local candidate, manifests, SBOM and provenance tooling | No remote CI/release |
| Reproducible build | PASS locally | Existing Part 4 local repeatability evidence | No external builder |
| Open high/critical findings | NO-GO | HIL restore encountered an IWDG/ST-Link flash-loader error; power-loss evidence absent | Must be resolved or accepted by owner |
| WRP decision | LIMITED | `docs/final-wrp-decision.md` | No expendable WRP test device designated |
| Owner acceptance | NOT PROVIDED | No separate owner authorization for irreversible provisioning | Mandatory blocker |

The following findings prevent a technical RDP2 recommendation: no physical
power-loss campaign, no independently controllable NRST or supply for a full
debugger-free lifecycle, incomplete final metadata-corruption rerun, and no
separate owner acceptance of irreversible loss of SWD/JTAG, ROM bootloader and
Option-Byte recovery.

No RDP2 owner-execution checklist or activation command is produced from this
no-go result.

**RDP2 NO-GO**
