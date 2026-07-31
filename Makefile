PYTHON ?= python3
SECURITY_FUZZ_ITERATIONS ?= 10000

C_HOST_SUITES := \
	tests/diagnostic_console \
	tests/host_verifier \
	tests/mpu_policy \
	tests/reset_cause \
	tests/rsm_core \
	tests/uart \
	tests/update_protocol \
	tests/update_storage

.PHONY: test-fast test-security fuzz coverage sanitize static-analysis mutation clean

test-fast:
	$(PYTHON) -m pytest -q -p no:cacheprovider tests
	$(MAKE) -C fuzz test
	$(MAKE) -C tests/diagnostic_console clean test
	$(MAKE) -C tests/host_verifier clean test
	$(MAKE) -C tests/mpu_policy clean test
	$(MAKE) -C tests/reset_cause clean test
	$(MAKE) -C tests/rsm_core clean test
	$(MAKE) -C tests/uart clean test
	$(MAKE) -C tests/update_protocol clean test
	$(MAKE) -C tests/update_storage clean test

test-security:
	$(PYTHON) -m coverage run --branch -m pytest -q -p no:cacheprovider tests
	$(PYTHON) -m coverage report -m
	$(MAKE) -C fuzz sanitize
	$(PYTHON) fuzz/scripts/run_campaign.py --iterations $(SECURITY_FUZZ_ITERATIONS) \
		--output fuzz/findings-local/security-smoke.json
	$(PYTHON) tools/collect_c_coverage.py
	$(PYTHON) tools/run_static_analysis.py
	$(PYTHON) -m ruff check fuzz/scripts/run_campaign.py tests/test_security_properties.py \
		tools/collect_c_coverage.py tools/run_static_analysis.py tools/mutation_smoke.py
	MYPYPATH=tools $(PYTHON) -m mypy --config-file mypy.ini fuzz/scripts/run_campaign.py \
		tests/test_security_properties.py tools/collect_c_coverage.py tools/run_static_analysis.py \
		tools/mutation_smoke.py
	$(PYTHON) -m bandit -q -r fuzz/scripts tools/collect_c_coverage.py tools/run_static_analysis.py \
		tools/mutation_smoke.py -x tests/test_security_properties.py
	$(MAKE) mutation PYTHON=$(PYTHON)
	$(PYTHON) tools/check_no_private_keys.py
	git diff --check

fuzz:
	$(MAKE) -C fuzz clean all
	$(PYTHON) fuzz/scripts/run_campaign.py --iterations 1000000 uart metadata policy \
		--output fuzz/findings-local/long-parser-policy.json
	$(PYTHON) fuzz/scripts/run_campaign.py --iterations 10000 package \
		--output fuzz/findings-local/full-crypto.json

coverage:
	$(PYTHON) -m coverage run --branch -m pytest -q -p no:cacheprovider tests
	$(PYTHON) -m coverage report -m
	$(PYTHON) tools/collect_c_coverage.py

sanitize:
	$(MAKE) -C fuzz sanitize
	$(MAKE) -C tests/host_verifier clean test SANITIZE=1
	$(MAKE) -C tests/update_protocol clean test SANITIZE=1
	$(MAKE) -C tests/update_storage clean test SANITIZE=1

static-analysis:
	$(PYTHON) tools/run_static_analysis.py

mutation:
	$(PYTHON) tools/mutation_smoke.py --json-output fuzz/findings-local/mutation.json

clean:
	$(MAKE) -C fuzz clean
