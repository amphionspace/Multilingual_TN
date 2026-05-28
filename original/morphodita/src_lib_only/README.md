# MorphoDiTa (library-only bundle)

Vendored single-file build of [MorphoDiTa](https://github.com/ufal/morphodita) for linking `ru_tts`.

| File | Role |
|------|------|
| `morphodita.h` | Public C++ API |
| `morphodita.cpp` | Merged library sources (generated upstream via `src_lib_only/Makefile`) |

License: Mozilla Public License 2.0 (see file headers in `morphodita.cpp`).

To regenerate from a full MorphoDiTa checkout, use the upstream `src_lib_only` target; this repo keeps the pre-merged sources so `test/scripts/build.sh` does not depend on `local_exp` or a local `build/_deps` tree.
