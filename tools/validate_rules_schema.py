#!/usr/bin/env python3
"""Validate rules/en.json, rules/zh.json, rules/ar.json, rules/bn.json, and rules/ru.json against docs/rule.schema.json (dev / CI).

Requires: pip install jsonschema
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    try:
        import jsonschema
    except ImportError:
        print("Missing package: pip install jsonschema", file=sys.stderr)
        print(
            "Example: python3 -m venv .venv && .venv/bin/pip install jsonschema "
            "&& .venv/bin/python tools/validate_rules_schema.py",
            file=sys.stderr,
        )
        return 1

    schema_path = ROOT / "docs" / "rule.schema.json"
    schema = json.loads(schema_path.read_text(encoding="utf-8"))
    for name in ("en.json", "zh.json", "ar.json", "bn.json", "ru.json"):
        path = ROOT / "rules" / name
        inst = json.loads(path.read_text(encoding="utf-8"))
        try:
            jsonschema.validate(inst, schema)
        except jsonschema.ValidationError as e:
            loc = "/".join(str(p) for p in e.absolute_path)
            print(f"FAIL {name}: {e.message}", file=sys.stderr)
            print(f"  at: {loc}", file=sys.stderr)
            return 1
        print("OK", path.relative_to(ROOT))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
