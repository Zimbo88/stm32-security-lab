# Diagnostics

Diagnostics expose only documented identity registers and explicitly selected
RCC, GPIOA, SCB, and DBGMCU registers. Reads with potentially state-changing
clock requirements are reported unavailable. `memory regions` prints names and
fixed boundaries only; it never reads arbitrary addresses.
