# Research: 学习提示气泡内容生成

**Spec**: [spec.md](./spec.md) ｜ **Plan**: [plan.md](./plan.md)

## 阅读说明

本文件是 Phase 0 产出。spec.md 的 4 项 Clarifications（2026-10-21）已在 spec 阶段解决，本阶段不再重复讨论"是否复用同一次慢流调用""是否展示语法点""设置开关是否默认开启""是否仅中/高配启用"这四项——它们已是既定前提。本阶段仅核实落地这些前提所需的**技术事实**（现有代码结构、既有机制的实际行为），以及记录用户本轮在 `/speckit.plan` 过程中就"同一次输出如何分隔中文结果与英文建议"这一新发现的架构决策点（命中判定程序 (a)）所作出的显式决策。

**用户决策（已取得，2026-10-21 本轮 /speckit.plan 对话中）**：就"中文结果与英文建议的分隔/解析格式"，Agent 给出候选方案 1（中文标签行）/2（新增专用 Unicode 符号对）/3（纯 ASCII marker `<<<EN_TIP>>>`，Recommendation）三项供决策，**用户采纳方案 3**。本文件 §3 据此记录最终方案与理由，不再是待决事项。

---

## 1. 现状核实：`LlmEngine::generate()` 当前实际行为（`babel/src/slowpath/LlmEngine.cpp`）

- `generate(const QString &normalizedInput) const` 签名固定，内部：清空 KV cache → 拼接 ChatML 格式 prompt（固定 `kSystemPrompt` + 用户输入）→ tokenize → 首次 `llama_decode()` → 自回归采样循环（`kMaxOutputTokens = 256` 上限，`ForbiddenTokenMask` 查表禁用 ⟦/⟧/| 三个码点，温度采样 `llama_sampler_init_temp(0.3f)` + `llama_sampler_init_dist(0)`）→ EOG 或语法梗阻时终止 → 返回拼接后的纯文本。
- 004-speculative-decoding 在此基础上新增了"草稿模型可用时走投机解码分支"的并行路径（`m_draftModelAvailable` 分支），但**两条分支共享同一份 prompt 构造代码**（`const std::string prompt = ...` 在分支判断之前完成一次），且两条分支各自独立维护禁用位图查表、各自的采样循环，**互不依赖对方的具体采样机制**。
- 现有 `kSystemPrompt` 只要求"输出改写后的标准中文句子，不要添加任何解释或前后缀"，完全没有要求模型同时产出英文建议——这正是 spec.md Input 描述的现状缺口。
- `ForbiddenTokenMask`（gbnf-grammar-sampling-overhead bug fix 产物）在模型加载完成时一次性预计算，按 token id 索引，**仅**排除 ⟦（U+27E6）/⟧（U+27E7）/|（U+007C）三个码点，不存在任何递归语法状态（`root ::= safe-char*`，Kleene 星号展开后每个 token 的合法性只取决于其自身，与之前生成了什么无关）。采样循环改为对该静态黑名单做 O(1) 查表，**不再**调用 `llama_sampler_init_grammar` 通用语法引擎（该引擎正是此前导致单次 `generate()` 耗时从 628ms 推高到约 2000ms 的性能反模式根因，详见 `.specify/bugs/gbnf-grammar-sampling-overhead/assessment.md`）。

**结论（事实性，供 §3 决策参考）**：当前输出约束机制是"自由解码 + 固定大小静态黑名单"，不存在任何"强制输出必须符合某种结构"的语法级保证；模型是否按双段格式输出完全依赖 prompt 指令遵循能力，不存在语法层面的硬保证。

---

## 2. 现状核实：`SessionController` 如何消费 `generate()` 返回值（`babel/src/session/SessionController.cpp`）

- `onSlowPathTriggered()` 在独立 `std::thread` 中调用 `engine->generate(inputSnapshot)`，结果通过 `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` 回传主线程，直接赋值给 `m_outputPreview.content`，随后 emit `outputPreviewTextChanged()`。
- `resetSessionData()`（热键重新唤起浮窗 / 取消时调用）**当前不**触碰 `m_learningTipText`/`m_learningTipVisible`——这两个字段自 001 起就从未被赋值过，该遗漏此前无可观察影响（气泡恒为空/不可见），但本特性引入真实内容后，若不在 `resetSessionData()` 中一并清空，会出现"上一次会话的英文建议残留展示到下一次全新会话"的问题，命中 spec.md User Story 2 "不展示残留上一轮已过时的建议内容"的语义边界（虽然原文字面指的是同一会话内的重新生成，但跨会话残留显然是更严重的同类问题，按 FR-004 精神理应一并处理，属于实现该 FR 所必需的内部步骤，宪章原则 I 已授权）。
- `outputPreviewText` 在本轮生成"进行中"期间（`Debouncing`/`Generating` 状态）**刻意保留上一轮已完成的旧内容**，`FloatingWindow.qml` 对此有专门的"内容 + （生成中…）"渲染逻辑（`SessionController.outputPreviewText.length > 0 ? ... + "（生成中…）" : "生成中…"`）；只有当新一轮生成在后台线程完成、结果通过 `QMetaObject::invokeMethod` 回传主线程的**唯一一处回调**中才会整体替换为新内容。这是 001 已确立的"自愈 UI"既有行为模式。

**结论（决策依据，§5 据此确定学习提示的更新时机）**：学习提示内容的更新时机应**完全复用**这一既有的"仅在生成完成回调中整体替换"模式，不额外引入"开始生成时先清空旧内容"的新时序——这直接解决了 spec.md Edge Cases 中"学习提示内容是否必须等待新一轮生成完全结束才更新，还是允许短暂展示旧内容"这一未决问题：**沿用 outputPreviewText 已确立的现状**（短暂展示旧内容，直到新一轮生成完成后一次性整体替换），两者在同一次回调中同步更新，不会出现"中文预览已更新但英文建议还没更新"的不同步窗口。此为已存在同类实现的直接复用（宪章原则 II 允许，来源：`SessionController.cpp` `onSlowPathTriggered()` 现状代码 + `FloatingWindow.qml` 既有渲染逻辑），不构成新的 `[NEEDS CLARIFICATION]`。

---

## 3. 【用户已决策】同一次输出中"中文结果"与"英文建议"的分隔/解析格式

本阶段在设计过程中新发现 1 项命中判定程序 (a)（直接影响 SC-003/SC-005"建议与预览内容是否对应"这一外部可观察的正确性语义）的架构选择，未由 Agent 自行假设，已在 `/speckit.plan` 本轮对话中提请用户决策。

### 候选方案（已提交用户决策）

1. **中文标签行**：`标准中文：<中文结果>\n地道英文：<英文建议>`，按 `地道英文：` 首次出现切分。
2. **新增一对专用 Unicode 符号**（与既有 ⟦/⟧ 同风格）：`<中文结果>⟪<英文建议>⟫`，需同步扩展 `ForbiddenTokenMask` 预计算黑名单。
3. **纯 ASCII marker**（Recommendation）：`<中文结果>\n<<<EN_TIP>>>\n<英文建议>`，按 `<<<EN_TIP>>>` 首次出现切分；`<`/`>` 不在现有黑名单范围内，无需扩展 `ForbiddenTokenMask`。

### 用户决策

**采纳方案 3**：固定分隔符常量 `<<<EN_TIP>>>`（命名为 `LlmEngine::kLearningTipSeparator`，`data-model.md`/`contracts/llm-engine-learning-tip-contract.md` 据此确定接口）。

### 决策理由（供后续追溯）

- 相较方案 1：中文短语（"地道英文："）理论上仍可能被模型当作译写内容本身的一部分自然说出（尽管概率低），纯 ASCII 技术标记序列与自然中英文文本的字面冲突概率更低。
- 相较方案 2：无需扩展 `ForbiddenTokenMask` 的黑名单维护面，不触碰 gbnf-grammar-sampling-overhead bug 修复后已确立的"仅排除 ⟦/⟧/| 三码点"现状，维持该模块的改动最小化。
- 与 Clarifications 已确定的"方案一：复用同一次慢流调用，不触发第二次独立模型调用"保持风格一致——本方案同样只依赖 prompt 指令 + 调用方后处理解析，不引入任何新的语法引擎/有状态约束机制。

---

## 4. 本特性是否需要扩展 `ForbiddenTokenMask`/重新引入 GBNF 语法引擎（用户关注点，已核实为否）

**Decision（已核实，非待决）**：**不需要**扩展 `ForbiddenTokenMask`，也**不需要**重新引入 `llama_sampler_init_grammar` 通用语法引擎。

**依据**：
- 方案 3 选定的分隔符 `<<<EN_TIP>>>` 由纯 ASCII 字符（`<`/`E`/`N`/`_`/`T`/`I`/`P`/`>`）组成，均不在现有黑名单（⟦/⟧/|）范围内，模型可以自由输出这些字符，无需任何采样层面的放行/禁止调整。
- 本特性对输出格式的约束**完全依赖 prompt 指令遵循**，不要求任何语法层面的"强制保证"（这也是 Clarifications 方案一本身的题中之义——若要求语法级强制保证双段结构，必然需要引入有状态递归语法，与"不触发第二次独立模型调用"的简洁性诉求相悖，且会重新引入 gbnf-grammar-sampling-overhead bug 修复专门移除的性能反模式）。
- `ForbiddenTokenMask::build()` 本身的预计算逻辑（匹配 ⟦/⟧/| 三个固定码点）**不发生任何修改**，继续覆盖目标模型与草稿模型两套独立词表，与 004 既有机制完全不变。

**与 004 投机解码的正交性核实**：投机解码分支（草稿-验证循环）只改变"token 如何被采样产出"的机制层面，对"模型被要求产出什么内容"（即 prompt 里的双段格式指令）完全无感知——草稿模型与目标模型两侧均继续使用各自预计算好的禁用位图，`includeLearningTip` 参数只影响 prompt 文本本身与循环的 token 上限，不改变任一采样循环内部的判定逻辑。即：`includeLearningTip` 与 `useSpeculative`（m_draftModelAvailable）两个维度完全正交，二者的全部 4 种组合均无需额外适配代码。

---

## 5. SessionController 如何判定本轮是否请求学习提示生成（FR-008/FR-009）

**Decision（内部实现细节，宪章原则 IV 自主决定，供 `data-model.md`/`contracts/` 落地）**：

```text
wantLearningTip = learningTipEnabled（新设置开关，默认 true）
                  AND currentTier ∈ { Medium, High }
```

- `currentTier` 的获取方式**直接复用** 003/004 已确立的既有模式：对 `m_modelRuntimeState.currentModelPath.filename()` 实时调用 `ModelCatalog::classifyTierByFileName()`（003 `data-model.md` 已明确"不缓存/持久化档位信息，通过实时对文件名重新分类获得"），不新增任何档位缓存字段，不偏离既有模式（来源：`SessionController.cpp` `pairedDraftModelPathForTier()`/`updateSpeculativeDecodingNoticeText()` 现状代码）。
- `wantLearningTip == false` 时（低配/Unknown 档位，或用户已关闭开关），`generate()` 调用**完全不附加**双段格式指令（`includeLearningTip` 参数为 `false`，prompt 与 001～004 现状逐字节一致），`kMaxOutputTokens` 维持既有 `256` 不变——确保 FR-009"低配默认档位保持现有延迟预算不变"在**prompt 层面**也成立（而不仅仅是"生成后丢弃学习提示部分"这种仍然增加模型负担的弱实现）。这同时满足 FR-008"用户关闭开关后...不再生成...学习提示内容"的字面语义——真正不生成，而非生成后隐藏。
- `wantLearningTip == true` 时，`generate()` 调用附加双段格式指令（`includeLearningTip = true`），采样循环上限从 `kMaxOutputTokens(256)` 提升为更大的 `kMaxOutputTokensWithLearningTip`（§6 给出具体取值与理由），因为单次输出需要同时容纳中文译文与英文建议两部分内容。

此设计一并回答了用户在发起本轮规划时提出的"低配档位下 `learningTipText`/`learningTipVisible` 应保持什么状态"：恒为空字符串/`false`，且连带不产生任何用于判断的双段 prompt 开销——与 FR-004 失败兜底语义（"不展示报错文案、乱码、占位符，或残留上一轮已过时的建议内容"）表现一致，但触发原因不同（FR-004 是"生成失败/判定无效"后的兜底，低配档位是"从未尝试生成"），两者最终呈现给用户的状态完全相同（气泡不可见、文本为空），故复用同一套字段语义、不新增额外的"不适用"状态区分（宪章原则 V 最小必要修改）。

---

## 6. `kMaxOutputTokens` 上限是否需要为双段输出调整（内部实现细节）

**Decision（宪章原则 IV 自主决定）**：是，新增内部常量 `kMaxOutputTokensWithLearningTip = 400`，仅当 `includeLearningTip == true` 时替换原 `kMaxOutputTokens = 256` 作为采样循环上限；`includeLearningTip == false` 时沿用既有 `256` 不变。

**理由**：双段输出需要在同一次自回归生成中先后容纳"标准中文译写结果" + 分隔标记 + "英文表达建议"三部分内容，仅用原 256 token 上限在长句场景下可能导致英文建议部分被截断（生成尚未产出分隔标记前就触碰上限）。与既有 `kMaxOutputTokens = 256` 本身的确定方式相同（"MVP 固定上限，避免单次生成无界耗时"，原实现也未经专门调研或用户决策，是 Agent 按工程经验直接设定的内部常量），本次按同等粒度设定一个更大的固定上限，不引入自适应机制，不新增配置项。该数值仅影响中/高配档位（已被 Clarifications 接受"允许比低配更高的延迟预算"）下的最坏情况耗时上界，不影响 FR-009 低配延迟预算承诺。

**Alternatives considered**：
- 不区分、统一提升全局 `kMaxOutputTokens`——会连带影响低配档位，直接违反 FR-009，已否决。
- 让该上限可配置/自适应（按输入长度动态计算）——超出本特性最小必要范围（spec.md 未提出该项要求），且与既有 `kMaxOutputTokens` 固定常量的工程风格不一致，已否决。

---

## 7. `LlmEngine::generate()` 对外签名是否需要变更（内部实现细节）

**Decision（宪章原则 IV 自主决定，非待决架构问题——不改变任何外部用户可观察行为，纯 C++ 内部类之间的调用约定）**：

新增一个带默认值的参数：

```cpp
QString generate(const QString &normalizedInput, bool includeLearningTip = false) const;
```

返回类型**保持 `QString` 不变**——`LlmEngine` 仍只返回"一段拼接后的生成文本"，`includeLearningTip == true` 时该文本内部按 `kLearningTipSeparator` 分隔两部分，但**解析/切分逻辑不放在 `LlmEngine` 内部**，而是作为 `SessionController.cpp` 内的纯字符串处理辅助函数（`parseLearningTipOutput()`，不依赖 `LlmEngine` 任何私有状态）。

**理由**：
- 默认参数 `false` 使 001～004 全部既有调用点（`tests/slowpath/LlmEngineGenerateTest.cpp`/`LlmEngineUnavailableTest.cpp` 中的单参数调用、`SessionController.cpp` 中除本特性新增分支外的既有逻辑）**源码零改动即可继续编译通过且行为完全不变**，是对"generate() 对外签名不变"这一 004 已确立惯例的最大程度延续（004 FR-002 原文是"输入输出均为 QString"字面不变，本次虽新增参数，但保持了"仅传一个 QString 即可拿到纯中文译写结果"这一最基础调用方式的向后兼容，未删除/未改变任何既有可用的调用形态）。
- 不把解析逻辑下放到 `LlmEngine` 内部，是因为该引擎类的既有职责边界是"纯文本生成"，不掺杂业务层面的"如何展示两个不同 UI 区域"的语义（该语义属于 `SessionController` 层，呼应 `contracts/llm-engine-grammar-internal-contract.md` 已确立的"`LlmEngine` 不是 QML 可见类型，不掺杂会话/展示语义"的既有定位）；且解析逻辑本身只是基于公开常量 `kLearningTipSeparator` 的字符串查找，无需访问 `LlmEngine` 任何私有成员，没有技术上必须内聚到该类的理由。

**Alternatives considered**：
- 改为返回结构体 `struct GenerationResult { QString standardOutput; QString learningTip; bool learningTipPresent; };`——更类型安全，但会导致全部既有调用点（含 3 处既有测试文件）必须同步修改字段访问方式才能通过编译，改动面显著大于"新增一个带默认值的 bool 参数"，不符合宪章原则 V"最小必要修改"，已否决。
- 把解析逻辑作为 `LlmEngine` 的 `public static` 方法——与"保持职责单一"的既有定位（§7 理由第二点）略有张力，权衡后选择放在调用方 `SessionController.cpp`，已否决该替代方案。

---

## 8. 设置页面开关的持久化策略（用户关注点，已核实为"沿用既有不持久化模式"）

**Decision（已核实为既有同类实现的直接复用，非新决策，宪章原则 II 允许沿用并需注明来源）**：

新增开关 `learningTipEnabled`（默认 `true`）**不引入任何磁盘持久化**（不使用 QSettings 或任何其他文件/数据库写入），每次应用重启固定恢复为默认值 `true`，仅存在于 `SessionController` 的进程内存字段中。

**核实依据（来源文件位置）**：
1. 全仓库范围检索确认**不存在任何 `QSettings` 用法**（`grep -rln "QSettings" babel/src` 无匹配），当前项目不存在任何既有的磁盘级设置持久化基础设施可供复用。
2. 002 `data-model.md`"持久化范围声明"：`ActiveModelRuntimeState`（承载当前模型选择）"最终选型：不持久化——用户的模型选择不跨应用重启记忆，每次启动固定使用低配默认模型"，为**用户本人在 002 阶段已显式决策**的既有先例。
3. 004 `SessionController.h`"`speculativeDecodingEnabled` 事后修订"注释："默认 false（不随应用重启持久化，与 002/003 既有'模型选择不跨重启持久化'约定一致）"——这是与本特性**同属"设置页面功能开关"这一类别**的直接先例（而非仅是"模型选择"这一相邻但不完全同类的场景），结构、用途、UI 呈现方式（设置页面内一个二选一开关）均与本特性新开关高度一致。

本特性新开关沿用上述已确立的统一模式，不构成偏离既有模式的新选择，故不触发宪章原则 II"偏离既有模式 MUST 提问"的要求。

---

## 9. 决策事项汇总

| 事项 | 类型 | 结果 |
|---|---|---|
| 中文结果/英文建议分隔格式 | 命中判定程序 (a)，已交用户决策 | **用户采纳方案 3**：`<<<EN_TIP>>>` ASCII marker（§3） |
| 是否扩展 `ForbiddenTokenMask`/GBNF 语法引擎 | 事实核实，非待决 | **否**（§4） |
| 学习提示更新时机（是否等待新一轮生成完全结束） | Edge Case，已用既有同类实现解决 | **沿用 `outputPreviewText` 既有"仅完成回调整体替换"模式**（§2） |
| 档位判定与 prompt 分支逻辑 | 内部实现细节，原则 IV 自主决定 | §5 |
| 采样 token 上限调整 | 内部实现细节，原则 IV 自主决定 | **新增 `kMaxOutputTokensWithLearningTip = 400`**（§6） |
| `generate()` 对外签名 | 内部实现细节，原则 IV 自主决定 | **新增默认参数 `includeLearningTip = false`，返回类型不变**（§7） |
| 设置开关持久化策略 | 已存在同类实现，原则 II 允许沿用 | **不持久化，沿用 002/004 既有模式**（§8） |
| 过短输入/本身已地道英文输入等 Edge Cases | 不新增启发式规则 | 交由模型输出结果与既有解析/空值兜底机制自然处理，不在应用层新增关键词/长度判断（最小必要修改） |

**本阶段无遗留 `[NEEDS CLARIFICATION]`**：唯一命中判定程序 (a) 的新发现事项（§3）已取得用户显式决策，`data-model.md`/`contracts/`/`quickstart.md` 据此直接写入最终值，不再是候选方案占位。
