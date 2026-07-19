"""UART assertion evaluation for HIL test evidence."""

from __future__ import annotations

import re

from .model import AssertionKind, AssertionResult, AssertionSpec

FIELD_RE = re.compile(r"^\s*(?P<name>[A-Za-z0-9][A-Za-z0-9 /_-]*?)\s*=\s*(?P<value>.*?)\s*$")


def normalize_space(text: str) -> str:
    return " ".join(text.split())


def parse_fields(text: str) -> dict[str, str]:
    fields: dict[str, str] = {}
    for line in text.splitlines():
        match = FIELD_RE.match(line)
        if match:
            fields[normalize_space(match.group("name")).lower()] = match.group("value").strip()
    return fields


def _literal_match(text: str, expression: str) -> tuple[bool, str | None]:
    normalized_text = normalize_space(text)
    normalized_expression = normalize_space(expression)
    if normalized_expression not in normalized_text:
        return False, None
    index = normalized_text.find(normalized_expression)
    return True, normalized_text[max(0, index - 40) : index + len(normalized_expression) + 40]


def _regex_match(text: str, expression: str) -> tuple[bool, str | None]:
    match = re.search(expression, text, flags=re.MULTILINE)
    if not match:
        return False, None
    return True, match.group(0)


def _field_match(text: str, spec: AssertionSpec) -> tuple[bool, str | None]:
    if spec.field is None:
        return False, "field assertion missing field name"
    fields = parse_fields(text)
    key = normalize_space(spec.field).lower()
    observed = fields.get(key)
    expected = str(spec.expected if spec.expected is not None else spec.expression)
    if observed is None:
        return False, None
    matched = observed == expected
    return matched, f"{spec.field} = {observed}"


def evaluate_assertion(text: str, spec: AssertionSpec) -> AssertionResult:
    if spec.kind == AssertionKind.LITERAL:
        matched, excerpt = _literal_match(text, spec.expression)
    elif spec.kind == AssertionKind.REGEX:
        matched, excerpt = _regex_match(text, spec.expression)
    elif spec.kind == AssertionKind.FIELD_EQUALS:
        matched, excerpt = _field_match(text, spec)
    else:
        matched, excerpt = False, "unsupported assertion kind"
    return AssertionResult(spec=spec, matched=matched, excerpt=excerpt)


def evaluate_assertions(
    text: str,
    required: list[AssertionSpec],
    forbidden: list[AssertionSpec],
) -> tuple[list[AssertionResult], list[AssertionResult], bool]:
    required_results = [evaluate_assertion(text, item) for item in required]
    forbidden_results = [evaluate_assertion(text, item) for item in forbidden]
    passed = all(item.matched for item in required_results) and not any(
        item.matched for item in forbidden_results
    )
    return required_results, forbidden_results, passed
