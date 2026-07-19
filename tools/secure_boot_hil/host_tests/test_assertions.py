from __future__ import annotations

from secure_boot_hil.assertions import evaluate_assertion, evaluate_assertions, parse_fields
from secure_boot_hil.model import AssertionKind, AssertionSpec

BOOT_TEXT = """
Verification     = OK
Signature and payload hash accepted.
EXP066 RESEARCH PLATFORM
"""


def test_literal_assertion_normalizes_spacing() -> None:
    result = evaluate_assertion(
        BOOT_TEXT,
        AssertionSpec(kind=AssertionKind.LITERAL, expression="Verification = OK"),
    )
    assert result.matched
    assert result.excerpt is not None


def test_regex_assertion() -> None:
    result = evaluate_assertion(
        BOOT_TEXT,
        AssertionSpec(kind=AssertionKind.REGEX, expression=r"EXP066\s+RESEARCH"),
    )
    assert result.matched


def test_field_assertion() -> None:
    result = evaluate_assertion(
        BOOT_TEXT,
        AssertionSpec(
            kind=AssertionKind.FIELD_EQUALS,
            expression="OK",
            field="Verification",
            expected="OK",
        ),
    )
    assert result.matched


def test_required_and_forbidden_evaluation() -> None:
    required, forbidden, matched = evaluate_assertions(
        BOOT_TEXT,
        [AssertionSpec(kind=AssertionKind.LITERAL, expression="EXP066 RESEARCH PLATFORM")],
        [AssertionSpec(kind=AssertionKind.LITERAL, expression="Bootloader halted safely.")],
    )
    assert matched
    assert required[0].matched
    assert not forbidden[0].matched


def test_parse_fields() -> None:
    assert parse_fields("SHA-512          = 123 us\n")["sha-512"] == "123 us"
