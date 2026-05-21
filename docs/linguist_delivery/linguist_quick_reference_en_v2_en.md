# Linguist Quick Reference (en, rules_v2)

For printing and daily use.  
Only templates + commands.

## Edit File

- `rules_v2/en.full.json`

## Template 1: Number (digit-by-digit)

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

## Template 2: Unit (number + fixed unit)

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

## Template 3: Abbreviation (replace-only)

```json
{
  "id": "en_case_abbr_ai",
  "pattern": "\\bAI\\b",
  "replace": "A I"
}
```

## Build + Run (v2)

```bash
ICU_ROOT=/opt/homebrew/opt/icu4c g++ -std=c++17 -O2 en.cpp tts_normalizer_engine.cpp \
  -I. -Ithird_party -I"$ICU_ROOT/include" -L"$ICU_ROOT/lib" \
  -licui18n -licuuc -licudata -o test/bin/en_tts

TTS_RULES_FORMAT=v2 ./test/bin/en_tts < test/in/en.txt > /tmp/en_v2_test.out
```

## Parity Check (v1 vs v2)

```bash
./test/bin/en_tts < test/in/en.txt > /tmp/en_v1_test.out
TTS_RULES_FORMAT=v2 ./test/bin/en_tts < test/in/en.txt > /tmp/en_v2_test.out
diff -u /tmp/en_v1_test.out /tmp/en_v2_test.out
```

Expected: empty diff.
