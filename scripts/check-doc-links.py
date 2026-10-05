#!/usr/bin/env python3
"""Check local Markdown links in the maintained product documentation."""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REQUIRED = [
    ROOT / "README.md",
    ROOT / "docs/status/FINAL_PRODUCT_STATUS.md",
    ROOT / "docs/security/SECURITY_AUDIT.md",
    ROOT / "docs/security/SECURITY_FINDINGS.md",
    ROOT / "docs/security/SECURITY_TESTING.md",
    ROOT / "docs/security/SECURITY_VALIDATION_REPORT.md",
    ROOT / "docs/security/THREAT_MODEL.md",
    ROOT / "LIMITATIONS.md",
    ROOT / "docs/architecture/ORCHESTRA_OS_ARCHITECTURE.md",
    ROOT / "docs/installation/INSTALL.md",
    ROOT / "docs/usage/USAGE.md",
    ROOT / "docs/troubleshooting/TROUBLESHOOTING.md",
    ROOT / "docs/development/DEVELOPMENT.md",
    ROOT / "docs/research/RESEARCH_TO_CODE.md",
    ROOT / "docs/releases/v1.0.0-research-stable.md",
    ROOT / "docs/validation/FINAL_VALIDATION_REPORT.md",
]
EXCLUDED = ("artifacts/", "docs/history/", "docs/paper/data/", "docs/paper/audit/")
SOURCES = sorted({*REQUIRED, *(path for path in ROOT.rglob("*.md")
    if ".git" not in path.parts and not path.relative_to(ROOT).as_posix().startswith(EXCLUDED))})
LINK = re.compile(r"!?(?:\[[^\]]*\])\(([^)]+)\)")


def main() -> int:
    failures: list[str] = []
    for source in SOURCES:
        if not source.is_file():
            failures.append(f"missing documentation source: {source.relative_to(ROOT)}")
            continue
        for raw_target in LINK.findall(source.read_text(encoding="utf-8")):
            target = raw_target.strip().split(" ", 1)[0].strip("<>")
            if not target or target.startswith(("http://", "https://", "mailto:", "#")):
                continue
            target = target.split("#", 1)[0]
            if not target:
                continue
            resolved = (source.parent / target).resolve()
            try:
                resolved.relative_to(ROOT)
            except ValueError:
                failures.append(f"outside repository: {source.relative_to(ROOT)} -> {target}")
                continue
            if not resolved.exists():
                failures.append(f"missing link: {source.relative_to(ROOT)} -> {target}")
    if failures:
        for failure in failures:
            print(failure, file=sys.stderr)
        return 1
    print(f"DOC_LINKS_PASS ({len(SOURCES)} maintained documents)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
