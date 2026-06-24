"""
ICU regex building blocks for Phase B (pattern_defs + {{NAME}} expansion).

Loaded JSON carries pattern_defs; C++ stabilizes defs then expands each rule.pattern.
"""

from __future__ import annotations

import re
from typing import Final


def R(name: str) -> str:
    """Reference a named block in JSON patterns."""
    return "{{" + name + "}}"


_PLACEHOLDER_RE = re.compile(r"\{\{([A-Za-z0-9_]+)\}\}")


def verify_pattern_defs(defs: dict[str, str], *, label: str = "") -> None:
    """Ensure every {{key}} in values refers to an existing def key (cycles caught at runtime)."""
    keys = set(defs.keys())
    for def_key, val in defs.items():
        for m in _PLACEHOLDER_RE.finditer(val):
            refk = m.group(1)
            if refk not in keys:
                raise ValueError(f"{label}pattern_defs[{def_key}] references unknown {refk!r}")


def _shared_blocks() -> dict[str, str]:
    """Fragments reused across en/zh pipelines."""
    return {
        "SH_LB_ND": r"(?<!\d)",
        "SH_RB_ND": r"(?!\d)",
        "SH_D4": r"\d{4}",
        "SH_D12": r"\d{1,2}",
        "SH_CAP_D4": r"(\d{4})",
        "SH_CAP_D12": r"(\d{1,2})",
        "SH_D24": r"(\d{2}|\d{4})",
        "SH_DIGITS_PLUS": r"(\d+)",
        "SH_NUM_DEC_CAP": r"(\d+(?:\.\d+)?)",
        "SH_PREFIX_NUM_UNIT": r"(\d+(?:\.\d+)?)\s*",
        "SH_LOOSE_DECIMAL": r"\d+(?:\.\d+)?",
        "SH_MINUS_DIGIT": r"(?<![A-Za-z0-9])-(\d)",
        "SH_IPV4": r"(?:(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.){3}(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)(?::\d{1,5})?",
        "SH_IPV6": r"([0-9a-fA-F]{1,4}:){2,7}[0-9a-fA-F]{1,4}|([0-9a-fA-F]{1,4}:){1,7}:|:(:([0-9a-fA-F]{1,4})){1,7}|::",
        "SH_ROMAN": r"\b(?=[MDCLXVI]+\b)M{0,4}(CM|CD|D?C{0,3})(XC|XL|L?X{0,3})(IX|IV|V?I{0,3})\b",
        "SH_POW10": r"10([\u207B\u00B2\u00B3\u00B9\u2070\u2074-\u2079]+)",
        "SH_INTEGRAL": r"∫([\u2080-\u2089]+)([\u00B2\u00B3\u00B9\u2070\u2074-\u2079]+)",
        # Includes mainland-style mobile with spaces (138 0013 8000); digits op skips spaces.
        "SH_PHONE_DASH": (
            r"("
            r"\d-\d{4}-\d{4}|"
            r"\d{2}-\d{4}-\d{4}|"
            r"\d{3,4}-\d{3,4}-\d{4}|"
            r"\d{2,4}-\d{2,4}-\d{7,8}|"
            r"\d{3,4}-\d{7,8}-\d{1,4}|"
            r"\d{3,4}-\d{7,8}|"
            r"1[3-9]\d(?:[\s\u00A0\u3000]+\d{4}){2}(?![0-9])|"
            r"\d{7,20}"
            r")"
        ),
        "SH_EMAIL": r"([a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,})",
        # Emoji class (same as emoji_rule generator)
        "SH_EMOJI_CLASS": (
            "["
            "\U0001F600-\U0001F64F"
            "\U0001F300-\U0001F5FF"
            "\U0001F680-\U0001F6FF"
            "\U0001F900-\U0001F9FF"
            "\u2600-\u26FF"
            "\u2700-\u27BF"
            "]"
        ),
    }


def _en_only_blocks() -> dict[str, str]:
    la_unit_en: Final = r"(?=[\s]|[.,，。：；！？”“‘’《》;:!?\"')\]»]|$)"
    # Longest atoms first so e.g. 'km/s' wins over the bare 'km' prefix when both could match.
    _comp_atoms_en: Final = "|".join(
        sorted(
            [
                "m/s", "mm", "bn", "min", "nm", "km/h", "kg", "kPa", "km", "μm",
                "ml", "mL", "mA", "kW·h", "kΩ", "kW",
                r"J/\(kg·℃\)", "m/s²", "kg/m³", "rad/s", "kcal", "cm/s", "cm³", "cm²",
                "MHz", "GHz", "kHz", "MPa", "hPa", "rpm", "N·m", "m²", "m³", "kJ", "eV",
                "ms", "μs", "ns", "mV", "kV", "MΩ", "pF", "μF", "nF", "mH", "μH",
                "mW", "MW", "dB", "kN", "dm", "cm",
                "km/s", "mi/s", "mi/h", "mph", "mi",
                "lb", "lbs", "ft", "yd", "oz",
            ],
            key=len,
            reverse=True,
        )
    )
    # Tolerate optional whitespace around '/' so 'km / s' matches like 'km/s'.
    comp_alt_en: Final = r"(" + _comp_atoms_en.replace("/", r"\s*/\s*") + r")"
    single_alt_en: Final = (
        r"(°C|°F|cal|mol|Pa|Hz|㎡|℃|℉|Ω|W|M|N|J|T|K|H|G|L|l|t|b|s|h|m|g|°|′|″|'|\")"
    )
    return {
        "EN_CALL_DIAL": r"(?:call|dial)\s*" + R("SH_DIGITS_PLUS"),
        "EN_DATE_ISO": (
            R("SH_LB_ND")
            + R("SH_CAP_D4")
            + r"[-/]"
            + R("SH_CAP_D12")
            + r"[-/]"
            + R("SH_CAP_D12")
            + R("SH_RB_ND")
        ),
        "EN_MONTH_NAME": (
            r"(?i:January|February|March|April|May|June|July|August|September|October|November|December"
            r"|Jan|Feb|Mar|Apr|Jun|Jul|Aug|Sept|Sep|Oct|Nov|Dec)"
        ),
        "EN_DATE_DMY_LONG": (
            r"\b(\d{1,2})(?:st|nd|rd|th)?\s+("
            + R("EN_MONTH_NAME")
            + r")\.?(?:,\s*|\s+)(\d{4})\b"
        ),
        "EN_DATE_MDY_LONG": (
            r"\b("
            + R("EN_MONTH_NAME")
            + r")\.?\s+(\d{1,2})(?:st|nd|rd|th)?(?:,\s*|\s+)(\d{4})\b"
        ),
        "EN_DATE_DMY_NUMERIC": r"\b(\d{1,2})[./-](\d{1,2})[./-](\d{4})\b",
        # Standalone ~1200-1999 (last two digits not 00): "1996" → nineteen ninety-six.
        # (1) After whitespace, BOL, or punctuation — no extra leading space in replacement.
        "EN_YEAR_PROSE_SPLIT": r"(?:(?<=\s)|(?<![0-9A-Za-z\s]))(1[2-9])((?:0[1-9]|[1-9][0-9]))(?![0-9])",
        # (2) Glued after a letter (his1996) — leading space so "his" and "nineteen" do not merge.
        "EN_YEAR_PROSE_SPLIT_LETTER": r"(?<=[A-Za-z])(1[2-9])((?:0[1-9]|[1-9][0-9]))(?![0-9])",
        "EN_DATE_DMY_SHORT": (
            r"\b(\d{1,2})(?:st|nd|rd|th)?\s+("
            + R("EN_MONTH_NAME")
            + r")\.?(?:,\s*|\s+)(\d{2})\b(?!\d)"
        ),
        "EN_DATE_MDY_SHORT": (
            r"\b("
            + R("EN_MONTH_NAME")
            + r")\.?\s+(\d{1,2})(?:st|nd|rd|th)?(?:,\s*|\s+)(\d{2})\b(?!\d)"
        ),
        "EN_FRACTION_MIXED": r"\b(\d{1,3})\s+(\d{1,2})\s*/\s*(\d{1,2})\b",
        "EN_FRACTION_BARE": r"\b(\d{1,2})\s*/\s*(\d{1,2})\b",
        "EN_EM_DASH": r"——",
        # Bare digit–digit ranges (no currency) must not match the middle of tier codes
        # like "A-01-101" (letter–hyphen–…); those use EN_LETTER_BUILDING_UNIT instead.
        "EN_PRICE_RANGE": (
            r"(?<![A-Za-z]-)([￥$₽₩¥￡€£]?)\s*(\d+)\s*[—-]{1}\s*([￥$￡₽₩¥€£]?)\s*(\d+)"
        ),
        # Building / flat style: single letter + two hyphen-separated number tiers
        # (e.g. A-01-101 → letter A, block "01" as cardinal, unit "101" digit-by-digit).
        "EN_LETTER_BUILDING_UNIT": r"\b([A-Za-z])-(\d+)-(\d+)\b",
        "EN_LABEL_ROOM": (
            r"\b((?:[Rr][Oo][Oo][Mm])|(?:[Ss][Uu][Ii][Tt][Ee])|(?:[Hh][Aa][Ll][Ll]))\s+(\d+)([A-Za-z])\b"
        ),
        "EN_TIMEZONE_UTC": r"\b(UTC|GMT)\s*([+-])\s*(\d{1,2})(?::(\d{2}))?\b",
        "EN_TIMEZONE_UTC_PLUS_HM": r"\b(UTC|GMT)\s*\+\s*(\d{1,2}):(0[1-9]|[1-9]\d)\b",
        "EN_TIMEZONE_UTC_MINUS_HM": r"\b(UTC|GMT)\s*-\s*(\d{1,2}):(0[1-9]|[1-9]\d)\b",
        "EN_TIMEZONE_UTC_PLUS_H": r"\b(UTC|GMT)\s*\+\s*(\d{1,2})(?::00)?(?![\d:])\b",
        "EN_TIMEZONE_UTC_MINUS_H": r"\b(UTC|GMT)\s*-\s*(\d{1,2})(?::00)?(?![\d:])\b",
        "EN_TIME_12": r"(?<!\d)(\d{1,2}):(\d{2})(?::(\d{2}))?\s*([AaPp][Mm])(?!\w)",
        "EN_TIME_24": r"(?<!\d)(\d{1,2}):(\d{2})(?::(\d{2}))?(?!\d)",
        # bn/billion/million/… after amount (longer words first so "million" is not split as "m").
        # Outer (...) required: ICU (?i:…) is non-capturing; scale must be group 3 for lookup_map.
        "EN_CURRENCY_SCALE_TOKEN": (
            r"((?i:billion|million|trillion|thousand|bn|tn|K|k|M|m|B|T))"
        ),
        "EN_CURRENCY_SCALED": (
            r"(USD|EUR|GBP|RUB|KRW|CNY|[¥₽$￥￡₩€£])?\s*(\d+(?:,\d{3})*(?:\.\d+)?)\s*"
            + R("EN_CURRENCY_SCALE_TOKEN")
            + r"(?![a-zA-Z])\s*(USD|EUR|GBP|RUB|KRW|CNY|[¥₽$￥￡₩€£])?"
        ),
        "EN_CURRENCY_SCALED_PREFIX": (
            r"(USD|EUR|GBP|RUB|KRW|CNY|[¥₽$￥￡₩€£])\s*(\d+(?:,\d{3})*(?:\.\d+)?)\s*"
            + R("EN_CURRENCY_SCALE_TOKEN")
            + r"(?![a-zA-Z])\s*(USD|EUR|GBP|RUB|KRW|CNY|[¥₽$￥￡₩€£])?"
        ),
        "EN_CURRENCY_SCALED_SUFFIX": (
            r"\s*(\d+(?:,\d{3})*(?:\.\d+)?)\s*"
            + R("EN_CURRENCY_SCALE_TOKEN")
            + r"(?![a-zA-Z])\s*(USD|EUR|GBP|RUB|KRW|CNY|[¥₽$￥￡₩€£])"
        ),
        "EN_CURRENCY_ALT": (
            r"(USD|EUR|GBP|RUB|KRW|CNY|[¥₽$￥￡₩€£])?\s*(\d+(?:,\d{3})*(?:\.\d+)?)\s*(USD|EUR|GBP|RUB|KRW|CNY|[¥₽$￥￡₩€£])?"
        ),
        "EN_CURRENCY_ALT_PREFIX": (
            r"(USD|EUR|GBP|RUB|KRW|CNY|[¥₽$￥￡₩€£])\s*(\d+(?:,\d{3})*(?:\.\d+)?)\s*(USD|EUR|GBP|RUB|KRW|CNY|[¥₽$￥￡₩€£])?"
        ),
        "EN_CURRENCY_ALT_SUFFIX": (
            r"\s*(\d+(?:,\d{3})*(?:\.\d+)?)\s*(USD|EUR|GBP|RUB|KRW|CNY|[¥₽$￥￡₩€£])"
        ),
        # Require end of numeric run (not another digit) so "10,900ft" matches as one amount
        # (digit + letter has no \\b between 0 and f; without this, loose_num reads "10" then "900").
        "EN_NUM_THOUSANDS": r"\d{1,3}(?:,\d{3})+(?:\.\d+)?(?![0-9])",
        # e.g. AK47s, MP3s — trailing "s" is plural, not "seconds". Two caps before digits; 1+ digits.
        # Excludes "wait 5s" (no two capitals immediately before the digit run).
        "EN_LETTER_PLUS_NUM_PLURAL_S": r"(?<=[A-Z]{2})(\d{2,}|\d)s\b",
        "EN_PERCENT": R("SH_NUM_DEC_CAP") + r"\s*%",
        "EN_PER_MILLE": R("SH_NUM_DEC_CAP") + r"\s*‰",
        "EN_COMPOSITE_UNIT": R("SH_PREFIX_NUM_UNIT") + comp_alt_en + la_unit_en,
        "EN_SINGLE_UNIT": R("SH_PREFIX_NUM_UNIT") + single_alt_en + la_unit_en,
        # 'in' as inches: only when followed by clause-terminating punctuation
        # (e.g. ')', '.', ','). Avoids false positives like "1 in the morning".
        "EN_INCH_UNIT": (
            r"(\d+(?:\.\d+)?)\s*(in)\b\s*(?=[.,;:)\]\}\"'»。，；：！？”’》]|$)"
        ),
        # Airbus A220 / A350 / … — "three fifty" not cardinal "three hundred fifty" (before loose_num).
        "EN_AIRBUS_A_SERIES": r"(?i)(\b(?:Airbus\s+)?A)(\d{3})\b",
        # Boeing 787 Dreamliner — "seven eighty-seven" not "seven hundred eighty-seven".
        "EN_BOEING_787_DREAMLINER": r"(?i)\b(787)\s+(Dreamliner)\b",
        "EN_ORDINAL": r"(\d+)(st|nd|rd|th)\b",
        # "19th-21st September" → "19th to 21st September" (date span; hyphen alone is often silent).
        # Only when a month name immediately follows the second ordinal (avoids "2nd-3rd quarter").
        "EN_ORDINAL_DASH_RANGE_TO_MONTH": (
            r"(?i)\b(\d{1,2}(?:st|nd|rd|th))\s*[-–—]\s*(\d{1,2}(?:st|nd|rd|th))"
            r"(?=\s+" + R("EN_MONTH_NAME") + r")"
        ),
        # International phone like "+44 7756 165803" or "+1-555-123-4567".
        # Requires '+' + 1-3 digit country code + at least one more digit group
        # separated by whitespace or dash (so bare "+1" never matches).
        "EN_INTL_PHONE": (
            r"\+\d{1,3}(?:[\s\u00A0-]+\d{2,8}){1,6}(?!\d)"
        ),
        # URL or bare domain: requires scheme/www. prefix or known TLD suffix to avoid
        # false positives on ordinary "Smith.Co" style text.
        "EN_URL_OR_DOMAIN": (
            r"(?:https?://|www\.)[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}(?:/[a-zA-Z0-9_@%&?+=\\~/-]*)*"
            r"|[a-zA-Z0-9.-]+\.(?:com|cn|net|org|edu|gov|io)(?:/[a-zA-Z0-9_@%&?+=\\~/-]*)*"
        ),
    }


def _ar_only_blocks() -> dict[str, str]:
    """Arabic lookahead + unit alternations (mirrors colleague `rules/ar.json` + main `en` parity)."""
    la_ar: Final = r"(?=[\s]|[.,،。：；！？”“‘’《》;:!?\"')\]»]|$)"
    comp_alt_ar: Final = (
        r"(m/s|mm|bn|min|nm|km/h|kg|kPa|km|μm|ml|mL|mA|kW·h|kΩ|kW|"
        r"J/\(kg·℃\)|J/kg·℃|m/s²|kg/m³|rad/s|kcal|cm/s|cm³|cm²|MHz|GHz|kHz|MPa|hPa|rpm|N·m|m²|m³|kJ|eV|"
        r"ms|μs|ns|mV|kV|MΩ|pF|μF|nF|mH|μH|mW|MW|dB|kN|dm|cm)"
    )
    single_alt_ar: Final = (
        r"(°C|°F|cal|mol|Pa|Hz|㎡|℃|℉|Ω|A|V|W|M|N|J|T|K|C|F|H|G|L|l|t|b|B|s|h|m|g|°|′|″|'|\")"
    )
    return {
        "AR_DATE_ISO": (
            R("SH_LB_ND")
            + R("SH_CAP_D4")
            + r"[-/]"
            + R("SH_CAP_D12")
            + r"[-/]"
            + R("SH_CAP_D12")
            + R("SH_RB_ND")
        ),
        "AR_EM_DASH": r"——",
        "AR_PRICE_RANGE": (
            r"([￥$₽₩¥￡€£]?)\s*(\d+)\s*[—-]{1}\s*([￥$￡₽₩¥€£]?)\s*(\d+)"
        ),
        "AR_TIME_24": r"(?<!\d)(\d{1,2}):(\d{2})(?::(\d{2}))?(?!\d)",
        "AR_TIME_24_HMS": r"(?<!\d)(\d{1,2}):(\d{2}):(\d{2})(?!\d)",
        "AR_TIME_24_HM": r"(?<!\d)(\d{1,2}):(\d{2})(?![\d:])(?!\d)",
        "AR_CURRENCY_ALT": (
            r"(USD|EUR|GBP|RUB|KRW|CNY|[¥₽$￥₩€£])?\s*([\d,]+(?:\.\d+)?)\s*(USD|EUR|GBP|RUB|KRW|CNY|[¥₽$￥₩€£])?"
        ),
        "AR_CURRENCY_ALT_PREFIX": (
            r"(USD|EUR|GBP|RUB|KRW|CNY|[¥₽$￥₩€£])\s*([\d,]+(?:\.\d+)?)\s*(USD|EUR|GBP|RUB|KRW|CNY|[¥₽$￥₩€£])?"
        ),
        "AR_CURRENCY_ALT_SUFFIX": (
            r"([\d,]+(?:\.\d+)?)\s*(USD|EUR|GBP|RUB|KRW|CNY|[¥₽$￥₩€£])"
        ),
        "AR_PERCENT": R("SH_NUM_DEC_CAP") + r"%",
        "AR_PER_MILLE": R("SH_NUM_DEC_CAP") + r"‰",
        "AR_COMPOSITE_UNIT": R("SH_PREFIX_NUM_UNIT") + comp_alt_ar + la_ar,
        "AR_SINGLE_UNIT": R("SH_PREFIX_NUM_UNIT") + single_alt_ar + la_ar,
        "AR_ORDINAL": r"(\d+)(st|nd|rd|th)\b",
        "AR_NUM_NOUN_PEOPLE": (
            r"(?:راكب(?:اً|ا)?|ركاب|شخص(?:اً|ا)?|أشخاص|فرد(?:اً|ا)?|أفراد|صوت(?:اً|ا)?|"
            r"أصوات|ناخب(?:اً|ا)?|ناخبين|فلسطيني(?:اً|ا)?|فلسطينيين)"
        ),
        "AR_NUM_NOUN_AGE": r"(?:عام(?:اً|ا)?|سنة|سن(?:ة)?|ربيع(?:اً|ا)?)",
        "AR_NUM_NOUN_DURATION": (
            r"(?:يوم(?:اً|ا)?|أيام|شهر(?:اً|ا)?|أشهر|أسبوع(?:اً|ا)?|أسابيع|ساعة|ساعات|"
            r"دقيقة|دقائق|ثانية|ثوان(?:ي)?)"
        ),
        "AR_NUM_NOUN_MONEY": (
            r"(?:دولار(?:ا)?|درهم(?:ا)?|ريال(?:ا)?|يورو(?:ا)?|جنيه(?:ا)?|دينار(?:ا)?)"
        ),
        "AR_NUM_NOUN_OBLIQUE": (
            r"(?:{{AR_NUM_NOUN_PEOPLE}}|{{AR_NUM_NOUN_AGE}}|{{AR_NUM_NOUN_DURATION}}|"
            r"{{AR_NUM_NOUN_MONEY}})"
        ),
    }


def _bn_only_blocks() -> dict[str, str]:
    """Bengali pipeline (ported from `ICU2/ICU_bn/rules/bn.json`); mostly EN-shaped + Bengali dial."""
    la_bn: Final = r"(?=[\s\u0980-\u09FF]|[.,،।：；！？”“‘’《》;:!?\"')\]»]|$)"
    comp_alt_bn: Final = (
        r"(m/s|mm|bn|min|nm|km/h|kg|kPa|km|μm|ml|mL|mA|kW·h|kΩ|kW|"
        r"J/\(kg·℃\)|m/s²|kg/m³|rad/s|kcal|cm/s|cm³|cm²|MHz|GHz|kHz|MPa|hPa|rpm|N·m|m²|m³|kJ|eV|"
        r"ms|μs|ns|mV|kV|MΩ|pF|μF|nF|mH|μH|mW|MW|dB|kN|dm|cm)"
    )
    single_alt_bn: Final = (
        r"(°C|°F|cal|mol|Pa|Hz|㎡|℃|℉|Ω|A|V|W|M|N|J|T|K|C|F|H|G|L|l|t|b|B|s|h|m|g|°|′|″|'|\")"
    )
    return {
        "BN_CALL_DIAL_LATIN": r"(?:call|dial)\s*" + R("SH_DIGITS_PLUS"),
        "BN_CALL_DIAL_NATIVE": r"(?:কল করুন|ডায়াল করুন)\s*" + R("SH_DIGITS_PLUS"),
        "BN_DATE_ISO": (
            R("SH_LB_ND")
            + R("SH_CAP_D4")
            + r"[-/]"
            + R("SH_CAP_D12")
            + r"[-/]"
            + R("SH_CAP_D12")
            + R("SH_RB_ND")
        ),
        "BN_EM_DASH": r"——",
        "BN_PRICE_RANGE": (
            r"([￥$₽₩¥￡€£৳]?)\s*(\d+)\s*[—-]{1}\s*([￥$￡₽₩¥€£৳]?)\s*(\d+)"
        ),
        "BN_LABEL_ROOM": (
            r"\b((?:[Rr][Oo][Oo][Mm])|(?:[Ss][Uu][Ii][Tt][Ee])|(?:[Hh][Aa][Ll][Ll]))\s+(\d+)([A-Za-z])\b"
        ),
        "BN_TIME_12": r"(?<!\d)(\d{1,2}):(\d{2})(?::(\d{2}))?\s*([AaPp][Mm])(?!\w)",
        "BN_TIME_24": r"(?<!\d)(\d{1,2}):(\d{2})(?::(\d{2}))?(?!\d)",
        "BN_CURRENCY_ALT": (
            r"(USD|EUR|GBP|RUB|KRW|CNY|[¥₽$￥₩€£৳])?\s*([\d,]+(?:\.\d+)?)\s*(USD|EUR|GBP|RUB|KRW|CNY|[¥₽$￥₩€£৳])?"
        ),
        "BN_CURRENCY_ALT_PREFIX": (
            r"(USD|EUR|GBP|RUB|KRW|CNY|[¥₽$￥₩€£৳])\s*([\d,]+(?:\.\d+)?)\s*(USD|EUR|GBP|RUB|KRW|CNY|[¥₽$￥₩€£৳])?"
        ),
        "BN_CURRENCY_ALT_SUFFIX": (
            r"([\d,]+(?:\.\d+)?)\s*(USD|EUR|GBP|RUB|KRW|CNY|[¥₽$￥₩€£৳])"
        ),
        "BN_PERCENT": R("SH_NUM_DEC_CAP") + r"%",
        "BN_PER_MILLE": R("SH_NUM_DEC_CAP") + r"‰",
        "BN_COMPOSITE_UNIT": R("SH_PREFIX_NUM_UNIT") + comp_alt_bn + la_bn,
        "BN_SINGLE_UNIT": R("SH_PREFIX_NUM_UNIT") + single_alt_bn + la_bn,
        "BN_ORDINAL": r"(\d+)(st|nd|rd|th)\b",
    }


def _ru_only_blocks() -> dict[str, str]:
    """Russian pipeline: EN-shaped with Russian dial + dedicated morph-aware number pass."""
    la_ru: Final = r"(?=[\s\u0400-\u04FF]|[.,،。：；！？”“‘’《》;:!?\"')\]»]|$)"
    comp_alt_ru: Final = (
        r"(m/s|mm|bn|min|nm|km/h|kg|kPa|km|μm|ml|mL|mA|kW·h|kΩ|kW|"
        r"J/\(kg·℃\)|m/s²|kg/m³|rad/s|kcal|cm/s|cm³|cm²|MHz|GHz|kHz|MPa|hPa|rpm|N·m|m²|m³|kJ|eV|"
        r"ms|μs|ns|mV|kV|MΩ|pF|μF|nF|mH|μH|mW|MW|dB|kN|dm|cm)"
    )
    single_alt_ru: Final = (
        r"(°C|°F|cal|mol|Pa|Hz|㎡|℃|℉|Ω|A|V|W|M|N|J|T|K|C|F|H|G|L|l|t|b|B|s|h|m|g|°|′|″|'|\")"
    )
    return {
        "RU_CALL_DIAL_LATIN": r"(?:call|dial)\s*" + R("SH_DIGITS_PLUS"),
        "RU_CALL_DIAL_NATIVE": r"(?:позвонить)\s*" + R("SH_DIGITS_PLUS"),
        "RU_DATE_ISO": (
            R("SH_LB_ND")
            + R("SH_CAP_D4")
            + r"[-/]"
            + R("SH_CAP_D12")
            + r"[-/]"
            + R("SH_CAP_D12")
            + R("SH_RB_ND")
        ),
        "RU_EM_DASH": r"——",
        "RU_PRICE_RANGE": (
            r"([￥$₽₩¥￡€£₽]?)\s*(\d+)\s*[—-]{1}\s*([￥$￡₽₩¥€£₽]?)\s*(\d+)"
        ),
        "RU_TIME_12": r"(?<!\d)(\d{1,2}):(\d{2})(?::(\d{2}))?\s*([AaPp][Mm])(?!\w)",
        "RU_TIME_24": r"(?<!\d)(\d{1,2}):(\d{2})(?::(\d{2}))?(?!\d)",
        "RU_TIME_24_HMS": r"(?<!\d)(\d{1,2}):(\d{2}):(\d{2})(?!\d)",
        "RU_TIME_24_HM": r"(?<!\d)(\d{1,2}):(\d{2})(?![\d:])(?!\d)",
        "RU_CURRENCY_ALT": (
            r"(USD|EUR|GBP|RUB|KRW|CNY|[¥₽$￥₩€£])?\s*([\d,]+(?:\.\d+)?)\s*(USD|EUR|GBP|RUB|KRW|CNY|[¥₽$￥₩€£])?"
        ),
        "RU_CURRENCY_ALT_PREFIX": (
            r"(USD|EUR|GBP|RUB|KRW|CNY|[¥₽$￥₩€£])\s*([\d,]+(?:\.\d+)?)\s*(USD|EUR|GBP|RUB|KRW|CNY|[¥₽$￥₩€£])?"
        ),
        "RU_CURRENCY_ALT_SUFFIX": (
            r"([\d,]+(?:\.\d+)?)\s*(USD|EUR|GBP|RUB|KRW|CNY|[¥₽$￥₩€£])"
        ),
        "RU_PERCENT": R("SH_NUM_DEC_CAP") + r"%",
        "RU_PER_MILLE": R("SH_NUM_DEC_CAP") + r"‰",
        "RU_COMPOSITE_UNIT": R("SH_PREFIX_NUM_UNIT") + comp_alt_ru + la_ru,
        "RU_SINGLE_UNIT": R("SH_PREFIX_NUM_UNIT") + single_alt_ru + la_ru,
        # Standalone numbers (avoid phone/IP/time ranges by excluding adjacent separators).
        "RU_MORPH_NUM": r"(?<![\d:.\-+/])(\d+(?:\.\d+)?)(?![\d:.\-+/])",
    }


def _zh_only_blocks() -> dict[str, str]:
    la_zh: Final = r"(?=[\s\u4e00-\u9fa5]|[.,，、。：；！？”“‘’《》;:!?\"')\]»]|$)"
    la_zh_plain: Final = r"(?=[\s]|[.,，、。：；！？”“‘’《》;:!?\"')\]»]|$)"
    comp_alt_zh: Final = (
        r"(m/s|mmHg|mm|bn|min|nm|km/h|kg|mol/L|kPa|km|μm|ml|mL|mg|mA|atm|kW·h|kWh|kΩ|kW|"
        r"J/\\(kg·℃\\)|m/s²|kg/m³|rad/s|kcal|cm/s|cm³|cm²|MHz|GHz|kHz|MPa|hPa|rpm|N·m|m²|m³|kJ|eV|"
        r"ms|μs|ns|mV|kV|MΩ|pF|μF|nF|mH|μH|mW|MW|dB|kN|dm|cm)"
    )
    # Omit bare `H`: chemistry readouts like `6H₂O` → `6H 二O` must not match digit+亨利
    # (亨利 stays available via composite units mH / μH).
    # Keep prime symbols (′/″) for angle units; do not treat ASCII quotes ('/") as units.
    # This avoids false positives like 数字"0" → 〇秒.
    single_alt_zh: Final = (
        r"(°C|°F|cal|mol|Pa|Hz|㎡|℃|℉|Ω|A|V|W|N|J|T|K|C|F|G|L|l|t|b|B|s|h|m|g|°|′|″)"
    )
    return {
        "ZH_DECADE": R("SH_LB_ND") + R("SH_D24") + r"[sS]",
        # 八零后 / 零零后 / 一零后 — decade digit pair + 后 (not eighty + 后).
        "ZH_DECADE_HOU": r"(?<!\d)(00|10|20|30|40|50|60|70|80|90)后(?!\d)",
        "ZH_DIAL": r"拨打" + R("SH_DIGITS_PLUS"),
        # 买票可打12306 / 拨95588 — call verbs + hotline/phone digits, read digit-by-digit.
        "ZH_CALL_VERB_NUM": r"((?:拨打|拨|打电话|打)\s*)(\d{5,12})(?!\d)",
        # Taiwan / +886 intl. dashed numbers — digit-by-digit (before SH_MINUS_DIGIT on -2, etc.).
        "ZH_PHONE_INTL_886": r"(?<![\d+])\+886(?:-\d{1,4}){2,}(?![0-9])",
        # Mainland +86 mobile with one dash, e.g. +86-1391234567 (avoid 86 -> 八十六).
        "ZH_PHONE_INTL_86_DASH": r"(?<![\d+])\+86-(\d{7,12})(?![0-9])",
        # 日本国码 +81（如「+81开头」）— 加八一，勿读成八十一；勿吞 +8190… 等更长号码.
        "ZH_PHONE_INTL_81": r"(?<![\d+])\+81(?![0-9])",
        # Generic intl. dashed numbers (e.g. +33-1-23456789) — read as 加 + full digits.
        # Keep country-specific +886/+86/+81 rules above for their dedicated behavior.
        "ZH_PHONE_INTL_DASH_GENERIC": r"(?<![\d+])\+(?!886|86|81)(\d{1,3}(?:-\d{1,8}){1,5})(?![0-9])",
        # ISBN-13 分段书写（978/979-…）；连字符不作「负」，整体按位读数码.
        "ZH_ISBN_978_DASH": r"(?<![0-9])((?:978|979)(?:-\d+)+)(?![0-9])",
        # 10086 / 10000 客服短号 — 热线…后按位读（避免 loose_num 一万〇八十六）.
        "ZH_HOTLINE_CMCC_10086": (
            r"(?<!\d)(客服热线|服务热线|咨询热线|投诉热线|人工热线|热线)\s*(10086)(?!\d)"
        ),
        "ZH_HOTLINE_CARRIER_10000": r"(?<!\d)(电信为|联通为|移动为)\s*(10000)(?!\d)",
        # 12315 消费者投诉举报 — 热线语境下按位读（避免 loose_num 一万二千…）.
        "ZH_HOTLINE_CONSUMER_12315": (
            r"(?<!\d)(消费者投诉热线|市场监管投诉举报热线|投诉举报热线|"
            r"消费者维权热线|维权热线|举报热线|投诉热线|12315热线|热线)\s*(12315)(?!\d)"
        ),
        # Emergency numbers in help context: 110/119/120/122 should be digit-by-digit.
        "ZH_EMERGENCY_CALL_CTX": (
            r"(?<!\d)"
            r"((?:紧急情况(?:下)?(?:请|可)?|遇到紧急情况)?\s*(?:直接)?(?:拨打|拨)|"
            r"(?:报警|火警|急救|匪警|交警|求救|救援)(?:电话)?)\s*"
            r"(110|119|120|122)(?!\d)"
        ),
        # 通用热线短号（如 法律援助热线12348）— 热线语境统一按位读.
        "ZH_HOTLINE_GENERIC": r"(?<!\d)((?:[\u4e00-\u9fff]{0,8})?热线)\s*(\d{3,8})(?!\d)",
        # 译名间隔号 U+00B7（如 尤瓦尔·赫拉利）— 不读；避免 A·B 拉丁单位如 kW·h 被吞掉.
        "ZH_HAN_MIDDLEDOT_SEP": r"(?<=[\u4e00-\u9fff])·(?=[\u4e00-\u9fff])",
        # 表格列分隔 U+007C / U+FF5C（姓名|年龄、用|分隔）— 读「竖线」；仅 CJK|CJK，不误伤 a|b / ||.
        "ZH_PIPE_DELIM_BETWEEN_CJK": (
            r"(?<=[\u4e00-\u9fff])\s*[|｜]\s*(?=[\u4e00-\u9fff])"
        ),
        # 〈总则〉单书名号（U+3008/U+3009）— 名号不读出，仅保留中间文字；须成对.
        "ZH_SINGLE_ANGLE_BRACKET_PAIR": r"〈([^〉]{0,256})〉",
        # 《劳动合同法》双书名号（U+300A/U+300B）— 同上，名号不读出.
        "ZH_DOUBLE_ANGLE_BRACKET_PAIR": r"《([^》]{0,512})》",
        # 分机是08 — 分机号按位读（避免 loose_num 读成「八」）.
        "ZH_PHONE_EXT_IS": r"(?<!\d)(分机是)(\d{1,4})(?!\d)",
        # 总机号后的分机：转分机123 / ……零转223（前一位已是读出的数字）— 分机号按位读.
        "ZH_PHONE_EXT_FENJI": r"(转分机)(\d{1,6})(?![0-9])",
        "ZH_PHONE_ZHUAN_EXT": r"(?<=[0-9零一二三四五六七八九])(转)(\d{1,6})(?![0-9])",
        # 手机尾号后 3–4 位，按位读（避免整段拼读如七千八百九十）。
        "ZH_PHONE_TAIL_CTX": r"(?<!\d)(尾号(?:码)?为)(\d{3,4})(?!\d)",
        # 收款账号：6228 4800 0000 0000 — spaced groups of 4 digits, read digit-by-digit.
        "ZH_BANK_CARD_CTX": (
            r"((?:收款账号|银行账号|银行卡号|借记卡号|信用卡号)\s*(?:[:：]\s*|是|为)\s*)"
            r"(\d{4}(?:\s*\d{4}){3,6})(?!\d)"
        ),
        # 卡号太长了，6225 8881 2345 6789 — 「卡号」与小逗号后的空格分段卡号（先于 loose_num）.
        "ZH_CARDNUM_KAH_COMMA_CTX": (
            r"(卡号[^，。\n]{0,48}?[，,]\s*)(\d{4}(?:\s*\d{4}){3,})(?![0-9])"
        ),
        # 账户卡号6217 0000 1234 5678 — 「卡号」后直接跟空格分段的四位一组（先于 loose_num）.
        "ZH_CARDNUM_SPACED_AFTER_KAH": (
            r"(卡号\s*)(\d{4}(?:\s+\d{4}){2,})(?![0-9])"
        ),
        # VIN码为LFV2A21K480123456 — vehicle VIN read as letters + digit-by-digit numbers.
        "ZH_VIN_CTX": (
            r"((?:VIN码|车架号)\s*(?:[:：]\s*|为|是)\s*)([A-HJ-NPR-Z0-9]{17})(?![A-Za-z0-9])"
        ),
        # 验证码是739284 / 密码为123456 — 安全码语境按位读，避免 loose_num 整段拼读.
        "ZH_SECRET_CODE_CTX": (
            r"((?:(?:短信)?验证码|校验码|动态码|口令|PIN码|PIN|密码)\s*(?:[:：]|是|为)?\s*)(\d{4,12})(?![0-9])"
        ),
        "ZH_DATE_YMD": (
            R("SH_CAP_D4") + r"年" + R("SH_CAP_D12") + r"月" + R("SH_CAP_D12") + r"[日号]"
        ),
        # Day-Month-Year with hyphens, e.g. 11-03-2026 → 二零二六年三月十一日 (before minus-digit).
        # Day–month–year with hyphen or slash (e.g. 3/11/2026 → 十一月三日/三号).
        "ZH_DATE_DMY_DASH": r"(?<!\d)(\d{1,2})[-/](\d{1,2})[-/](\d{4})(?!\d)",
        # 2026.01.01 — 须早于 ZH_VERSION_DOTTED（否则按「点」读）；年份前禁止紧贴拉丁字母（避免 v2026…）.
        "ZH_DATE_YMD_DOT": (
            r"(?<![0-9A-Za-z])(\d{4})\.(\d{1,2})\.(\d{1,2})(?![0-9])"
        ),
        # Product/version compare in prose: 2025比2024版 -> digit-by-digit years.
        "ZH_VERSION_YEAR_COMPARE": r"(?<!\d)(\d{4})\s*比\s*(\d{4})(版)(?!\d)",
        # '98年 / ’98年 — abbreviated years read digit-by-digit (九八年), not 九十八年.
        "ZH_YEAR_2_APOS": r"(?<![0-9A-Za-z])[\'’](\d{2})年",
        "ZH_YEAR_4": r"(?<!\d)(\d{4})年",
        "ZH_STD_GBT_CODE": r"(GB/T)\s*(\d+)\s*[-—]\s*(\d+)",
        # --- Math: named identities (see tools/generate_tts_rules_json.py tables) ---
        "ZH_BERNOULLI_EQ": r"P\s*\+\s*(?:1/2|½)\s*ρ\s*v²\s*\+\s*ρ\s*gh\s*=\s*常数",
        # Ek=1/2mv² — translational kinetic energy (before slash digit readout, etc.).
        "ZH_KINETIC_ENERGY": r"Ek\s*=\s*1/2\s*mv(?:²|\^2)",
        # Basel ζ(2) identity; variants before digit spellout rewrites.
        "ZH_BASEL_SUM_EQ": (
            r"∑\s*\(\s*(?:1|一)\s*(?:/|／)\s*n\s*(?:²|\^2)\s*\)\s*=\s*"
            r"π\s*(?:²|\^2)\s*(?:/|／)\s*(?:6|六)"
        ),
        # P(A∪B)=P(A)+P(B)-P(A∩B) — 容斥 / 加法公式（口语化读法，先于字面量 + − =）.
        "ZH_PROB_INCLUSION_AB": (
            r"P\s*\(\s*A\s*∪\s*B\s*\)\s*=\s*P\s*\(\s*A\s*\)\s*\+\s*P\s*\(\s*B\s*\)\s*-\s*P\s*\(\s*A\s*∩\s*B\s*\)"
        ),
        "ZH_TRIG_IDENTITY": r"sin\s*θ\s*\+\s*cos\s*θ\s*=\s*√\s*(\d+)\s*·\s*sin\s*\(\s*θ\s*\+\s*(\d+)\s*°\s*\)",
        # Formula fractions in equation context: ... = A/B  (read slash as 除以).
        "ZH_FORMULA_FRAC_EQ_RHS": (
            r"(=\s*)"
            r"(\([A-Za-z0-9₀-₉²³\^+\-]+\)|[A-Za-z][A-Za-z0-9₀-₉²³\^]*)"
            r"\s*(?:/|／)\s*"
            r"(\([A-Za-z0-9₀-₉²³\^+\-]+\)|[A-Za-z][A-Za-z0-9₀-₉²³\^]*)"
        ),
        # Formula fractions in equation context: A/B = ...  (avoid URL/path by left boundary).
        "ZH_FORMULA_FRAC_EQ_LHS": (
            r"(?<![0-9A-Za-z/:._-])"
            r"(\([A-Za-z0-9₀-₉²³\^+\-]+\)|[A-Za-z][A-Za-z0-9₀-₉²³\^]*)"
            r"\s*(?:/|／)\s*"
            r"(\([A-Za-z0-9₀-₉²³\^+\-]+\)|[A-Za-z][A-Za-z0-9₀-₉²³\^]*)"
            r"\s*(=\s*)"
        ),
        "ZH_ADD_EQ": r"(?<!\d)(\d+)\s*\+\s*(\d+)\s*=\s*(\d+)(?!\d)",
        # e.g. 10-4=6 — before SH_MINUS_DIGIT (otherwise -4 → 负四).
        "ZH_SUB_EQ": r"(?<!\d)(\d+)\s*-\s*(\d+)\s*=\s*(\d+)(?!\d)",
        # e.g. L=-(1/N)∑… — read 负N分之一 (not 减(一/N)).
        "ZH_NEG_RECIPROC_N": r"-\s*\(\s*1\s*/\s*([Nn])\s*\)",
        # 水和米的比例是2:1 — 读「二比一」；须先于 SH_IPV6（否则 :1 会被 IPv6 分支误匹配成冒号）.
        # 分母后禁止再接数字/冒号/hex，以免吃掉 IPv6（如 2001:0db8…）或端口片段。
        "ZH_RATIO_COLON": r"(?<![0-9.:])(\d{1,4})\s*:\s*(\d{1,4})(?![0-9:A-Fa-f])",
        # e.g. 分成1/2给我 — read as 二分之一 (digits-only simple fraction).
        "ZH_SIMPLE_FRAC": r"(?<![\d./])(\d{1,4})\s*(?:/|／)\s*(\d{1,4})(?![\d./])",
        "ZH_EM_DASH": r"——",
        "ZH_RANGE_DIG": r"(\d)[—-]{1}(\d)",
        "ZH_TIME_AMPM": (
            r"(?<!\d)(\d{1,2}):(\d{2})(?::(\d{2}))?\s*(AM|PM|am|pm|上午|下午)"
        ),
        "ZH_TIME_24_RANGE": r"(?<!\d)(\d{1,2}):(\d{2})(?::(\d{2}))?\s*[—-]\s*(\d{1,2}):(\d{2})(?::(\d{2}))?(?!\d)",
        "ZH_TIME_24": r"(?<!\d)(\d{1,2}):(\d{2})(?::(\d{2}))?(?!\d)",
        "ZH_TIME_24_HMS": r"(?<!\d)(\d{1,2}):(\d{2}):(\d{2})(?!\d)",
        "ZH_TIME_24_HM": r"(?<!\d)(\d{1,2}):(\d{2})(?![\d:])(?!\d)",
        "ZH_SEAT_CTX": r"((?:座位(?:号)?\s*(?:是|为)?\s*))(\d{1,3})([A-Za-z])",
        # 入住3楼 / 15楼301室 — 楼层数字按位读；楼后可跟房号数字故不能用「楼后非字母数字」断言。
        # 仅用 `(?!号)` 避免匹配「号楼」中的「楼」.
        "ZH_FLOOR_LOU": r"(?<![0-9])(\d{1,4})楼(?!号)",
        # 301至320室 / 302—308房间 — 起止房号均按位读（「301」若后跟「至」单条 ZH_ROOM_NUMBER 盖不住）.
        "ZH_ROOM_RANGE_THROUGH": (
            r"(?<![0-9])(\d{1,6})\s*(\-|—|–|至|到)\s*(\d{1,6})(室|房间|号房)(?![0-9])"
        ),
        # 酒店2006室 / 302房间 / 301号房 — 房号按位读（先于 loose_num 拼读）.
        "ZH_ROOM_NUMBER": (
            r"(?<![0-9])(\d{1,6})(室|房间|号房)(?![0-9])"
        ),
        # 酒店房间1218 — room label before number, also read digit-by-digit.
        "ZH_ROOM_LABEL_NUM": r"(房间)(\d{1,6})(?![0-9])",
        # e.g. 80、443和3306端口 — read each port digit-by-digit (high precision).
        "ZH_PORTS_TRIPLE": r"(?<!\d)(\d{1,5})\s*、\s*(\d{1,5})\s*和\s*(\d{1,5})\s*端口",
        # e.g. 21、23、110端口 (no 和 between last two) — same digit-by-digit readout.
        "ZH_PORTS_TRIPLE_DUN": r"(?<!\d)(\d{1,5})\s*、\s*(\d{1,5})\s*、\s*(\d{1,5})\s*端口",
        # e.g. 8443端口 — digit-by-digit (not 八千四百四十三); after triple-port rules.
        "ZH_PORT_NUM": r"(?<![0-9])(\d{1,5})(?=\s*端口)",
        # 5G与… / 4G网络 — cellular gen.; exclude GHz, GB, Gbps (G followed by letter/digit).
        "ZH_CELLULAR_NG": r"(?<![0-9])([23456])G(?![A-Za-z0-9])",
        "ZH_HKD_WAN": r"(HK\$|HKD)\s*(\d+(?:\.\d+)?)\s*万",
        # HK$1200亿 → 一千二百亿港元（须在 ZH_CURRENCY 之前，否则会读成「…港元亿」）.
        "ZH_HKD_YI": r"(HK\$|HKD)\s*(\d+(?:\.\d+)?)\s*亿",
        "ZH_CURRENCY": (
            r"(?<![A-Za-z0-9])(US\$|USD|EUR|HK\$|HKD|GBP|RUB|KRW|CNY|JPY|"
            r"AU\$|A\$|AUD|CA\$|C\$|CAD|NZ\$|NZD|S\$|SGD|"
            r"CHF|SEK|NOK|DKK|"
            r"INR|BRL|MXN|TRY|ZAR|AED|SAR|IDR|MYR|THB|VND|"
            r"R\$|[¥₽$￥₩€£₹₺])\s*([\d,]+(?:\.\d+)?)"
        ),
        # e.g. 1,234.56元 — thousand commas stripped then spellout (ZH_CURRENCY needs a symbol prefix).
        "ZH_YUAN_AMOUNT": r"(?<![\d,.\-+/])([\d,]+(?:\.\d+)?)(元)(?![0-9A-Za-z])",
        # 5.25%-5.5% — rate band; must run before SH_MINUS_DIGIT (otherwise %-5 → 负五).
        "ZH_PERCENT_RANGE": (
            r"(\d+(?:\.\d+)?)\s*[%％]\s*[-–—]\s*(\d+(?:\.\d+)?)\s*[%％]"
        ),
        # 3000~5000亩 — ~／～／∼ 读作「至」；先于 loose_num 以免读出「三千~五千」.
        "ZH_NUM_RANGE_TILDE_MU": (
            r"(?<![0-9])(\d+)\s*[~～∼]\s*(\d+)(亩)(?![0-9])"
        ),
        # 50~80毫米 — weather/measurement tilde range should read as 「至」.
        "ZH_NUM_RANGE_TILDE_MM": (
            r"(?<![0-9])(\d+(?:\.\d+)?)\s*[~～∼]\s*(\d+(?:\.\d+)?)(毫米)(?![0-9A-Za-z])"
        ),
        # 80-130kg — 连字符表区间「至」并转出千克；先于 SH_MINUS_DIGIT（否则会吃掉 -1 成「负一」）.
        "ZH_NUM_RANGE_HYPHEN_KG": (
            r"(?<![0-9,.])(\d+(?:\.\d+)?)\s*[-–—]\s*(\d+(?:\.\d+)?)\s*[kK][gG](?![0-9A-Za-z])"
        ),
        # 2-3个小时 / 1.5-2小时 — duration range should be read as 「至」, not minus.
        "ZH_HOUR_RANGE_CTX": (
            r"(?<![0-9,.])(\d+(?:\.\d+)?)\s*[-–—]\s*(\d+(?:\.\d+)?)(个?小时)(?![0-9A-Za-z])"
        ),
        # 18-35岁 — age range should be read as 「至」, not minus.
        "ZH_AGE_RANGE_CTX": (
            r"(?<![0-9,.])(\d{1,3})\s*[-–—]\s*(\d{1,3})(岁)(?=[\u4e00-\u9fff]|$)"
        ),
        # ©2020-2026 — copyright year span: digit-by-digit years with hyphen read as「杠」.
        "ZH_COPYRIGHT_YEAR_RANGE": r"(©\s*)(\d{4})\s*[-–—]\s*(\d{4})(?!\d)",
        # 版权保护©2026 / 版权所有©2026 — © 不读出；年份按位（二零二六）；非 ©2020-2026 区间首年.
        "ZH_COPYRIGHT_AFTER_BANQUAN_PHRASE": (
            r"(?<=版权(?:所有|声明|保护))\s*©\s*(\d{4})(?!\d)(?!\s*[-–—]\s*\d)"
        ),
        # Copyright©2026 — Latin notice + single year (digit-by-digit); not ©2020-2026 range.
        "ZH_COPYRIGHT_WORD_SINGLE_YEAR": (
            r"(?i)(?<![A-Za-z0-9])(Copyright)(\s*©\s*)(\d{4})"
            r"(?![0-9A-Za-z])(?!\s*[-–—]\s*\d)"
        ),
        # Scientific notation with E/e: 1E-7 / 6.02e+23 / 2e10.
        "ZH_SCI_E_NEG": r"(?<![0-9A-Za-z.])(\d+(?:\.\d+)?)\s*[eE]\s*-\s*(\d+)(?![0-9A-Za-z.])",
        "ZH_SCI_E_POS": r"(?<![0-9A-Za-z.])(\d+(?:\.\d+)?)\s*[eE]\s*\+\s*(\d+)(?![0-9A-Za-z.])",
        "ZH_SCI_E_UNSIGNED": r"(?<![0-9A-Za-z.])(\d+(?:\.\d+)?)\s*[eE]\s*(\d+)(?![0-9A-Za-z.])",
        "ZH_PERCENT": R("SH_NUM_DEC_CAP") + r"%",
        "ZH_PER_MILLE": R("SH_NUM_DEC_CAP") + r"‰",
        # la_zh (incl. Han) so …20″至40°… matches; la_zh_plain would fail before「至」.
        "ZH_DMS": (
            r"(?<!\d)(\d+(?:\.\d+)?)\s*°\s*(\d+(?:\.\d+)?)\s*′\s*(\d+(?:\.\d+)?)\s*(?:″|′′)"
            + la_zh
        ),
        # e.g. 40°10′ — degree+minute only (no ″); (?!\d) after ′ avoids eating 40°10′20″.
        "ZH_DM_NOSEC": (
            r"(?<!\d)(\d+(?:\.\d+)?)\s*°\s*(\d+(?:\.\d+)?)\s*′(?!\d)"
            + la_zh
        ),
        "ZH_LN_E_EQ": r"ln\s*\(\s*e\s*\)\s*=\s*(\d+)",
        "ZH_LOG10_EQ": r"log\s*(?:₁₀|10)\s*\(\s*(\d+)\s*\)\s*=\s*(\d+)",
        # Dotted version numbers, e.g. v2.5.1 / 8.0.36 (at least two dots).
        "ZH_VERSION_DOTTED": r"(?<![0-9A-Za-z_.-])([vV]?\d+(?:\.\d+){2,})(?![0-9A-Za-z_.-])",
        # Uppercase alpha+digit codes, e.g. GC2406 / AU9999.
        "ZH_ALPHA_NUM_CODE": r"(?<![0-9A-Za-z])([A-Z]{1,6}\d{3,8})(?![0-9A-Za-z])",
        # Stock-code contexts (high precision first; avoid global bare 6-digit rewrite).
        "ZH_STOCK_PAIR_AND": r"(?<!\d)([036]\d{5})\s*和\s*([036]\d{5})(?!\d)",
        "ZH_STOCK_RANGE_CTX": r"((?:沪市|深市(?:主板)?|代码)\s*)([036]\d{5})\s*(?:至|[—-])\s*([036]\d{5})",
        "ZH_STOCK_CODE_IS": r"(?<!\d)([036]\d{5})(?=是[\u4e00-\u9fff]{2,8})",
        "ZH_STOCK_CODE_NAME": r"(?<!\d)([036]\d{5})(?=[\u4e00-\u9fff]{2,8})",
        "ZH_STOCK_CODE_FROM_YEAR": r"(?<!\d)([036]\d{5})(?=自\d{4}年)",
        # Fund/ETF six-digit codes in rebalance wording (e.g. 510300/588000; not [036] A-share prefix).
        "ZH_FUND_REBALANCE_CODES": (
            r"(从)\s*(\d{6})\s*((?:调仓至|调至|转至|换至|切换到))\s*(\d{6})(?=[\u4e00-\u9fffA-Za-z]|$)"
        ),
        # e.g. 指数基金510300是沪深300ETF — six digits before 是…ETF (allows digits/Han between).
        "ZH_SIX_DIG_IS_ETF": r"(?<!\d)(\d{6})(?=是.{1,48}?ETF)",
        # 基金是110022 / 基金代码510300 — six-digit fund code, digit-by-digit (not 十一万…).
        "ZH_FUND_CODE_CTX": (
            r"(?<![0-9])"
            r"((?:基金代码[是为]?|基金编号[:：]?|基金[是为]))"
            r"(\d{6})(?!\d)"
        ),
        # 161725招商中证白酒指数基金 — 代码紧贴基金名称（含「基金」），按位读.
        "ZH_FUND_CODE_BEFORE_NAME": (
            r"(?<![0-9])(\d{6})(?=[\u4e00-\u9fff]+基金)(?!\d)"
        ),
        # 身份证前6位是110101 — administrative division prefix, digit-by-digit (not 十一万…).
        "ZH_ID_CARD_PREFIX_6": (
            r"(?<![0-9])"
            r"((?:身份证号码|身份证号|身份证)前(?:六|6)位(?:是|为|[:：]\s*))"
            r"(\d{6})(?!\d)"
        ),
        "ZH_POSTCODE_CTX": r"((?:邮编|邮政编码)\s*(?:[:：]\s*|是|为)?)(\d{6})(?!\d)",
        # e.g. 北京是100000开头 — six digits after 是/为 and before 开头 (no 邮编 keyword).
        "ZH_POSTCODE_KAITOU": r"(?<=[是为])(\d{6})(?=开头)(?!\d)",
        # Landline area code contexts: 010开头 / 021是上海 / 0755是深圳.
        "ZH_AREA_CODE_KAITOU": r"(?<!\d)(0\d{2,3})(?=开头)(?!\d)",
        "ZH_AREA_CODE_IS_CITY": r"(?<!\d)(0\d{2,3})(?=是[\u4e00-\u9fff]{1,8})(?!\d)",
        # 省份代码填写440000 — GB/T 2260 六位码按位读（先于 loose_num「四十四万」）.
        "ZH_PROVINCE_CODE_CTX": (
            r"(省份代码\s*(?:[:：]\s*|填写|为|是)?\s*)(\d{6})(?!\d)"
        ),
        # 代码从320100至321300 — 行政区划六位码区间按位读（先于 loose_num「三十二万」）.
        "ZH_ADMIN_DIV_CODE_RANGE": (
            r"(代码从)(\d{6})\s*(?:至|到)\s*(\d{6})(?![0-9])"
        ),
        # 编号1001至20000 — number-id ranges should be read digit-by-digit.
        "ZH_SERIAL_RANGE_CTX": (
            r"(编号)\s*(\d{2,10})\s*(?:至|到)\s*(\d{2,10})(?![0-9])"
        ),
        # 下辖13个地级市 — 个位数/两位数按位读（如 一三 个地级市）.
        "ZH_PREFECTURE_COUNT_CTX": (
            r"(下辖)(\d{1,2})(个地级市)(?![0-9])"
        ),
        # Flight code with digit-leading airline prefix, e.g. 3U8633航班.
        "ZH_FLIGHT_DIGIT_PREFIX": r"(?<![0-9A-Za-z])([0-9][A-Z])(\d{3,4})(?=(?:次)?航班)",
        # IATA + digits before「次航班/航班」— e.g. HU7890航班、CA1234次航班；字母 spaced，数字按位.
        "ZH_FLIGHT_IATA_BEFORE_FLIGHT_WORD": (
            r"(?<![0-9A-Za-z])([A-Za-z]{2,3}\d{3,5})(?=(?:次)?航班)"
        ),
        # 航班FR1234因… — code immediately after「航班」.
        "ZH_FLIGHT_IATA_AFTER_HANGBAN": (
            r"(?<=航班)(?<![0-9A-Za-z])([A-Za-z]{2,3}\d{3,5})(?![0-9A-Za-z])"
        ),
        # Boarding-pass code like 3C3456登机牌 — keep letter, digits read digit-by-digit.
        "ZH_BOARDING_PASS_CODE": r"(?<![0-9A-Za-z])([0-9][A-Z]\d{3,6})(?=登机牌)",
        # Gate code like B12登机口 — keep letter, digits read digit-by-digit.
        "ZH_GATE_CODE": r"(?<![0-9A-Za-z])([A-Z]\d{1,4})(?=登机口)",
        # RTX 4070显卡 / A17处理器 — electronics model ids: keep prefix, digits digit-by-digit.
        "ZH_ELECTRONICS_MODEL_CTX": (
            r"(?<![0-9A-Za-z])([A-Z]{1,10})\s*(\d{2,6})"
            r"(?=(?:Ti|SUPER|Super|Max|MAX|Pro|PRO)?(?:版)?(?:显卡|处理器|芯片|手机|平板|电视|显示器|笔记本|路由器|耳机|主板|机型))"
        ),
        # 4K游戏/视频/分辨率 — K here is resolution tier, not kelvin unit.
        "ZH_RESOLUTION_K_CTX": (
            r"(?<![0-9A-Za-z])([248])([kK])(?=(?:游戏|视频|画质|分辨率|显示器|电视|电影|渲染|输出|播放|屏))"
        ),
        # 沪B·D12345 — 间隔号 U+00B7 不读；须早于 zh 全局「·→点乘」替换.
        "ZH_PLATE_CN_MIDDOT_SUFFIX": (
            r"(?<![0-9A-Za-z])([\u4e00-\u9fff][A-Z])\s*·\s*([A-Z]\d{4,6})(?![0-9A-Za-z])"
        ),
        # 新能源号牌 粤BD08596 / 京AF12093 — 省份汉字+2 字母+5 位数字（先于单字母号牌）.
        "ZH_PLATE_CN_NE": (
            r"(?<![0-9A-Za-z])([\u4e00-\u9fff][A-Z]{2})(\d{5})(?![0-9A-Za-z])"
        ),
        # 大陆号牌 沪C3691K / 川AZ029F — 省份汉字+1 字母+4–6 位字母数字混排（含至少 1 个字母）.
        "ZH_PLATE_CN_ALNUM": (
            r"(?<![0-9A-Za-z])([\u4e00-\u9fff][A-Z])"
            r"((?=[A-HJ-NP-Z0-9]*[A-HJ-NP-Z])[A-HJ-NP-Z0-9]{4,6})(?![0-9A-Za-z])"
        ),
        # Mainland CN plate head + numeric tail, e.g. 浙A88888 / 粤B12345.
        "ZH_PLATE_CN_NUM": r"(?<![0-9A-Za-z])([\u4e00-\u9fff][A-Z])\s*([0-9]{5,6})(?![0-9A-Za-z])",
        # File extensions, e.g. .mp3 / .wav / .tar.
        "ZH_FILE_EXT": r"(?<=[A-Za-z0-9_\u4e00-\u9fff])(\.[A-Za-z]{1,8}\d{0,2})(?![A-Za-z0-9])",
        # e.g. recording_2026.wav — whole token before ZH_FILE_EXT / loose decimal; digits digit-by-digit.
        "ZH_FILENAME_UNDER_DIGITS_EXT": (
            r"(?<![A-Za-z0-9])"
            r"([A-Za-z][A-Za-z0-9_-]*_\d{1,12}\.[A-Za-z]{1,8})"
            r"(?![A-Za-z0-9])"
        ),
        # ubuntu-24.04.1-desktop-amd64.iso — stem: hyphen→空格分段；句点不读；数字按位（先于 SH_MINUS_DIGIT / loose）.
        "ZH_LINUX_DISTRO_FILENAME": (
            r"(?<![A-Za-z0-9])"
            r"([a-zA-Z][a-zA-Z0-9]*(?:-[a-zA-Z0-9.]+)+)"
            r"\.([a-zA-Z]{2,8})(?![A-Za-z0-9])"
        ),
        # USSD *#06# (IMEI) / *#21# 等 — 星号、井号、中间数字按位、收尾井号；先于 SH_PHONE_DASH / loose.
        "ZH_USSD_STAR_HASH_DIGITS_HASH": (
            r"(?<![*#0-9A-Za-z])\*#(\d{1,8})#(?![0-9#*A-Za-z])"
        ),
        # 话题#科技创新#下 — 「话题」后成对井号包住话题名：井号→空格（内文保留）；先于其它井号处理.
        "ZH_HASHTAG_HUATI_WRAPPED": (
            r"(?<=话题)#([^#]{1,128})#(?=[\u4e00-\u9fff，。、；：！？\s]|$)"
        ),
        # 微博式 #中文话题# 接正文（非 *#数字#、非 URL 片段）；内须含汉字；排除「话题#」已由上条处理.
        "ZH_HASHTAG_INLINE_CJK_WRAPPED": (
            r"(?<!\*)(?<!话题)(?<=[\u4e00-\u9fff，。：；、\s]|^)(?<![/#])"
            r"#((?=.*[\u4e00-\u9fff])[^#]{1,128})#(?=[\u4e00-\u9fff，。、；：！？\s]|$)"
        ),
        # C 风格注释读法：/* */（中间仅空白）、//（非 URL 内）；须先于裸「/」类规则.
        "ZH_COMMENT_BLOCK_C_EMPTY": r"/\*\s*\*/",
        "ZH_COMMENT_DOUBLE_SLASH": r"(?<![/:])//",
        # (a)总则 / （a）总则 — 枚举小标题：去括号并在字母两侧加空格，便于 TTS 与后文分界.
        "ZH_PAREN_LATIN_ENUM_LETTER": r"\(([a-zA-Z])\)",
        "ZH_PAREN_FULLWIDTH_LATIN_ENUM": r"（([a-zA-Z])）",
        # 总则、b — 列表项间顿号后接拉丁字母时去掉顿号（字母项已有前导空格，避免双空格）.
        "ZH_COMMA_BEFORE_LATIN_ENUM": r"(?<=[\u4e00-\u9fff])、(?=\s*[a-zA-Z])",
        "ZH_COMPOSITE_UNIT": R("SH_PREFIX_NUM_UNIT") + comp_alt_zh + la_zh,
        # Same trailing context as composite: values like 37.3℃算低烧 need Han after ℃.
        "ZH_SINGLE_UNIT": R("SH_PREFIX_NUM_UNIT") + single_alt_zh + la_zh,
        # 汉字(拉丁)：仅当括号内像「拼音」（含声调、小写、空格分音节等）时才走 pinyin_han_paren_zh。
        # 纯大写缩写如 (FAX)(HTTP) 不得匹配，否则 FAX 会被音节器拆坏并输出「传?」这类结果。
        "ZH_PINYIN_HAN": (
            r"(\p{Han}+)[（(](?![A-Z]{2,}[）)])"
            r"([A-Za-zāáǎàēéěèīíǐìōóǒòūúǔùǖǘǚǜü'\s]+)[）)]"
        ),
        "ZH_PINYIN_SOLO": (
            r"(?<=[^a-zA-Z\s]|^)(?=[A-Za-zāáǎàēéěèīíǐìōóǒòūúǔùǖǘǚǜü']*?"
            r"[āáǎàēéěèīíǐìōóǒòūúǔùǖǘǚǜü'])"
            r"([A-Za-zāáǎàēéěèīíǐìōóǒòūúǔùǖǘǚǜü']+)"
        ),
        # Standalone TLD tokens in prose (before ZH_URL_OR_DOMAIN spaced-letter readout).
        "ZH_TLD_COM_CN": r"\.com\.cn",
        "ZH_TLD_SUFFIX": r"(?<![/.A-Za-z0-9])(\.(?:com(?!\.cn)|org|net|cn|edu|gov|io))(?![A-Za-z0-9.])",
        "ZH_URL_OR_DOMAIN": (
            r"(?:https?://|www\.)[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}(?:/[a-zA-Z0-9_@%&?+=\\~/-]*)*"
            r"|[a-zA-Z0-9.-]+\.(?:com|cn|net|org|edu|gov|io)(?:/[a-zA-Z0-9_@%&?+=\\~/-]*)*"
        ),
        # EUI-48 MAC (six colon-separated octets); before time rules. (?!:[0-9A-Fa-f]) avoids IPv6 prefix.
        "ZH_MAC_EUI48": (
            r"(?<![0-9A-Fa-f:])([0-9A-Fa-f]{2}(?::[0-9A-Fa-f]{2}){5})(?!:[0-9A-Fa-f])(?![0-9A-Fa-f])"
        ),
        # e.g. 格力KFR-35GW — Latin + hyphen + capacity + suffix (before SH_MINUS_DIGIT on -3).
        "ZH_APPLIANCE_MODEL": (
            r"(?<![A-Za-z0-9])([A-Z]{2,8}-\d{2,4}[A-Z]{1,8})(?![A-Za-z0-9-])"
        ),
        # Wi-Fi / why-fi / Hi-Fi — hyphen silent before literal「字母-字母→减」.
        "ZH_WIFI_HYPHEN_SILENT": (
            r"(?<![A-Za-z0-9])"
            r"([Ww][Ii]-[Ff][Ii]|[Ww][Hh][Yy]-[Ff][Ii]|[Hh][Ii]-[Ff][Ii])"
            r"(?![A-Za-z0-9])"
        ),
        # CPU/GPU model like i9-14900K — hyphen should be silent; digits read digit-by-digit.
        "ZH_TECH_MODEL_HYPHEN": (
            r"(?<![A-Za-z0-9])([A-Za-z]{1,6}\d{1,3}-\d{3,6}[A-Za-z0-9]{0,4})(?![A-Za-z0-9-])"
        ),
        # Type-C / Type-A — tech connector naming; hyphen should be silent, not「减」.
        "ZH_TYPE_HYPHEN_TERM": (
            r"(?<![A-Za-z0-9])([Tt]ype-[A-Za-z0-9]{1,8})(?![A-Za-z0-9-])"
        ),
        # e.g. 合同编号HT-2026-001 — Latin + hyphen + numeric segments (not "负"; hyphens silent).
        # Requires ≥2 digits per segment so bare "X-2" still goes to SH_MINUS_DIGIT.
        "ZH_LATIN_NUM_HYPHEN_CODE": (
            r"(?<![A-Za-z0-9])([A-Z]{2,}(?:-\d{2,})+)(?![A-Za-z0-9])"
        ),
    }


def build_en_pattern_defs() -> dict[str, str]:
    out = {}
    out.update(_shared_blocks())
    out.update(_en_only_blocks())
    verify_pattern_defs(out, label="en ")
    return out


def build_zh_pattern_defs() -> dict[str, str]:
    out = {}
    out.update(_shared_blocks())
    out.update(_zh_only_blocks())
    verify_pattern_defs(out, label="zh ")
    return out


def build_ar_pattern_defs() -> dict[str, str]:
    out = {}
    out.update(_shared_blocks())
    out.update(_ar_only_blocks())
    verify_pattern_defs(out, label="ar ")
    return out


def build_bn_pattern_defs() -> dict[str, str]:
    out = {}
    out.update(_shared_blocks())
    out.update(_bn_only_blocks())
    verify_pattern_defs(out, label="bn ")
    return out


def build_ru_pattern_defs() -> dict[str, str]:
    out = {}
    out.update(_shared_blocks())
    out.update(_ru_only_blocks())
    verify_pattern_defs(out, label="ru ")
    return out


EN_PATTERN_DEFS: Final = build_en_pattern_defs()
ZH_PATTERN_DEFS: Final = build_zh_pattern_defs()
AR_PATTERN_DEFS: Final = build_ar_pattern_defs()
BN_PATTERN_DEFS: Final = build_bn_pattern_defs()
RU_PATTERN_DEFS: Final = build_ru_pattern_defs()
