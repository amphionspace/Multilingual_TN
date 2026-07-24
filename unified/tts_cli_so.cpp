// Thin CLI over the shared library's C API — the runnable demo / verification driver
// for libtts_normalizer. Built by test/scripts/build_tts_so.sh as build/tts_cli_so.
//
//   build/tts_cli_so --data data/ru < input.txt > output.txt
//
// It links libtts_normalizer and uses ONLY the public C API, so it also serves as a
// minimal integration example.
#include <cstdio>
#include <iostream>
#include <string>

#include "tts_capi.h"

int main(int argc, char** argv) {
    std::string dir;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--data" && i + 1 < argc) {
            dir = argv[++i];
        }
    }
    if (dir.empty()) {
        std::cerr << "Usage: tts_cli_so --data <resource-dir>   (e.g. --data data/ru)\n";
        return 2;
    }

    tts_normalizer_t* h = tts_init(dir.c_str());
    if (h == nullptr) {
        const char* e = tts_last_error();
        std::cerr << "tts_init failed: " << (e ? e : "unknown error") << std::endl;
        return 1;
    }

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) {
            std::cout << std::endl;
            continue;
        }
        const char* out = tts_normalize(h, line.c_str());
        std::cout << (out ? out : "") << std::endl;
    }
    tts_free(h);
    return 0;
}
