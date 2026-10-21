# Bug Fix: 合法英文单词被 FastCorrector 误判为走音拼音并替换为无关汉字

- **Slug**: english-word-pinyin-misdetection
- **Fixed**: 2026-10-21
- **Assessment**: ./assessment.md
- **Status**: applied

## Summary

按用户明确决策的"词表 + 阈值组合方案"（assessment.md 候选方向 1 + 2）实施修复：
新增高频英文常用词表（随包分发自工作区既有 `temp/COCA60000.txt`，约 6 万词），
在 `FastCorrector::buildLatticeCell()` 中 `looksLikeLatin()` 判定为真之后、进入
三级拼音匹配管线之前优先查表拦截；同时收紧 `PinyinTrie::queryByFullPinyin()` 的
前缀溢出量与 `editDistanceFallbackCandidates()` 的最小 token 长度，覆盖 assessment
明确指出的"全拼 Trie 前缀命中"与"编辑距离回退命中"两条触发路径。另按用户决策
的"纵深防御"范围，为 `PinyinLattice::toAnnotatedPromptText()`/`toDisplayText()`
新增对"无歧义但高风险坍缩"格位的原始 token 痕迹保留机制，避免 `meeting`→客厅
这类单候选坍缩彻底抹除原始击键内容。

## Changes

| File | Change | Notes |
|------|--------|-------|
| `babel/src/fastpath/EnglishWordList.h` | 新增 | 高频英文常用词表类：`loadFromFile()`/`fromWords()` 构建，`contains()` 大小写不敏感查询；头部注释如实标注 `temp/COCA60000.txt` 来源与许可不确定性风险（用户 accept_risk 决策） |
| `babel/src/fastpath/EnglishWordList.cpp` | 新增 | 实现：纯文本逐行加载，小写归一化存储 |
| `babel/content/dictionaries/common-english-words.txt` | 新增 | 随包分发资源，内容为 `temp/COCA60000.txt` 原样副本（60022 行） |
| `babel/CMakeLists.txt` | 修改 | `DICTIONARY_SOURCES` glob 新增该词表文件，随 Qt 资源一并内嵌 |
| `babel/main.cpp` | 修改 | 新增 `deployBundledCommonEnglishWordsIfNeeded()`：独立于既有 `.dict.yaml` 部署判断，仅按词表文件自身是否存在决定是否首次部署到约定目录（修复对"已运行过旧版本的用户"也能生效） |
| `babel/src/fastpath/DictionaryPaths.h`/`.cpp` | 修改 | 新增 `conventionalCommonEnglishWordsFilePath()` 静态方法，复用既有 `conventionalDictionaryDirectory()` 约定 |
| `babel/src/fastpath/FastCorrector.h` | 修改 | 构造函数新增可选参数 `EnglishWordList commonEnglishWords = EnglishWordList()`（默认空集合，不影响既有测试/既有调用方） |
| `babel/src/fastpath/FastCorrector.cpp` | 修改 | ① `buildLatticeCell()` 新增词表拦截分支（`looksLikeLatin()` 之后、三级管线之前）；② 新增 `kMinEditDistanceFallbackTokenLength=2`，`editDistanceFallbackCandidates()` 对短于该长度的 token 直接跳过（修复 `I`→级）；③ Trie 查询结果改用新 `TrieMatch` 结构，并据 `exactLengthMatch` 设置 `CandidateEntry::latinOriginLengthMismatch`；④ `editDistanceFallbackCandidates()` 同样按"命中词条全拼长度 != token 长度"设置该标记 |
| `babel/src/fastpath/PinyinTrie.h`/`.cpp` | 修改 | `queryByFullPinyin()`/`queryByAbbrev()` 返回类型由 `vector<pair<string,double>>` 改为 `vector<TrieMatch>`（新增 `exactLengthMatch` 字段）；`queryByFullPinyin()` 内部新增 `kMaxFullPinyinPrefixOverreach=1` 前缀溢出上限剪枝（修复 `want`→万台）；`queryByAbbrev()` 行为不变（溢出不限制） |
| `babel/src/fastpath/PinyinLattice.h`/`.cpp` | 修改 | `CandidateEntry` 新增 `Source::EnglishWordListPassthrough` 枚举值与 `latinOriginLengthMismatch` 字段（默认 `false`，不影响既有手工构造的测试候选）；`toAnnotatedPromptText()`/`toDisplayText()` 新增对"无歧义但 `latinOriginLengthMismatch==true`"格位的原始 token 痕迹保留逻辑 |
| `babel/src/session/SessionController.cpp` | 修改 | 新增 `loadProductionCommonEnglishWords()`，`bootstrapPlatformDefaults()` 一并传入 `FastCorrector` 构造函数 |
| `tests/fastpath/PinyinTrieTest.cpp` | 修改 + 新增测试 | 适配新 `TrieMatch` 返回类型；新增 2 个溢出剪枝/精确匹配标记回归测试 |
| `tests/fastpath/FastCorrectorTest.cpp` | 新增测试 | 5 个新测试：词表拦截、大小写不敏感、空词表不影响既有行为、单字母 token 跳过编辑距离回退、Trie 溢出匹配标记 |
| `tests/fastpath/PinyinLatticeTest.cpp` | 新增测试 | 3 个新测试：高风险单候选坍缩在 `toAnnotatedPromptText()`/`toDisplayText()` 中保留痕迹、无实际替换时不追加冗余痕迹 |

## Diff Highlights

`FastCorrector::buildLatticeCell()` 新增词表拦截（节选）：

```cpp
if (m_commonEnglishWords.contains(token)) {
  CandidateEntry passthrough;
  passthrough.surfaceForm = QString::fromStdString(token);
  passthrough.rawWeight = 1.0;
  passthrough.normalizedWeight = 1.0;
  passthrough.source = CandidateEntry::Source::EnglishWordListPassthrough;
  cell.candidates = {std::move(passthrough)};
  return cell;
}
```

`PinyinTrie::collectCandidates()` 前缀溢出剪枝（节选）：

```cpp
void PinyinTrie::collectCandidates(const TrieNode *node, std::size_t depthBelowPrefix,
                                    std::size_t maxOverreach, std::vector<TrieMatch> *out) {
  if (depthBelowPrefix <= maxOverreach) {
    for (const auto &[surfaceForm, weight] : node->candidates) {
      out->push_back(TrieMatch{surfaceForm, weight, depthBelowPrefix == 0});
    }
  }
  if (depthBelowPrefix >= maxOverreach) return;  // 剪枝
  for (const auto &[ch, child] : node->children) {
    collectCandidates(child.get(), depthBelowPrefix + 1, maxOverreach, out);
  }
}
```

`PinyinLattice::toDisplayText()` 纵深防御（节选）：

```cpp
result += surface;
if (isRiskyLatinCollapse(cell) && cell.originalToken != surface) {
  result += QLatin1Char('(');
  result += cell.originalToken;
  result += QLatin1Char(')');
}
```

## Tests Added or Updated

- `tests/fastpath/FastCorrectorTest.cpp::EnglishWordListInterceptsCommonWordBeforePinyinPipeline` — 词表命中优先于 Trie 碰撞候选
- `tests/fastpath/FastCorrectorTest.cpp::EnglishWordListLookupIsCaseInsensitive` — 大小写不敏感
- `tests/fastpath/FastCorrectorTest.cpp::EmptyEnglishWordListDoesNotInterceptAnything` — 默认空词表不影响既有行为/既有召回率
- `tests/fastpath/FastCorrectorTest.cpp::EditDistanceFallbackSkipsSingleLetterTokens` — 固定 `I`→级 根因（最小长度校验）
- `tests/fastpath/FastCorrectorTest.cpp::PinyinTrieOverreachMatchIsMarkedAsLatinOriginLengthMismatch` — Trie 溢出匹配被标记供纵深防御使用
- `tests/fastpath/PinyinTrieTest.cpp::QueryByFullPinyinRejectsExcessivePrefixOverreach` — 固定 `want`→万台 根因（溢出上限剪枝）
- `tests/fastpath/PinyinTrieTest.cpp::QueryByFullPinyinExactMatchReportsExactLengthMatchTrue` — `exactLengthMatch` 字段正确性
- `tests/fastpath/PinyinLatticeTest.cpp::ToAnnotatedPromptTextAnnotatesRiskySingleCandidateCollapse` — 纵深防御：慢流 prompt 保留原始 token
- `tests/fastpath/PinyinLatticeTest.cpp::ToDisplayTextAppendsOriginalTokenForRiskySingleCandidateCollapse` — 纵深防御：用户可见预览保留原始 token（不违反 FR-008）
- `tests/fastpath/PinyinLatticeTest.cpp::ToDisplayTextDoesNotAppendTraceWhenNoActualSubstitutionHappened` — 排除"无命中原样保留"场景的冗余追加

## Local Verification

- 环境：`export VCPKG_ROOT=~/vcpkg`，`cmake --preset macos`（增量 configure，复用既有 `build/macos`），`cmake --build build/macos --config Debug`（Xcode 生成器，含 Babel 主应用 + 全部 GoogleTest 目标）→ **BUILD SUCCEEDED**，无编译警告/错误。
- 真实生产词库复测（临时、未提交的验证用例，运行后已删除，`git status`/`git diff` 核实未残留；方法与 assessment.md 一致）：使用 `babel/content/dictionaries/`（经 `RimeDictLoader` 加载，实测 **891061** 条）+ 随包词表（实测 **53968** 条有效词条，差异源于原始 60022 行中含连字符/撇号等非纯字母词形，不参与 `looksLikeLatin()` 分支查询，不影响修复效果）构造 `FastCorrector`，对 assessment.md 列出的全部问题词（`I`/`want`/`name`/`nice`/`meeting`，以及扩展的 `a`/`my`/`an`/`ok`/`hi`/`cat`/`dog`/.../`coffee` 共 37 个样例词）逐一验证：**全部命中 `EnglishWordListPassthrough`（或既有 `Passthrough`/`EditDistanceFallback` 无命中原样保留），`toDisplayText()`/`toAnnotatedPromptText()` 输出与原始 token 完全一致，无一例被替换为无关汉字**。
- 既有正例回归（`FastCorrectorTest.cpp`）：`KeyboardProximityToleratesTypo`（`hqo`→好）、`CorrectsMixedPinyinDraftToChinese`（`wo xiang shuo zhege idea hen hao`→我想说这个idea很好）等全部按原断言字符串精确通过，未退化。
- 性能回归（`FastCorrectorRealDictionaryPerformanceTest.cpp`，未修改，真实 89 万词条规模）：单次 `buildLattice()` 调用仍 <300ms（既有阈值），与修复前基准一致，新增的词表查询（O(1) 哈希）与 Trie 溢出剪枝（深度优先剪枝，不增加渐进复杂度）未引入可观测性能回归。
- 命令：`ctest -C Debug --output-on-failure`（测试二进制已逐一 `codesign --force --sign -` ad-hoc 签名，CMake 层面已通过 `tests/CMakeLists.txt` 既有 POST_BUILD 签名钩子自动完成）→ **98/98 通过（100%）**，1 个按设计 SKIPPED（`PasteInjectorPermissionTest`，本沙箱 `AXIsProcessTrusted()` 恒为 true，与本次修复无关，为既有已知限制）。总耗时约 186 秒。

## Deviations from Assessment

无重大偏离。以下为assessment"候选方向"中需要在实现阶段自主决策、且已据此落地的具体选择（均在用户授权范围内）：

- **词表拦截粒度**：直接复用 `temp/COCA60000.txt` 全量 60022 行（不裁剪），按用户决策"不需要用户进一步拍板具体数值"处理；实测有效大小写字母词条 53968 条，哈希查询开销可忽略。
- **候选方向 2 的具体阈值**：`kMinEditDistanceFallbackTokenLength=2`（编辑距离回退最小 token 长度）、`kMaxFullPinyinPrefixOverreach=1`（全拼 Trie 前缀溢出上限）。均已用全部既有正例测试（含 `KeyboardProximityToleratesTypo`/`CorrectsMixedPinyinDraftToChinese`）验证不退化，且用真实 89 万词条词库验证能消除 assessment 列出的全部问题词的误替换。
- **候选方向 4（纵深防御）的触发条件与格式**：选用"候选数=1 且候选标记 `latinOriginLengthMismatch`（即该候选来自拼音管线对全字母 token 的非精确长度匹配）"作为触发条件，而非对"任意无歧义拼音匹配格位"一概标注——后者会导致 `toDisplayText()` 对所有正常拼音输入（如 `wo`→我、`xiang`→想）都追加冗余标注，违反既有正例回归测试与 FR-008 的"用户可见预览不应引入噪声"隐含预期。本触发条件经验证：对 `meeting`→客厅（编辑距离下长度不等）、`want`→万台（前缀溢出）等假设性残留风险场景正确触发，对全部既有正例测试场景（`wo`/`xiang`/`shuo`/`zhege`/`hen`/`hao`/`hqo` 等精确长度匹配）均不触发，零误报。
- `toDisplayText()` 的痕迹保留格式选用轻量级 `候选(原始token)` 后缀（不含权重/候选集合），而非完整的 `⟦...⟧` 标注语法——后者会暴露候选权重，违反该函数既有文档明确的 FR-008 约束（"MUST NOT 向用户呈现候选集合及其权重"）。

## Follow-ups

- **老用户升级路径**：`main.cpp` 新增的 `deployBundledCommonEnglishWordsIfNeeded()` 已独立于既有 `.dict.yaml` 部署判断（仅检查词表文件自身是否存在），已在本次修复中验证对"已运行过旧版本的用户"同样生效，无需额外跟进。
- **许可/合规**：按用户 "accept_risk" 决策，本次未撰写正式 LICENSE/NOTICE 声明文件；若后续产品/法务层面需要补充，可参照 `babel/content/dictionaries/NOTICE-rime-ice.txt` 现有格式为 `common-english-words.txt` 补充一份。
- **姓氏/专有名词碰撞**（assessment.md Risks 已指出）：词表拦截与拼音匹配之间的真实语义冲突（如 "Li"/"Wang"/"He"/"Ma" 既是常见英文单词又是中文姓氏拼音）未在本次修复范围内特殊处理，脱离上下文确实无法 100% 消除，符合 assessment 预期管理（"不要求 100% 准确，只要求显著改善高频场景"）。
- **碰撞规模的量化复核与产品决策确认（fix 验证阶段新增发现）**：独立验证阶段用真实词库（8105.dict.yaml + base.dict.yaml + ext.dict.yaml）精确统计了"词条全拼与 `common-english-words.txt` 完全相同"的碰撞总数——**单字拼音碰撞 1334 个、多字词碰撞 836 个，合计 2170 个**，规模远超上一条仅泛泛提及的"姓氏/专有名词"风险类别，具体包含 `hen`(很)/`chi`(吃) 这类高频单字拼音，也包含 `name`(那么)/`like`(立刻)/`buying`(不应) 这类双向均为高频词的极端案例（`name` 恰是本 bug 修复要求正确识别为英文的原始例子之一）。按英文词频排名设阈值测试证实：即使仅拦截排名前 3000 的高频英文词，仍有 476 个碰撞无法消除，因为 `you`(排名14)/`he`(15)/`a`(5)/`an`(29)/`man`(94)/`name`(298)/`like`(74) 等词本身就是英文里最常见的词之一，任何静态阈值规则都无法把它们排除在"应判定为英文"之外。**用户审阅此量化数据后明确决策**：本项目定位并非传统拼音输入法或翻译软件，而是"允许中英混杂、语法不规范输入，由 AI 推理用户意图、引导输出标准中文（后续含标准英文对照）"的练习型工具；产品设计哲学是"英文优先、拼音为辅"——存在双向高频碰撞时，MUST 优先采用英文含义、放弃拼音解释，这与当前实现（词表命中即无条件透传、不进入拼音管线）的行为**完全一致，确认为按设计工作，不是需要在本 bug 范围内修复的缺陷**。后续候选词列表特性（允许用户在输入过程中手动选择歧义候选，不在本 bug 范围）是官方规划中用于让用户在这类碰撞场景下手动纠偏的真正解法，相关需求应另行走 `speckit.specify` 立项，不在本次 bug 修复中实现。
- **"无歧义即完全抹除原始 token"的更通用设计问题**（assessment.md Risks 已指出）：本次修复仅覆盖"该候选由拼音匹配管线对全字母 token 产生、且长度不精确匹配"这一子情形；对"合法拼音误判为错误汉字，但恰好长度精确匹配（如编辑距离为 0 但语义错误的巧合同音字）"这一更广泛的既有设计权衡，仍按 assessment Open Questions 的讨论维持现状，未在本 bug 范围内处理（因用户决策仅限定"英文单词"场景的纵深防御）。
