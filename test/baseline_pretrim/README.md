# Pre-trim regression baseline (S0)

Captured BEFORE any ICU data/link trimming, to prove trimming does not change TN output.

- TN commit:   merged main (ICU size-reduction merged in; includes rules_v2 frontend-punctuation integration for zh/ru/ar/bn)
- Branch:      main (post-merge of chore/icu-size-reduction)
- ICU version: 78.3 (system/homebrew, full data)
- Binary:      test/bin/tts_cli (unified architecture)
- Date:        2026-07-24 (regenerated on merged main so trim verification diffs against current TN output)

## Corpora
- transsion1000/  : 1000-sentence sample x 5 langs (en zh ar ru bn)
- cmos/           : transsion_cmos x 3 langs (en zh ru)
- smoke/          : test/in x 5 langs (+ zh_car_plates)

Inputs are pinned in INPUTS.md5; outputs in OUTPUTS.md5.
1000-sample inputs live outside the repo at:
  ICU/local_exp/test/transsion_testset/<lang>_1000_sample_sent.txt

## How to verify after trimming
Rebuild tts_cli against the trimmed ICU, regenerate the SAME outputs into this
tree (same relative paths), then recompute and diff the checksum listing:

  ( cd test/baseline_pretrim && find . -name '*.out' | sort | xargs md5 -r ) \
    | diff - test/baseline_pretrim/OUTPUTS.md5 && echo "PASS: byte-identical"

Any diff => trimming removed ICU data/code that TN actually needs.
