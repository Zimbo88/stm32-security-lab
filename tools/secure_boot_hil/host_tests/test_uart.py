from __future__ import annotations

from secure_boot_hil.uart import extract_frames, select_newest_complete_frame

BANNER = """========================================
STM32F429 SECURITY LAB
EXP045 BOOTLOADER V2
========================================
"""


def test_one_complete_frame() -> None:
    frames = extract_frames((BANNER + "Verification     = OK\nEXP066 RESEARCH PLATFORM\n").encode())
    assert len(frames) == 1
    assert frames[0].complete


def test_stale_partial_then_complete_frame() -> None:
    raw = (
        b"partial stale boot\nVerification     = OK\n"
        + (BANNER + "Verification     = OK\nEXP066 RESEARCH PLATFORM\n").encode()
    )
    selected = select_newest_complete_frame(extract_frames(raw))
    assert selected is not None
    assert "EXP066 RESEARCH PLATFORM" in selected.text


def test_two_complete_frames_selects_newest() -> None:
    raw = (
        BANNER
        + "Verification     = OK\nEXP066 RESEARCH PLATFORM\n"
        + BANNER
        + "BAD MAGIC\nApplication will NOT be started.\n"
    ).encode()
    selected = select_newest_complete_frame(extract_frames(raw))
    assert selected is not None
    assert "BAD MAGIC" in selected.text


def test_incomplete_frame_is_recorded() -> None:
    frames = extract_frames((BANNER + "Verification     = OK\n").encode())
    assert len(frames) == 1
    assert not frames[0].complete


def test_empty_capture_has_no_frames() -> None:
    assert extract_frames(b"") == []


def test_invalid_utf8_is_decoded_with_replacement() -> None:
    frames = extract_frames(b"\xff" + (BANNER + "Bootloader halted safely.\n").encode())
    assert frames[0].complete
    assert "\ufffd" in frames[0].text or frames[0].start_offset > 0
