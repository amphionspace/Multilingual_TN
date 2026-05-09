#!/usr/bin/env python3
"""Emit docs/tts_locale_field_matrix.{md,csv} from rules/*.json (reference only; not loaded at runtime)."""

from __future__ import annotations

import csv
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RULES = ROOT / "rules"
DOCS = ROOT / "docs"

# Locales we ship today; add a column header here when a new rules/<locale>.json exists.
LOCALE_FILES = ("en.json", "zh.json", "ar.json", "bn.json", "ru.json")

# Optional future locales — column present but left empty until JSON exists.
FUTURE_LOCALE_COLUMNS = ("ja", "de", "ko", "fr")

# Root keys always listed in the matrix even if missing from all shipped JSON (engine may still read them).
EXTRA_ROOT_KEYS = frozenset({"month_names_en", "month_names_ar", "month_names_bn", "month_names_ru"})

ROOT_FIELD_HELP: dict[str, str] = {
    "locale": "BCP-47-ish tag consumed by the engine (`localeTag_`).",
    "digit_names": "Length-10 array: names for digits 0–9 (used by `digits` / `walk` / `date_ymd` digit modes).",
    "currency_map": "Object: currency symbol / code → spoken suffix after spellout amount.",
    "unit_composite": "Object: composite unit token → spoken suffix after spellout number.",
    "unit_single": "Object: single-letter or short unit → spoken suffix after spellout number.",
    "pattern_defs": "Phase B: map NAME → ICU substring; rules may use {{NAME}}; expanded in loadRules.",
    "month_names_en": "Optional length-12 array overriding English month names (Jan–Dec); engine default if omitted.",
    "month_names_ar": "Optional length-12 array for Arabic Gregorian month names (`date_iso_en` when locale is ar).",
    "month_names_bn": "Optional length-12 array for Bengali Gregorian month names (`date_iso_en` when locale is bn).",
    "month_names_ru": "Optional length-12 array for Russian month names (`date_iso_en` when locale is ru).",
    "rules": "Ordered array of rule objects; order is pipeline order.",
}

RULE_FIELD_HELP: dict[str, str] = {
    "id": "Stable rule id for logs / handoff (generator: `en_00`, `zh_10`, `ar_05`, `bn_08`, `ru_03`, …).",
    "pattern": "ICU regular expression; required on every rule.",
    "action": "Handler name: `exec`, `pinyin_han_paren_zh`, `pinyin_standalone_zh`, `emoji_clear`, or omit for replace-only.",
    "replace": "Replacement template with `$0`…`$9`; used when `action` is absent.",
    "params": "Object; for `action: exec`, must include `steps` (array of `{op, ...}`).",
}


def load_doc(path: Path) -> dict | None:
    if not path.is_file():
        return None
    return json.loads(path.read_text(encoding="utf-8"))


def rule_key_union(docs: dict[str, dict]) -> list[str]:
    keys: set[str] = set()
    for doc in docs.values():
        for r in doc.get("rules") or []:
            if isinstance(r, dict):
                keys.update(r.keys())
    return sorted(keys)


def root_rows(docs: dict[str, dict | None], all_locales: tuple[str, ...]) -> list[tuple[str, str, ...]]:
    keys: set[str] = set(EXTRA_ROOT_KEYS)
    for doc in docs.values():
        if doc:
            keys.update(doc.keys())
    rows: list[tuple[str, str, ...]] = []
    for key in sorted(keys):
        desc = ROOT_FIELD_HELP.get(key, "")
        cells = [desc]
        for loc in all_locales:
            doc = docs.get(loc)
            cells.append("✓" if doc and key in doc else "")
        rows.append((key, *cells))
    return rows


def rule_rows(docs: dict[str, dict | None], rule_keys: list[str], all_locales: tuple[str, ...]) -> list[tuple[str, str, ...]]:
    rows: list[tuple[str, str, ...]] = []
    for key in rule_keys:
        desc = RULE_FIELD_HELP.get(key, "")
        cells = [desc]
        for loc in all_locales:
            doc = docs.get(loc)
            if not doc:
                cells.append("")
                continue
            present = any(isinstance(r, dict) and key in r for r in (doc.get("rules") or []))
            cells.append("✓" if present else "")
        rows.append((key, *cells))
    return rows


def main() -> None:
    DOCS.mkdir(parents=True, exist_ok=True)

    docs: dict[str, dict | None] = {}
    for fn in LOCALE_FILES:
        loc = fn.replace(".json", "")
        docs[loc] = load_doc(RULES / fn)

    shipped = tuple(loc for loc, d in docs.items() if d is not None)
    all_cols = shipped + FUTURE_LOCALE_COLUMNS

    root = root_rows(docs, all_cols)
    rkeys = rule_key_union({k: v for k, v in docs.items() if v})
    rule_r = rule_rows(docs, rkeys, all_cols)

    md_path = DOCS / "tts_locale_field_matrix.md"
    csv_path = DOCS / "tts_locale_field_matrix.csv"

    # --- Markdown ---
    lines: list[str] = [
        "# 多语言规则文件：统一字段矩阵（参考）",
        "",
        "本文件由 `tools/export_locale_field_matrix.py` **根据当前 `rules/*.json` 自动生成**，仅供查阅与交接；**运行时不会读取**本文件或 CSV。",
        "",
        "表中 **✓** 表示该语言的 JSON **根对象**或**至少一条规则**使用了该字段；空白表示当前仓库中**未使用**（将来可填）。新增语言时：",
        "",
        "1. 增加 `rules/<locale>.json`；",
        "2. 在脚本 `LOCALE_FILES` 或 `FUTURE_LOCALE_COLUMNS` 中加入列名；",
        "3. 重新运行 `python3 tools/export_locale_field_matrix.py`。",
        "",
        "## 根级字段（`rules/<locale>.json` 顶层）",
        "",
        "| 字段 | 说明 | " + " | ".join(all_cols) + " |",
        "|------|------|" + "|".join(["--------"] * len(all_cols)) + "|",
    ]
    for key, desc, *cells in root:
        esc = desc.replace("|", "\\|").replace("\n", " ")
        row_cells = " | ".join(c or " " for c in cells)
        lines.append(f"| `{key}` | {esc} | {row_cells} |")
    lines.extend(
        [
            "",
            "## 规则对象字段（`rules[]` 每一项可出现键的并集）",
            "",
            "同一语言里并非每条规则都包含全部键（例如 `exec` 用 `params`，纯替换规则用 `replace` 且无 `action`）。",
            "",
            "| 字段 | 说明 | " + " | ".join(all_cols) + " |",
            "|------|------|" + "|".join(["--------"] * len(all_cols)) + "|",
        ]
    )
    for key, desc, *cells in rule_r:
        esc = desc.replace("|", "\\|").replace("\n", " ")
        row_cells = " | ".join(c or " " for c in cells)
        lines.append(f"| `{key}` | {esc} | {row_cells} |")

    lines.extend(
        [
            "",
            "## 附属数据（非 `rules/<locale>.json` 主文档）",
            "",
            "| 文件 | 说明 | en | zh | " + " | ".join(FUTURE_LOCALE_COLUMNS) + " |",
            "|------|------|----|----|" + "|".join(["----"] * len(FUTURE_LOCALE_COLUMNS)) + "|",
            "| `rules/zh_pinyin.json` | 带声调拼音音节 → 单字（供 `pinyin_*` 规则查表）。 | | ✓ | "
            + " | ".join([""] * len(FUTURE_LOCALE_COLUMNS))
            + " |",
            "",
            "## 单表 CSV",
            "",
            "便于在 Excel / Numbers 中筛选：见同目录 **`tts_locale_field_matrix.csv`**（UTF-8）。",
            "",
        ]
    )
    md_path.write_text("\n".join(lines) + "\n", encoding="utf-8")

    # --- CSV (Excel-friendly UTF-8 with BOM) ---
    with csv_path.open("w", encoding="utf-8-sig", newline="") as f:
        w = csv.writer(f)
        w.writerow(["section", "field", "description"] + list(all_cols))
        for key, desc, *cells in root:
            w.writerow(["root", key, desc] + cells)
        for key, desc, *cells in rule_r:
            w.writerow(["rule", key, desc] + cells)
        w.writerow(
            ["auxiliary", "rules/zh_pinyin.json", "Pinyin syllable → Hanzi map (Chinese only).", "", "✓"]
            + [""] * len(FUTURE_LOCALE_COLUMNS)
        )

    print("Wrote", md_path, csv_path)


if __name__ == "__main__":
    main()
