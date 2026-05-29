#include "ru_year_spellout.hpp"

#include <cctype>
#include <unordered_map>
#include <vector>

namespace {

std::string toLowerAscii(std::string s) {
    for (char& c : s) {
        if (static_cast<unsigned char>(c) < 128) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    return s;
}

bool endsWithUtf8(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string replaceSuffixUtf8(std::string word, const std::string& from, const std::string& to) {
    if (endsWithUtf8(word, from)) {
        word.replace(word.size() - from.size(), from.size(), to);
    }
    return word;
}

const std::vector<std::string>& neutUnits1to9() {
    static const std::vector<std::string> u = {
        "первое", "второе", "третье", "четвертое", "пятое", "шестое", "седьмое", "восьмое", "девятое"};
    return u;
}

const std::vector<std::string>& neutTeens() {
    static const std::vector<std::string> t = {"десятое",     "одиннадцатое", "двенадцатое",  "тринадцатое",
                                             "четырнадцатое", "пятнадцатое",  "шестнадцатое", "семнадцатое",
                                             "восемнадцатое", "девятнадцатое"};
    return t;
}

const char* neutTensFused(int tens) {
    switch (tens) {
        case 20:
            return "двадцатое";
        case 30:
            return "тридцатое";
        case 40:
            return "сороковое";
        case 50:
            return "пятидесятое";
        case 60:
            return "шестидесятое";
        case 70:
            return "семидесятое";
        case 80:
            return "восьмидесятое";
        case 90:
            return "девяностое";
        default:
            return nullptr;
    }
}

const char* tensWord(int tens) {
    switch (tens) {
        case 20:
            return "двадцать";
        case 30:
            return "тридцать";
        case 40:
            return "сорок";
        case 50:
            return "пятьдесят";
        case 60:
            return "шестьдесят";
        case 70:
            return "семьдесят";
        case 80:
            return "восемьдесят";
        case 90:
            return "девяносто";
        default:
            return nullptr;
    }
}

const char* hundredsCardinalWord(int hundreds) {
    switch (hundreds) {
        case 100:
            return "сто";
        case 200:
            return "двести";
        case 300:
            return "триста";
        case 400:
            return "четыреста";
        case 500:
            return "пятьсот";
        case 600:
            return "шестьсот";
        case 700:
            return "семьсот";
        case 800:
            return "восемьсот";
        case 900:
            return "девятьсот";
        default:
            return nullptr;
    }
}

const char* hundredsOrdinalNeutWord(int hundreds) {
    switch (hundreds) {
        case 100:
            return "сотое";
        case 200:
            return "двухсотое";
        case 300:
            return "трехсотое";
        case 400:
            return "четырехсотое";
        case 500:
            return "пятисотое";
        case 600:
            return "шестисотое";
        case 700:
            return "семисотое";
        case 800:
            return "восьмисотое";
        case 900:
            return "девятисотое";
        default:
            return nullptr;
    }
}

std::string neutTail(int yy) {
    if (yy <= 0 || yy > 99) {
        return {};
    }
    if (yy <= 9) {
        return neutUnits1to9()[static_cast<size_t>(yy - 1)];
    }
    if (yy <= 19) {
        return neutTeens()[static_cast<size_t>(yy - 10)];
    }
    const int tens = (yy / 10) * 10;
    const int unit = yy % 10;
    if (unit == 0) {
        const char* fused = neutTensFused(tens);
        return fused ? std::string(fused) : std::string();
    }
    const char* tw = tensWord(tens);
    if (!tw) {
        return {};
    }
    return std::string(tw) + " " + neutUnits1to9()[static_cast<size_t>(unit - 1)];
}

std::string yearNeuterPhrase(int year) {
    const int century = year / 100;
    const int yy = year % 100;
    if (century == 18) {
        if (yy == 0) {
            return "тысяча восьмисотое";
        }
        const std::string tail = neutTail(yy);
        return tail.empty() ? std::string() : "тысяча восемьсот " + tail;
    }
    if (century == 19) {
        if (yy == 0) {
            return "тысяча девятьсотое";
        }
        const std::string tail = neutTail(yy);
        return tail.empty() ? std::string() : "тысяча девятьсот " + tail;
    }
    if (century == 20) {
        if (yy == 0) {
            return "двухтысячное";
        }
        const std::string tail = neutTail(yy);
        return tail.empty() ? std::string() : "две тысячи " + tail;
    }
    // Extend ordinal-year support beyond 1800..2099 for list-style year prompts.
    // We intentionally keep 18xx/19xx/20xx legacy behavior above untouched.
    if (century < 10 || century > 29) {
        return {};
    }

    std::string prefix;
    if (century <= 19) {
        prefix = "тысяча";
        const int hundreds = (century - 10) * 100;
        if (hundreds > 0) {
            const char* h = hundredsCardinalWord(hundreds);
            if (!h) {
                return {};
            }
            prefix += " ";
            prefix += h;
        }
    } else {
        prefix = "две тысячи";
        const int hundreds = (century - 20) * 100;
        if (hundreds > 0) {
            const char* h = hundredsCardinalWord(hundreds);
            if (!h) {
                return {};
            }
            prefix += " ";
            prefix += h;
        }
    }

    if (yy == 0) {
        const int hundreds = (century % 10) * 100;
        if (hundreds == 0) {
            return century == 20 ? "двухтысячное" : "тысячное";
        }
        const char* ho = hundredsOrdinalNeutWord(hundreds);
        if (!ho) {
            return {};
        }
        if (century <= 19) {
            return std::string("тысяча ") + ho;
        }
        return std::string("две тысячи ") + ho;
    }

    const std::string tail = neutTail(yy);
    return tail.empty() ? std::string() : prefix + " " + tail;
    return {};
}

std::string inflectLastWord(std::string phrase, const std::string& mode, int century) {
    const size_t sp = phrase.rfind(' ');
    std::string prefix;
    std::string last = phrase;
    if (sp != std::string::npos) {
        prefix = phrase.substr(0, sp + 1);
        last = phrase.substr(sp + 1);
    }
    if (mode == "neut") {
        return phrase;
    }
    if (mode == "gen") {
        if (century == 18 && last == "третье") {
            return phrase;
        }
        if (last == "третье") {
            last = "третьего";
        } else {
            last = replaceSuffixUtf8(std::move(last), "ое", "ого");
        }
        return prefix + last;
    }
    if (mode == "fem") {
        if (last == "третье") {
            return phrase;
        }
        last = replaceSuffixUtf8(std::move(last), "ое", "ая");
        return prefix + last;
    }
    if (mode == "nom") {
        if (last == "третье") {
            last = "третий";
        } else if (last == "второе") {
            last = "второй";
        } else if (last == "шестое") {
            last = "шестой";
        } else if (last == "седьмое") {
            last = "седьмой";
        } else if (last == "восьмое") {
            last = "восьмой";
        } else {
            last = replaceSuffixUtf8(std::move(last), "ое", "ый");
            last = replaceSuffixUtf8(std::move(last), "ее", "ий");
        }
        return prefix + last;
    }
    if (mode == "loc") {
        if (last == "третье") {
            last = "третьем";
        } else {
            last = replaceSuffixUtf8(std::move(last), "ое", "ом");
        }
        return prefix + last;
    }
    return phrase;
}

const std::unordered_map<std::string, int>& cardinalUnitMap() {
    static const std::unordered_map<std::string, int> m = {
        {"один", 1},       {"одна", 1},       {"одного", 1},     {"первого", 1},    {"первый", 1},
        {"два", 2},        {"две", 2},        {"второго", 2},    {"второй", 2},
        {"три", 3},        {"третьего", 3},   {"третий", 3},     {"третье", 3},
        {"четыре", 4},     {"четвертого", 4}, {"четвертый", 4},
        {"пять", 5},       {"пятого", 5},     {"пятый", 5},
        {"шесть", 6},      {"шестого", 6},    {"шестой", 6},
        {"семь", 7},       {"седьмого", 7},   {"седьмой", 7},
        {"восемь", 8},     {"восьмого", 8},   {"восьмой", 8},
        {"девять", 9},     {"девятого", 9},   {"девятый", 9},
        {"десять", 10},    {"десятого", 10},  {"десятый", 10},
        {"одиннадцать", 11}, {"одиннадцатого", 11}, {"одиннадцатый", 11},
        {"двенадцать", 12},  {"двенадцатого", 12},
        {"тринадцать", 13},  {"тринадцатого", 13},
        {"четырнадцать", 14}, {"четырнадцатого", 14},
        {"пятнадцать", 15},  {"пятнадцатого", 15},
        {"шестнадцать", 16}, {"шестнадцатого", 16},
        {"семнадцать", 17},  {"семнадцатого", 17},
        {"восемнадцать", 18}, {"восемнадцатого", 18},
        {"девятнадцать", 19}, {"девятнадцатого", 19},
        {"одном", 1},      {"одним", 1},      {"первом", 1},
        {"двум", 2},       {"втором", 2},
        {"трем", 3},       {"третьем", 3},
        {"четырем", 4},    {"четвертом", 4},
        {"пяти", 5},       {"пятом", 5},
        {"шести", 6},      {"шестом", 6},
        {"семи", 7},       {"седьмом", 7},
        {"восьми", 8},     {"восьмом", 8},
        {"девяти", 9},     {"девятом", 9},
        {"десяти", 10},    {"десятом", 10},
    };
    return m;
}

const std::unordered_map<std::string, int>& cardinalTensFusedMap() {
    static const std::unordered_map<std::string, int> m = {
        {"двадцать", 20}, {"двадцатого", 20}, {"двадцатый", 20}, {"двадцатом", 20},
        {"тридцать", 30}, {"тридцатого", 30}, {"тридцатом", 30},
        {"сорок", 40}, {"сорокового", 40},
        {"пятьдесят", 50}, {"пятидесятого", 50},
        {"шестьдесят", 60}, {"шестидесятого", 60},
        {"семьдесят", 70}, {"семидесятого", 70},
        {"восемьдесят", 80}, {"восьмидесятого", 80},
        {"девяносто", 90}, {"девяностого", 90},
    };
    return m;
}

int parseYyFromWords(const std::vector<std::string>& words, size_t from) {
    if (from >= words.size()) {
        return -1;
    }
    const auto& units = cardinalUnitMap();
    const auto& tensMap = cardinalTensFusedMap();
    if (from + 1 < words.size()) {
        const auto ti = tensMap.find(words[from]);
        const auto ui = units.find(words[from + 1]);
        if (ti != tensMap.end() && ui != units.end() && ti->second >= 20 && ui->second <= 9) {
            return ti->second + ui->second;
        }
    }
    const auto one = tensMap.find(words[from]);
    if (one != tensMap.end()) {
        return one->second;
    }
    const auto u = units.find(words[from]);
    if (u != units.end()) {
        return u->second;
    }
    return -1;
}

} // namespace

std::string russianYearOrdinal(int year, const std::string& mode) {
    if (year < 1000 || year > 2999) {
        return std::to_string(year);
    }
    const std::string neut = yearNeuterPhrase(year);
    if (neut.empty()) {
        return std::to_string(year);
    }
    return inflectLastWord(neut, mode, year / 100);
}

bool stripOptionalPrefix(std::string& s, const std::string& prefix) {
    if (s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0) {
        s.erase(0, prefix.size());
        return true;
    }
    return false;
}

void normalizeMangledLocativeThousands(std::string& s) {
    const std::string p20 = "двух тысячах";
    if (s.size() >= p20.size() && s.compare(0, p20.size(), p20) == 0) {
        std::string tail = s.substr(p20.size());
        while (!tail.empty() && tail.front() == ' ') {
            tail.erase(tail.begin());
        }
        s = "две тысячи";
        if (!tail.empty()) {
            s += " " + tail;
        }
    }
}

int parseRussianSpokenYear(const std::string& phraseUtf8) {
    std::string s = toLowerAscii(phraseUtf8);
    stripOptionalPrefix(s, "в ");
    const std::string suffixGoda = " года";
    const std::string suffixGodu = " году";
    if (s.size() > suffixGoda.size() && s.compare(s.size() - suffixGoda.size(), suffixGoda.size(), suffixGoda) == 0) {
        s.erase(s.size() - suffixGoda.size());
    } else if (s.size() > suffixGodu.size() &&
               s.compare(s.size() - suffixGodu.size(), suffixGodu.size(), suffixGodu) == 0) {
        s.erase(s.size() - suffixGodu.size());
    }
    while (!s.empty() && s.front() == ' ') {
        s.erase(s.begin());
    }
    while (!s.empty() && s.back() == ' ') {
        s.pop_back();
    }
    const std::string odna = "одна ";
    if (s.compare(0, odna.size(), odna) == 0) {
        s.erase(0, odna.size());
    }
    normalizeMangledLocativeThousands(s);

    int century = -1;
    std::string rest;
    const std::string p18a = "тысяча восьмисот";
    const std::string p18b = "тысяча восемьсот";
    const std::string p19 = "тысяча девятьсот";
    const std::string p20a = "двухтысяч";
    const std::string p20b = "две тысячи";
    if (s == p18a || s == p18a + "ое" || s == p18a + "ого" || s == p18a + "ый") {
        return 1800;
    }
    if (s.compare(0, p18b.size(), p18b) == 0) {
        century = 18;
        rest = s.substr(p18b.size());
    } else if (s == p19 || s == p19 + "ое" || s == p19 + "ого" || s == p19 + "ый") {
        return 1900;
    } else if (s.compare(0, p19.size(), p19) == 0) {
        century = 19;
        rest = s.substr(p19.size());
    } else if (s == p20a || s == p20a + "ное" || s == p20a + "ного" || s == p20a + "ный") {
        return 2000;
    } else if (s.compare(0, p20b.size(), p20b) == 0) {
        century = 20;
        rest = s.substr(p20b.size());
    } else {
        return -1;
    }

    if (century < 0) {
        return -1;
    }
    while (!rest.empty() && rest.front() == ' ') {
        rest.erase(rest.begin());
    }
    if (rest.empty()) {
        return century * 100;
    }

    std::vector<std::string> words;
    size_t i = 0;
    while (i < rest.size()) {
        while (i < rest.size() && rest[i] == ' ') {
            ++i;
        }
        if (i >= rest.size()) {
            break;
        }
        size_t j = i;
        while (j < rest.size() && rest[j] != ' ') {
            ++j;
        }
        words.push_back(rest.substr(i, j - i));
        i = j;
    }
    const int yy = parseYyFromWords(words, 0);
    if (yy < 0 || yy > 99) {
        return -1;
    }
    return century * 100 + yy;
}
