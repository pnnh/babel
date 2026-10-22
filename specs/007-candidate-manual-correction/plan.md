# Implementation Plan: 候选词列表手动纠偏

**Branch**: `007-candidate-manual-correction`（Git 当前实际所在分支为 `main`，延续 001～006 既有做法——本仓库不为单个特性单独建立 feature branch，特性产物以 `specs/007-candidate-manual-correction/` 目录隔离）

**Date**: 2026-10-22

**Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `/specs/007-candidate-manual-correction/spec.md`

**Note**: This template is filled in by the `/speckit.plan` command. See `.specify/templates/plan-template.md` for the execution workflow.

## Summary

本特性在浮窗输出预览区域下方新增竖排候选词列表（编号 1–9，可能性降序），用户可通过数字键手动纠偏当前词的自动消歧结果（核心场景："hen"默认命中英文，用户按数字键改选"很"）。spec.md 两轮澄清已确定 23 条 FR，关键决策：替换采用字面文本替换（不引入格位索引状态机）、取消英文词表短路改为合并产出且英文固定占位 1、Top-K 由 8 调整为 9、新增光标位置穿透机制（候选列表跟随光标锚定词）、数字键展示即劫持、候选列表始终展示（唯一候选编号仍为"1"）、新增前缀级候选预测（拼音复用 `PinyinTrie`，英文需将 `EnglishWordList` 重构为前缀树）、前缀预测候选池单独放宽 `PinyinTrie` 溢出上限、前缀预测最小触发长度统一 2 字符、扩展分词边界标点清单（排除连字符/下划线）。

本轮 `/speckit.plan` 对话中就"`submitKeystrokes()` 签名扩展策略"这一命中宪章判定程序 (a)/(b)（影响既有 Q_INVOKABLE 公共契约与既有测试调用点）的架构分歧，向用户提出 3 个编号方案并取得显式决策（方案 B：新增带默认值 `int cursorPosition = -1` 形参，向后兼容）；`FastCorrector::buildLattice()` 的对称签名变更按同一模式自主决定（内部 C++ 到 C++ 接口，不跨 QML 边界，原则 IV）。除该项外，`research.md` 记录的全部其余设计点（`EnglishWordList` 前缀树重构方案、`PinyinTrie` 双溢出上限参数、QML 候选展示契约形状、数字键拦截逻辑、分词器重写算法、替换+自动追加分隔符算法、前缀预测与完整词形候选的合并分支判定）均落入"内部细节"（原则 IV）或"已被 FR 字面明确授权，仅需确定具体落地方式"范畴，经逐条核对宪章判定程序 (a)–(e) 后均无需额外用户决策，详见下文 Constitution Check 与 `research.md` 各节。

核心技术发现（`research.md` 详述）：① 当前 `FastCorrector::buildLattice()` 用 `istringstream >> token` 分词，完全丢失字符偏移信息且从不把标点与相邻词断开——这正是 FR-005/FR-006/FR-011/FR-020 共同依赖的"token 起止偏移"基础设施缺口，本特性需一并新增；② 新分词器产出的"标点独立 cell"经既有 `PinyinLattice::toDisplayText()`/`toAnnotatedPromptText()` 既有渲染规则处理后与现状输出逐字符等价，**两个既有渲染方法本身无需任何代码改动**；③ `rawInput`（QML 本地编辑缓冲区）与 C++ 状态刻意解耦（既有设计），故候选替换只能由 C++ 计算结果、QML 显式回写+重新提交，不能让 C++ 单方面"推"文本进 QML。

## Technical Context

**Language/Version**: C++23（沿用 001～006 既有 `CMakeLists.txt` 配置，本特性未变更语言标准）；QML（Qt6 Quick），沿用既有 `FloatingWindow.qml` 结构。

**Primary Dependencies**: Qt6（既有依赖，`Q_PROPERTY`/`Q_INVOKABLE`/信号机制复用既有模式，零新增第三方依赖）；复用既有 `FastCorrector`/`PinyinTrie`/`EnglishWordList`/`PinyinLattice`/`SessionController` 基础设施，不引入新的库/模块。

**Storage**: 不引入任何新的持久化存储；候选列表、光标锚定目标词、"是否前缀模式"均为无状态派生结果（随每次 `buildLattice()` 重新计算），不新增数据库表/文件（spec Assumptions/Key Entities 明确要求）。

**Testing**: 延续 GoogleTest + `gtest_discover_tests`；涉及以下测试文件（`research.md` §9）：
- `tests/fastpath/FastCorrectorTest.cpp`：更新 2 个既有断言（`EnglishWordListInterceptsCommonWordBeforePinyinPipeline`/`EnglishWordListLookupIsCaseInsensitive`，按 FR-008 由"短路单候选"改为"合并后固定置顶"）；新增分词偏移量/前缀预测触发条件/英文候选强制置顶（多候选场景）测试。
- `tests/fastpath/PinyinTrieTest.cpp`：新增双溢出上限参数测试。
- `tests/fastpath/PinyinLatticeTest.cpp`：新增 `tokenStart`/`tokenEnd`/`currentTokenIndex` 字段测试。
- `tests/fastpath/EnglishWordListTest.cpp`（**新增文件**，类比既有 `PinyinTrieTest.cpp` 先例）：覆盖前缀树 `queryByPrefix()`/排序/大小写不敏感等场景。
- `tests/session/SessionControllerTest.cpp`：新增 `selectCandidate()`/`candidateReplacementReady`/`currentCandidates`/光标位置穿透测试。

**Target Platform**: 延续 001～006 既有桌面三平台 Windows/macOS/Linux 支持范围，未扩大/缩小目标平台集合。

**Project Type**: desktop-app（延续既有单一常驻 Qt6 Quick 进程，不新增独立窗口/进程/服务）。

**Performance Goals**（引用 spec Success Criteria，非本阶段新定义指标）：
- SC-001/SC-003/SC-006：候选刷新、前缀预测候选刷新均需与既有击键响应处于同一延迟量级（FR-016 不依赖慢流/语义推理，候选计算全程纯内存快流计算）。
- SC-002：100% 场景下英文/拼音候选可通过数字键互相切换。
- SC-004：新默认尺寸完整容纳 9 项候选不截断。

**Constraints**:
- `submitKeystrokes(text)`/`buildLattice(text)` 既有单参数调用形式 MUST 保持源码兼容（新增形参均带默认值，`research.md` §3，用户已决策）。
- `EnglishWordList::loadFromFile()`/`fromWords()`/`contains()`/`size()` 对外签名 MUST 不变（`data-model.md` 五）。
- MUST NOT 引入"格位索引 + 用户覆盖状态"一类附加状态机制（FR-006/spec Assumptions）。
- MUST NOT 改变 `PinyinLattice::toDisplayText()`/`toAnnotatedPromptText()` 对外渲染结果（`research.md` §1 已验证新分词方案与现状等价）。
- MUST NOT 依赖本地 LLM/实时语义推理计算候选排序（FR-016）。

**Scale/Scope**: 本特性修改范围限定于 `babel/src/fastpath/{FastCorrector,PinyinTrie,EnglishWordList,PinyinLattice}.{h,cpp}`、`babel/src/session/SessionController.{h,cpp}`、`babel/content/FloatingWindow.qml` 及对应测试文件，另需同步更新 002 契约文件中的 Top-K 数值引用；不新增新的 QML 文件，不新增第三方依赖。

本计划在设计过程中**新发现 1 项**命中宪章判定程序 (a)/(b)（`submitKeystrokes()`/既有 Q_INVOKABLE 公共契约签名扩展策略，直接影响外部可观察接口与既有测试源码兼容性）的架构选择点；**用户已在本轮 `/speckit.plan` 对话中就该项作出显式决策**：采纳方案 B（带默认值形参，向后兼容），详见 `research.md` §3。该决策已回填至本文件 Technical Context/Constitution Check 与全部 Phase 1 设计产物，无矛盾遗留。

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

*Language (宪章《语言与文档规范》)：本 plan 及同特性 research/data-model/quickstart/contracts 的说明性文字 MUST 使用简体中文；API/路径/库名等标识符可保留英文。*（本文件及全部同特性产物均已遵循。）

**原则 I–VI 逐条核对**：

- **原则 I（显式需求是唯一授权范围）**：本计划全部设计内容均可追溯至 `spec.md` FR-001～FR-023、Key Entities、两轮 Clarifications 原文，或本阶段向用户提出并取得显式决策的 `submitKeystrokes()` 签名方案（见上）。未引入任何未在 spec.md 中声明的功能/字段（如未新增候选个性化学习、未新增鼠标点选、未新增候选翻页——均为 spec Out of Scope 明确排除项）。
- **原则 II（未决策不得默认，Assumption 不得充当决策）**：`research.md` 全部"Assumption"条目（§1 新旧渲染等价性推导、§4 多英文候选次优排序规则、§5 英文词权重换算公式）均附客观依据（代码阅读结论/数学推导/既有数值量级比对），不承载任何改变外部可观察行为的未决选择；唯一命中判定程序 (a)/(b) 的真实架构分歧（`submitKeystrokes()` 签名策略）已通过提问取得用户显式决策，未由 Agent 代为拍板。
- **原则 III（推荐不是批准）**：本轮对话中向用户提出的 3 个编号方案（A/B/C）+ 1 个 Recommendation，用户明确选择"B"并确认其余 5 项已自主决定的方案无异议——这是有效批准（用户选择编号），已在 `research.md` §3 如实记录。
- **原则 IV（内部细节低摩擦，外部行为高控制）**：`EnglishWordList` 前缀树是否复用 `PinyinTrie::TrieNode`（决定：新建独立结构）、`PinyinTrie` 双上限常量的具体数值（`kMaxPrefixPredictionOverreach = 3`，内部调参起点）、浮窗新尺寸具体像素值（480×480）、候选区域 QML 视觉样式、`currentCandidates` 的 `QVariantMap` 字段命名——均不改变任何外部契约/数据模型/权限/安全/性能量级，已按原则 IV 自主决定并记录于 `research.md`/`data-model.md`，不升级为 `[NEEDS CLARIFICATION]`。
- **原则 V（最小必要修改与实施边界）**：Project Structure 下方"触碰文件清单"逐一说明必要性；新增的唯一测试文件 `tests/fastpath/EnglishWordListTest.cpp` 对应 FR-018 新增的真实可测试单元（前缀树），非顺便重构；002 `contracts/llm-prompt-annotation-contract.md` 的"K=8→9"同步是该文件自身预先声明允许的调整（"只需同步更新本文件"），002 其余历史产物（`plan.md`/`research.md`/`data-model.md`/`quickstart.md`/`tasks.md`）**不**追溯改写（保持历史决策记录真实性，原则 V"MUST NOT 修改与本需求无必要关系的既有模块"的反向推论——它们是已完成特性的历史快照，不是面向未来的活契约）。
- **原则 VI（预授权质量基线不得发明产品语义）**：新增公共入口 `selectCandidate(int number)` 的输入校验（越界/无当前词时 no-op，不抛异常）、QML 数字键拦截新增的修饰键守卫（`Qt.NoModifier`）均为新增逻辑的既有基线要求，未改变任何既有错误语义/测试断言/模块结构。

**判定程序门禁**：对本 plan 中每一项用户未显式指定的事项，已逐条执行宪章《判定程序》(a)–(e)。**1 项**命中 (a)/(b) 的事项（`submitKeystrokes()` 签名扩展策略）已取得用户显式决策并回填（见上）；其余事项判定结果全部为"否"（不改变外部可观察行为/公共契约数据模型/安全隐私/依赖部署/成本性能量级），已按原则 IV 自主决定并记录。本计划**无遗留** `[NEEDS CLARIFICATION]`。

**Gate 结论**：**PASS**——唯一一项待用户显式决策的架构分歧点已由用户显式决策（方案 B）并回填至 `research.md`/`data-model.md`/`contracts/`，无条件通过。

**Post-Design Re-check**（Phase 1 设计产物完成后复查）：`data-model.md`/`contracts/*.md` 均已据用户决策写入最终值，未留存候选方案占位；`contracts/fastpath-candidate-generation-contract.md`/`qml-cpp-session-interface-increment.md`/`candidate-selection-replacement-contract.md` 三份契约均已明确各自适用范围与强约束边界，均不构成新的 `[NEEDS CLARIFICATION]`。**本次复查结论：PASS（无条件通过，无新增违规，无遗留事项）**，可直接进入 `/speckit.tasks` 的最终排期与 `/speckit.implement` 的实现阶段。

## Project Structure

### Documentation (this feature)

```text
specs/007-candidate-manual-correction/
├── plan.md                                              # 本文件（/speckit.plan 命令输出）
├── research.md                                          # Phase 0 输出（含 §3 用户决策，无遗留 NEEDS CLARIFICATION）
├── data-model.md                                        # Phase 1 输出
├── quickstart.md                                        # Phase 1 输出（12 个验证场景）
├── contracts/
│   ├── fastpath-candidate-generation-contract.md        # FR-003/004/005/007/008/009/010/016/017/018/022/023
│   ├── qml-cpp-session-interface-increment.md           # FR-001/002/006/010/011/012/013/014（002 契约第四次增量）
│   └── candidate-selection-replacement-contract.md      # FR-004/006/007/011/013/015/019/020/021
└── tasks.md                                              # Phase 2 输出（/speckit.tasks 命令，本命令不创建）
```

### Source Code (repository root)

本特性严格限定于以下既有文件的增量修改 + 1 个必要的新增测试文件（原则 V"implement MAY 新增该任务模块下必需的测试文件"）：

```text
babel/src/fastpath/PinyinLattice.h           # 新增 CandidateEntry::Source::EnglishPrefixTrie；
                                              # PinyinLatticeCell::tokenStart/tokenEnd；
                                              # PinyinLattice::currentTokenIndex
babel/src/fastpath/PinyinLattice.cpp         # 无需代码改动（research.md §1 已验证渲染结果等价）
babel/src/fastpath/FastCorrector.h           # buildLattice() 新增 cursorPosition=-1 形参；
                                              # kTopKCandidates 8→9；新增 public static
                                              # isWordBoundaryDelimiter()
babel/src/fastpath/FastCorrector.cpp         # 分词器重写（§1/§2）；buildLatticeCell() 新增
                                              # enablePrefixPrediction 形参；候选合并/强制置顶/
                                              # currentTokenIndex 计算逻辑（§4）
babel/src/fastpath/PinyinTrie.h              # 两个 overreach 常量迁移至头文件（public static
                                              # constexpr）；queryByFullPinyin() 新增默认形参
babel/src/fastpath/PinyinTrie.cpp            # 对应实现调整（常量定义迁移）
babel/src/fastpath/EnglishWordList.h         # 重构为前缀树；新增 PrefixMatch/queryByPrefix()
babel/src/fastpath/EnglishWordList.cpp       # 对应实现（TrieNode + 词频排名权重换算）
babel/src/session/SessionController.h       # submitKeystrokes() 新增 cursorPosition=-1 形参；
                                              # 新增 currentCandidates 属性/selectCandidate()
                                              # 方法/candidateReplacementReady+
                                              # currentCandidatesChanged 信号
babel/src/session/SessionController.cpp      # 对应实现
babel/src/session/DraftTypes.h               # 不改动（既有 RawDraftText::cursorPosition 字段
                                              # 首次被实际赋值使用）
babel/content/FloatingWindow.qml             # 新增候选区域（竖排编号展示）；数字键 1–9 拦截；
                                              # 窗口尺寸 480×270→480×480；submitKeystrokes()
                                              # 调用点新增光标参数；新增 candidateReplacementReady
                                              # 的 Connections 处理函数

tests/fastpath/FastCorrectorTest.cpp         # 更新 2 个既有断言 + 新增分词偏移/前缀预测/
                                              # 英文候选置顶测试
tests/fastpath/PinyinTrieTest.cpp            # 新增双溢出上限参数测试
tests/fastpath/PinyinLatticeTest.cpp         # 新增 tokenStart/tokenEnd/currentTokenIndex 测试
tests/fastpath/EnglishWordListTest.cpp       # 新增文件（前缀树单元测试，类比既有
                                              # PinyinTrieTest.cpp 先例）
tests/session/SessionControllerTest.cpp      # 新增 selectCandidate/candidateReplacementReady/
                                              # currentCandidates/光标穿透测试

specs/002-lattice-dict-model-switch/contracts/llm-prompt-annotation-contract.md
                                              # "K = 8" → "K = 9" 数值引用同步（已完成，该文件
                                              # 预先声明允许此类调整，不改变标注语法本身）
```

**Structure Decision**：延续 001～006 既有"单一桌面应用项目，特性产物以 `specs/<NNN>/` 目录隔离、不建立独立 feature branch"的既定结构；本特性不引入任何新的顶层目录或新的构建目标，不新增 QML 文件（候选区域作为 `FloatingWindow.qml` 既有 `Column` 的新增子项实现）。

## Complexity Tracking

*无 Constitution Check 违规，本节不适用。*
