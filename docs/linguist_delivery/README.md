# Linguist Delivery Pack (en Pilot)

This folder is the single handoff entry for linguist-facing documentation.

## Files

- `linguist_one_pager_en_v2_en.md`  
  Full practical guide (editing flow, templates, validation commands).
- `linguist_quick_reference_en_v2_en.md`  
  One-page printable cheat sheet (templates + commands only).
- `rule_v2.schema.json`  
  Draft schema for direct-authored `rules_v2` files.
- `rules_v2_draft.md`  
  Scope and current runtime support notes for `rules_v2`.

## Current en Working File

- `rules_v2/en.full.json`

## Runtime Switch

- Use `TTS_RULES_FORMAT=v2` to load `rules_v2/en.full.json` in `en.cpp`.
