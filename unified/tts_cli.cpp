// Unified CLI: replaces the five per-language main()s. Behavior is chosen entirely by
// the resource directory passed via --data, e.g.:
//   tts_cli --data data/ru < input > output
//   tts_cli --data data/en < input > output
#include <iostream>
#include <string>

#include "text_normalizer.hpp"

int main(int argc, char** argv) {
    std::string dataDir;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--data" && i + 1 < argc) {
            dataDir = argv[++i];
        }
    }
    if (dataDir.empty()) {
        std::cerr << "Usage: tts_cli --data <resource-dir>   (e.g. --data data/ru)\n";
        return 2;
    }

    TextNormalizer normalizer;
    if (!normalizer.Init(dataDir)) {
        std::cerr << "Init failed: " << normalizer.error() << std::endl;
        return 1;
    }

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) {
            std::cout << std::endl;
            continue;
        }
        std::cout << normalizer.normalizeLine(line) << std::endl;
    }
    return 0;
}
