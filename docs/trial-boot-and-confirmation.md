# Trial boot and confirmation

Status: IMPLEMENTED, HOST TESTED, and prepared for hardware validation.

The bootloader state `CANDIDATE_READY` is converted to `PENDING_TRIAL` only
after the candidate signature, manifest, vector table, payload range and hash
verify. The candidate gets three total boot opportunities. Every reboot before
confirmation consumes one remaining persistent attempt. The confirmed slot is
stored separately as `active_slot` and is not modified by that decrement.

The application health gate requires:

- early platform initialization;
- the vector table identifying the running slot;
- platform self-tests and critical hardware initialization;
- UART diagnostics ready;
- RSM initialized;
- the software IWDG active;
- a stable execution point / configured minimum delay;
- no test or runtime health failure;
- metadata permitting confirmation.

The default platform delay is zero for the normal lab build, but the gate is
still reached only from the main loop after all initialization checks. The
`trial_delayed_confirm` test build uses a 100-loop delay. A candidate that
starts but never reaches this gate remains pending. A candidate that reaches
the gate but cannot commit confirmation also remains pending and is bounded by
the Stage-0 attempt policy.

Confirmation changes `PENDING_TRIAL` to `CONFIRMED`, sets the candidate as the
active slot and clears the trial counter. Confirmation is idempotent for the
already-confirmed running slot. It is rejected for the wrong slot, ambiguous
metadata or a non-pending candidate.

Expected evidence includes `BOOT_RESET`, `WATCHDOG`, `confirmation`, telemetry
metadata state/attempt fields, and the Stage-0 `TRIAL`, `FALLBACK` or
`CONFIRMED` decision line.

