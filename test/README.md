# TTS normalizer — locale harness

This folder holds **sample inputs**, **baseline outputs**, **compiled drivers** (`bin/`), and **scripts** to exercise `en` / `zh` / `ar` / `bn` / `ru` without touching product code.

## Layout

| Path | Purpose |
|------|---------|
| `bin/*_tts` | Per-locale normalizer (rebuild with `scripts/build.sh`). |
| `rules` | Symlink → `../rules` so binaries resolve `rules/<locale>.json` when **cwd is this directory** (`test/`). |
| `in/<locale>.txt` | One line = one sentence to normalize (stdin line-by-line). |
| `out/<locale>.out` | Latest run output (regenerated; safe to delete). |
| `expected/<locale>.golden` | Human-reviewed baseline; `scripts/run_all.sh` diffs against this. |
| `scripts/build.sh` | Compile locale drivers into `bin/` (needs ICU; `ru` also builds/links MorphoDiTa). |
| `scripts/run_all.sh` | Run all locales, write `out/`, compare to `expected/`. |
| `scripts/run_transsion_testset.sh` | One-command run for `transsion_testset/*_1000_sample_sent.txt`; writes timestamped outputs + summary report. |

## Quick start

```bash
cd /Users/amphion/Desktop/work/reference/ICU/test
./scripts/run_all.sh
```

Rebuild after C++ or JSON changes:

```bash
cd /Users/amphion/Desktop/work/reference/ICU/test
./scripts/build.sh
./scripts/run_all.sh
```

Run the client testset in one command:

```bash
cd /Users/amphion/Desktop/work/reference/ICU/test
./scripts/run_transsion_testset.sh
```

Useful overrides:

```bash
# custom model path for Russian
RU_MORPH_MODEL=/path/to/model.tagger ./scripts/run_transsion_testset.sh

# custom output root / run tag
TRANS_OUT_DIR=./transsion_runs RUN_TAG=trial_v2 ./scripts/run_transsion_testset.sh
```

Refresh baselines (guarded flow) when output changes are intentional:

```bash
UPDATE_LOCALES=ru CONFIRM_GOLDEN_UPDATE=I_HAVE_REVIEWED_DIFF ./scripts/run_all.sh
```

Update multiple locales:

```bash
UPDATE_LOCALES="zh,ru" CONFIRM_GOLDEN_UPDATE=I_HAVE_REVIEWED_DIFF ./scripts/run_all.sh
```

Non-interactive (CI-like) update after review:

```bash
UPDATE_LOCALES=ru CONFIRM_GOLDEN_UPDATE=I_HAVE_REVIEWED_DIFF AUTO_APPROVE_UPDATE=1 ./scripts/run_all.sh
```

## Requirements

- **ICU** (e.g. `brew install icu4c`). `scripts/build.sh` looks under `/opt/homebrew/opt/icu4c` and `/usr/local/opt/icu4c`.
- **Rules JSON** must exist under `../rules/` (regenerate from repo root: `python3 tools/generate_tts_rules_json.py`).
- **Russian morph model** for `ru_tts`: default path is `../original/morphodita/models/russian-syntagrus-morphodita-only.tagger` or override with `RU_MORPH_MODEL=/path/to/model.tagger`.

## Notes

- Drivers resolve `rules/` using `__FILE__` at compile time; with the usual relative `en.cpp` path, **`cwd` must be `ICU/test/`** (hence the `rules` symlink here), or run from `ICU/` with paths adjusted—`run_all.sh` always `cd`s to `test/`.
- Golden updates are intentionally guarded: old `UPDATE_GOLDEN=1` is rejected to prevent accidental full overwrite.
- `UPDATE_LOCALES` only updates explicitly named locales; all others remain strict diff checks.
- `CONFIRM_GOLDEN_UPDATE=I_HAVE_REVIEWED_DIFF` is required before any overwrite.
- In interactive terminals, each locale update asks for approval; for automation, set `AUTO_APPROVE_UPDATE=1` explicitly.
- `run_transsion_testset.sh` skips unsupported locales in the dataset (e.g. `es`) and records the reason in `transsion_runs/<run_tag>/report/summary.csv`.
