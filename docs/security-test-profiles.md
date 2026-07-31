# Security test profiles

The root Makefile provides three local profiles. Python dependencies for the
security profile are pinned in `requirements-security.txt`.

## Fast

```bash
make test-fast
```

Runs the complete existing Python/C host suites plus a short sanitizer-backed
fuzz smoke. It should be suitable for normal development.

## Security

```bash
make test-security PYTHON=/path/to/security-venv/bin/python
```

Runs Python branch coverage, C host sanitizer suites, the bounded fuzz runner,
gcov coverage, GCC analyzer, private-key scan and whitespace checks. Optional
analyzers report their availability explicitly.

## Long-running

```bash
make fuzz
```

Runs one million deterministic executions for UART, metadata and policy and a
10,000-execution full package/crypto campaign. The crypto count is lower by
design because every full verification performs real Ed25519/SHA-512 work;
the command records this exception. Outputs stay local and ignored.

No GitHub Actions or release workflow was changed in this part. These targets
are CI-shaped local entry points for the later CI work.
