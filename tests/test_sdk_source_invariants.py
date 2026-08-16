from __future__ import annotations

# Dual-version (2025/2026) source consistency gate.
#
# This test is a CODE-REVIEW GATE, not a unit test: it asserts that the two
# SDK sources stay byte-identical except for the version-stamped trace
# filenames, and that neither source writes dimension geometry through the
# non-public `ovDimStartPt`/`ovDimEndPt` selectors.
#
# IMPORTANT: the structural assertions below only hold for the *post-fix*
# source tree (after the 8 bug fixes + the pre-existing WIP that removed the
# `{ ovDimStartPt, ... }` variable-block write at ApplyDimensionVariableTransaction
# are committed). On the pre-fix HEAD this test intentionally FAILS.
# Wire it into .github/workflows/test.yml (a "Run dual-version gate" step) only
# AFTER those source changes are committed, otherwise CI will be red.
#
# Run manually today with:
#   python3 tests/test_sdk_source_invariants.py \
#     --source-2025 sdk-projects/2025/AutoDimensionPlugin/Source/AutoDimensionObj.cpp \
#     --source-2026 sdk-projects/2026/AutoDimensionPlugin/Source/AutoDimensionObj.cpp

import argparse
import re
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def function_body(source: str, name: str) -> str:
    start = source.find(name)
    require(start >= 0, f"missing function: {name}")
    brace = source.find("{", start)
    require(brace >= 0, f"missing function body: {name}")
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[brace : index + 1]
    raise AssertionError(f"unterminated function: {name}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-2025", type=Path, required=True)
    parser.add_argument("--source-2026", type=Path, required=True)
    args = parser.parse_args()

    source_2025 = args.source_2025.read_text(encoding="utf-8")
    source_2026 = args.source_2026.read_text(encoding="utf-8")
    normalized_2025 = source_2025.replace("vw-autodim-runtime-2025.txt", "vw-autodim-runtime-VERSION.txt")
    normalized_2026 = source_2026.replace("vw-autodim-runtime-2026.txt", "vw-autodim-runtime-VERSION.txt")
    normalized_2025 = normalized_2025.replace("vw-autodim-align-2025.txt", "vw-autodim-align-VERSION.txt")
    normalized_2026 = normalized_2026.replace("vw-autodim-align-2026.txt", "vw-autodim-align-VERSION.txt")
    require(normalized_2025 == normalized_2026, "2025 and 2026 implementations have drifted")

    require("{ ovDimStartPt," not in source_2025 and "{ ovDimEndPt," not in source_2025,
            "dimension endpoints must not be written through private SDK selectors")

    presentation = function_body(source_2025, "CopyDimensionPresentationFrom")
    require("ovDimStartOffset," not in presentation and "ovDimStartOffsetInCurrUnits," not in presentation,
            "presentation copying must not overwrite replacement geometry offsets")

    transaction = function_body(source_2025, "ApplyDimensionVariableTransaction")
    require("changedIndices.empty()" in transaction and "VariableBlocksEquivalent" in transaction,
            "dimension transactions must reject same-value no-op writes")

    edit = function_body(source_2025, "EditSelectedDimensions")
    align_match = re.search(r"case kEditAlign:(.*?)(?:case|default:)", edit, re.S)
    require(align_match is not None, "missing dimension-alignment case")
    align = align_match.group(1)
    require("alignedDimensionOffset" in align and "alignmentTargetLine" in edit and "editPoint" in edit,
            "alignment must derive each target offset from the clicked world-space line")
    require("GetDimensionAxis" in align,
            "alignment must use the rendered H/V axis for constrained dimensions")
    require("CreateLinearReplacement" in align and "DeleteObject(dimension, true)" in align,
            "alignment must rebuild dimension geometry instead of relying on an in-place offset write")
    require("along > endpointTolerance" in edit and "axisLength + endpointTolerance" in edit and '"extended"' in edit,
            "split/extend must use the clicked projection for both operations")

    reset_match = re.search(r"case kEditResetTextPosition:(.*?)(?:case|default:)", edit, re.S)
    require(reset_match is not None, "missing reset-text-position case")
    reset = reset_match.group(1)
    for selector in ("ovDimTextPosCalculated", "ovDimTextPosInside", "ovDimTextOffsetInCurrUnits", "ovDimTextAboveLineInCurrUnits"):
        require(selector in reset, f"reset-text-position does not reset {selector}")
    require("ovDimTextRotation" not in reset, "reset-text-position must not change text rotation")

    print("SDK source invariants passed")


if __name__ == "__main__":
    main()
