# Limitations

STM32 Security Lab is a research implementation and does not claim production
certification.

## Current limitations

- no independent third-party security audit;
- no formal verification of the complete boot chain;
- no certified fault-injection resistance;
- no dedicated secure element for private-key storage;
- no production manufacturing key ceremony;
- no fleet-scale revocation or lifecycle service;
- no guarantee across every STM32F429 package or board revision;
- no production-grade remote update transport;
- research logs may depend on specific debugger and host-tool versions.

## Physical attacks

A valid cryptographic design does not by itself guarantee resistance to:

- voltage glitches;
- clock glitches;
- electromagnetic fault injection;
- laser fault injection;
- power or electromagnetic side channels;
- decapsulation;
- invasive silicon analysis.

The repository provides a baseline from which such effects can be measured,
not a certification that they are prevented.

## Tooling reliability

ST-LINK, USB, UART, and host drivers can fail independently of the target
firmware. Test reports must keep infrastructure failures separate from
security-policy results.

## Device protection

RDP Level 2 and related irreversible settings should not be enabled until the
researcher accepts permanent loss of standard debug access.
