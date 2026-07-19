"""Initial secure-boot HIL test catalog."""

from __future__ import annotations

from .model import AssertionKind, AssertionSpec, FlashAction, FlashOperation, TestCase, Verdict


def literal(text: str) -> AssertionSpec:
    return AssertionSpec(kind=AssertionKind.LITERAL, expression=text)


def field_equals(field: str, expected: str | int) -> AssertionSpec:
    return AssertionSpec(
        kind=AssertionKind.FIELD_EQUALS,
        expression=str(expected),
        field=field,
        expected=expected,
    )


APPLICATION_STARTED = literal("EXP066 RESEARCH PLATFORM")
HALTED = literal("Bootloader halted safely.")
NOT_STARTED = literal("Application will NOT be started.")
VERIFICATION_OK = literal("Verification = OK")


def write(region: str, artifact: str) -> FlashAction:
    return FlashAction(operation=FlashOperation.WRITE, region=region, artifact=artifact)


def fill(region: str, value: int) -> FlashAction:
    return FlashAction(operation=FlashOperation.WRITE, region=region, fill=value)


def confirmed(slot: str) -> list[FlashAction]:
    return [
        write("metadata_a", f"metadata_slot_{slot}_copy_a"),
        write("metadata_b", f"metadata_slot_{slot}_copy_b"),
    ]


def confirmed_with_copy_fault(slot: str, region: str, fill_value: int) -> list[FlashAction]:
    other = "metadata_b" if region == "metadata_a" else "metadata_a"
    other_copy = "copy_b" if other == "metadata_b" else "copy_a"
    return [
        fill(region, fill_value),
        write(other, f"metadata_slot_{slot}_{other_copy}"),
    ]


def positive_case(
    identifier: str, name: str, objective: str, actions: list[FlashAction]
) -> TestCase:
    return TestCase(
        identifier=identifier,
        name=name,
        objective=objective,
        preconditions=["validated bootloader and signed application images are available"],
        flash_actions=actions,
        required_uart=[
            VERIFICATION_OK,
            literal("Signature and payload hash accepted."),
            APPLICATION_STARTED,
        ],
        forbidden_uart=[NOT_STARTED, HALTED],
        expected_verdict=Verdict.PASS,
        tags=["boot", "positive"],
        timeout_seconds=5.0,
    )


def rejection_case(
    identifier: str,
    name: str,
    objective: str,
    actions: list[FlashAction],
    evidence: str,
) -> TestCase:
    return TestCase(
        identifier=identifier,
        name=name,
        objective=objective,
        preconditions=["bootloader is present and a malformed slot image is flashed"],
        flash_actions=actions,
        required_uart=[literal(evidence), NOT_STARTED, HALTED],
        forbidden_uart=[APPLICATION_STARTED],
        expected_verdict=Verdict.PASS,
        tags=["auth", "negative"],
        timeout_seconds=5.0,
    )


def observe_case(
    identifier: str, name: str, objective: str, actions: list[FlashAction]
) -> TestCase:
    return TestCase(
        identifier=identifier,
        name=name,
        objective=objective,
        preconditions=["slot-policy state is intentionally exercised for observation"],
        flash_actions=actions,
        required_uart=[literal("Slot policy"), literal("Verification")],
        forbidden_uart=[],
        expected_verdict=Verdict.OBSERVE,
        tags=["policy", "observe"],
        timeout_seconds=5.0,
    )


def initial_catalog(reset_cycles: int = 10) -> list[TestCase]:
    tests = [
        positive_case(
            "SB-BOOT-001",
            "Valid confirmed Slot A",
            "Prove that a valid Slot A image can boot through Stage-0.",
            [*confirmed("a"), write("slot_a", "slot_a_valid")],
        ),
        observe_case(
            "SB-BOOT-002",
            "Both slots valid",
            "Record slot selection when both authenticated slots are present.",
            [*confirmed("a"), write("slot_a", "slot_a_valid"), write("slot_b", "slot_b_valid")],
        ),
        positive_case(
            "SB-BOOT-003",
            "Return to valid state",
            "Confirm restoration of the nominal Slot A image after corruption tests.",
            [*confirmed("a"), write("slot_a", "slot_a_valid")],
        ),
        positive_case(
            "SB-STABILITY-001",
            "Repeated valid reset cycles",
            f"Run {reset_cycles} reset cycles against a valid Slot A image.",
            [*confirmed("a"), write("slot_a", "slot_a_valid")],
        ),
        rejection_case(
            "SB-AUTH-001",
            "Slot A signature invalid",
            "Verify that a changed Slot A signature is rejected.",
            [*confirmed("a"), write("slot_a", "slot_a_bad_signature")],
            "ED25519 SIGNATURE INVALID",
        ),
        rejection_case(
            "SB-AUTH-002",
            "Slot B signature invalid",
            "Verify that a changed Slot B signature is rejected when selected.",
            [*confirmed("b"), write("slot_b", "slot_b_bad_signature")],
            "ED25519 SIGNATURE INVALID",
        ),
        rejection_case(
            "SB-AUTH-003",
            "Both signatures invalid",
            "Verify that two invalid signatures cannot produce application execution.",
            [
                *confirmed("a"),
                write("slot_a", "slot_a_bad_signature"),
                write("slot_b", "slot_b_bad_signature"),
            ],
            "ED25519 SIGNATURE INVALID",
        ),
        rejection_case(
            "SB-HASH-001",
            "Slot A payload modified",
            "Verify that a changed Slot A payload is rejected by SHA-512.",
            [*confirmed("a"), write("slot_a", "slot_a_modified_payload")],
            "PAYLOAD SHA512 MISMATCH",
        ),
        rejection_case(
            "SB-HASH-002",
            "Slot B payload modified",
            "Verify that a changed Slot B payload is rejected by SHA-512.",
            [*confirmed("b"), write("slot_b", "slot_b_modified_payload")],
            "PAYLOAD SHA512 MISMATCH",
        ),
        rejection_case(
            "SB-HASH-003",
            "Both payloads modified",
            "Verify that changed payloads cannot produce application execution.",
            [
                *confirmed("a"),
                write("slot_a", "slot_a_modified_payload"),
                write("slot_b", "slot_b_modified_payload"),
            ],
            "PAYLOAD SHA512 MISMATCH",
        ),
        rejection_case(
            "SB-MANIFEST-001",
            "Slot A erased header",
            "Verify that an erased Slot A signed-image header is rejected.",
            [*confirmed("a"), write("slot_a", "slot_a_erased_header")],
            "BAD MAGIC",
        ),
        rejection_case(
            "SB-MANIFEST-002",
            "Slot B erased header",
            "Verify that an erased Slot B signed-image header is rejected.",
            [*confirmed("b"), write("slot_b", "slot_b_erased_header")],
            "BAD MAGIC",
        ),
        rejection_case(
            "SB-MANIFEST-003",
            "Both headers erased",
            "Verify that erased signed-image headers cannot boot.",
            [
                *confirmed("a"),
                write("slot_a", "slot_a_erased_header"),
                write("slot_b", "slot_b_erased_header"),
            ],
            "BAD MAGIC",
        ),
        rejection_case(
            "SB-MANIFEST-004",
            "Slot A zero header",
            "Verify that a zeroed Slot A signed-image header is rejected.",
            [*confirmed("a"), write("slot_a", "slot_a_zero_header")],
            "BAD MAGIC",
        ),
        rejection_case(
            "SB-MANIFEST-005",
            "Slot B zero header",
            "Verify that a zeroed Slot B signed-image header is rejected.",
            [*confirmed("b"), write("slot_b", "slot_b_zero_header")],
            "BAD MAGIC",
        ),
        rejection_case(
            "SB-MANIFEST-006",
            "Both headers zeroed",
            "Verify that zeroed signed-image headers cannot boot.",
            [
                *confirmed("a"),
                write("slot_a", "slot_a_zero_header"),
                write("slot_b", "slot_b_zero_header"),
            ],
            "BAD MAGIC",
        ),
    ]
    tests.extend(
        observe_case(identifier, name, objective, actions)
        for identifier, name, objective, actions in [
            (
                "SB-SLOT-001",
                "Only Slot A valid",
                "Record policy behavior with only Slot A valid.",
                [*confirmed("a"), write("slot_a", "slot_a_valid")],
            ),
            (
                "SB-SLOT-002",
                "Only Slot B valid",
                "Record policy behavior with only Slot B valid.",
                [*confirmed("b"), write("slot_b", "slot_b_valid")],
            ),
            (
                "SB-SLOT-003",
                "Slot A invalid and Slot B valid",
                "Record fallback behavior from invalid Slot A to valid Slot B.",
                [
                    *confirmed("a"),
                    write("slot_a", "slot_a_bad_signature"),
                    write("slot_b", "slot_b_valid"),
                ],
            ),
            (
                "SB-SLOT-004",
                "Slot A valid and Slot B invalid",
                "Record policy behavior with valid Slot A and invalid Slot B.",
                [
                    *confirmed("a"),
                    write("slot_a", "slot_a_valid"),
                    write("slot_b", "slot_b_bad_signature"),
                ],
            ),
            (
                "SB-SLOT-005",
                "Both slots invalid",
                "Record fail-closed behavior when no authenticated slot is valid.",
                [
                    *confirmed("a"),
                    write("slot_a", "slot_a_bad_signature"),
                    write("slot_b", "slot_b_bad_signature"),
                ],
            ),
            (
                "SB-META-001",
                "Metadata A erased",
                "Record redundant metadata recovery with copy A erased.",
                [
                    *confirmed_with_copy_fault("a", "metadata_a", 0xFF),
                    write("slot_a", "slot_a_valid"),
                ],
            ),
            (
                "SB-META-002",
                "Metadata B erased",
                "Record redundant metadata recovery with copy B erased.",
                [
                    *confirmed_with_copy_fault("a", "metadata_b", 0xFF),
                    write("slot_a", "slot_a_valid"),
                ],
            ),
            (
                "SB-META-003",
                "Metadata A zeroed",
                "Record redundant metadata recovery with copy A zeroed.",
                [
                    *confirmed_with_copy_fault("a", "metadata_a", 0x00),
                    write("slot_a", "slot_a_valid"),
                ],
            ),
            (
                "SB-META-004",
                "Metadata B zeroed",
                "Record redundant metadata recovery with copy B zeroed.",
                [
                    *confirmed_with_copy_fault("a", "metadata_b", 0x00),
                    write("slot_a", "slot_a_valid"),
                ],
            ),
            (
                "SB-META-005",
                "Both metadata copies erased",
                "Record fail-closed behavior with both metadata copies erased.",
                [
                    fill("metadata_a", 0xFF),
                    fill("metadata_b", 0xFF),
                    write("slot_a", "slot_a_valid"),
                ],
            ),
            (
                "SB-META-006",
                "Both metadata copies zeroed",
                "Record fail-closed behavior with both metadata copies zeroed.",
                [
                    fill("metadata_a", 0x00),
                    fill("metadata_b", 0x00),
                    write("slot_a", "slot_a_valid"),
                ],
            ),
            (
                "SB-RECOVERY-001",
                "Invalid metadata and invalid slots",
                "Record recovery behavior with invalid metadata and invalid slots.",
                [
                    fill("metadata_a", 0x00),
                    fill("metadata_b", 0x00),
                    write("slot_a", "slot_a_bad_signature"),
                    write("slot_b", "slot_b_bad_signature"),
                ],
            ),
            (
                "SB-ROLLBACK-001",
                "Rollback policy observation",
                "Record rollback-related behavior without changing the documented policy.",
                [*confirmed("a"), write("slot_a", "slot_a_valid"), write("slot_b", "slot_b_valid")],
            ),
        ]
    )
    tests.append(
        observe_case(
            "SB-STABILITY-002",
            "Interrupted execution restore",
            "Exercise transaction restoration after interrupted execution in host tests.",
            [write("slot_a", "slot_a_valid")],
        )
    )
    tests.append(
        TestCase(
            identifier="SB-PERF-001",
            name="Boot performance metrics",
            objective="Record CPU clock and boot verification timing from UART output.",
            preconditions=["valid Slot A image is present"],
            flash_actions=[*confirmed("a"), write("slot_a", "slot_a_valid")],
            required_uart=[
                field_equals("Verification", "OK"),
                literal("SHA-512"),
                literal("Ed25519"),
                literal("CPU clock"),
            ],
            forbidden_uart=[NOT_STARTED, HALTED],
            expected_verdict=Verdict.OBSERVE,
            tags=["performance", "observe"],
            timeout_seconds=5.0,
        )
    )
    return tests


def filter_tests(
    tests: list[TestCase],
    *,
    identifiers: set[str] | None = None,
    tags: set[str] | None = None,
    exclude_tags: set[str] | None = None,
) -> list[TestCase]:
    selected = tests
    if identifiers:
        selected = [test for test in selected if test.identifier in identifiers]
    if tags:
        selected = [test for test in selected if tags.intersection(test.tags)]
    if exclude_tags:
        selected = [test for test in selected if not exclude_tags.intersection(test.tags)]
    return selected
