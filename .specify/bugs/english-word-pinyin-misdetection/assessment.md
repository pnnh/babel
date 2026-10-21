# Bug Assessment: 合法英文单词被 FastCorrector 误判为走音拼音并替换为无关汉字

- **Slug**: english-word-pinyin-misdetection
- **Created**: 2026-10-20
- **Source**: pasted text（用户直接提供的缺陷报告，含复现文本与候选输出，无 URL）
- **Verdict**: valid
- **Severity**: high

## Report（原始内容摘要）

用户报告：`FastCorrector` 的编辑距离回退匹配（`editDistanceFallbackCandidates()`）对所有"看起来全是字母"的 token（`looksLikeLatin()` 判定）都会尝试模糊匹配真实拼音词库（002 引入的 iDvel/rime-ice，约 89 万词条），没有区分"token 本身已是合法常见英文单词"与"这是打错字的拼音"。报告以输入 `"I today xinqing hen bad. Wo want qu chi shaokao."` 为例，称 `I`→级、`want`→万台 被误替换，而 `today`/`bad.`/`shaokao.` 正确保持原样；并称该污染通过 `PinyinLattice::toAnnotatedPromptText()` 传导至慢流 LLM，拉低译文质量（附三张用户截图，未随附件提供，无法验证）。报告将此病灶与 001 阶段"chi→是"问题相提并论。

## 独立复现与验证（已执行，非转述）

本次评估在现有代码基础上，用仓库随包分发的真实生产词库（`babel/content/dictionaries/`，经 `RimeDictLoader` 加载，实测 **891061** 条）构造了两个临时、未提交的 GoogleTest 用例（评估结束已删除，未改动任何受跟踪源文件；`git status`/`git diff` 核实为空），直接调用 `FastCorrector::buildLattice()` 验证报告中的现象，并补充了严重度探测。结果：

1. **逐字核对报告给出的例句**，候选输出与报告完全一致（候选权重误差 <0.001，可忽略浮点精度差异）：
   ```
   TOKEN=[I]    -> 级(EditDistanceFallback) w=0.2394  哦 w=0.2190  集 w=0.2061  几 w=0.1103  及 w=0.0735  机 w=0.0668  记 w=0.0437  季 w=0.0411
   TOKEN=[want] -> 万台(FullPinyinTrie)      w=0.4041  万套 w=0.1725  万条 w=0.1314  万桶 w=0.1034  弯头 w=0.0671  万通先锋 w=0.0491  顽童 w=0.0414  晚唐 w=0.0309
   TOKEN=[today]    -> today(EditDistanceFallback, 无命中原样保留) w=1
   TOKEN=[bad.]     -> bad.(Passthrough，含标点非拼音 token) w=1
   TOKEN=[shaokao.] -> shaokao.(EditDistanceFallback, 无命中原样保留) w=1
   ```
   报告的核心现象（合法英文单词被错误替换为无关生僻汉字，而部分英文 token 又能正确保留）**确认为真**。

2. **关键修正**：报告将 `want`→万台 的成因归结为"编辑距离回退 … 编辑距离在阈值内"，但实测候选的 `source` 字段显示 `want` 命中来自 **`FullPinyinTrie`**（`buildLatticeCell()` 第 1 步 `m_trie.queryByFullPinyin()`），**并非**编辑距离回退（第 3 步 `editDistanceFallbackCandidates()`）。`PinyinTrie::queryByFullPinyin()` 本质是纯前缀遍历（`queryPrefix()`，`PinyinTrie.cpp:49-67`），对任意以该字符串为前缀的词条无条件返回，不做任何长度/距离校验——`want` 恰是词条"万台"拼音全拼 `wantai`（`base.dict.yaml:407759`，`wan tai`，空格在 `derivePinyinForms()` 中被去除拼接）的前缀，故被**精确**前缀命中，权重未经任何距离惩罚（比编辑距离回退更"自信"、优先级更高，因 `buildLatticeCell()` 第 1 步先于第 3 步执行，命中数已达 `kTopKCandidates=8` 后根本不会触发编辑距离回退）。`I` 单字母则确因与词库中 `ji`/`e` 等单字拼音的编辑距离在 `kMaxAcceptableDistance=1.5` 内而被 `editDistanceFallbackCandidates()` 命中。两条路径的根因相同（`looksLikeLatin()` 对所有全字母 token 一视同仁地触发拼音匹配管线，未验证该 token 是否已是合法英文词），但**具体触发机制不止一种**，修复时需同时覆盖"全拼 Trie 前缀命中"与"编辑距离回退命中"两条路径，而不能只收紧 `kMaxAcceptableDistance`。

3. **严重度/覆盖面探测**：对 46 个常见英文高频短词（冠词/代词/介词/动词，如 `a/my/is/at/in/on/to/he/we/it/am/by/do/up/me/go/no/so/an/if/ok/hi` 及 `cat/dog/run/sun/man/can/see/say/bus/box/fan/key/pen` 等）逐一测试，**46/46（100%）** 均产生 8 个候选、均判定为"存在歧义"（`isAmbiguous()==true`），即全部被污染。进一步对较长常见词测试（`idea/about/name/good/nice/great/happy/simple/project/deadline/meeting/weekend/coffee/email`），`idea/about/good/great/happy/simple/project/deadline/coffee/email/weekend` 正确原样保留，但 `name`→那么、`nice`→你曾 被污染（多候选，歧义），**`meeting`→客厅 被污染为唯一候选（`isAmbiguous()==false`）**。
   - 对于"存在歧义"的格位（候选数 ≥2），`toAnnotatedPromptText()`（`PinyinLattice.cpp:43-70`）会以 `⟦原始token:候选1 w1|候选2 w2|...⟧` 形式把**原始 token 一并带给慢流 LLM**，LLM 仍有机会从原始拼写恢复语义——这部分缓解了报告描述的"直接传导"的影响面。
   - 但对于"无歧义"（候选数=1，如本次发现的 `meeting`→客厅）的格位，`toAnnotatedPromptText()` 只输出 `候选.surfaceForm`，**原始英文单词被完全、静默地抹除**，慢流 LLM 根本看不到 `meeting` 这个词曾经存在，无法通过上下文恢复——这是比报告举例更严重的子情形。
   - 另外，`PinyinLattice::toDisplayText()`（`PinyinLattice.cpp:21-40`，`SessionController::submitKeystrokes()` 中赋值给用户可见的 `rawInputSlotText`／`m_normalizedCandidate.content`，`SessionController.cpp:536`）同样无条件取 `cell.candidates.front().surfaceForm`，**不区分是否存在歧义**。这意味着污染不仅限于"传导给慢流 LLM 的隐藏 prompt"，**用户在浮窗中实时看到的输入预览本身也会把 `meeting` 显示为"客厅"**——比报告描述的"下游 LLM 质量下降"更直接、更易被用户当场察觉，影响面和可见性都应上调。

综上，报告描述的现象整体属实（verdict=valid），但两处需要澄清修正：①`want` 的触发路径是全拼 Trie 前缀匹配而非编辑距离回退；②实际受影响范围远大于报告举出的两个例子（常见短英文虚词/代词近乎 100% 中招，且存在原始词被完全静默抹除、并直接污染用户可见输入预览的更严重子情形）。

## Symptom

当用户输入包含中英文混排的文本（FastCorrector 显式支持的场景，见 spec 002 `"idea"` 示例）时，任何"全部由字母组成"的 token（`looksLikeLatin()` 为真）都会被无条件送入拼音匹配管线（全拼 Trie 前缀查询 → 简拼 Trie 查询 → 编辑距离回退），即使该 token 本身就是一个合法、常见的英文单词。真实规模词库（约 89 万条）下，很大比例的短英文单词恰好是某个拼音全拼字符串的前缀，或与某个单字/双字拼音的编辑距离足够小，从而被替换为语义无关的汉字——且这一替换既写入用户可见的输入预览（`rawInputSlotText`），也写入传给慢流 LLM 的标注 prompt（`toAnnotatedPromptText()`），在后者"无歧义"的情形下原始英文词会被完全抹除、不可恢复。

期望行为：已经是合法英文单词的 token 应当保持原样透传（类比 `looksLikeLatin()` 为假时的"非拼音 token 透传"分支），不应进入拼音匹配管线。

## Reproduction

1. 使用仓库随包分发的真实 iDvel/rime-ice 词库（`babel/content/dictionaries/`）构造 `FastCorrector`（经 `RimeDictLoader::loadDictionaryDirectory()` + `loadAllDictionaryEntries()`，与生产环境 `SessionController::bootstrapPlatformDefaults()` → `loadProductionDictionaryEntries()` 路径一致）。
2. 调用 `corrector.buildLattice(QStringLiteral("I today xinqing hen bad. Wo want qu chi shaokao."))`。
3. 检查各 `PinyinLatticeCell::candidates`：`I` 的最高权重候选为"级"（来源 `EditDistanceFallback`），`want` 的最高权重候选为"万台"（来源 `FullPinyinTrie`），均与原词语义无关；`today`/`bad.`/`shaokao.` 原样保留。
4. 额外复现（本次评估新增，用于确认覆盖面）：对 `a/my/is/at/in/on/to/he/we/it` 等高频短英文词重复步骤 2-3，46/46 测试样本全部被污染；对 `meeting` 测试，候选坍缩为唯一项"客厅"，`isAmbiguous()==false`。

无 `[NEEDS CLARIFICATION]` 项——以上均已在当前代码 + 真实词库上直接验证。唯一未能验证的是报告提及的"三张用户测试截图"（未随附件提供，无法核实慢流 LLM 最终译文质量下降的具体外部表现，但基于确认的 prompt 污染机制，该下游影响在逻辑上是合理推论）。

## Suspected Code Paths

- `babel/src/fastpath/FastCorrector.cpp:15-17` — `looksLikeLatin()`：唯一的"是否进入拼音匹配管线"判定依据，仅检查字符集合（全字母），不检查该 token 是否已是合法英文词。这是本缺陷的根本闸门。
- `babel/src/fastpath/FastCorrector.cpp:124-219` — `buildLatticeCell()`：`looksLikeLatin()` 为真时无条件执行三级匹配（全拼 Trie → 简拼 Trie → 编辑距离回退），merged 为空时才原样保留；这是报告描述现象的直接触发点。
- `babel/src/fastpath/PinyinTrie.cpp:49-67`、`69-72` — `queryPrefix()` / `queryByFullPinyin()`：纯前缀遍历，无长度/距离校验，是 `want`→万台 的**实际**成因（报告误归为编辑距离回退，见上文"关键修正"）。
- `babel/src/fastpath/FastCorrector.cpp:67-121` — `editDistanceFallbackCandidates()` 与 `FastCorrector.cpp:20` 的 `kMaxAcceptableDistance = 1.5`：`I`→级 的实际成因，仅覆盖该病灶的一部分触发路径。
- `babel/src/fastpath/FastCorrector.cpp:42-65` — `weightedEditDistance()` 与 `babel/src/fastpath/KeyboardProximityMap.cpp:19-33` 的 `proximityWeight()`：决定编辑距离回退路径的距离计算与键位邻近惩罚，影响哪些短 token 能落入 `kMaxAcceptableDistance` 阈值内。
- `babel/src/fastpath/PinyinLattice.cpp:43-70` — `toAnnotatedPromptText()`：确认了"有歧义格位保留原始 token 标注、无歧义格位完全丢弃原始 token"这一非对称行为，是本次评估新发现的更严重子情形（`meeting`→客厅 静默无痕替换）的直接代码位置。
- `babel/src/fastpath/PinyinLattice.cpp:21-40` — `toDisplayText()`：确认了用户可见的输入预览（经 `SessionController.cpp:536` 赋值给 `rawInputSlotText`）同样无条件采用 `candidates.front()`，污染不止于隐藏的慢流 prompt。
- `babel/src/session/SessionController.cpp:567-571` — `submitKeystrokes()` 中 `slowPathInput = lattice.toAnnotatedPromptText()` 一并转交 `LlmEngine::generate()`，确认报告所述"传导到下游"的调用链。

## Root Cause Hypothesis

**置信度：高。** `looksLikeLatin()` 当前语义是"这个 token 的字符集合像拼音"，但代码把它当作"这个 token 需要被当作拼音来解析"的充分条件使用，二者并不等价——大量合法英文单词（尤其是常见短虚词/代词，以及恰好是某个真实拼音前缀或与某单字拼音编辑距离很近的内容词，如本次新发现的 `meeting`）天然满足前者但不满足后者。001 阶段"chi→是"修复（见 `specs/002-lattice-dict-model-switch/spec.md` 对该历史问题的描述）通过"Lattice 多候选 + 真实词库"解决的是"合法拼音音节被误配到错误汉字"这一类问题；002 在引入 89 万词条真实词库后，相同的"无差别触发拼音匹配管线"逻辑被放大到了一个此前（14 条内置种子词典规模下）几乎不会暴露的新维度——合法英文单词与真实词库发生意外前缀/编辑距离碰撞。两次都是同一个"`looksLikeLatin()` 后无条件信任拼音匹配管线输出"设计决策的不同表现形式，报告对此病灶归属的整体判断正确。

## Proposed Remediation

**不在本阶段实施**，以下为供 `/speckit.bug.fix` 参考的候选方向，具体取舍标注为待决策事项（见下）。

候选方向（均需同时覆盖"全拼 Trie 前缀命中"与"编辑距离回退命中"两条路径，不能只处理其中一条）：

1. **高频英文常用词"提前拦截名单"**：在 `looksLikeLatin()` 判定为真之后、进入三级匹配管线之前，先查一个体积较小的高频英文词表；命中则直接走"透传"分支（类比现有 `!looksLikeLatin()` 分支）。
   - **相关发现**：仓库工作区中已存在一个未纳入版本控制的 `temp/COCA60000.txt`（60022 行高频英文词，来源疑似 COCA 语料库词频表，约 580KB），似是此前为该方向准备的候选资源，但其体积（6 万词）与报告措辞"体积较小"存在张力，且该文件未提交、许可/来源未经确认。是否采用该文件、是否需要裁剪到更小的高频子集，应作为待决策事项列出，不在评估阶段预设结论。
2. **收紧/调整编辑距离与前缀匹配对短 token、全字母 token 的适用条件**：例如要求 `editDistanceFallbackCandidates()` 和 `queryByFullPinyin()` 的前缀命中额外满足"候选词条拼音长度与 token 长度之比"或"token 长度 ≥ 某阈值"等约束，减少短词的意外碰撞概率。此方向对 `queryByFullPinyin()` 的影响尤其关键，因为该路径目前无任何距离/长度校验。
3. **基于 token 形态/构词规律的启发式判断**：例如结合大小写模式（句首大写单字母 `I`、全小写等）、是否命中常见英文闭音节/后缀模式等，与方向 1/2 组合使用而非单独依赖。
4. **（本次评估新增的待讨论点）区分"有歧义格位"与"无歧义格位"在 `toAnnotatedPromptText()` 中的处理**：即便上述方向未能 100% 杜绝误触发，至少可以考虑让"无歧义但来自 Fallback/Trie 的英文字母 token"候选在送入慢流前附带原始 token 标注（而非完全丢弃），作为纵深防御，降低"静默完全抹除"这一更严重子情形的发生概率。此点与方向 1-3 是否重叠/是否必要，也应作为待决策事项，不在本评估阶段预设结论。

**Alternatives 权衡**（均为开放问题，非本评估阶段结论）：
- 方向 1（词表拦截）实现简单、效果可预测，但需要决定词表来源/规模/许可，以及"token 命中词表"与"token 同时也是合理拼音"冲突时（理论上少见但非不可能）的优先级。
- 方向 2（收紧阈值/约束）无需引入新数据资源，但可能影响真正的拼音纠错召回率（例如合法的键位误触拼音也可能被误伤），需要用现有 `FastCorrectorTest.cpp`/`FastCorrectorRealDictionaryPerformanceTest.cpp` 中的正例回归验证。
- 方向 3（启发式）单独使用误判率可能偏高，更适合作为方向 1/2 的辅助信号。

**Files likely to change**：
- `babel/src/fastpath/FastCorrector.h` / `.cpp`（`looksLikeLatin()`、`buildLatticeCell()`、`editDistanceFallbackCandidates()`，新增英文词表加载/查询若采用方向 1）
- `babel/src/fastpath/PinyinTrie.cpp` / `.h`（`queryPrefix()`/`queryByFullPinyin()` 若采用方向 2 的前缀匹配约束）
- `babel/src/fastpath/PinyinLattice.cpp` / `.h`（若采用方向 4，调整 `toAnnotatedPromptText()`/`toDisplayText()` 的无歧义分支行为）
- 若引入词表文件：新增资源文件 + `babel/src/fastpath/DictionaryPaths.{h,cpp}` 风格的路径约定，以及对应 CMake 资源打包规则（参照 `babel/content/dictionaries/` 现有随包分发方式）

**Tests to add or update**：
- `tests/fastpath/FastCorrectorTest.cpp`：新增用真实/扩展词库对一批常见英文单词（含本评估确认的 `I/want/name/nice/meeting` 等）的回归断言，验证不再被替换为无关汉字；需要注意现有测试多用 `builtinSeedDictionary()`（14 条内置种子词典），不足以暴露此类问题，新增用例应显式构造或加载接近真实规模的词库（参考 `FastCorrectorRealDictionaryPerformanceTest.cpp` 的词库加载方式）。
- 新增对"无歧义单候选替换合法英文词"这一更严重子情形的专项回归（覆盖 `meeting`→客厅 这类坍缩为单候选的案例），避免修复只覆盖"有歧义"情形。
- `tests/fastpath/PinyinLatticeTest.cpp`：若调整 `toAnnotatedPromptText()`/`toDisplayText()` 行为，需同步更新/新增断言。
- 若采用词表方向，建议补充边界测试：词表命中但大小写/标点变体（如 `Want`、`want,`）、词表未覆盖的长尾英文词、词表与合理拼音碰巧撞词（如中文人名拼音恰好是英文常见词，例如 "Li"/"Wang" 这类姓氏拼音）等场景，作为词表方案潜在副作用的回归保护。

## Risks & Considerations

- **性能**：若采用方向 1（词表拦截），需在真实 89 万词条规模 + 新增词表查询下维持 FR-003/SC-002 的 <15ms 延迟目标（可参考 `FastCorrectorRealDictionaryPerformanceTest.cpp` 现有基准测试模式，放宽阈值但覆盖性能回归）。词表查询本身应为 O(1) 哈希查找，预期开销可忽略，但需要实测验证。
- **召回率回归**：任何收紧匹配条件的方案都有"误伤真实拼音纠错"的风险，需要用现有正例测试（如 `KeyboardProximityToleratesTypo`、`CorrectsMixedPinyinDraftToChinese`）持续验证不退化。
- **许可/合规**：若采用 `temp/COCA60000.txt`（或任何第三方高频词表）作为随包分发资源，需要像 002 阶段对 iDvel/rime-ice（GPLv3）一样明确记录许可与分发义务，这是产品/法务决策范畴，不应由实现阶段自行拍板。
- **姓氏/专有名词碰撞**：中文拼音姓氏（如 "Li" "Wang" "He" "Ma"）本身也是常见英文单词或词形相近，词表拦截和拼音匹配之间存在真实的语义冲突（究竟应判定为英文单词还是姓氏拼音，脱离上下文不可判定），任何方案都无法 100% 消除此类歧义，只能降低概率，需要在验收标准中明确"不要求 100% 准确，只要求显著改善高频场景"这一预期管理。
- **"无歧义即完全抹除原始 token"这一更通用的设计问题**：本次评估确认的 `meeting`→客厅（候选坍缩为 1 个时，`toAnnotatedPromptText()`/`toDisplayText()` 均不保留原始 token）并非英文单词场景独有，对任何"无歧义误判"（即便是合法拼音误判为错误汉字，只是恰好只命中 1 个候选）都同样成立，可能是比本 bug 范围更广的既有设计权衡，是否需要作为独立问题跟进，建议在 fix 阶段一并评估范围边界。

## Open Questions

- [NEEDS CLARIFICATION: 是否采用"高频英文常用词表提前拦截"方案；若采用，词表来源/规模/许可如何确定——是否使用工作区中已存在但未提交的 `temp/COCA60000.txt`（60022 词，来源/许可未经确认），还是裁剪为更小规模的自定义列表？此为产品/法务层面的决策分歧点，本评估不预设结论。]
- [NEEDS CLARIFICATION: 若不采用词表方案，收紧编辑距离/前缀匹配阈值的具体参数（如最小 token 长度、长度比约束）如何确定？是否有可接受的"漏判"（仍有少量英文词被误判）与"误伤"（真实拼音纠错召回率下降）权衡基准？]
- [NEEDS CLARIFICATION: "无歧义候选完全抹除原始 token"（`toAnnotatedPromptText()`/`toDisplayText()` 的非对称行为）是否应在本 bug 范围内一并修复，还是作为独立的后续问题跟踪？]
- [NEEDS CLARIFICATION: 报告提及的"三张用户测试截图"未随附件提供，无法核实慢流 LLM 最终译文质量下降的具体外部表现（已通过代码路径确认污染会传导至 prompt，但截图本身的内容无法验证）。]

