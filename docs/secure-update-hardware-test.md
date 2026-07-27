# Secure Update Hardware Test Plan

This is the first hardware-in-the-loop plan for the EXP045 secure-update
chain. It is explicitly an RDP0 test plan. Do not enable RDP1 or RDP2, do not
change Option Bytes, and do not rely on the ST ROM bootloader for the update
path.

Use these shell variables in the examples:

```sh
export PORT=/dev/ttyUSB0
export PYTHONPATH=tools
export PKG_A=firmware/exp066_research_platform_core/build/slot_a/exp066_research_platform_core_slot_a_slot_a_update_v2.bin
export PKG_B=firmware/exp066_research_platform_core/build/slot_b/exp066_research_platform_core_slot_b_slot_b_update_v2.bin
export PUBKEY=firmware/exp045_bootloader_v2/src/firmware_public_key.h
```

Build `PKG_A` and `PKG_B` with `make -C firmware/exp066_research_platform_core
slot-releases`. Their manifest vector addresses must be `0x08020200` and
`0x08080200` respectively. Use packages with monotonically increasing image
versions for the positive tests.

## Phase A - Setup

| Test | Preparation | Command | Expected UART output | Expected metadata | Expected bootslot | Pass/fail criterion | Recovery |
| --- | --- | --- | --- | --- | --- | --- | --- |
| A1 wiring | Connect USART1 PA9 to USB-UART RX, PA10 to USB-UART TX, and common ground. Use 3.3 V TTL only. Leave ST-Link connected. Keep RDP at Level 0. | `python3 -m serial.tools.miniterm "$PORT" 115200` | UART is readable, no level shifting errors, no continuous framing garbage. | Unchanged. | Unchanged. | Serial adapter sees target output or remains idle without errors. | Power down and recheck TX/RX direction, ground, and 3.3 V levels. |
| A2 serial settings | Close any terminal that owns the port. Use 115200 baud, 8 data bits, no parity, 1 stop bit, no flow control. | `stty -F "$PORT" 115200 cs8 -cstopb -parenb -ixon -ixoff -crtscts` | No direct bootloader output expected from this command. | Unchanged. | Unchanged. | Command succeeds and the port is not busy. | Stop competing terminal sessions or unplug/replug the adapter. |
| A3 safety baseline | Confirm ST-Link can still attach before any update. Do not mass erase in this step. | `st-info --probe` | None from UART required. | Unchanged. | Unchanged. | Probe reports the STM32F429 target. | Fix SWD wiring before continuing. |

## Phase B - Initial State

| Test | Preparation | Command | Expected UART output | Expected metadata | Expected bootslot | Pass/fail criterion | Recovery |
| --- | --- | --- | --- | --- | --- | --- | --- |
| B1 build bootloader | Repository checkout is the release-candidate tree. | `make -C firmware/exp045_bootloader_v2 clean report` | None from target. Build report shows the bootloader binary and remaining space. | Unchanged. | Unchanged. | Build succeeds and binary size is inside `0x8000` bytes. | Stop; fix build before hardware. |
| B2 flash bootloader manually | RDP Level 0 confirmed. This is a manual flash operation, not performed by automation. | `st-flash write firmware/exp045_bootloader_v2/build/exp045_bootloader_v2.bin 0x08000000` | Target may reset and later print the boot banner. | Existing metadata unchanged. | Existing boot decision. | ST-Link write succeeds; no Option Bytes are changed. | Reflash known-good bootloader manually. |
| B3 install confirmed Slot A | Build or obtain a valid signed Slot A image at `0x08020000` and confirmed metadata records for version `N`. | `tools/build/boot_metadata_provision.bin create-confirmed --slot a --image-version N --copy-a-output hil-results/meta_a.bin --copy-b-output hil-results/meta_b.bin` | None from target. | Generated records encode `CONFIRMED`, active slot A, no candidate, confirmation true. | A. | Records are generated and reviewed before manual flashing to metadata sectors. | Regenerate metadata; do not proceed with ambiguous records. |
| B4 flash Slot A and metadata manually | Slot A update package and metadata records from B3 are ready. | `st-flash write "$PKG_A" 0x08020000` | None required during flash. | After manual metadata flash: `CONFIRMED`, active A. | A. | Bootloader later selects A and application starts. | Reflash Slot A and confirmed metadata. |
| B5 observe boot output | Power cycle or reset with no UART input. | `python3 -m serial.tools.miniterm "$PORT" 115200` | Banner contains `EXP045 BOOTLOADER V2`, reset cause, slot policy, `Signature and payload hash accepted.`, then jump. | `CONFIRMED`, active A. | A. | Application for Slot A runs and can confirm if needed. | Reflash known-good Slot A and metadata. |
| B6 capture baseline | Enter console with a complete text line during the entry window. | Type `metadata` then `slots` then `boot` in the console. | Console starts with `diagnostic console readonly`; metadata and slot descriptors print. | `CONFIRMED`, active A, candidate none. | A after `boot`. | Captured output matches expected slot bases and metadata. | Reset target and retry with a shorter time from reset to first line. |

## Phase C - Connection

| Test | Preparation | Command | Expected UART output | Expected metadata | Expected bootslot | Pass/fail criterion | Recovery |
| --- | --- | --- | --- | --- | --- | --- | --- |
| C1 binary info | Target is reset and inside the bounded entry window. | `python3 -m stm32ctl --port "$PORT" info` | Binary `SUPD` ACK on UART; host prints protocol version 1 and max write data 1020 bytes. | Unchanged. | No boot while command owns update entry. | Exit code 0 and protocol fields match docs. | Reset target and retry; inspect wiring if timeout. |
| C2 binary status | Same as C1. | `python3 -m stm32ctl --port "$PORT" status` | Binary ACKs for HELLO and GET_STATUS; host prints session and installer state. | Unchanged. | No boot while command owns update entry. | Exit code 0 and status decodes. | Reset target and retry. |
| C3 text console | Send a text line instead of binary HELLO. | `python3 -m serial.tools.miniterm "$PORT" 115200`, then type `help` and `boot`. | `diagnostic console readonly`, prompt, command list, then `boot`. | Unchanged. | A. | Console commands are read-only and `boot` continues normal boot. | Reset target. |
| C4 entry timeout | Do not send any UART byte after reset. | `python3 -m serial.tools.miniterm "$PORT" 115200` | Normal boot banner after entry window expires. | Unchanged. | A. | No indefinite wait; application starts. | Reset target. |
| C5 random UART noise | Send random bytes that do not form a valid frame or text line. | `python3 - <<'PY'\nimport os, serial\ns=serial.Serial('/dev/ttyUSB0',115200,timeout=0.1)\ns.write(os.urandom(64))\ns.close()\nPY` | Bootloader eventually falls through to normal boot or ignores noise. | Unchanged. | A. | Noise does not hold boot forever and does not enter update mode. | Reset target; stop noise source. |

## Phase D - Update A To B

| Test | Preparation | Command | Expected UART output | Expected metadata | Expected bootslot | Pass/fail criterion | Recovery |
| --- | --- | --- | --- | --- | --- | --- | --- |
| D1 verify package locally | Slot A is confirmed version `N`; `PKG_B` is version `N+1` or higher and targets Slot B. | `python3 -m stm32ctl --port "$PORT" update --package "$PKG_B" --public-key-header "$PUBKEY" --quiet --block-size 512` | Host first prints no progress because `--quiet`; UART carries only binary frames. | During transfer: `WRITING`, active A, candidate B. After finish: `CANDIDATE_READY` committed before reset. | Reset into B trial. | Command exits 0 and target performs controlled reset. | If command fails before finish, reset; active A must still boot. |
| D2 transfer with progress | Use a package large enough to require multiple `WRITE_BLOCK` frames. | `python3 -m stm32ctl --port "$PORT" update --package "$PKG_B" --public-key-header "$PUBKEY" --block-size 512` | Host stderr shows `update: done/total`; final stdout says `update complete`. | Same as D1. | B after reset. | All ACKs received; no text is mixed into binary stream. | Reset and inspect `status` or console metadata. |
| D3 candidate boot | Let the reset from D1/D2 complete. | `python3 -m serial.tools.miniterm "$PORT" 115200` | Boot banner, slot policy selects candidate, signature/hash accepted, jump to Slot B. | Boot selection transitions from `CANDIDATE_READY` to trial state before jump. | B. | Slot B firmware starts. | If B fails, bootloader must fall back to confirmed A on later boot attempts. |
| D4 application confirmation | Slot B application contains the confirmation call. | Observe app UART or other confirmation indicator, then reset. | Next boot reports confirmed selection without fallback. | `CONFIRMED`, active B, candidate none, confirmation true, version `N+1`. | B. | Console `metadata` shows active B confirmed. | If not confirmed, repeat with app build that calls confirmation. |

## Phase E - Update B To A

| Test | Preparation | Command | Expected UART output | Expected metadata | Expected bootslot | Pass/fail criterion | Recovery |
| --- | --- | --- | --- | --- | --- | --- | --- |
| E1 verify Slot A package | Slot B is confirmed version `M`; `PKG_A` is version `M+1` or higher and targets Slot A. | `python3 -m stm32ctl --port "$PORT" update --package "$PKG_A" --public-key-header "$PUBKEY" --block-size 512` | Binary transfer with ACKs and progress. | During transfer: `WRITING`, active B, candidate A. After finish: `CANDIDATE_READY`. | Reset into A trial. | Command exits 0; target resets. | If failed, active B must remain bootable. |
| E2 candidate and confirmation | Let candidate A boot and confirm. | `python3 -m serial.tools.miniterm "$PORT" 115200` | Boot banner selects A and jumps; later reset remains on A. | `CONFIRMED`, active A, candidate none, version `M+1`. | A. | A runs and confirms. | Reflash confirmed B metadata only if trial recovery is not enough. |

## Phase F - Negative Tests

| Test | Preparation | Command | Expected UART output | Expected metadata | Expected bootslot | Pass/fail criterion | Recovery |
| --- | --- | --- | --- | --- | --- | --- | --- |
| F1 bad CRC | Reset target and send one corrupted binary HELLO frame during the entry window. | `PYTHONPATH=tools python3 - <<'PY'\nimport os, serial\nfrom stm32ctl.protocol import Command, encode_frame\ns=serial.Serial(os.environ['PORT'],115200,timeout=1)\nf=bytearray(encode_frame(Command.HELLO,0,b'')); f[-1] ^= 1\ns.write(f)\nprint(s.read(64).hex())\ns.close()\nPY` | Target returns binary `NACK(CRC)` when command/sequence are known, or times out/resyncs safely. | No `CANDIDATE_READY`; active confirmed slot unchanged. | Confirmed slot. | No flash release and no boot blockage. | Send `ABORT_UPDATE` if session started, then reset. |
| F2 bad signature | Create `bad_sig.update.bin` by flipping one signature byte after local verification copy. | `python3 -m stm32ctl --port "$PORT" update --package bad_sig.update.bin --public-key-header "$PUBKEY"` | Host should fail local package validation before transfer. If forced by custom sender, target returns verify NACK. | Unchanged or `WRITING` rejected/aborted; no ready candidate. | Confirmed slot. | No candidate boot. | Reset; active slot boots. |
| F3 bad SHA-512 | Modify one payload byte while leaving manifest/signature unchanged. | `python3 -m stm32ctl --port "$PORT" update --package bad_hash.update.bin --public-key-header "$PUBKEY"` | Host local verification fails. Forced transfer must end with verify/hash NACK. | No `CANDIDATE_READY`. | Confirmed slot. | Hash mismatch never boots. | Reset and boot confirmed slot. |
| F4 same version | Package version equals active confirmed version. | `python3 -m stm32ctl --port "$PORT" update --package same_version.update.bin --public-key-header "$PUBKEY"` | Local package may verify; target returns rollback NACK at begin. | Unchanged. | Confirmed slot. | Rollback protection rejects before candidate erase. | Reset. |
| F5 lower version | Package version is below active confirmed version. | `python3 -m stm32ctl --port "$PORT" update --package lower_version.update.bin --public-key-header "$PUBKEY"` | Target returns rollback NACK. | Unchanged. | Confirmed slot. | Lower version cannot write candidate. | Reset. |
| F6 wrong target | Manifest target compatibility or image type is invalid. | `python3 -m stm32ctl --port "$PORT" update --package wrong_target.update.bin --public-key-header "$PUBKEY"` | Host local validation fails before port write. | Unchanged. | Confirmed slot. | Exit code 7 for package validation. | Use a correct package. |
| F7 aborted transfer | Start an update and interrupt before finish. | Press Ctrl-C during `python3 -m stm32ctl --port "$PORT" update --package "$PKG_B"` | Host prints interrupted/error; best-effort abort is attempted if begin succeeded. | Either unchanged or rejected `WRITING`; never `CANDIDATE_READY`. | Confirmed slot. | Reset boots confirmed slot. | Reset; if metadata is `WRITING`, boot policy must not boot candidate. |
| F8 mid-frame timeout | Send partial header or partial payload then stop. | Custom sender writes first 5 bytes of a valid frame and closes port. | If command known, `NACK(TIMEOUT)`; otherwise timeout and resync. | Unchanged. | Confirmed slot. | Later reset boots confirmed slot. | Reset. |
| F9 lost final ACK | Drop host RX after `FINISH_UPDATE` is sent. | Disconnect RX after final payload, before finish ACK. | Target may still reset after committing `CANDIDATE_READY`; host times out. | If finish reached and verified: `CANDIDATE_READY`; otherwise confirmed slot. | Candidate only if verification completed. | No half-written image becomes bootable. | Reconnect UART; inspect metadata after reset. |
| F10 random data | Send repeated random frames and bytes. | `python3 - <<'PY'\nimport os, serial, time\ns=serial.Serial('/dev/ttyUSB0',115200,timeout=0.1)\nfor _ in range(50):\n    s.write(os.urandom(37)); time.sleep(0.01)\ns.close()\nPY` | Parser resynchronizes or entry window times out. | Unchanged. | Confirmed slot. | No permanent boot block. | Reset. |
| F11 oversize package | Package payload exceeds slot maximum or frame payload exceeds 1024. | `python3 -m stm32ctl --port "$PORT" update --package too_large.update.bin --public-key-header "$PUBKEY"` | Host local validation or target length/verify NACK. | Unchanged. | Confirmed slot. | No write beyond candidate slot. | Use a valid package. |

## Phase G - Power-Loss Tests

Perform these only at RDP0 with ST-Link attached and a known-good confirmed slot
already bootable. Use a switchable target supply or ST-Link reset/power control.

| Test | Preparation | Command | Expected UART output | Expected metadata | Expected bootslot | Pass/fail criterion | Recovery |
| --- | --- | --- | --- | --- | --- | --- | --- |
| G1 power loss during `WRITING` | Start update and cut power immediately after BEGIN_UPDATE ACK. | `python3 -m stm32ctl --port "$PORT" update --package "$PKG_B" --block-size 512` then remove power after begin is logged. | Transfer stops; after restore boot banner appears. | `WRITING` or confirmed state; never `CANDIDATE_READY`. | Confirmed slot. | Candidate is not booted. | Power restore, reset; if needed re-run valid update. |
| G2 during sector erase | Cut power while candidate sectors are being erased. | Same as G1; cut power early in first WRITE_BLOCK. | Transfer stops. | `WRITING`; candidate incomplete. | Confirmed slot. | Bootloader refuses candidate. | Power restore; confirmed slot boots. |
| G3 during block programming | Cut power mid-transfer after several progress updates. | Same update command; remove power around 50 percent progress. | Transfer stops. | `WRITING`; candidate incomplete. | Confirmed slot. | No partial image boots. | Power restore; retry update. |
| G4 before `FINISH_UPDATE` | Cut power after all WRITE_BLOCK ACKs but before FINISH_UPDATE. | Use a custom sender or disconnect before finish. | No finish ACK. | `WRITING`; no final verification completed. | Confirmed slot. | Candidate not bootable. | Reset; retry full update. |
| G5 during final verification | Cut power immediately after FINISH_UPDATE is sent. | Use a custom sender to log send time, then remove power. | Host times out. | Either `WRITING` or `CANDIDATE_READY` only if verification and commit completed. | Confirmed slot or candidate trial. | If candidate boots, metadata must prove `CANDIDATE_READY` was committed. | Inspect metadata via console after power restore. |
| G6 after `CANDIDATE_READY` before reset | Cut power after successful finish ACK, before reset completes. | `python3 -m stm32ctl --port "$PORT" update --package "$PKG_B"` and remove power at final success. | May show `update complete` or timeout. | `CANDIDATE_READY`. | Candidate trial on next boot. | Candidate verification happens again before jump. | Let candidate boot and confirm, or allow rollback. |
| G7 before candidate confirmation | Let candidate boot but cut power before application confirms. | Remove power immediately after candidate jump. | Boot banner before jump; app may not print confirmation. | Trial/pending state with attempts decremented on each boot. | Candidate until attempts expire, then confirmed fallback. | Boot policy eventually returns to confirmed slot if no confirmation. | Restore power; either allow confirmation or wait for fallback. |

## Timing Calibration

The first RDP0 test uses the existing poll-budget entry and frame timeouts.
Before changing firmware timing, measure them on hardware:

1. Capture reset-to-normal-boot time with no UART input.
2. Capture reset-to-HELLO success time using `stm32ctl info`.
3. Capture console inactivity timeout after entering text mode.
4. Repeat at least ten times and record min/max in the HIL log.

If the spread is too wide for reliable operation, add a SysTick or timer based
millisecond clock in a separate change. The current RC intentionally avoids a
late timing architecture change before the first RDP0 hardware run.
