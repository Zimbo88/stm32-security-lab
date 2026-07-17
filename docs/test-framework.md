# Test framework

The initial registry is `gpio`, `button`, `clock`, and `ram`. They are
non-destructive placeholders in this milestone and return a clear PASS result
without changing clock sources or scanning memory. Future hardware-specific
implementations must preserve unrelated configuration and use bounded buffers.
