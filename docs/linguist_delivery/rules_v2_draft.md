# rules_v2 Draft (zh Pilot)

This draft starts the transition from Python-generated rules to direct linguist-authored configs.

## Scope in this round

- Added `docs/linguist_delivery/rule_v2.schema.json` as the draft contract.
- Added `rules_v2/en.full.json` as the parity baseline sample for English.
- Added C++ loader entry `TtsNormalizerEngine::loadRulesV2()` as a skeleton adapter.

## v2 Structure

- `format`: fixed string `tts_rules_v2`.
- `locale`: locale tag, e.g. `zh`.
- `resources`: locale dictionaries (`digit_names`, `currency_map`, `unit_*`, optional `month_names`, `pattern_defs`).
- `pipeline.rules`: ordered list of replace/exec rules, same runtime semantics as current engine.

## Runtime skeleton behavior

`loadRulesV2()` currently:

1. validates minimal root fields (`format`, `pipeline`),
2. adapts v2 payload into current runtime JSON shape,
3. reuses existing loading path (`loadRulesFromJson`).

This keeps risk low while enabling early linguist-facing file design.

## Not in this round (next steps)

- Dedicated v2 validator script and CI gate.
- Enable/disable switches and richer macro packs for linguists.
- Migration helpers from current `rules/*.json` to `rules_v2/*.json`.
