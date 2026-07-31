# Key-loss and compromise response

If the private key is lost but not compromised, the existing device can still
boot, but no new trusted update or repair package can be produced. Before RDP2,
an operator with the known-good evidence may use SWD to restore a complete
baseline on an expendable board. This is not a post-RDP2 recovery method.

If a key was copied or may be compromised, stop signing, preserve the incident
record, and treat every image authorized by it as suspect. There is no in-field
revocation or alternate key path. Before RDP2, a reviewed SWD/manufacturing
operation can replace Stage 0 and its public key. After RDP2, the device is
practically bound to the original key under the current design.

An incorrect public key rejects all correctly signed packages. A damaged Stage
0 can prevent verification, recovery, or UART entry. Before RDP2 use the
known-good Stage-0 hash and the documented repair route; after RDP2 no debugger
or ROM-bootloader repair is assumed.

Test-key mistakes require a new, explicitly labelled research baseline rather
than silent migration. Keep encrypted independent backups of the private seed,
public fingerprint, Stage-0 and slot artifacts, build metadata, layout, and a
Git bundle. Never place private material in the repository, baseline folder,
UART logs, or release manifests.
