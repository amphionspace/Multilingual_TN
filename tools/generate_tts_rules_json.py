#!/usr/bin/env python3
"""Generate ICU/rules/*.json from structured data (edit here, not C++)."""

from __future__ import annotations

import json
import re
import subprocess
import sys
from pathlib import Path

from pattern_blocks import AR_PATTERN_DEFS
from pattern_blocks import BN_PATTERN_DEFS
from pattern_blocks import EN_PATTERN_DEFS
from pattern_blocks import R
from pattern_blocks import RU_PATTERN_DEFS
from pattern_blocks import ZH_PATTERN_DEFS

ROOT = Path(__file__).resolve().parents[1]
RULES = ROOT / "rules"


def LIT(text: str) -> dict:
    return {"op": "lit", "text": text}


def exec_rule(pattern: str, steps: list) -> dict:
    """All structured actions go through C++ op dispatch (`action: exec`)."""
    return {"pattern": pattern, "action": "exec", "params": {"steps": steps}}


# walk_readout `map` keys are UTF-8 single-character strings
EMAIL_ZH_MAP = {".": "点", "@": "艾特", "_": "下划线", "-": "杠"}
EMAIL_EN_MAP = {".": " dot ", "@": " at ", "_": " underscore ", "-": " dash "}
PHONE_EN_MAP = {"+": " plus ", "-": " ", " ": " ", "\u00A0": " "}
URL_EN_MAP = {
    ".": " dot ",
    ":": " colon ",
    "/": " slash ",
    "?": " question mark ",
    "=": " equals ",
    "&": " and ",
    "#": " hash ",
    "-": " dash ",
    "_": " underscore ",
    "%": " percent ",
    "+": " plus ",
    "@": " at ",
    "~": " tilde ",
}
URL_ZH_MAP = {
    ".": "点",
    ":": "冒号",
    "/": "斜杠",
    "-": "杠",
    "_": "下划线",
    "?": "问号",
    "=": "等于",
    "&": "和",
    "%": "百分号",
}
VERSION_ZH_MAP = {
    ".": "点",
}
# Latin filename with underscore + numeric tail + extension (before ZH_FILE_EXT splits ".wav").
FILENAME_UNDER_DIGITS_ZH_MAP = {
    ".": "点",
    "_": "下划线",
    "-": "杠",
}
CODE_ZH_MAP: dict[str, str] = {}


# ---------------------------------------------------------------------------
# Chinese math: (1) whole-line / identity templates via `exec_rule`;
# (2) generic symbol readouts via simple `replace` (ordered, longest first).
#
# Full LaTeX semantics are out of scope; add new identities as new patterns +
# literals or exec steps.  Do not replace bare "∫" here — SH_INTEGRAL expects
# Unicode sub/superscript bounds after ∫.
# ---------------------------------------------------------------------------


def zh_formula_identity_pre_arith() -> list[dict]:
    """Run before ZH_ADD_EQ so '+' inside identities is not split as arithmetic."""
    return [
        exec_rule(
            R("ZH_PROB_INCLUSION_AB"),
            [LIT("P A并B 等于P A加P B减P A交B")],
        ),
        exec_rule(
            R("ZH_BERNOULLI_EQ"),
            [LIT("P加二分之一ρv平方加ρgh等于常数")],
        ),
        exec_rule(
            R("ZH_KINETIC_ENERGY"),
            [LIT("Ek等于二分之一mv平方")],
        ),
        exec_rule(
            R("ZH_BASEL_SUM_EQ"),
            # 不用阿拉伯数字 1：后续 loose 规则会给数字加不自然空格。
            [LIT("西格玛 n 从一到无穷，一除以 n 平方，等于六分之派平方")],
        ),
    ]


def zh_formula_identity_post_date() -> list[dict]:
    """Run after ZH_DATE_YMD; patterns still match sin/θ/ln/log in source text."""
    return [
        exec_rule(
            R("ZH_TRIG_IDENTITY"),
            [
                LIT("sine θ 加 cosine θ 等于 根号"),
                {"op": "grp", "g": 1, "as": "spellout"},
                LIT("乘以sine θ加"),
                {"op": "grp", "g": 2, "as": "spellout"},
                LIT("度"),
            ],
        ),
        exec_rule(
            R("ZH_LN_E_EQ"),
            [
                LIT("e的自然对数等于"),
                {"op": "grp", "g": 1, "as": "spellout"},
            ],
        ),
        exec_rule(
            R("ZH_LOG10_EQ"),
            [
                LIT("以十为底"),
                {"op": "grp", "g": 1, "as": "spellout"},
                LIT("的对数等于"),
                {"op": "grp", "g": 2, "as": "spellout"},
            ],
        ),
    ]


def zh_math_symbol_replace_rules() -> list[dict]:
    """ICU replace rules: order matters (longer / more specific first)."""
    pairs: list[tuple[str, str]] = [
        # 集合符号 +「表示…」教学句式：先补逗号（须仍在 ∈/∉/∅ 前），再展开符号名.
        (r"(?<=[\u4e00-\u9fff])(?<!其)中(?=∈|∉|∅)", "中，"),
        ("∉表示不属于", "不属于符号表示不属于"),
        ("∈表示属于", "属于符号表示属于"),
        ("∅表示空集", "空集符号表示空集"),
        # powers of common symbols (before single-char π, θ, …)
        ("π²", "派的平方"),
        ("π^2", "派的平方"),
        ("θ²", "θ的平方"),
        ("ρ²", "柔的平方"),
        ("φ²", "斐的平方"),
        ("ω²", "欧米伽的平方"),
        ("λ²", "拉姆达的平方"),
        ("σ²", "西格马的平方"),
        # E=mc² — superscript ² otherwise reads oddly;「mc平方」before literal「=」→「等于」.
        ("mc²", "mc平方"),
        (r"mc\^2", "mc平方"),  # literal ^ in regex (otherwise ^ is line anchor)
        # Single-letter variables: a²/b²/r² -> a平方/b平方/r平方.
        # Guard with (?<![0-9A-Za-z]) so numeric units like 20m² / 30cm² keep unit-specific handling.
        (r"(?<![0-9A-Za-z])([A-Za-z])²", r"$1平方"),
        (r"(?<![0-9A-Za-z])([A-Za-z])\^2", r"$1平方"),
        ("∏", "连乘"),
        ("Π", "连乘"),
        ("∑", "西格玛"),
        ("Σ", "西格玛"),
        ("∞", "无穷"),
        ("√", "根号"),
        ("∂", "偏微分"),
        ("∇", "纳布拉"),
        ("≠", "不等于"),
        ("≤", "小于等于"),
        ("≥", "大于等于"),
        ("<", "小于"),
        (">", "大于"),
        ("≈", "约等于"),
        ("÷", "除以"),
        ("±", "正负"),
        ("∓", "负正"),
        # 集合 / 概率常见符号（与 legacy zh.cpp 一致）.
        ("∪", "并"),
        ("∩", "交"),
        # 先于「·→点乘」，否则 kW·h 无法被 ZH_COMPOSITE_UNIT 识别.
        ("kW·h", "kWh"),
        ("·", "点乘"),
        # x + COMBINING MACRON (U+0304): sample mean / bar notation (not precomposed in ASCII).
        ("x\u0304", "x拔"),
        ("X\u0304", "X拔"),
        # Skip ′ / ″ here: ZH_DMS must match arcminute/arcsecond primes first.
        # Greek (common in formulas; may rarely collide outside STEM text)
        ("α", "阿尔法"),
        ("β", "贝塔"),
        ("γ", "伽马"),
        ("δ", "德尔塔"),
        ("ε", "艾普西龙"),
        ("ζ", "泽塔"),
        ("η", "伊塔"),
        ("ι", "约塔"),
        ("κ", "卡帕"),
        ("λ", "拉姆达"),
        ("μ", "缪"),
        ("ν", "纽"),
        ("ξ", "克西"),
        ("π", "派"),
        ("ρ", "柔"),
        ("σ", "西格马"),
        ("τ", "陶"),
        ("υ", "宇普西龙"),
        ("φ", "斐"),
        ("χ", "希"),
        ("ψ", "普赛"),
        ("ω", "欧米伽"),
        ("Γ", "伽马"),
        ("Δ", "德尔塔"),
        ("Λ", "拉姆达"),
        ("Ξ", "克西"),
        # Π / Σ already above (∏/连乘, ∑/西格玛); capitals share those glyphs.
        ("Φ", "斐"),
        ("Ψ", "普赛"),
        ("Ω", "欧米伽"),
    ]
    return [{"pattern": pat, "replace": rep} for pat, rep in pairs]


# Unicode subscripts U+2080–U+2089 → spoken digit (leading space separates from letters, e.g. SO₂ → SO 二).
_CHEM_SUBSCRIPT_ZH_MAP: dict[str, str] = {
    "₀": " 零",
    "₁": " 一",
    "₂": " 二",
    "₃": " 三",
    "₄": " 四",
    "₅": " 五",
    "₆": " 六",
    "₇": " 七",
    "₈": " 八",
    "₉": " 九",
}


def zh_chemistry_formula_exec_rules() -> list[dict]:
    """
    Common Unicode-subscript formulas → letter/digit style readout (walk_readout):
    e.g. O₂ → O 二, SO₂ → SO 二 (Latin kept, subscripts as Chinese digit names).
    Order: longer / more specific before O₂ / N₂ to avoid eating SO₂, etc.
    """
    step: list[dict] = [
        {"op": "walk_readout", "g": 0, "letter": "keep", "map": _CHEM_SUBSCRIPT_ZH_MAP},
    ]
    # 葡萄糖：下标₁₂须读「十二」；walk_readout 会逐位读「一」「二」，故单独整段替换.
    glucose = exec_rule("C₆H₁₂O₆", [LIT("C六H十二O六")])
    # Same coverage as prior replace-table; patterns are literal ICU strings (no pattern_defs).
    ordered_formulas: list[str] = [
        "LiFePO₄",
        "Fe₂O₃",
        "Fe₃O₄",
        "Al₂O₃",
        "Na₂CO₃",
        "P₂O₅",
        "CaCO₃",
        "KMnO₄",
        "H₂SO₄",
        "HNO₃",
        "H₂O₂",
        "MnO₂",
        "SiO₂",
        "NaOH",
        "NH₃",
        "CH₄",
        "C₂H₅OH",
        "NO₂",
        "N₂O",
        "CO₂",
        "SO₃",
        "SO₂",
        "H₂O",
        "NaCl",
        "KCl",
        "HCl",
        "FeO",
        "CuO",
        "CaO",
        "ZnO",
        "MgO",
        "CO",
        "NO",
        "O₃",
        "O₂",
        "N₂",
    ]
    return [glucose, *[exec_rule(p, step) for p in ordered_formulas]]


def zh_unicode_roman_replace_rules() -> list[dict]:
    """
    Unicode Number Forms (U+2160–U+216F, U+2170–U+217F): Ⅷ → 八, etc.
    Runs before ASCII-only SH_ROMAN (M D C L X V I).
    """
    rows: list[tuple[str, str]] = [
        ("Ⅰ", "一"),
        ("Ⅱ", "二"),
        ("Ⅲ", "三"),
        ("Ⅳ", "四"),
        ("Ⅴ", "五"),
        ("Ⅵ", "六"),
        ("Ⅶ", "七"),
        ("Ⅷ", "八"),
        ("Ⅸ", "九"),
        ("Ⅹ", "十"),
        ("Ⅺ", "十一"),
        ("Ⅻ", "十二"),
        ("ⅰ", "一"),
        ("ⅱ", "二"),
        ("ⅲ", "三"),
        ("ⅳ", "四"),
        ("ⅴ", "五"),
        ("ⅵ", "六"),
        ("ⅶ", "七"),
        ("ⅷ", "八"),
        ("ⅸ", "九"),
        ("ⅹ", "十"),
        ("ⅺ", "十一"),
        ("ⅻ", "十二"),
        ("Ⅼ", "五十"),
        ("Ⅽ", "一百"),
        ("Ⅾ", "五百"),
        ("Ⅿ", "一千"),
        ("ⅼ", "五十"),
        ("ⅽ", "一百"),
        ("ⅾ", "五百"),
        ("ⅿ", "一千"),
    ]
    return [{"pattern": a, "replace": b} for a, b in rows]


def zh_vol_cn_mag_suffix_rules() -> list[dict]:
    """
    数字 + 中文数量级 + 体积/面积单位（如 126亿m³）— ZH_COMPOSITE_UNIT 要求数字紧贴单位，故单独处理。
    """
    out: list[dict] = []
    for suf, spoken in (("m³", "立方米"), ("m²", "平方米"), ("cm³", "立方厘米"), ("cm²", "平方厘米")):
        pat = (
            r"(?<![0-9.])(\d+(?:\.\d+)?)\s*(亿|万|千万|百万|千)\s*"
            + re.escape(suf)
            + r"(?![0-9A-Za-z])"
        )
        out.append(
            exec_rule(
                pat,
                [
                    {"op": "grp", "g": 1, "as": "spellout"},
                    {"op": "grp", "g": 2, "as": "raw"},
                    LIT(spoken),
                ],
            )
        )
    return out


def zh_no_rank_replace_rules() -> list[dict]:
    """
    No.1 / No.10 … → English 'number one' (before loose_num turns digits into 一).
    Order: two-digit ranks first so No.10 is not consumed as No.1 + 0.
    """
    words: dict[int, str] = {
        1: "number one",
        2: "number two",
        3: "number three",
        4: "number four",
        5: "number five",
        6: "number six",
        7: "number seven",
        8: "number eight",
        9: "number nine",
        10: "number ten",
        11: "number eleven",
        12: "number twelve",
        13: "number thirteen",
        14: "number fourteen",
        15: "number fifteen",
        16: "number sixteen",
        17: "number seventeen",
        18: "number eighteen",
        19: "number nineteen",
        20: "number twenty",
    }
    out: list[dict] = []
    for n in range(20, 0, -1):
        out.append({"pattern": rf"(?i)No\.{n}(?![0-9])", "replace": words[n]})
    return out


def en_digit_names() -> list[str]:
    return [
        "zero ",
        "one ",
        "two ",
        "three ",
        "four ",
        "five ",
        "six ",
        "seven ",
        "eight ",
        "nine ",
    ]


def zh_digit_names() -> list[str]:
    return ["零", "一", "二", "三", "四", "五", "六", "七", "八", "九"]


def en_currency_map() -> dict[str, str]:
    return {
        "$": "dollars",
        "USD": "dollars",
        "€": "euros",
        "EUR": "euros",
        "£": "pounds",
        "￡": "pounds",
        "GBP": "pounds",
        "₽": "rubles",
        "RUB": "rubles",
        "₩": "won",
        "KRW": "won",
        "¥": "yuan",
        "￥": "yuan",
        "CNY": "yuan",
    }


def zh_currency_map() -> dict[str, str]:
    return {
        # US / Europe
        "$": "美元",
        "US$": "美元",
        "USD": "美元",
        "€": "欧元",
        "EUR": "欧元",
        "£": "英镑",
        "￡": "英镑",
        "GBP": "英镑",
        # East Asia
        "₽": "卢布",
        "RUB": "卢布",
        "₩": "韩元",
        "KRW": "韩元",
        "HK$": "港元",
        "HKD": "港元",
        "¥": "元",
        "￥": "元",
        "CNY": "元",
        "JPY": "日元",
        # Commodity / developed markets
        "AU$": "澳元",
        "A$": "澳元",
        "AUD": "澳元",
        "CA$": "加元",
        "C$": "加元",
        "CAD": "加元",
        "NZ$": "新西兰元",
        "NZD": "新西兰元",
        "S$": "新加坡元",
        "SGD": "新加坡元",
        "CHF": "瑞士法郎",
        "SEK": "瑞典克朗",
        "NOK": "挪威克朗",
        "DKK": "丹麦克朗",
        # Emerging major economies
        "₹": "印度卢比",
        "INR": "印度卢比",
        "R$": "巴西雷亚尔",
        "BRL": "巴西雷亚尔",
        "MXN": "墨西哥比索",
        "₺": "土耳其里拉",
        "TRY": "土耳其里拉",
        "ZAR": "南非兰特",
        "AED": "阿联酋迪拉姆",
        "SAR": "沙特里亚尔",
        "IDR": "印尼盾",
        "MYR": "马来西亚林吉特",
        "THB": "泰铢",
        "VND": "越南盾",
    }


def en_unit_composite() -> dict[str, str]:
    # value: spoken suffix after spellout number (keep leading spaces like original)
    return {
        "kg": "  kilograms ",
        "mm": "  millimeters ",
        "kPa": "  kilopascals ",
        "min": "  minutes ",
        "km": "  kilometers ",
        "nm": "  nanometers ",
        "μm": "  micrometers ",
        "km/h": "  kilometers per hour ",
        "ml": "  milliliters ",
        "mL": "  milliliters ",
        "mA": "  milliamperes ",
        "kW·h": "  kilowatt-hours ",
        "kΩ": "  kiloohms ",
        "kW": "  kilowatts ",
        "bn": "  billion ",
        "cm/s": "  centimeters per second ",
        "m/s²": "  meters per second squared ",
        "rad/s": "  radians per second ",
        "rpm": "  revolutions per minute ",
        "mV": "  millivolts ",
        "kV": "  kilovolts ",
        "MΩ": "  megaohms ",
        "pF": "  picofarads ",
        "μF": "  microfarads ",
        "nF": "  nanofarads ",
        "mH": "  millihenries ",
        "μH": "  microhenries ",
        "mW": "  milliwatts ",
        "MW": "  megawatts ",
        "dB": "  decibels ",
        "kN": "  kilonewtons ",
        "MPa": "  megapascals ",
        "hPa": "  hectopascals ",
        "N·m": "  newton-meters ",
        "kg/m³": "  kilograms per cubic meter ",
        "dm": "  decimeters ",
        "cm": "  centimeters ",
        "m²": "  square meters ",
        "cm²": "  square centimeters ",
        "m³": "  cubic meters ",
        "cm³": "  cubic centimeters ",
        "kJ": "  kilojoules ",
        "kcal": "  kilocalories ",
        "eV": "  electronvolts ",
        "J/(kg·℃)": "  joules per kilogram Celsius ",
        "kHz": "  kilohertz ",
        "MHz": "  megahertz ",
        "GHz": "  gigahertz ",
        "ms": "  milliseconds ",
        "μs": "  microseconds ",
        "ns": "  nanoseconds ",
        "m/s": "  meters per second ",
        "km/s": "  kilometers per second ",
        "mi/s": "  miles per second ",
        "mi/h": "  miles per hour ",
        "mph": "  miles per hour ",
        "mi": "  miles ",
        "in": "  inches ",
        "lb": "  pounds ",
        "lbs": "  pounds ",
        "ft": "  feet ",
        "yd": "  yards ",
        "oz": "  ounces ",
    }


def zh_unit_composite() -> dict[str, str]:
    return {
        "kg": "千克",
        "mm": "毫米",
        "kPa": "千帕",
        "min": "分钟",
        "km": "千米",
        "nm": "纳米",
        "μm": "微米",
        "km/h": "千米每时",
        "ml": "毫升",
        "mL": "毫升",
        "mg": "毫克",
        "mA": "毫安",
        "atm": "个大气压",
        "kW·h": "千瓦时",
        "kWh": "千瓦时",
        "kΩ": "千欧",
        "kW": "千瓦",
        "cm/s": "厘米每秒",
        "mol/L": "摩尔每升",
        "m/s²": "米每二次方秒",
        "rad/s": "弧度每秒",
        "rpm": "每分钟转数",
        "mV": "毫伏",
        "kV": "千伏",
        "MΩ": "兆欧",
        "pF": "皮法",
        "μF": "微法",
        "nF": "纳法",
        "mH": "毫亨",
        "μH": "微亨",
        "mW": "毫瓦",
        "MW": "兆瓦",
        "dB": "分贝",
        "kN": "千牛",
        "MPa": "兆帕",
        "hPa": "百帕",
        "mmHg": "毫米汞柱",
        "N·m": "牛米",
        "kg/m³": "千克每立方米",
        "dm": "分米",
        "cm": "厘米",
        "m²": "平方米",
        "cm²": "平方厘米",
        "m³": "立方米",
        "cm³": "立方厘米",
        "kJ": "千焦",
        "kcal": "大卡",
        "eV": "电子伏特",
        "J/kg·℃": "焦每千克摄氏度",
        "J/(kg·℃)": "焦每千克摄氏度",
        "kHz": "千赫兹",
        "MHz": "兆赫兹",
        "GHz": "吉赫兹",
        "ms": "毫秒",
        "μs": "微秒",
        "ns": "纳秒",
        "m/s": "米每秒",
    }


def en_unit_single() -> dict[str, str]:
    return {
        "s": "  seconds ",
        "h": "  hours ",
        "m": "  meters ",
        "g": "  grams ",
        "°": "  degrees ",
        "′": "  minutes ",
        "″": "  seconds ",
        "'": "  minutes ",
        '"': "  seconds ",
        "Pa": "  pascals ",
        "℃": "  degrees Celsius ",
        "℉": "  degrees Fahrenheit ",
        "Ω": "  ohms ",
        "㎡": "  square meters ",
        "W": "  watts ",
        "Hz": "  hertz ",
        "cal": "  calories ",
        "N": "  newtons ",
        "J": "  joules ",
        "T": "  teslas ",
        "K": "  kelvins ",
        "H": "  henries ",
        "G": "  gauss ",
        "L": "  liters ",
        "l": "  liters ",
        "t": "  tonnes ",
        "b": "  bits ",
        "°C": "  degrees Celsius ",
        "°F": "  degrees Fahrenheit ",
        "mol": "  moles ",
        "M": "  million ",
    }


def ar_digit_names() -> list[str]:
    return [
        "صفر ",
        "واحد ",
        "اثنان ",
        "ثلاثة ",
        "أربعة ",
        "خمسة ",
        "ستة ",
        "سبعة ",
        "ثمانية ",
        "تسعة ",
    ]


def ar_month_names() -> list[str]:
    """Gregorian month names (MSA); used by `date_iso_en` when `locale` is `ar`."""
    return [
        "يناير",
        "فبراير",
        "مارس",
        "أبريل",
        "مايو",
        "يونيو",
        "يوليو",
        "أغسطس",
        "سبتمبر",
        "أكتوبر",
        "نوفمبر",
        "ديسمبر",
    ]


def ar_currency_map() -> dict[str, str]:
    return {
        "$": "دولار",
        "USD": "دولار",
        "€": "يورو",
        "EUR": "يورو",
        "£": "جنيه",
        "￡": "جنيه",
        "GBP": "جنيه",
        "₽": "روبل",
        "RUB": "روبل",
        "₩": "وون",
        "KRW": "وون",
        "¥": "يوان",
        "￥": "يوان",
        "CNY": "يوان",
    }


def ar_unit_composite() -> dict[str, str]:
    return {
        "m/s": "متر في الثانية",
        "mm": "مليمتر",
        "km": "كيلومتر",
        "km/h": "كيلومتر في الساعة",
        "kg": "كيلوجرام",
        "cm": "سنتيمتر",
        "dm": "ديسيمتر",
        "cm/s": "سنتيمتر في الثانية",
        "m/s²": "متر في الثانية المربعة",
        "cm²": "سنتيمتر مربع",
        "cm³": "سنتيمتر مكعب",
        "m²": "متر مربع",
        "m³": "متر مكعب",
        "ml": "مليلتر",
        "mL": "مليلتر",
        "kJ": "كيلو جول",
        "kcal": "كيلو كالوري",
        "kHz": "كيلو هرتز",
        "MHz": "ميجا هرتز",
        "GHz": "جيجا هرتز",
        "rpm": "دورة في الدقيقة",
        "kg/m³": "كيلوجرام لكل متر مكعب",
        "min": "دقيقة",
        "nm": "نانومتر",
        "μm": "ميكرومتر",
        "mA": "ملي أمبير",
        "kW·h": "كيلوواط ساعة",
        "kΩ": "كيلو أوم",
        "kW": "كيلوواط",
        "bn": "مليار",
        "mV": "ملي فولت",
        "kV": "كيلو فولت",
        "MΩ": "ميجا أوم",
        "mW": "ملي واط",
        "MW": "ميجا واط",
        "dB": "ديسيبل",
        "kN": "كيلو نيوتن",
        "MPa": "ميجا باسكال",
        "hPa": "هيكتو باسكال",
        "kPa": "كيلو باسكال",
        "N·m": "نيوتن متر",
        "rad/s": "راديان في الثانية",
        "eV": "إلكترون فولت",
        "J/kg·℃": "جول لكل كيلوجرام درجة مئوية",
        "ms": "ملي ثانية",
        "μs": "ميكرو ثانية",
        "ns": "نانو ثانية",
        "pF": "بيكو فاراد",
        "μF": "ميكرو فاراد",
        "nF": "نانو فاراد",
        "mH": "ملي هنري",
        "μH": "ميكرو هنري",
    }


def ar_unit_single() -> dict[str, str]:
    return {
        "s": "ثانية",
        "h": "ساعة",
        "°": "درجة",
        "′": "دقيقة",
        "″": "ثانية",
        "°C": "درجة مئوية",
        "℃": "درجة مئوية",
        "°F": "درجة فهرنهايت",
        "℉": "درجة فهرنهايت",
        "K": "كلفن",
        "m": "متر",
        "g": "جرام",
        "t": "طن",
        "L": "لتر",
        "l": "لتر",
        "V": "فولت",
        "A": "أمبير",
        "W": "واط",
        "Ω": "أوم",
        "Hz": "هرتز",
        "T": "تسلا",
        "H": "هنري",
        "C": "كولوم",
        "F": "فاراد",
        "N": "نيوتن",
        "J": "جول",
        "Pa": "باسكال",
        "cal": "سعرة حرارية",
        "mol": "مول",
        "b": "بت",
        "B": "بايت",
        "M": "مليون",
        "G": "جاوس",
    }


def bn_digit_names() -> list[str]:
    return [
        "শূন্য ",
        "এক ",
        "দুই ",
        "তিন ",
        "চার ",
        "পাঁচ ",
        "ছয় ",
        "সাত ",
        "আট ",
        "নয় ",
    ]


def bn_month_names() -> list[str]:
    """Gregorian months in Bengali (`date_iso_en` when locale is `bn`)."""
    return [
        "জানুয়ারি",
        "ফেব্রুয়ারি",
        "মার্চ",
        "এপ্রিল",
        "মে",
        "জুন",
        "জুলাই",
        "আগস্ট",
        "সেপ্টেম্বর",
        "অক্টোবর",
        "নভেম্বর",
        "ডিসেম্বর",
    ]


def bn_currency_map() -> dict[str, str]:
    return {
        "$": "ডলার",
        "USD": "ডলার",
        "€": "ইউরো",
        "EUR": "ইউরো",
        "£": "পাউন্ড",
        "￡": "পাউন্ড",
        "GBP": "পাউন্ড",
        "₽": "রুবেল",
        "RUB": "রুবেল",
        "₩": "ওয়ন",
        "KRW": "ওয়ন",
        "¥": "টাকা",
        "￥": "টাকা",
        "CNY": "চীনা ইউয়ান",
        "৳": "টাকা",
    }


def bn_unit_composite() -> dict[str, str]:
    return {
        "kg": "কিলোগ্রাম",
        "mm": "মিলিমিটার",
        "kPa": "কিলোপাস্কাল",
        "min": "মিনিট",
        "km": "কিলোমিটার",
        "nm": "ন্যানোমিটার",
        "μm": "মাইক্রোমিটার",
        "km/h": "কিলোমিটার প্রতি ঘণ্টা",
        "ml": "মিলিলিটার",
        "mL": "মিলিলিটার",
        "mA": "মিলিঅ্যাম্পিয়ার",
        "kW·h": "কিলোওয়াট-ঘণ্টা",
        "kΩ": "কিলোওম",
        "kW": "কিলোওয়াট",
        "cm/s": "সেন্টিমিটার প্রতি সেকেন্ড",
        "m/s²": "মিটার প্রতি সেকেন্ড বর্গ",
        "rad/s": "রেডিয়ান প্রতি সেকেন্ড",
        "rpm": "প্রতি মিনিটে ঘূর্ণন",
        "mV": "মিলিভোল্ট",
        "kV": "কিলোভোল্ট",
        "MΩ": "মেগাওম",
        "pF": "পিকোফ্যারাড",
        "μF": "মাইক্রোফ্যারাড",
        "nF": "ন্যানোফ্যারাড",
        "mH": "মিলিহেনরি",
        "μH": "মাইক্রোহেনরি",
        "mW": "মিলিওয়াট",
        "MW": "মেগাওয়াট",
        "dB": "ডেসিবেল",
        "kN": "কিলোনিউটন",
        "MPa": "মেগাপাস্কাল",
        "hPa": "হেক্টোপাস্কাল",
        "N·m": "নিউটন-মিটার",
        "kg/m³": "কিলোগ্রাম প্রতি ঘনমিটার",
        "dm": "ডেসিমিটার",
        "cm": "সেন্টিমিটার",
        "m²": "বর্গ মিটার",
        "cm²": "বর্গ সেন্টিমিটার",
        "m³": "ঘন মিটার",
        "cm³": "ঘন সেন্টিমিটার",
        "kJ": "কিলোজুল",
        "kcal": "কিলোক্যালোরি",
        "eV": "ইলেকট্রনভোল্ট",
        "J/(kg·℃)": "জুল প্রতি কিলোগ্রাম সেলসিয়াস",
        "kHz": "কিলোহার্জ",
        "MHz": "মেগাহার্জ",
        "GHz": "গিগাহার্জ",
        "ms": "মিলিসেকেন্ড",
        "μs": "মাইক্রোসেকেন্ড",
        "ns": "ন্যানোসেকেন্ড",
        "m/s": "মিটার প্রতি সেকেন্ড",
    }


def bn_unit_single() -> dict[str, str]:
    return {
        "s": "সেকেন্ড",
        "h": "ঘণ্টা",
        "m": "মিটার",
        "g": "গ্রাম",
        "°": "ডিগ্রি",
        "′": "মিনিট",
        "″": "সেকেন্ড",
        "'": "মিনিট",
        '"': "সেকেন্ড",
        "Pa": "পাস্কাল",
        "℃": "সেলসিয়াস",
        "℉": "ফারেনহাইট",
        "Ω": "ওম",
        "㎡": "বর্গ মিটার",
        "A": "অ্যাম্পিয়ার",
        "V": "ভোল্ট",
        "W": "ওয়াট",
        "Hz": "হার্জ",
        "cal": "ক্যালোরি",
        "N": "নিউটন",
        "J": "জুল",
        "T": "টেসলা",
        "K": "কেলভিন",
        "C": "কুলম্ব",
        "F": "ফ্যারাড",
        "H": "হেনরি",
        "G": "গ্যাউস",
        "L": "লিটার",
        "l": "লিটার",
        "t": "টন",
        "b": "বিট",
        "B": "বাইট",
        "°C": "সেলসিয়াস",
        "°F": "ফারেনহাইট",
        "mol": "মোল",
    }


def ru_digit_names() -> list[str]:
    return [
        "ноль ",
        "один ",
        "два ",
        "три ",
        "четыре ",
        "пять ",
        "шесть ",
        "семь ",
        "восемь ",
        "девять ",
    ]


def ru_month_names() -> list[str]:
    return [
        "января",
        "февраля",
        "марта",
        "апреля",
        "мая",
        "июня",
        "июля",
        "августа",
        "сентября",
        "октября",
        "ноября",
        "декабря",
    ]


def ru_currency_map() -> dict[str, str]:
    return {
        "$": "долларов",
        "USD": "долларов",
        "€": "евро",
        "EUR": "евро",
        "£": "фунтов",
        "￡": "фунтов",
        "GBP": "фунтов",
        "₽": "рублей",
        "RUB": "рублей",
        "₩": "вон",
        "KRW": "вон",
        "¥": "юаней",
        "￥": "юаней",
        "CNY": "юаней",
    }


def ru_unit_composite() -> dict[str, str]:
    return {
        "kg": "килограммов",
        "mm": "миллиметров",
        "kPa": "килопаскалей",
        "min": "минут",
        "km": "километров",
        "nm": "нанометров",
        "μm": "микрометров",
        "km/h": "километров в час",
        "ml": "миллилитров",
        "mL": "миллилитров",
        "mA": "миллиампер",
        "kW·h": "киловатт-часов",
        "kΩ": "килоом",
        "kW": "киловатт",
        "cm/s": "сантиметров в секунду",
        "m/s²": "метров в секунду в квадрате",
        "rad/s": "радиан в секунду",
        "rpm": "оборотов в минуту",
        "mV": "милливольт",
        "kV": "киловольт",
        "MΩ": "мегаом",
        "pF": "пикофарад",
        "μF": "микрофарад",
        "nF": "нанофарад",
        "mH": "миллигенри",
        "μH": "микрогенри",
        "mW": "милливатт",
        "MW": "мегаватт",
        "dB": "децибел",
        "kN": "килоньютонов",
        "MPa": "мегапаскалей",
        "hPa": "гектопаскалей",
        "N·m": "ньютон-метров",
        "kg/m³": "килограммов на кубический метр",
        "dm": "дециметров",
        "cm": "сантиметров",
        "m²": "квадратных метров",
        "cm²": "квадратных сантиметров",
        "m³": "кубических метров",
        "cm³": "кубических сантиметров",
        "kJ": "килоджоулей",
        "kcal": "килокалорий",
        "eV": "электронвольт",
        "J/(kg·℃)": "джоулей на килограмм по Цельсию",
        "kHz": "килогерц",
        "MHz": "мегагерц",
        "GHz": "гигагерц",
        "ms": "миллисекунд",
        "μs": "микросекунд",
        "ns": "наносекунд",
        "m/s": "метров в секунду",
    }


def ru_unit_single() -> dict[str, str]:
    return {
        "s": "секунд",
        "h": "часов",
        "m": "метров",
        "g": "граммов",
        "°": "градусов",
        "′": "минут",
        "″": "секунд",
        "'": "минут",
        '"': "секунд",
        "Pa": "паскалей",
        "℃": "градусов Цельсия",
        "℉": "градусов Фаренгейта",
        "Ω": "ом",
        "㎡": "квадратных метров",
        "A": "ампер",
        "V": "вольт",
        "W": "ватт",
        "Hz": "герц",
        "cal": "калорий",
        "N": "ньютонов",
        "J": "джоулей",
        "T": "тесла",
        "K": "кельвинов",
        "C": "кулонов",
        "F": "фарад",
        "H": "генри",
        "G": "гаусс",
        "L": "литров",
        "l": "литров",
        "t": "тонн",
        "b": "бит",
        "B": "байт",
        "°C": "градусов Цельсия",
        "°F": "градусов Фаренгейта",
        "mol": "моль",
        "M": "миллионов",
    }


def zh_unit_single() -> dict[str, str]:
    return {
        "s": "秒",
        "h": "小时",
        "m": "米",
        "g": "克",
        "°": "度",
        "′": "分",
        "″": "秒",
        "Pa": "帕",
        "℃": "摄氏度",
        "℉": "华氏度",
        "Ω": "欧姆",
        "㎡": "平方米",
        "A": "安培",
        "V": "伏",
        "W": "瓦",
        "Hz": "赫兹",
        "cal": "卡路里",
        "N": "牛顿",
        "J": "焦耳",
        "T": "特斯拉",
        "K": "开尔文",
        "C": "库仑",
        "F": "法拉",
        "G": "高斯",
        "L": "升",
        "l": "升",
        "t": "吨",
        "b": "比特",
        "B": "字节",
        "°C": "摄氏度",
        "°F": "华氏度",
        "mol": "摩尔",
    }


def en_pipeline_rules() -> list[dict]:
    """Ordered pipeline (mirrors legacy en.cpp). Literal tail appended separately."""
    ipv4_walk = {
        "op": "walk",
        "g": 0,
        "map": {
            ".": {"en": "dot ", "zh": "点", "ar": "نقطة "},
            ":": {"en": "colon ", "zh": "冒号", "ar": "نقطتان "},
        },
    }
    ipv6_walk = {
        "op": "walk",
        "g": 0,
        "map": {":": {"en": "colon ", "zh": "冒号", "ar": "نقطتان "}},
        "other_en_space": True,
    }
    return [
        # International phone (+44 7756 165803) precedes the dash-phone catch-all so
        # the '+' is spoken as "plus" and each digit is read out.
        exec_rule(
            R("EN_INTL_PHONE"),
            [{"op": "walk_readout", "g": 0, "letter": "spaced", "map": PHONE_EN_MAP}],
        ),
        exec_rule(R("SH_PHONE_DASH"), [{"op": "digits", "g": 0}]),
        # URL / email must precede other rules so dots/colons/slashes inside them
        # aren't consumed by number, time, or unit patterns.
        exec_rule(R("SH_EMAIL"), [{"op": "walk_readout", "g": 1, "letter": "spaced", "map": EMAIL_EN_MAP}]),
        exec_rule(
            R("EN_URL_OR_DOMAIN"),
            [{"op": "walk_readout", "g": 0, "letter": "spaced", "map": URL_EN_MAP}],
        ),
        exec_rule(R("SH_IPV4"), [ipv4_walk]),
        exec_rule(R("EN_CALL_DIAL"), [LIT("dial "), {"op": "digits", "g": 1}]),
        # Timezone offset (UTC-8:00 / GMT+5) must precede time and minus-sign rules.
        exec_rule(
            R("EN_TIMEZONE_UTC"),
            [{"op": "timezone_en", "zone_g": 1, "sign_g": 2, "hour_g": 3, "minute_g": 4}],
        ),
        # Time rules run before SH_IPV6 because both consume ":"; "H:MM[:SS] AM/PM"
        # must win over IPv6 alternatives that otherwise gobble "H:MM:SS" as hex blocks.
        exec_rule(R("EN_TIME_12"), [{"op": "time_en_12h"}]),
        exec_rule(R("EN_TIME_24"), [{"op": "time_en_24h"}]),
        exec_rule(R("SH_IPV6"), [ipv6_walk]),
        exec_rule(R("SH_POW10"), [{"op": "pow10", "g": 1}]),
        exec_rule(R("SH_ROMAN"), [{"op": "roman", "g": 0}]),
        exec_rule(R("SH_INTEGRAL"), [{"op": "integral", "variant": "en", "g1": 1, "g2": 2}]),
        exec_rule(R("EN_DATE_ISO"), [{"op": "date_iso_en", "gy": 1, "gm": 2, "gd": 3}]),
        # Must precede EN_DATE_DMY_LONG, which otherwise consumes "21st September 2020" and
        # leaves "19th-", so "19th-21st September" never matches as a span.
        {"pattern": R("EN_ORDINAL_DASH_RANGE_TO_MONTH"), "replace": r" $1 to $2 "},
        exec_rule(
            R("EN_DATE_DMY_LONG"),
            [
                LIT(" "),
                {"op": "grp", "g": 1, "as": "ordinal"},
                LIT(" "),
                {"op": "grp", "g": 2, "as": "month_en_full"},
                LIT(", "),
                {"op": "grp", "g": 3, "as": "year_en_prose"},
                LIT(" "),
            ],
        ),
        exec_rule(
            R("EN_DATE_MDY_LONG"),
            [
                LIT(" "),
                {"op": "grp", "g": 1, "as": "month_en_full"},
                LIT(" "),
                {"op": "grp", "g": 2, "as": "ordinal"},
                LIT(", "),
                {"op": "grp", "g": 3, "as": "year_en_prose"},
                LIT(" "),
            ],
        ),
        exec_rule(
            R("EN_DATE_DMY_SHORT"),
            [
                LIT(" "),
                {"op": "grp", "g": 1, "as": "ordinal"},
                LIT(" "),
                {"op": "grp", "g": 2, "as": "month_en_full"},
                LIT(", "),
                {"op": "grp", "g": 3, "as": "year_2digit"},
                LIT(" "),
            ],
        ),
        exec_rule(
            R("EN_DATE_MDY_SHORT"),
            [
                LIT(" "),
                {"op": "grp", "g": 1, "as": "month_en_full"},
                LIT(" "),
                {"op": "grp", "g": 2, "as": "ordinal"},
                LIT(", "),
                {"op": "grp", "g": 3, "as": "year_2digit"},
                LIT(" "),
            ],
        ),
        exec_rule(R("EN_DATE_DMY_NUMERIC"), [{"op": "date_iso_en", "gy": 3, "gm": 2, "gd": 1}]),
        # Standalone ~1200-1999 years (e.g. "his 1996" / "his1996") before fractions, units, and loose_num.
        exec_rule(
            R("EN_YEAR_PROSE_SPLIT"),
            [
                {"op": "grp", "g": 1, "as": "spellout"},
                LIT(" "),
                {"op": "grp", "g": 2, "as": "spellout"},
                LIT(" "),
            ],
        ),
        exec_rule(
            R("EN_YEAR_PROSE_SPLIT_LETTER"),
            [
                LIT(" "),
                {"op": "grp", "g": 1, "as": "spellout"},
                LIT(" "),
                {"op": "grp", "g": 2, "as": "spellout"},
                LIT(" "),
            ],
        ),
        {"pattern": r"\b24\s*/\s*7\b", "replace": " twenty-four seven "},
        exec_rule(R("EN_FRACTION_MIXED"), [{"op": "fraction_en", "whole_g": 1, "num_g": 2, "den_g": 3}]),
        exec_rule(R("EN_FRACTION_BARE"), [{"op": "fraction_en", "num_g": 1, "den_g": 2}]),
        *[
            exec_rule(
                r"(?:[’']|(\d{1,3}))" + _tens + r"0[Ss]\b",
                [
                    LIT(" "),
                    {"op": "grp", "g": 1, "as": "spellout"},
                    LIT(" " + _word + " "),
                ],
            )
            for _tens, _word in (
                ("2", "twenties"),
                ("3", "thirties"),
                ("4", "forties"),
                ("5", "fifties"),
                ("6", "sixties"),
                ("7", "seventies"),
                ("8", "eighties"),
                ("9", "nineties"),
            )
        ],
        {"pattern": R("EN_EM_DASH"), "replace": " "},
        {"pattern": "&", "replace": " and "},
        {"pattern": r"[{}]", "replace": " "},
        {"pattern": r"[•‣◦▪▫◆◇★☆※]", "replace": " "},
        {"pattern": r"\bNo\.\s*(?=\d)", "replace": " number "},
        # Before EN_PRICE_RANGE so "01-101" inside "A-01-101" is not read as a numeric span.
        exec_rule(
            R("EN_LETTER_BUILDING_UNIT"),
            [
                LIT(" "),
                {"op": "grp", "g": 1, "as": "raw"},
                LIT(" "),
                {"op": "grp", "g": 2, "as": "spellout"},
                LIT(" "),
                {"op": "grp", "g": 3, "as": "digits"},
                LIT(" "),
            ],
        ),
        exec_rule(
            R("EN_PRICE_RANGE"),
            [
                LIT(" "),
                {"op": "grp", "g": 1, "as": "raw"},
                {"op": "grp", "g": 2, "as": "raw"},
                LIT(" to "),
                {"op": "grp", "g": 3, "as": "raw"},
                {"op": "grp", "g": 4, "as": "raw"},
                LIT(" "),
            ],
        ),
        exec_rule(R("SH_MINUS_DIGIT"), [{"op": "minus", "g": 1}]),
        exec_rule(
            R("EN_LABEL_ROOM"),
            [
                {
                    "op": "label_suffix",
                    "g1": 1,
                    "g2": 2,
                    "g3": 3,
                    "label_case": "lower",
                    "number_mode": "spellout",
                    "suffix_case": "upper",
                }
            ],
        ),
        exec_rule(
            R("EN_CURRENCY_SCALED"),
            [
                {
                    "op": "currency",
                    "sym_g1": 1,
                    "num_g": 2,
                    "scale_g": 3,
                    "sym_g2": 4,
                    "scale_map": {
                        "K": "thousand",
                        "k": "thousand",
                        "M": "million",
                        "m": "million",
                        "B": "billion",
                        "bn": "billion",
                        "T": "trillion",
                        "tn": "trillion",
                    },
                    "strip": ",",
                    "pad_en": True,
                }
            ],
        ),
        exec_rule(
            R("EN_CURRENCY_ALT"),
            [{"op": "currency", "sym_g1": 1, "num_g": 2, "sym_g2": 3, "strip": ",", "pad_en": True}],
        ),
        exec_rule(R("EN_PERCENT"), [{"op": "spellout_affix", "g": 1, "prefix": "", "suffix": " percent "}]),
        exec_rule(R("EN_PER_MILLE"), [{"op": "spellout_affix", "g": 1, "prefix": "", "suffix": " per mille "}]),
        exec_rule(R("EN_INCH_UNIT"), [{"op": "unit", "table": "composite", "num_g": 1, "unit_g": 2, "pad_en": True}]),
        # Aircraft designators (Airbus A###, 787 Dreamliner) before loose_num cardinal readout.
        exec_rule(
            R("EN_AIRBUS_A_SERIES"),
            [
                {"op": "grp", "g": 1, "as": "raw"},
                LIT(" "),
                {"op": "aircraft_series_en", "variant": "airbus_a", "num_g": 2},
                LIT(" "),
            ],
        ),
        exec_rule(
            R("EN_BOEING_787_DREAMLINER"),
            [
                {"op": "aircraft_series_en", "variant": "boeing_7xx", "num_g": 1},
                LIT(" "),
                {"op": "grp", "g": 2, "as": "raw"},
                LIT(" "),
            ],
        ),
        # Before composite/single unit rules so "10,900ft" is one comma-separated amount, not "900ft".
        exec_rule(R("EN_NUM_THOUSANDS"), [{"op": "loose_num", "g": 0, "strip": ","}]),
        # Before EN_SINGLE_UNIT: "47s" after AK must not become "forty-seven seconds".
        exec_rule(
            R("EN_LETTER_PLUS_NUM_PLURAL_S"),
            [
                LIT(" "),
                {"op": "grp", "g": 1, "as": "spellout"},
                LIT("s "),
            ],
        ),
        exec_rule(R("EN_COMPOSITE_UNIT"), [{"op": "unit", "table": "composite", "num_g": 1, "unit_g": 2, "pad_en": True}]),
        exec_rule(R("EN_SINGLE_UNIT"), [{"op": "unit", "table": "single", "num_g": 1, "unit_g": 2, "pad_en": True}]),
        {
            "pattern": r"\s*/\s*(?=(?:months?|days?|weeks?|years?|hours?|minutes?|seconds?|mins?|secs?|hrs?|yrs?|wks?|mos?|capita|person|head|unit|item|kg|lbs?|liters?|litres?|gallons?|ml|gram|ounces?)\b)",
            "replace": " per ",
        },
        exec_rule(R("EN_ORDINAL"), [{"op": "ordinal", "g": 1, "pad_before": " ", "pad_after": " "}]),
        exec_rule(R("SH_LOOSE_DECIMAL"), [{"op": "loose_num", "g": 0}]),
    ]


def zh_pipeline_rules() -> list[dict]:
    ipv4_walk = {
        "op": "walk",
        "g": 0,
        "map": {
            ".": {"en": "dot ", "zh": "点", "ar": "نقطة "},
            ":": {"en": "colon ", "zh": "冒号", "ar": "نقطتان "},
        },
    }
    ipv6_walk = {
        "op": "walk",
        "g": 0,
        "map": {":": {"en": "colon ", "zh": "冒号", "ar": "نقطتان "}},
        "other_en_space": True,
    }
    # MAC (EUI-48): digits → 中文数位; colon → 冒号; hex letters A–F → 直接读拉丁字母（空格分隔），
    # 不用「必西迪伊艾夫」类中文音译。须在 ZH_TIME_24 等规则之前匹配（避免 00:50 被误当时间）。
    mac_eui48_readout = {
        "op": "walk_readout",
        "g": 0,
        "letter": "spaced",
        "map": {
            ":": "冒号 ",
        },
    }
    # KFR-35GW: hyphen → 杠; digits 三五; letters kept (walk_readout).
    ac_model_readout = {
        "op": "walk_readout",
        "g": 1,
        "letter": "keep",
        "map": {"-": "杠"},
    }
    latin_num_hyphen_silent = {
        "op": "walk_readout",
        "g": 1,
        "letter": "keep",
        "map": {"-": ""},
    }
    bank_card_walk = {
        "op": "walk",
        "g": 2,
        "map": {
            " ": "",
            "\u3000": "",
        },
    }
    return [
        exec_rule(R("ZH_DECADE"), [{"op": "decade_zh", "g": 1}]),
        exec_rule(
            R("ZH_DECADE_HOU"),
            [
                {"op": "grp", "g": 1, "as": "digits"},
                LIT("后"),
            ],
        ),
        exec_rule(R("SH_EMAIL"), [{"op": "walk_readout", "g": 1, "letter": "spaced", "map": EMAIL_ZH_MAP}]),
        {"pattern": R("ZH_TLD_COM_CN"), "replace": "点 com 点 c n"},
        exec_rule(
            R("ZH_TLD_SUFFIX"),
            [{"op": "walk_readout", "g": 1, "letter": "keep", "map": VERSION_ZH_MAP}],
        ),
        exec_rule(
            R("ZH_URL_OR_DOMAIN"),
            [{"op": "walk_readout", "g": 0, "letter": "spaced", "map": URL_ZH_MAP}],
        ),
        exec_rule(
            R("ZH_LINUX_DISTRO_FILENAME"),
            [
                {"op": "walk", "g": 1, "map": {"-": " ", ".": "", "_": ""}},
                LIT("点"),
                {"op": "walk_readout", "g": 2, "letter": "keep", "map": {}},
            ],
        ),
        exec_rule(
            R("ZH_USSD_STAR_HASH_DIGITS_HASH"),
            [
                LIT("星号，井号，"),
                {"op": "grp", "g": 1, "as": "digits"},
                LIT("，井号"),
            ],
        ),
        {"pattern": R("ZH_HASHTAG_HUATI_WRAPPED"), "replace": " $1 "},
        {"pattern": R("ZH_HASHTAG_INLINE_CJK_WRAPPED"), "replace": " $1 "},
        {"pattern": R("ZH_COMMENT_BLOCK_C_EMPTY"), "replace": "斜杠星号星号斜杠"},
        {"pattern": "注释中//", "replace": "注释中，双斜杠"},
        {"pattern": R("ZH_COMMENT_DOUBLE_SLASH"), "replace": "双斜杠"},
        {"pattern": R("ZH_PAREN_LATIN_ENUM_LETTER"), "replace": " $1 "},
        {"pattern": R("ZH_PAREN_FULLWIDTH_LATIN_ENUM"), "replace": " $1 "},
        {"pattern": R("ZH_COMMA_BEFORE_LATIN_ENUM"), "replace": ""},
        # +886-2-2345-6789 → 加八八六二二三四五六七八九（连字符不作「负」）.
        exec_rule(
            R("ZH_PHONE_INTL_886"),
            [
                LIT("加"),
                {"op": "digits", "g": 0},
            ],
        ),
        exec_rule(
            R("ZH_PHONE_INTL_86_DASH"),
            [
                LIT("加八六"),
                LIT("-"),
                {"op": "digits", "g": 1},
            ],
        ),
        exec_rule(
            R("ZH_PHONE_INTL_81"),
            [
                LIT("加"),
                {"op": "digits", "g": 0},
            ],
        ),
        exec_rule(
            R("ZH_PHONE_INTL_DASH_GENERIC"),
            [
                LIT("加"),
                {"op": "digits", "g": 1},
            ],
        ),
        # 978/979-… ISBN 须先于 SH_PHONE_DASH（否则 979-8-7654-3210 会被 \d-\d{4}-\d{4} 截断）.
        exec_rule(R("ZH_ISBN_978_DASH"), [{"op": "digits", "g": 0}]),
        exec_rule(
            R("ZH_VIN_CTX"),
            [
                {"op": "grp", "g": 1, "as": "raw"},
                {"op": "walk_readout", "g": 2, "letter": "keep", "map": {}},
            ],
        ),
        exec_rule(R("SH_PHONE_DASH"), [{"op": "digits", "g": 0}]),
        exec_rule(
            R("ZH_PHONE_TAIL_CTX"),
            [
                {"op": "grp", "g": 1, "as": "raw"},
                {"op": "grp", "g": 2, "as": "digits"},
            ],
        ),
        exec_rule(
            R("ZH_PHONE_EXT_IS"),
            [
                {"op": "grp", "g": 1, "as": "raw"},
                {"op": "grp", "g": 2, "as": "digits"},
            ],
        ),
        exec_rule(
            R("ZH_PHONE_EXT_FENJI"),
            [
                {"op": "grp", "g": 1, "as": "raw"},
                {"op": "grp", "g": 2, "as": "digits"},
            ],
        ),
        exec_rule(
            R("ZH_PHONE_ZHUAN_EXT"),
            [
                {"op": "grp", "g": 1, "as": "raw"},
                {"op": "grp", "g": 2, "as": "digits"},
            ],
        ),
        exec_rule(
            R("ZH_BANK_CARD_CTX"),
            [
                {"op": "grp", "g": 1, "as": "raw"},
                bank_card_walk,
            ],
        ),
        exec_rule(
            R("ZH_CARDNUM_KAH_COMMA_CTX"),
            [
                {"op": "grp", "g": 1, "as": "raw"},
                {"op": "walk", "g": 2, "map": {" ": "", "\u3000": ""}},
            ],
        ),
        exec_rule(
            R("ZH_CARDNUM_SPACED_AFTER_KAH"),
            [
                {"op": "grp", "g": 1, "as": "raw"},
                {"op": "walk", "g": 2, "map": {" ": "", "\u3000": ""}},
            ],
        ),
        exec_rule(
            R("ZH_SECRET_CODE_CTX"),
            [
                {"op": "grp", "g": 1, "as": "raw"},
                {"op": "grp", "g": 2, "as": "digits"},
            ],
        ),
        exec_rule(R("SH_IPV4"), [ipv4_walk]),
        exec_rule(R("ZH_MAC_EUI48"), [mac_eui48_readout]),
        exec_rule(
            R("ZH_CALL_VERB_NUM"),
            [
                {"op": "grp", "g": 1, "as": "raw"},
                {"op": "grp", "g": 2, "as": "digits"},
            ],
        ),
        exec_rule(R("ZH_DIAL"), [LIT("拨打"), {"op": "digits", "g": 1}]),
        exec_rule(R("ZH_TIME_24_RANGE"), [{"op": "time_zh_range"}]),
        exec_rule(
            R("ZH_TIME_AMPM"),
            [{"op": "time_zh_ampm", "hour_g": 1, "minute_g": 2, "second_g": 3, "period_g": 4}],
        ),
        exec_rule(R("ZH_TIME_24"), [{"op": "time_zh_24"}]),
        exec_rule(
            R("ZH_STD_GBT_CODE"),
            [
                {"op": "grp", "g": 1, "as": "raw"},
                LIT(" "),
                {"op": "grp", "g": 2, "as": "digits"},
                {"op": "grp", "g": 3, "as": "digits"},
            ],
        ),
        *zh_formula_identity_pre_arith(),
        exec_rule(
            R("ZH_ADD_EQ"),
            [
                {"op": "grp", "g": 1, "as": "spellout"},
                LIT("加"),
                {"op": "grp", "g": 2, "as": "spellout"},
                LIT("等于"),
                {"op": "grp", "g": 3, "as": "spellout"},
            ],
        ),
        exec_rule(
            R("ZH_SUB_EQ"),
            [
                {"op": "grp", "g": 1, "as": "spellout"},
                LIT("减"),
                {"op": "grp", "g": 2, "as": "spellout"},
                LIT("等于"),
                {"op": "grp", "g": 3, "as": "spellout"},
            ],
        ),
        exec_rule(
            R("ZH_FORMULA_FRAC_EQ_RHS"),
            [
                {"op": "grp", "g": 1, "as": "raw"},
                {"op": "grp", "g": 2, "as": "raw"},
                LIT("除以"),
                {"op": "grp", "g": 3, "as": "raw"},
            ],
        ),
        exec_rule(
            R("ZH_FORMULA_FRAC_EQ_LHS"),
            [
                {"op": "grp", "g": 1, "as": "raw"},
                LIT("除以"),
                {"op": "grp", "g": 2, "as": "raw"},
                {"op": "grp", "g": 3, "as": "raw"},
            ],
        ),
        exec_rule(
            R("ZH_NEG_RECIPROC_N"),
            [
                LIT("负"),
                {"op": "grp", "g": 1, "as": "raw"},
                LIT("分之一"),
            ],
        ),
        exec_rule(
            R("ZH_SIMPLE_FRAC"),
            [
                {"op": "grp", "g": 2, "as": "spellout"},
                LIT("分之"),
                {"op": "grp", "g": 1, "as": "spellout"},
            ],
        ),
        exec_rule(
            R("ZH_DATE_DMY_DASH"),
            [
                {
                    "op": "date_ymd",
                    "g1": 3,
                    "g2": 2,
                    "g3": 1,
                    "year_mode": "digits",
                    "month_mode": "spellout",
                    "day_mode": "spellout",
                    "year_suffix": "年",
                    "month_suffix": "月",
                    "day_suffix": "号",
                }
            ],
        ),
        exec_rule(
            R("ZH_PERCENT_RANGE"),
            [
                {"op": "spellout_affix", "g": 1, "prefix": "百分之", "suffix": ""},
                LIT("至"),
                {"op": "spellout_affix", "g": 2, "prefix": "百分之", "suffix": ""},
            ],
        ),
        exec_rule(
            R("ZH_NUM_RANGE_TILDE_MU"),
            [
                {"op": "grp", "g": 1, "as": "spellout"},
                LIT("至"),
                {"op": "grp", "g": 2, "as": "spellout"},
                {"op": "grp", "g": 3, "as": "raw"},
            ],
        ),
        exec_rule(
            R("ZH_NUM_RANGE_TILDE_MM"),
            [
                {"op": "grp", "g": 1, "as": "spellout"},
                LIT("至"),
                {"op": "grp", "g": 2, "as": "spellout"},
                {"op": "grp", "g": 3, "as": "raw"},
            ],
        ),
        exec_rule(
            R("ZH_NUM_RANGE_HYPHEN_KG"),
            [
                {"op": "grp", "g": 1, "as": "spellout"},
                LIT("至"),
                {"op": "grp", "g": 2, "as": "spellout"},
                LIT("千克"),
            ],
        ),
        exec_rule(
            R("ZH_HOUR_RANGE_CTX"),
            [
                {"op": "grp", "g": 1, "as": "spellout"},
                LIT("至"),
                {"op": "grp", "g": 2, "as": "spellout"},
                {"op": "grp", "g": 3, "as": "raw"},
            ],
        ),
        exec_rule(
            R("ZH_AGE_RANGE_CTX"),
            [
                {"op": "grp", "g": 1, "as": "spellout"},
                LIT("至"),
                {"op": "grp", "g": 2, "as": "spellout"},
                {"op": "grp", "g": 3, "as": "raw"},
            ],
        ),
        exec_rule(
            R("ZH_COPYRIGHT_YEAR_RANGE"),
            [
                {"op": "grp", "g": 1, "as": "raw"},
                {"op": "grp", "g": 2, "as": "digits"},
                LIT("杠"),
                {"op": "grp", "g": 3, "as": "digits"},
            ],
        ),
        exec_rule(
            R("ZH_COPYRIGHT_AFTER_BANQUAN_PHRASE"),
            [
                LIT(" "),
                {"op": "grp", "g": 1, "as": "digits"},
            ],
        ),
        exec_rule(
            R("ZH_COPYRIGHT_WORD_SINGLE_YEAR"),
            [
                {"op": "grp", "g": 1, "as": "raw"},
                LIT(" "),
                {"op": "grp", "g": 3, "as": "digits"},
            ],
        ),
        exec_rule(
            R("ZH_SCI_E_NEG"),
            [
                {"op": "grp", "g": 1, "as": "spellout"},
                LIT("乘以十的负"),
                {"op": "grp", "g": 2, "as": "spellout"},
                LIT("次方"),
            ],
        ),
        exec_rule(
            R("ZH_SCI_E_POS"),
            [
                {"op": "grp", "g": 1, "as": "spellout"},
                LIT("乘以十的"),
                {"op": "grp", "g": 2, "as": "spellout"},
                LIT("次方"),
            ],
        ),
        exec_rule(
            R("ZH_SCI_E_UNSIGNED"),
            [
                {"op": "grp", "g": 1, "as": "spellout"},
                LIT("乘以十的"),
                {"op": "grp", "g": 2, "as": "spellout"},
                LIT("次方"),
            ],
        ),
        exec_rule(R("ZH_APPLIANCE_MODEL"), [ac_model_readout]),
        exec_rule(R("ZH_WIFI_HYPHEN_SILENT"), [latin_num_hyphen_silent]),
        exec_rule(R("ZH_TECH_MODEL_HYPHEN"), [latin_num_hyphen_silent]),
        exec_rule(R("ZH_TYPE_HYPHEN_TERM"), [latin_num_hyphen_silent]),
        exec_rule(R("ZH_LATIN_NUM_HYPHEN_CODE"), [latin_num_hyphen_silent]),
        exec_rule(R("SH_MINUS_DIGIT"), [{"op": "minus", "g": 1}]),
        exec_rule(
            R("ZH_RATIO_COLON"),
            [
                {"op": "grp", "g": 1, "as": "spellout"},
                LIT("比"),
                {"op": "grp", "g": 2, "as": "spellout"},
            ],
        ),
        exec_rule(R("SH_IPV6"), [ipv6_walk]),
        exec_rule(R("SH_POW10"), [{"op": "pow10", "g": 1}]),
        *zh_unicode_roman_replace_rules(),
        exec_rule(R("SH_ROMAN"), [{"op": "roman", "g": 0}]),
        exec_rule(R("SH_INTEGRAL"), [{"op": "integral", "variant": "zh", "g1": 1, "g2": 2}]),
        exec_rule(
            R("ZH_DATE_YMD"),
            [
                {
                    "op": "date_ymd",
                    "g1": 1,
                    "g2": 2,
                    "g3": 3,
                    "year_mode": "digits",
                    "month_mode": "spellout",
                    "day_mode": "spellout",
                    "year_suffix": "年",
                    "month_suffix": "月",
                    "day_suffix": "日",
                }
            ],
        ),
        exec_rule(
            R("ZH_DATE_YMD_DOT"),
            [
                {
                    "op": "date_ymd",
                    "g1": 1,
                    "g2": 2,
                    "g3": 3,
                    "year_mode": "digits",
                    "month_mode": "spellout",
                    "day_mode": "spellout",
                    "year_suffix": "年",
                    "month_suffix": "月",
                    "day_suffix": "日",
                }
            ],
        ),
        exec_rule(
            R("ZH_VERSION_YEAR_COMPARE"),
            [
                {"op": "grp", "g": 1, "as": "digits"},
                LIT("比"),
                {"op": "grp", "g": 2, "as": "digits"},
                {"op": "grp", "g": 3, "as": "raw"},
            ],
        ),
        *zh_formula_identity_post_date(),
        exec_rule(
            R("ZH_STOCK_PAIR_AND"),
            [
                {"op": "grp", "g": 1, "as": "digits"},
                LIT("和"),
                {"op": "grp", "g": 2, "as": "digits"},
            ],
        ),
        exec_rule(
            R("ZH_STOCK_RANGE_CTX"),
            [
                {"op": "grp", "g": 1, "as": "raw"},
                {"op": "grp", "g": 2, "as": "digits"},
                LIT("至"),
                {"op": "grp", "g": 3, "as": "digits"},
            ],
        ),
        exec_rule(R("ZH_STOCK_CODE_IS"), [{"op": "grp", "g": 1, "as": "digits"}]),
        exec_rule(R("ZH_STOCK_CODE_NAME"), [{"op": "grp", "g": 1, "as": "digits"}]),
        exec_rule(R("ZH_STOCK_CODE_FROM_YEAR"), [{"op": "grp", "g": 1, "as": "digits"}]),
        exec_rule(
            R("ZH_FUND_REBALANCE_CODES"),
            [
                {"op": "grp", "g": 1, "as": "raw"},
                {"op": "grp", "g": 2, "as": "digits"},
                {"op": "grp", "g": 3, "as": "raw"},
                {"op": "grp", "g": 4, "as": "digits"},
            ],
        ),
        exec_rule(R("ZH_SIX_DIG_IS_ETF"), [{"op": "grp", "g": 1, "as": "digits"}]),
        exec_rule(
            R("ZH_FUND_CODE_CTX"),
            [
                {"op": "grp", "g": 1, "as": "raw"},
                {"op": "grp", "g": 2, "as": "digits"},
            ],
        ),
        exec_rule(R("ZH_FUND_CODE_BEFORE_NAME"), [{"op": "grp", "g": 1, "as": "digits"}]),
        exec_rule(
            R("ZH_ID_CARD_PREFIX_6"),
            [
                {"op": "grp", "g": 1, "as": "raw"},
                {"op": "grp", "g": 2, "as": "digits"},
            ],
        ),
        exec_rule(
            R("ZH_POSTCODE_CTX"),
            [
                {"op": "grp", "g": 1, "as": "raw"},
                {"op": "grp", "g": 2, "as": "digits"},
            ],
        ),
        exec_rule(R("ZH_POSTCODE_KAITOU"), [{"op": "grp", "g": 1, "as": "digits"}]),
        exec_rule(R("ZH_AREA_CODE_KAITOU"), [{"op": "grp", "g": 1, "as": "digits"}]),
        exec_rule(R("ZH_AREA_CODE_IS_CITY"), [{"op": "grp", "g": 1, "as": "digits"}]),
        exec_rule(
            R("ZH_PROVINCE_CODE_CTX"),
            [
                {"op": "grp", "g": 1, "as": "raw"},
                {"op": "grp", "g": 2, "as": "digits"},
            ],
        ),
        exec_rule(
            R("ZH_ADMIN_DIV_CODE_RANGE"),
            [
                {"op": "grp", "g": 1, "as": "raw"},
                {"op": "grp", "g": 2, "as": "digits"},
                LIT("至"),
                {"op": "grp", "g": 3, "as": "digits"},
            ],
        ),
        exec_rule(
            R("ZH_SERIAL_RANGE_CTX"),
            [
                {"op": "grp", "g": 1, "as": "raw"},
                {"op": "grp", "g": 2, "as": "digits"},
                LIT("至"),
                {"op": "grp", "g": 3, "as": "digits"},
            ],
        ),
        exec_rule(
            R("ZH_PREFECTURE_COUNT_CTX"),
            [
                {"op": "grp", "g": 1, "as": "raw"},
                {"op": "grp", "g": 2, "as": "digits"},
                {"op": "grp", "g": 3, "as": "raw"},
            ],
        ),
        exec_rule(
            R("ZH_FLIGHT_IATA_BEFORE_FLIGHT_WORD"),
            [{"op": "walk_readout", "g": 1, "letter": "spaced", "map": {}}],
        ),
        exec_rule(
            R("ZH_FLIGHT_IATA_AFTER_HANGBAN"),
            [{"op": "walk_readout", "g": 1, "letter": "spaced", "map": {}}],
        ),
        exec_rule(
            R("ZH_FLIGHT_DIGIT_PREFIX"),
            [
                {"op": "grp", "g": 1, "as": "raw"},
                {"op": "grp", "g": 2, "as": "digits"},
            ],
        ),
        exec_rule(
            R("ZH_BOARDING_PASS_CODE"),
            [{"op": "walk_readout", "g": 1, "letter": "keep", "map": {}}],
        ),
        exec_rule(
            R("ZH_GATE_CODE"),
            [{"op": "walk_readout", "g": 1, "letter": "keep", "map": {}}],
        ),
        exec_rule(
            R("ZH_ELECTRONICS_MODEL_CTX"),
            [
                {"op": "grp", "g": 1, "as": "raw"},
                LIT(" "),
                {"op": "grp", "g": 2, "as": "digits"},
            ],
        ),
        exec_rule(
            R("ZH_RESOLUTION_K_CTX"),
            [
                {"op": "grp", "g": 1, "as": "spellout"},
                {"op": "grp", "g": 2, "as": "raw"},
            ],
        ),
        exec_rule(R("ZH_YEAR_2_APOS"), [{"op": "grp", "g": 1, "as": "digits"}, LIT("年")]),
        exec_rule(R("ZH_YEAR_4"), [{"op": "grp", "g": 1, "as": "digits"}, LIT("年")]),
        {"pattern": R("ZH_EM_DASH"), "replace": ""},
        {"pattern": R("ZH_RANGE_DIG"), "replace": r"$1至$2"},
        exec_rule(
            R("ZH_HKD_WAN"),
            [
                {"op": "grp", "g": 2, "as": "spellout"},
                LIT("万港币"),
            ],
        ),
        exec_rule(
            R("ZH_HKD_YI"),
            [
                {"op": "grp", "g": 2, "as": "spellout"},
                LIT("亿港元"),
            ],
        ),
        exec_rule(
            R("ZH_CURRENCY"),
            [{"op": "currency", "two_group": True, "sym_g": 1, "num_g": 2, "strip": ",", "pad_en": False}],
        ),
        # Bare ₽ (e.g. ₽兑美元) — must follow ZH_CURRENCY so ₽100 stays one currency match.
        {"pattern": "₽", "replace": "卢布"},
        # Bare ₩ (e.g. ₩贬值) — must follow ZH_CURRENCY so ₩1000 stays one currency match.
        {"pattern": "₩", "replace": "韩元"},
        # Bare £ / ￡ (e.g. £兑美元) — must follow ZH_CURRENCY so £100 stays one currency match.
        {"pattern": "£|￡", "replace": "英镑"},
        exec_rule(
            R("ZH_YUAN_AMOUNT"),
            [{"op": "currency", "two_group": True, "sym_g": 2, "num_g": 1, "strip": ",", "pad_en": False}],
        ),
        exec_rule(R("ZH_PERCENT"), [{"op": "spellout_affix", "g": 1, "prefix": "百分之", "suffix": ""}]),
        exec_rule(R("ZH_PER_MILLE"), [{"op": "spellout_affix", "g": 1, "prefix": "千分之", "suffix": ""}]),
        exec_rule(R("ZH_DMS"), [{"op": "dms_zh", "g1": 1, "g2": 2, "g3": 3}]),
        exec_rule(
            R("ZH_DM_NOSEC"),
            [
                {"op": "grp", "g": 1, "as": "spellout"},
                LIT("度"),
                {"op": "grp", "g": 2, "as": "spellout"},
                LIT("分"),
            ],
        ),
        {"pattern": R("ZH_HAN_MIDDLEDOT_SEP"), "replace": ""},
        {"pattern": R("ZH_PIPE_DELIM_BETWEEN_CJK"), "replace": "竖线"},
        {"pattern": R("ZH_SINGLE_ANGLE_BRACKET_PAIR"), "replace": "$1"},
        {"pattern": R("ZH_DOUBLE_ANGLE_BRACKET_PAIR"), "replace": "$1"},
        exec_rule(
            R("ZH_PLATE_CN_MIDDOT_SUFFIX"),
            [
                {"op": "grp", "g": 1, "as": "raw"},
                LIT(" "),
                {"op": "walk_readout", "g": 2, "letter": "keep", "map": {}},
            ],
        ),
        *zh_math_symbol_replace_rules(),
        *zh_chemistry_formula_exec_rules(),
        exec_rule(
            R("ZH_SEAT_CTX"),
            [
                {"op": "grp", "g": 1, "as": "raw"},
                {"op": "grp", "g": 2, "as": "spellout"},
                {"op": "grp", "g": 3, "as": "raw"},
            ],
        ),
        exec_rule(
            R("ZH_FLOOR_LOU"),
            [
                {"op": "grp", "g": 1, "as": "digits"},
                LIT("楼"),
            ],
        ),
        exec_rule(
            R("ZH_ROOM_RANGE_THROUGH"),
            [
                {"op": "grp", "g": 1, "as": "digits"},
                {"op": "grp", "g": 2, "as": "raw"},
                {"op": "grp", "g": 3, "as": "digits"},
                {"op": "grp", "g": 4, "as": "raw"},
            ],
        ),
        exec_rule(
            R("ZH_ROOM_NUMBER"),
            [
                {"op": "grp", "g": 1, "as": "digits"},
                {"op": "grp", "g": 2, "as": "raw"},
            ],
        ),
        exec_rule(
            R("ZH_ROOM_LABEL_NUM"),
            [
                {"op": "grp", "g": 1, "as": "raw"},
                {"op": "grp", "g": 2, "as": "digits"},
            ],
        ),
        exec_rule(
            R("ZH_PORTS_TRIPLE"),
            [
                {"op": "grp", "g": 1, "as": "digits"},
                LIT("、"),
                {"op": "grp", "g": 2, "as": "digits"},
                LIT("和"),
                {"op": "grp", "g": 3, "as": "digits"},
                LIT("端口"),
            ],
        ),
        exec_rule(
            R("ZH_PORTS_TRIPLE_DUN"),
            [
                {"op": "grp", "g": 1, "as": "digits"},
                LIT("、"),
                {"op": "grp", "g": 2, "as": "digits"},
                LIT("、"),
                {"op": "grp", "g": 3, "as": "digits"},
                LIT("端口"),
            ],
        ),
        exec_rule(R("ZH_PORT_NUM"), [{"op": "grp", "g": 1, "as": "digits"}]),
        exec_rule(
            R("ZH_CELLULAR_NG"),
            [
                {"op": "grp", "g": 1, "as": "spellout"},
                LIT("G"),
            ],
        ),
        exec_rule(R("ZH_COMPOSITE_UNIT"), [{"op": "unit", "table": "composite", "num_g": 1, "unit_g": 2, "pad_en": False}]),
        exec_rule(R("ZH_SINGLE_UNIT"), [{"op": "unit", "table": "single", "num_g": 1, "unit_g": 2, "pad_en": False}]),
        *zh_vol_cn_mag_suffix_rules(),
        exec_rule(
            R("ZH_VERSION_DOTTED"),
            [{"op": "walk_readout", "g": 1, "letter": "keep", "map": VERSION_ZH_MAP}],
        ),
        exec_rule(
            R("ZH_ALPHA_NUM_CODE"),
            [{"op": "walk_readout", "g": 1, "letter": "keep", "map": CODE_ZH_MAP}],
        ),
        exec_rule(
            R("ZH_PLATE_CN_NUM"),
            [
                {"op": "grp", "g": 1, "as": "raw"},
                LIT(" "),
                {"op": "grp", "g": 2, "as": "digits"},
            ],
        ),
        exec_rule(
            R("ZH_FILENAME_UNDER_DIGITS_EXT"),
            [
                {
                    "op": "walk_readout",
                    "g": 1,
                    "letter": "keep",
                    "map": FILENAME_UNDER_DIGITS_ZH_MAP,
                }
            ],
        ),
        exec_rule(
            R("ZH_FILE_EXT"),
            [{"op": "walk_readout", "g": 1, "letter": "keep", "map": VERSION_ZH_MAP}],
        ),
        {"pattern": R("ZH_PINYIN_HAN"), "action": "pinyin_han_paren_zh"},
        {"pattern": R("ZH_PINYIN_SOLO"), "action": "pinyin_standalone_zh"},
        exec_rule(
            R("ZH_HOTLINE_CMCC_10086"),
            [
                {"op": "grp", "g": 1, "as": "raw"},
                {"op": "grp", "g": 2, "as": "digits"},
            ],
        ),
        exec_rule(
            R("ZH_HOTLINE_CARRIER_10000"),
            [
                {"op": "grp", "g": 1, "as": "raw"},
                {"op": "grp", "g": 2, "as": "digits"},
            ],
        ),
        exec_rule(
            R("ZH_HOTLINE_CONSUMER_12315"),
            [
                {"op": "grp", "g": 1, "as": "raw"},
                {"op": "grp", "g": 2, "as": "digits"},
            ],
        ),
        exec_rule(
            R("ZH_EMERGENCY_CALL_CTX"),
            [
                {"op": "grp", "g": 1, "as": "raw"},
                {"op": "grp", "g": 2, "as": "digits"},
            ],
        ),
        exec_rule(
            R("ZH_HOTLINE_GENERIC"),
            [
                {"op": "grp", "g": 1, "as": "raw"},
                {"op": "grp", "g": 2, "as": "digits"},
            ],
        ),
        *zh_no_rank_replace_rules(),
        exec_rule(R("SH_LOOSE_DECIMAL"), [{"op": "loose_num", "g": 0}]),
    ]


def ar_pipeline_rules() -> list[dict]:
    """Ordered pipeline (ported from legacy `ICU2/ICU_ar/rules/ar.json` actions → exec ops)."""
    ipv4_walk = {
        "op": "walk",
        "g": 0,
        "map": {
            ".": {"en": "dot ", "zh": "点", "ar": "نقطة "},
            ":": {"en": "colon ", "zh": "冒号", "ar": "نقطتان "},
        },
    }
    ipv6_walk = {
        "op": "walk",
        "g": 0,
        "map": {":": {"en": "colon ", "zh": "冒号", "ar": "نقطتان "}},
        "other_en_space": True,
    }
    return [
        exec_rule(R("SH_PHONE_DASH"), [{"op": "digits", "g": 0}]),
        exec_rule(R("SH_IPV4"), [ipv4_walk]),
        exec_rule(R("SH_IPV6"), [ipv6_walk]),
        exec_rule(R("SH_POW10"), [{"op": "pow10", "g": 1}]),
        exec_rule(R("SH_ROMAN"), [{"op": "roman", "g": 0}]),
        exec_rule(R("SH_INTEGRAL"), [{"op": "integral", "variant": "ar", "g1": 1, "g2": 2}]),
        exec_rule(R("AR_DATE_ISO"), [{"op": "date_iso_en", "gy": 1, "gm": 2, "gd": 3}]),
        {"pattern": R("AR_EM_DASH"), "replace": " "},
        exec_rule(
            R("AR_PRICE_RANGE"),
            [
                LIT(" "),
                {"op": "grp", "g": 1, "as": "raw"},
                {"op": "grp", "g": 2, "as": "raw"},
                LIT(" إلى "),
                {"op": "grp", "g": 3, "as": "raw"},
                {"op": "grp", "g": 4, "as": "raw"},
                LIT(" "),
            ],
        ),
        exec_rule(R("SH_MINUS_DIGIT"), [{"op": "minus", "g": 1}]),
        exec_rule(
            R("AR_TIME_24"),
            [{"op": "time_clock", "colon": " و", "minute_suffix": "", "second_suffix": ""}],
        ),
        exec_rule(
            R("AR_CURRENCY_ALT"),
            [
                {
                    "op": "currency",
                    "sym_g1": 1,
                    "num_g": 2,
                    "sym_g2": 3,
                    "strip": ",",
                    "pad_en": False,
                }
            ],
        ),
        exec_rule(R("AR_PERCENT"), [{"op": "spellout_affix", "g": 1, "prefix": "", "suffix": " بالمائة"}]),
        exec_rule(R("AR_PER_MILLE"), [{"op": "spellout_affix", "g": 1, "prefix": "", "suffix": " بالألف"}]),
        exec_rule(
            R("AR_COMPOSITE_UNIT"),
            [{"op": "unit", "table": "composite", "num_g": 1, "unit_g": 2, "pad_en": False}],
        ),
        exec_rule(R("AR_SINGLE_UNIT"), [{"op": "unit", "table": "single", "num_g": 1, "unit_g": 2, "pad_en": False}]),
        exec_rule(R("AR_ORDINAL"), [{"op": "ordinal", "g": 1, "pad_before": " ", "pad_after": " "}]),
        exec_rule(R("SH_LOOSE_DECIMAL"), [{"op": "loose_num", "g": 0}]),
    ]


def bn_pipeline_rules() -> list[dict]:
    """Ordered pipeline (ported from legacy `ICU2/ICU_bn/rules/bn.json` actions → exec ops)."""
    ipv4_walk = {
        "op": "walk",
        "g": 0,
        "map": {
            ".": {"bn": "ডট "},
            ":": {"bn": "কোলন "},
        },
    }
    ipv6_walk = {
        "op": "walk",
        "g": 0,
        "map": {":": {"bn": "কোলন "}},
        "other_en_space": True,
    }
    return [
        exec_rule(R("SH_PHONE_DASH"), [{"op": "digits", "g": 0}]),
        exec_rule(R("SH_IPV4"), [ipv4_walk]),
        exec_rule(R("BN_CALL_DIAL_LATIN"), [LIT("dial "), {"op": "digits", "g": 1}]),
        exec_rule(R("BN_CALL_DIAL_NATIVE"), [LIT("কল করুন "), {"op": "digits", "g": 1}]),
        exec_rule(R("SH_IPV6"), [ipv6_walk]),
        exec_rule(R("SH_POW10"), [{"op": "pow10", "g": 1}]),
        exec_rule(R("SH_ROMAN"), [{"op": "roman", "g": 0}]),
        exec_rule(R("SH_INTEGRAL"), [{"op": "integral", "variant": "bn", "g1": 1, "g2": 2}]),
        exec_rule(R("BN_DATE_ISO"), [{"op": "date_iso_en", "gy": 1, "gm": 2, "gd": 3}]),
        {"pattern": R("BN_EM_DASH"), "replace": " "},
        exec_rule(
            R("BN_PRICE_RANGE"),
            [
                LIT(" "),
                {"op": "grp", "g": 1, "as": "raw"},
                {"op": "grp", "g": 2, "as": "raw"},
                LIT(" থেকে "),
                {"op": "grp", "g": 3, "as": "raw"},
                {"op": "grp", "g": 4, "as": "raw"},
                LIT(" "),
            ],
        ),
        exec_rule(R("SH_MINUS_DIGIT"), [{"op": "minus", "g": 1}]),
        exec_rule(
            R("BN_LABEL_ROOM"),
            [
                {
                    "op": "label_suffix",
                    "g1": 1,
                    "g2": 2,
                    "g3": 3,
                    "label_case": "lower",
                    "number_mode": "spellout",
                    "suffix_case": "upper",
                }
            ],
        ),
        exec_rule(R("BN_TIME_12"), [{"op": "time_en_12h"}]),
        exec_rule(R("BN_TIME_24"), [{"op": "time_en_24h"}]),
        exec_rule(
            R("BN_CURRENCY_ALT"),
            [
                {
                    "op": "currency",
                    "sym_g1": 1,
                    "num_g": 2,
                    "sym_g2": 3,
                    "strip": ",",
                    "pad_en": False,
                }
            ],
        ),
        exec_rule(R("BN_PERCENT"), [{"op": "spellout_affix", "g": 1, "prefix": "", "suffix": " শতাংশ"}]),
        exec_rule(R("BN_PER_MILLE"), [{"op": "spellout_affix", "g": 1, "prefix": "", "suffix": " প্রতিমাইল"}]),
        exec_rule(
            R("BN_COMPOSITE_UNIT"),
            [{"op": "unit", "table": "composite", "num_g": 1, "unit_g": 2, "pad_en": False}],
        ),
        exec_rule(R("BN_SINGLE_UNIT"), [{"op": "unit", "table": "single", "num_g": 1, "unit_g": 2, "pad_en": False}]),
        exec_rule(R("BN_ORDINAL"), [{"op": "ordinal", "g": 1, "pad_before": " ", "pad_after": " "}]),
        exec_rule(R("SH_LOOSE_DECIMAL"), [{"op": "loose_num", "g": 0}]),
    ]


def ru_pipeline_rules() -> list[dict]:
    ipv4_walk = {
        "op": "walk",
        "g": 0,
        "map": {
            ".": {"ru": " точка "},
            ":": {"ru": " двоеточие "},
        },
    }
    ipv6_walk = {
        "op": "walk",
        "g": 0,
        "map": {":": {"ru": " двоеточие "}},
        "other_en_space": True,
    }
    return [
        exec_rule(R("RU_MORPH_NUM"), [{"op": "ru_num_morph", "g": 1}]),
        exec_rule(R("SH_PHONE_DASH"), [{"op": "digits", "g": 0}]),
        exec_rule(R("SH_IPV4"), [ipv4_walk]),
        exec_rule(R("RU_CALL_DIAL_LATIN"), [LIT("позвонить "), {"op": "digits", "g": 1}]),
        exec_rule(R("RU_CALL_DIAL_NATIVE"), [LIT("позвонить "), {"op": "digits", "g": 1}]),
        exec_rule(R("SH_IPV6"), [ipv6_walk]),
        exec_rule(R("SH_POW10"), [{"op": "pow10", "g": 1, "prefix_ru": "десять в степени ", "suffix_ru": "", "minus_ru": "минус "}]),
        exec_rule(R("SH_ROMAN"), [{"op": "roman", "g": 0}]),
        exec_rule(
            R("SH_INTEGRAL"),
            [{"op": "integral", "variant": "bn", "g1": 1, "g2": 2, "prefix_bn": "интеграл от ", "mid_bn": " до ", "suffix_bn": ""}],
        ),
        exec_rule(R("RU_DATE_ISO"), [{"op": "date_iso_en", "gy": 1, "gm": 2, "gd": 3}]),
        {"pattern": R("RU_EM_DASH"), "replace": " "},
        exec_rule(
            R("RU_PRICE_RANGE"),
            [
                LIT(" "),
                {"op": "grp", "g": 1, "as": "raw"},
                {"op": "grp", "g": 2, "as": "raw"},
                LIT(" до "),
                {"op": "grp", "g": 3, "as": "raw"},
                {"op": "grp", "g": 4, "as": "raw"},
                LIT(" "),
            ],
        ),
        exec_rule(R("SH_MINUS_DIGIT"), [{"op": "minus", "g": 1, "minus_ru": "минус "}]),
        exec_rule(R("RU_TIME_12"), [{"op": "time_en_12h"}]),
        exec_rule(
            R("RU_TIME_24"),
            [{"op": "time_clock", "colon": " часов ", "minute_suffix": " минут", "second_suffix": " секунд"}],
        ),
        exec_rule(
            R("RU_CURRENCY_ALT"),
            [{"op": "currency", "sym_g1": 1, "num_g": 2, "sym_g2": 3, "strip": ",", "pad_en": False, "fallback_ru": "рублей"}],
        ),
        exec_rule(R("RU_PERCENT"), [{"op": "spellout_affix", "g": 1, "prefix": "", "suffix": " процентов"}]),
        exec_rule(R("RU_PER_MILLE"), [{"op": "spellout_affix", "g": 1, "prefix": "", "suffix": " промилле"}]),
        exec_rule(
            R("RU_COMPOSITE_UNIT"),
            [{"op": "unit", "table": "composite", "num_g": 1, "unit_g": 2, "pad_en": False}],
        ),
        exec_rule(R("RU_SINGLE_UNIT"), [{"op": "unit", "table": "single", "num_g": 1, "unit_g": 2, "pad_en": False}]),
    ]


def load_literal_tsv(tsv: str) -> list[dict]:
    rules: list[dict] = []
    for line in tsv.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split("\t")
        if len(parts) < 2:
            continue
        pat, rep = parts[0], parts[1]
        rules.append({"pattern": pat, "replace": rep})
    return rules


# Tail literals: add / edit rows here (pattern TAB replacement). Escape backslashes for JSON handled by json.dumps.
EN_LITERALS_TSV = r"""
# --- Abbreviations (legacy en.cpp) ---
((?:^|\s)etc\.(?=$|\s))	 et cetera 
((?:^|\s)e\.g\.(?=$|\s))	 for example 
((?:^|\s)i\.e\.(?=$|\s))	 that is 
((?:^|\s)vs\.(?=$|\s))	 versus 
((?:^|\s)approx\.(?=$|\s))	 approximately 
((?:^|\s)St\.(?=$|\s))	 Street 
((?:^|\s)Ave\.(?=$|\s))	 Avenue 
((?:^|\s)Rd\.(?=$|\s))	 Road 
((?:^|\s)Blvd\.(?=$|\s))	 Boulevard 
((?:^|\s)Apt\.(?=$|\s))	 Apartment 
((?:^|\s)Corp\.(?=$|\s))	 Corporation 
((?:^|\s)Inc\.(?=$|\s))	 Incorporated 
((?:^|\s)Mr\.(?=$|\s))	 Mister 
((?:^|\s)Mrs\.(?=$|\s))	 Missus 
((?:^|\s)Dr\.(?=$|\s))	 Doctor 
((?:^|\s)Prof\.(?=$|\s))	 Professor 
((?:^|\s)Jr\.(?=$|\s))	 Junior 
((?:^|\s)Sr\.(?=$|\s))	 Senior 
J/\\(kg·℃\\)	 joules per kilogram Celsius 
kg/m³	 kilograms per cubic meter 
kW·h	 kilowatt-hours 
km/h	 kilometers per hour 
m/s²	 meters per second squared 
rad/s	 radians per second 
 kcal 	 kilocalories 
cm/s	 centimeters per second 
m/s	 meters per second 
cm³	 cubic centimeters 
cm²	 square centimeters 
 m³ 	 cubic meters 
 m² 	 square meters 
MHz	 megahertz 
GHz	 gigahertz 
kHz	 kilohertz 
MPa	 megapascals 
hPa	 hectopascals 
kPa	 kilopascals 
 rpm 	 revolutions per minute 
 min 	 minutes 
 kJ 	 kilojoules 
 eV 	 electronvolts 
\bms\b	 milliseconds 
 μs 	 microseconds 
\bns\b	 nanoseconds 
 mV 	 millivolts 
 kV 	 kilovolts 
 MΩ 	 megaohms 
 kΩ 	 kiloohms 
 pF 	 picofarads 
 μF 	 microfarads 
 nF 	 nanofarads 
 mH 	 millihenries 
 μH 	 microhenries 
 mW 	 milliwatts 
 MW 	 megawatts 
 kW 	 kilowatts 
 dB 	 decibels 
 kN 	 kilonewtons 
 N·m 	 newton-meters 
 dm 	 decimeters 
 cm 	 centimeters 
 mm 	 millimeters 
 nm 	 nanometers 
 μm 	 micrometers 
 kg 	 kilograms 
 km 	 kilometers 
 ml 	 milliliters 
 mL 	 milliliters 
 mA 	 milliamperes 
_	 underscore 
\\+	 plus 
(?<![a-zA-Z])ln(?=[^a-zA-Z]|$)	 natural log 
(?<![a-zA-Z])log(?=[^a-zA-Z]|$)	 log 
@	 at 
\\.\\.\\.|…	 
®	 registered trademark 
"""

ZH_LITERALS_TSV = r"""
J/kg·℃	焦每千克摄氏度
kg/m³	千克每立方米
kW·h	千瓦时
kWh	千瓦时
km/h	千米每时
m/s²	米每二次方秒
rad/s	弧度每秒
kcal	大卡
kJ/mol	千焦每摩尔
cm/s	厘米每秒
m/s	米每秒
cm³	立方厘米
cm²	平方厘米
 m³ 	立方米
 m² 	平方米
MHz	兆赫兹
GHz	吉赫兹
kHz	千赫兹
MPa	兆帕
hPa	百帕
mmHg	毫米汞柱
kPa	千帕
 rpm 	每分钟转数
 min 	分钟
 kJ 	千焦
 eV 	电子伏特
 ms 	毫秒
 μs 	微秒
 ns 	纳秒
 mV 	毫伏
 kV 	千伏
 MΩ 	兆欧
 kΩ 	千欧
 pF 	皮法
 μF 	微法
 nF 	纳法
 mH 	毫亨
 μH 	微亨
 mW 	毫瓦
 MW 	兆瓦
 kW 	千瓦
 dB 	分贝
 kN 	千牛
 N·m 	牛米
 dm 	分米
 cm 	厘米
 mm 	毫米
 nm 	纳米
 μm 	微米
 kg 	千克
 km 	千米
 ml 	毫升
 mL 	毫升
 mA 	毫安
e\^	 e x p 
No\\.	第
_	下划线
# 公式/变量式中两侧均为字母或数字时的减号（如 ΔU=Q-W 中的 Q-W）；须在「非字母侧减号」规则之前.
(?<=[A-Za-z0-9])-(?=[A-Za-z0-9])	减
(?<![A-Za-z\u4e00-\u9fff])-(?![A-Za-z\u4e00-\u9fff\(])	减
\+	加
!=	不等于
=	等于
×	乘以
≈	约等于
(?<![a-zA-Z])ln(?=[^a-zA-Z]|$)	自然对数
(?<![a-zA-Z])log(?=[^a-zA-Z]|$)	对数
@	艾特
\\.	点
®	注册标志
"""

# Literal tail from legacy `ICU2/ICU_ar/rules/ar.json` (pattern TAB replacement).
_AR_LITERAL_ROWS: list[tuple[str, str]] = [
    (r"(?:^|\s)صلعم(?:$|\s)", " صلى الله عليه وسلم "),
    (r"(?:^|\s)رحم(?:$|\s)", " رحمه الله "),
    (r"(?:^|\s)إلخ(?:$|\s)", " إلى آخره "),
    (r"(?:^|\s)مم(?:$|\s)", " مليمتر "),
    (r"(?:^|\s)سم(?:$|\s)", " سنتيمتر "),
    (r"(?:^|\s)م(?:$|\s)", " متر "),
    (r"(?:^|\s)ملغ(?:$|\s)", " مليغرام "),
    (r"(?:^|\s)غ(?:$|\s)", " غرام "),
    (r"(?:^|\s)(?:كج|كلغ)(?:$|\s)", " كيلوغرام "),
    (r"(?:^|\s)مل(?:$|\s)", " ملليلتر "),
    (r"(?:^|\s)ل(?:$|\s)", " لتر "),
    (r"(?:^|\s)ث(?:$|\s)", " ثانية "),
    (r"(?:^|\s)(?:دق|د)(?:$|\s)", " دقيقة "),
    (r"(?:^|\s)س(?:$|\s)", " ساعة "),
    (r"(?:^|\s)ج\.ب(?:$|\s)", " غيغابايت "),
    (r"(?:^|\s)م\.ب(?:$|\s)", " ميغابايت "),
    (r"(?:^|\s)د\.م(?:$|\s)", " رسالة مباشرة "),
    ("_", " شرطة سفلية "),
    (r"\+", " زائد "),
    (r"(?<![a-zA-Z])ln(?=[^a-zA-Z]|$)", " لوغاريتم طبيعي "),
    (r"(?<![a-zA-Z])log(?=[^a-zA-Z]|$)", " لوغاريتم "),
    ("@", " آت "),
    (r"\.(?!\s|$)", " نقطة "),
    ("®", " علامة تجارية مسجلة "),
    ("Α", " ألفا "),
    ("α", " ألفا "),
    ("Β", " بيتا "),
    ("β", " بيتا "),
    ("Γ", " غاما "),
    ("γ", " غاما "),
    ("Δ", " دلتا "),
    ("δ", " دلتا "),
    ("Ε", " إبسيلون "),
    ("ε", " إبسيلون "),
    ("Ζ", " زيتا "),
    ("ζ", " زيتا "),
    ("Η", " إيتا "),
    ("η", " إيتا "),
    ("Θ", " ثيتا "),
    ("θ", " ثيتا "),
    ("Ι", " أيوتا "),
    ("ι", " أيوتا "),
    ("Κ", " كابا "),
    ("κ", " كابا "),
    ("Λ", " لامدا "),
    ("λ", " لامدا "),
    ("Μ", " مو "),
    ("μ", " مو "),
    ("Ν", " نو "),
    ("ν", " نو "),
    ("Ξ", " كسي "),
    ("ξ", " كسي "),
    ("Ο", " أوميكرون "),
    ("ο", " أوميكرون "),
    ("Π", " باي "),
    ("π", " باي "),
    ("Ρ", " رو "),
    ("ρ", " رو "),
    ("Σ", " سيغما "),
    ("σ", " سيغما "),
    ("ς", " سيغما نهائية "),
    ("Τ", " تاو "),
    ("τ", " تاو "),
    ("Υ", " يوبسيلون "),
    ("υ", " يوبسيلون "),
    ("Φ", " فاي "),
    ("φ", " فاي "),
    ("Χ", " خي "),
    ("χ", " خي "),
    ("Ψ", " سيبسي "),
    ("ψ", " سيبسي "),
    ("Ω", " أوميغا "),
    ("ω", " أوميغا "),
]
AR_LITERALS_TSV = "\n".join(f"{pat}\t{rep}" for pat, rep in _AR_LITERAL_ROWS)

# Literal tail from legacy `ICU2/ICU_bn/rules/bn.json`.
_BN_LITERAL_ROWS: list[tuple[str, str]] = [
    (r"((?:^|\s)etc\.(?=$|\s))", " এবং তার পরবর্তীকাল "),
    (r"((?:^|\s)e\.g\.(?=$|\s))", " উদাহরণস্বরূপ "),
    (r"((?:^|\s)i\.e\.(?=$|\s))", " অর্থাৎ "),
    (r"((?:^|\s)vs\.(?=$|\s))", " বনাম "),
    (r"((?:^|\s)approx\.(?=$|\s))", " প্রায় "),
    ("_", " আন্ডারস্কোর "),
    (r"\+", " যোগ "),
    (r"(?<![a-zA-Z])ln(?=[^a-zA-Z]|$)", " প্রাকৃতিক লগারিদম "),
    (r"(?<![a-zA-Z])log(?=[^a-zA-Z]|$)", " লগারিদম "),
    ("@", " এট "),
    (r"\.", " ডট "),
    ("®", " নিবন্ধিত ট্রেডমার্ক "),
]
BN_LITERALS_TSV = "\n".join(f"{pat}\t{rep}" for pat, rep in _BN_LITERAL_ROWS)

# Literal tail from legacy `original/morphodita/ru.cpp` (minimal starter set).
_RU_LITERAL_ROWS: list[tuple[str, str]] = [
    (r"((?:^|\s)etc\.(?=$|\s))", " и так далее "),
    (r"((?:^|\s)e\.g\.(?=$|\s))", " например "),
    (r"((?:^|\s)i\.e\.(?=$|\s))", " то есть "),
    (r"((?:^|\s)vs\.(?=$|\s))", " против "),
    (r"((?:^|\s)approx\.(?=$|\s))", " примерно "),
    ("_", " нижнее подчёркивание "),
    (r"\+", " плюс "),
    (r"(?<![a-zA-Z])ln(?=[^a-zA-Z]|$)", " натуральный логарифм "),
    (r"(?<![a-zA-Z])log(?=[^a-zA-Z]|$)", " логарифм "),
    ("@", " эт "),
    (r"\.(?!\s|$)", " точка "),
    ("®", " зарегистрированный товарный знак "),
]
RU_LITERALS_TSV = "\n".join(f"{pat}\t{rep}" for pat, rep in _RU_LITERAL_ROWS)


def extract_pinyin_map_from_zh_cpp(zh_cpp: Path) -> dict[str, str]:
    text = zh_cpp.read_text(encoding="utf-8")
    start = text.find("pinyinMap = {")
    if start < 0:
        raise RuntimeError("Could not find pinyinMap in zh.cpp")
    start = text.find("{", start)
    end = text.find("};", start)
    block = text[start + 1 : end]
    out: dict[str, str] = {}
    for m in re.finditer(r'\{"([^"]+)",\s*"([^"]*)"\}', block):
        out[m.group(1)] = m.group(2)
    return out


def assign_rule_ids(rules: list[dict], prefix: str) -> None:
    """Sequential stable ids (en_00 … / zh_00 … / ar_00 … / bn_00 … / ru_00 …) for logs; suffix matches engine rule_index."""
    if not rules:
        return
    width = len(str(len(rules) - 1))
    for i, r in enumerate(rules):
        rid = f"{prefix}_{i:0{width}d}"
        # Put id first in the emitted JSON for easier human lookup
        body = {k: v for k, v in r.items() if k != "id"}
        r.clear()
        r["id"] = rid
        r.update(body)


def emoji_rule() -> dict:
    return {"pattern": R("SH_EMOJI_CLASS"), "action": "emoji_clear"}


def main() -> None:
    RULES.mkdir(parents=True, exist_ok=True)

    pin_out = RULES / "zh_pinyin.json"
    if pin_out.exists():
        pinyin = json.loads(pin_out.read_text(encoding="utf-8"))
    else:
        legacy = ROOT / "zh_legacy_with_pinyin_map.cpp"
        if not legacy.exists():
            raise SystemExit(
                "Missing rules/zh_pinyin.json. Add zh_legacy_with_pinyin_map.cpp (old zh.cpp) once, "
                "or copy zh_pinyin.json from CI artifacts, then re-run."
            )
        pinyin = extract_pinyin_map_from_zh_cpp(legacy)
    pin_out.write_text(json.dumps(pinyin, ensure_ascii=False, indent=0) + "\n", encoding="utf-8")

    en_rules = en_pipeline_rules() + load_literal_tsv(EN_LITERALS_TSV) + [emoji_rule()]
    assign_rule_ids(en_rules, "en")

    en_doc = {
        "locale": "en",
        "digit_names": en_digit_names(),
        "currency_map": en_currency_map(),
        "unit_composite": en_unit_composite(),
        "unit_single": en_unit_single(),
        "pattern_defs": dict(EN_PATTERN_DEFS),
        "rules": en_rules,
    }
    (RULES / "en.json").write_text(json.dumps(en_doc, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    # TTS 后端常无法合成「〇」(U+3007)；在字面量与 pipeline 之后统一改为「零」.
    zh_rules = (
        zh_pipeline_rules()
        + load_literal_tsv(ZH_LITERALS_TSV)
        + [{"pattern": "〇", "replace": "零"}, emoji_rule()]
    )
    assign_rule_ids(zh_rules, "zh")

    zh_doc = {
        "locale": "zh",
        "digit_names": zh_digit_names(),
        "currency_map": zh_currency_map(),
        "unit_composite": zh_unit_composite(),
        "unit_single": zh_unit_single(),
        "pattern_defs": dict(ZH_PATTERN_DEFS),
        "rules": zh_rules,
    }
    (RULES / "zh.json").write_text(json.dumps(zh_doc, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    ar_rules = ar_pipeline_rules() + load_literal_tsv(AR_LITERALS_TSV) + [emoji_rule()]
    assign_rule_ids(ar_rules, "ar")

    ar_doc = {
        "locale": "ar",
        "digit_names": ar_digit_names(),
        "month_names_ar": ar_month_names(),
        "currency_map": ar_currency_map(),
        "unit_composite": ar_unit_composite(),
        "unit_single": ar_unit_single(),
        "pattern_defs": dict(AR_PATTERN_DEFS),
        "rules": ar_rules,
    }
    (RULES / "ar.json").write_text(json.dumps(ar_doc, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    bn_rules = bn_pipeline_rules() + load_literal_tsv(BN_LITERALS_TSV) + [emoji_rule()]
    assign_rule_ids(bn_rules, "bn")

    bn_doc = {
        "locale": "bn",
        "digit_names": bn_digit_names(),
        "month_names_bn": bn_month_names(),
        "currency_map": bn_currency_map(),
        "unit_composite": bn_unit_composite(),
        "unit_single": bn_unit_single(),
        "pattern_defs": dict(BN_PATTERN_DEFS),
        "rules": bn_rules,
    }
    (RULES / "bn.json").write_text(json.dumps(bn_doc, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    ru_rules = ru_pipeline_rules() + load_literal_tsv(RU_LITERALS_TSV) + [emoji_rule()]
    assign_rule_ids(ru_rules, "ru")

    ru_doc = {
        "locale": "ru",
        "digit_names": ru_digit_names(),
        "month_names_ru": ru_month_names(),
        "currency_map": ru_currency_map(),
        "unit_composite": ru_unit_composite(),
        "unit_single": ru_unit_single(),
        "pattern_defs": dict(RU_PATTERN_DEFS),
        "rules": ru_rules,
    }
    (RULES / "ru.json").write_text(json.dumps(ru_doc, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    print(
        "Wrote",
        RULES / "en.json",
        RULES / "zh.json",
        RULES / "ar.json",
        RULES / "bn.json",
        RULES / "ru.json",
        RULES / "zh_pinyin.json",
    )

    matrix_script = Path(__file__).resolve().parent / "export_locale_field_matrix.py"
    mr = subprocess.run([sys.executable, str(matrix_script)], cwd=str(ROOT))
    if mr.returncode != 0:
        print("Warning: export_locale_field_matrix.py failed", file=sys.stderr)


if __name__ == "__main__":
    main()
