# English-Chinese G2P resource bundle

This directory is a self-contained runtime profile for mixed English-Chinese G2P.
It must not resolve dictionaries or token inventories from the parent TTS repository.

- `config.json` selects the backend, rule pipeline, dictionaries, and data-driven exceptions.
- `resources/` contains the Chinese lexicon, user dictionary, Pinyin-to-Bopomofo map, and merged CMU dictionary.
- `model_tokens.json` defines the stable phoneme-to-id contract used by the paired acoustic model.

Normal data updates should modify these resource and JSON files without changing the public C++ API.
New algorithms may still require a native-library update.
