# Contract: 快流候选生成（分词/合并/排序/前缀预测）

**Spec**: [spec.md](../spec.md)（FR-003/004/005/007/008/009/010/016/017/018/022/023）｜ **Research**: [research.md](../research.md) §1/§4/§5 ｜ **Data Model**: [data-model.md](../data-model.md) 一/二/三/四/五/八

## 适用范围

本契约定义 `FastCorrector::buildLattice()` 从"原始击键文本 + 光标位置"产出 `PinyinLattice` 的完整行为规则，覆盖本特性对 `FastCorrector`/`PinyinTrie`/`EnglishWordList`/`PinyinLattice` 四个类的全部变更。不定义 QML 展示层（见 `qml-cpp-session-interface-increment.md`）或候选替换写回（见 `candidate-selection-replacement-contract.md`）。

## 一、分词规则（FR-005）

1. 连续空白字符：消费为分隔符，**不产生 cell**（与现状完全一致）。
2. 连续"分隔标点"字符（见 `research.md` §1 完整清单，半角 28 个 + 对应全角，**不含** `-`/`_`/`＿`）：消费为一个独立 cell，`source = Passthrough`。
3. 其余连续字符（字母/汉字/数字/`-`/`_`/`＿`/未列出字符）：消费为一个"词 cell"，交由下述规则 2/3 处理；`tokenStart`/`tokenEnd` 记录该 cell 在原始 `QString` 中的字符偏移区间 `[tokenStart, tokenEnd)`。
4. **MUST**：规则 1–3 的切分结果经过 `PinyinLattice::toDisplayText()`/`toAnnotatedPromptText()` 渲染后，与"本特性交付前（标点粘连原 token 末尾）"产生的最终输出文本**逐字符相等**（`research.md` §1 已给出三组样例推演验证），即本契约**不改变**任一既有测试对渲染结果的断言。

## 二、词 cell 候选合并规则（FR-008/FR-009）

对任一"词 cell"（`looksLikeLatin(token)` 为真的 token）：

1. **MUST NOT** 因命中 `EnglishWordList::contains(token)` 而提前返回/跳过后续规则 2–4（取消既有短路）。
2. 命中 `contains(token)`：加入候选池，`source = EnglishWordListPassthrough`。
3. 全拼 Trie `queryByFullPinyin(token, kTopKCandidates, overreach)`（`overreach` 取值见三、前缀预测分支判定）、简拼 Trie（`token.size() <= 4` 时）、编辑距离回退：规则与现状完全一致（无条件执行，不因规则 2 命中而跳过）。
4. 去重（`seen` 集合，按 `surfaceForm` 字符串）、按 `rawWeight` 降序排序、截断 Top-K=9（FR-010，原 8）、`normalizedWeight` 基于截断后子集归一化：与现状流程一致，仅 K 值变化。
5. **MUST**：若候选池中存在 `source ∈ {EnglishWordListPassthrough, EnglishPrefixTrie}` 的候选，取其中 `rawWeight` 最高者，**强制置于列表第 1 位**，不受其权重与其余候选权重比较结果影响；其余候选（含未被选中的次优英文候选）**MUST** 按 `rawWeight` 正常降序排列于第 2 位及以后，**MUST NOT** 整体前移/额外加权。

非词 cell（标点/已是中文等 `looksLikeLatin(token)==false`）：恒为单候选、`source = Passthrough`，行为不变。

## 三、前缀预测分支（FR-017/FR-018/FR-022/FR-023）

一个词 cell 启用前缀预测分支（即二、规则 3 中全拼 Trie 查询使用 `PinyinTrie::kMaxPrefixPredictionOverreach` 而非 `kMaxFullPinyinPrefixOverreach`，且额外合并 `EnglishWordList::queryByPrefix()` 结果），**当且仅当**同时满足：

1. `tokenEnd == rawInput.length()`（该 token 之后再无任何字符，即"尚未被分隔符终结"）；
2. `token.size() >= 2`（FR-023，拼音与英文场景统一阈值，不区分）。

不满足以上任一条件（已被分隔符终结的"完整词"，含光标回头编辑场景）：**MUST** 使用默认 `kMaxFullPinyinPrefixOverreach`，**MUST NOT** 触发 `EnglishWordList::queryByPrefix()` 合并——即"是否前缀模式"与"该 cell 是否为光标锚定的当前词（`currentTokenIndex`）"是两个独立判定维度，互不依赖。

`EnglishWordList::queryByPrefix()` 产出的候选 `source = EnglishPrefixTrie`，与 `EnglishWordListPassthrough` 共用同一 `seen` 去重键（`surfaceForm`），同等参与二、规则 5 的"单一最佳英文候选强制置顶"裁决。

## 四、`currentTokenIndex` 计算（FR-011/FR-013/FR-019）

`cursorPosition` 参数（`< 0` 或 `> rawInput.length()` 时钳制为 `rawInput.length()`）确定后，`PinyinLattice.currentTokenIndex` **MUST** 取满足 `cell.tokenStart <= cursorPosition <= cell.tokenEnd` 的词 cell（**不含**标点 cell）的 `tokenIndex`；若不存在这样的词 cell（光标落在空白/标点区间，或输入为空），**MUST** 为 `-1`。

## 五、强约束汇总

- **MUST NOT** 新增 `InjectionOutcome`/`FloatingSessionState` 等既有状态机取值（本契约不涉及这些类型）。
- **MUST**：`tests/fastpath/*.cpp` 既有全部 `buildLattice(text)`/`queryByFullPinyin(p)`/`queryByFullPinyin(p, n)` 单/双参调用形式因新增默认值形参继续编译通过，且（除 `EnglishWordListInterceptsCommonWordBeforePinyinPipeline`/`EnglishWordListLookupIsCaseInsensitive` 两个按 FR-008 字面要求更新断言的用例外）其余既有断言结果不变。
- **MUST**：`EnglishWordList::loadFromFile()`/`fromWords()`/`contains()`/`size()` 对外签名与可观察行为（除新增 `queryByPrefix()` 外）不变。

## 六、与 002 契约的关系

本契约的 Top-K=9 变更需要同步更新 `specs/002-lattice-dict-model-switch/contracts/llm-prompt-annotation-contract.md` 中"K = 8"的数值引用（该文件已预先声明此类调整"只需同步更新本文件"，不构成标注语法本身的变更）。
