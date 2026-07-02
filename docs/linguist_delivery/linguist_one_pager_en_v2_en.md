# Linguist One-Pager (en, rules_v2)

This guide is for linguists editing English TN rules **without coding assistants**.
It assumes you only edit one file and run a few terminal commands.

## 1) File You Edit

Edit only:

- `rules_v2/en.full.json`

Do **not** edit:

- `tts_normalizer_engine.cpp`

## 2) Where to Add New Rules

Inside `en.full.json`, go to:

- `pipeline.rules` (main rule list)
- optional: `resources.pattern_defs` (reusable regex fragments)

Rule order matters:

- Put **more specific** rules first.
- Put **more general** rules later.

## 3) Rule Shape (Quick Reference)

### Replace-only rule

```json
{
  "id": "en_case_replace_xxx",
  "pattern": "your_regex",
  "replace": "your_text"
}
```

### Exec rule

```json
{
  "id": "en_case_exec_xxx",
  "pattern": "your_regex",
  "action": "exec",
  "params": {
    "steps": [
      { "op": "grp", "g": 1, "as": "spellout" },
      { "op": "lit", "text": "..." }
    ]
  }
}
```

Common ops in en:

- `lit`: output fixed text
- `grp` + `as: "digits"`: read each digit
- `grp` + `as: "spellout"`: cardinal number reading
- `grp` + `as: "raw"`: keep original text
- `walk`: char-by-char conversion via `map`

## 4) Three Common Templates (en)

## A) Number Template (digit-by-digit)

Use when each digit should be spoken separately.

```json
{
  "id": "en_case_phone_digits",
  "pattern": "(?<!\\d)(\\d{7,11})(?!\\d)",
  "action": "exec",
  "params": {
    "steps": [
      { "op": "grp", "g": 1, "as": "digits" }
    ]
  }
}
```

## B) Unit Template (number + fixed unit)

Use when unit wording is fixed and simple.

```json
{
  "id": "en_case_weight_kg",
  "pattern": "(?<!\\d)(\\d+)\\s*kg\\b",
  "action": "exec",
  "params": {
    "steps": [
      { "op": "grp", "g": 1, "as": "spellout" },
      { "op": "lit", "text": " kilograms" }
    ]
  }
}
```

## C) Abbreviation Template (replace-only)

Use for stable acronym expansions.

```json
{
  "id": "en_case_abbr_ai",
  "pattern": "\\bAI\\b",
  "replace": "A I"
}
```

## 5) Safe Editing Checklist

Before save:

- Keep valid JSON (commas, quotes, brackets).
- Ensure every new rule has a unique `id`.
- Escape backslashes in regex (`\\d`, `\\b`).
- Confirm rule position (specific before generic).

## 6) Run and Validate (No Agent Required)

From repo root (`for_github`):

```bash
# Build en binary
ICU_ROOT=/opt/homebrew/opt/icu4c g++ -std=c++17 -O2 en.cpp tts_normalizer_engine.cpp \
  -I. -Ithird_party -I"$ICU_ROOT/include" -L"$ICU_ROOT/lib" \
  -licui18n -licuuc -licudata -o test/bin/en_tts

# Run en with rules_v2
TTS_RULES_FORMAT=v2 ./test/bin/en_tts < test/in/en.txt > /tmp/en_v2_test.out
```

Optional parity check against current production-style rules:

```bash
./test/bin/en_tts < test/in/en.txt > /tmp/en_v1_test.out
TTS_RULES_FORMAT=v2 ./test/bin/en_tts < test/in/en.txt > /tmp/en_v2_test.out
diff -u /tmp/en_v1_test.out /tmp/en_v2_test.out
```

Expected:

- If `diff` is empty, outputs are identical.
- If there are diffs, review whether they are intended.

## 7) Troubleshooting

- `Failed to load TTS rules v2`: JSON format issue or missing required fields.
- Regex does not trigger: check ordering and boundaries (`\\b`, lookarounds).
- Too many side effects: move rule lower, or make pattern more specific.

## 8) Team Convention (Recommended)

- Prefix IDs with `en_case_...`.
- Add one rule per change when possible (easier review).
- Keep a short comment in your change request: "input -> expected output".
