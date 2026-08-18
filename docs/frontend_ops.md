# Frontend atomic ops and named compositions

The TN frontend bookends are configured under `frontend` in `rules_v2/*.full.json`.
Business-level operations can be defined as named compositions in `frontend.op_defs` and
are expanded into a flat execution plan during `TextNormalizer::Init`. Composition is not
interpreted recursively for every input line.

## Example

```json
{
  "frontend": {
    "resources": {
      "char_sets": {
        "vertical_line_punct_chars": "|｜丨"
      }
    },
    "op_defs": {
      "normalize_vertical_lines": [
        {
          "op": "replace_char_set",
          "params": {
            "char_set": "vertical_line_punct_chars",
            "replacement": "，"
          }
        },
        {
          "op": "collapse_literal_runs",
          "params": { "literal": "，" }
        }
      ]
    },
    "post_stages": [
      { "id": "fe_vertical", "use": "normalize_vertical_lines" }
    ]
  }
}
```

A stage may use an explicit `use`, or keep an existing `op` name that matches an
`op_defs` entry. The latter lets one JSON file remain usable by older binaries that still
implement the legacy operation directly. Parameters on the calling stage are defaults;
parameters declared by a child stage take precedence.

## Atomic operations

| Op | Required parameters | Purpose |
|---|---|---|
| `regex_replace` | `pattern`, `replacement`; optional `flags: "icase"` | Replace every regex match |
| `literal_replace` | `from`, `to` | Replace every exact substring |
| `replace_char_set` | `char_set`, `replacement` | Replace each code point from a named resource set |
| `collapse_literal_runs` | `literal` | Collapse adjacent repetitions of one literal |
| `trim_spaces` | none | Remove leading and trailing ASCII spaces |
| `replace_many` | `values` plus `replacement` or `replacement_constant` | Replace every named literal with one value; legacy `aliases`/`target` names are accepted, and may coexist only when they name the same resources |

`regex_replace` uses the C++ ECMAScript regex grammar so migrated legacy rules retain
their existing matching and replacement behavior. Regex objects are compiled once during
initialization.

Operations that require Unicode algorithms or linguistic context remain native primitives.
Examples include Unicode normalization, abbreviation collapsing, clause-break handling and
Chinese parenthetical pauses. Add a new C++ primitive only when the existing atomic operations
cannot express the behavior.

## Loading and validation

- English definitions are the shared base. Locale `resources` and `op_defs` overlay entries
  with the same name. Non-sandhi profiles fail initialization if that shared base or its
  active frontend stages are missing.
- A locale may override a named recipe while reusing the shared stage that calls it. Chinese
  uses this for `nfkc_with_locale_protection`: only Chinese temporarily shields mathematical
  sub/superscripts so its formula rules can consume them; other locales keep plain NFKC.
- Locale `pre_stages` and `post_stages` retain their replacement semantics;
  `post_stages_extra` is inserted before the final sentence-punctuation stage.
- Named compositions are expanded at initialization. Unknown operations or references,
  cycles and excessive nesting are rejected for every definition; selected stages also reject
  missing resources and invalid parameters. Errors make `Init` fail instead of becoming a
  silent no-op.
- The Chinese G2P sandhi-only profile does not inherit TN frontend bookends.
- Legacy operation names remain registered for old JSON files. A legacy operation is migrated
  only when its named composition is byte-equivalent for all supported boundary cases.

## 前端原子操作与命名组合

前端规则可以在 `frontend.op_defs` 中用多个简单原子操作定义一个命名组合。组合会在
`TextNormalizer::Init` 时完成展开和校验，逐行推理时只执行已经展开好的线性步骤，不会
反复递归解释 JSON。

规则阶段既可以用 `{ "use": "组合名" }` 显式引用，也可以继续使用与组合同名的
`{ "op": "名称" }`。后一种写法用于兼容旧动态库。调用阶段的参数作为默认值传入，
组合内部步骤的同名参数优先。

纯替换、字符集处理和连续字符压缩应优先写在 JSON 中；只有 Unicode 算法、上下文断句、
中文括号停顿等无法由通用操作准确表达的逻辑才保留为 C++ 原生原子操作。规则加载时会
检查未知操作、资源缺失、循环引用和参数错误，避免错误配置在运行时静默失效。

同名 `op_defs` 可被语种配置覆盖，而共享 stage 无需复制。中文的
`nfkc_with_locale_protection` 就利用这个机制临时保护数学/化学上下标；
其他语种仍执行普通 NFKC，不会因中文规则引入输出变化。
