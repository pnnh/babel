# Data Model: 候选词列表手动纠偏

**Spec**: [spec.md](./spec.md) ｜ **Research**: [research.md](./research.md)

## 阅读说明

本文件记录本特性对既有实体（`babel/src/fastpath/PinyinLattice.h`、`PinyinTrie.h`、`EnglishWordList.h`、`babel/src/session/DraftTypes.h`、`SessionController.h`）的**增量变更**。未被下表提及的字段/方法原样有效，未变更。全部字段已回填最终值（唯一一项需要用户决策的架构分歧——`submitKeystrokes()` 签名扩展策略——已在本轮 `/speckit.plan` 对话中取得用户显式决策，见 `research.md` §3），不存在"【待决】"占位。

---

## 一、`CandidateEntry`（`babel/src/fastpath/PinyinLattice.h`）

| 变更 | 内容 |
|---|---|
| `Source` 枚举新增取值 | `EnglishPrefixTrie`：前缀预测产出的英文候选（区别于既有 `EnglishWordListPassthrough` 精确命中），`research.md` §4/§5 |

其余字段（`surfaceForm`/`rawWeight`/`normalizedWeight`/`latinOriginLengthMismatch`）不变。

## 二、`PinyinLatticeCell`（同文件）

| 新增字段 | 类型 | 语义 | 对应 FR |
|---|---|---|---|
| `tokenStart` | `int` | 该 token 在原始 `rawInput` `QString` 中的起始字符偏移（含） | FR-006/FR-011/FR-020 |
| `tokenEnd` | `int` | 该 token 在原始 `rawInput` `QString` 中的结束字符偏移（不含，`[tokenStart, tokenEnd)`） | 同上 |

默认值均为 `0`（与既有 `tokenIndex` 默认值风格一致）；由 `FastCorrector::buildLattice()` 新分词器产出时一并赋值（`research.md` §2）。`isAmbiguous()` 等既有方法不变。

## 三、`PinyinLattice`（同文件）

| 新增字段 | 类型 | 语义 | 对应 FR |
|---|---|---|---|
| `currentTokenIndex` | `int`，默认 `-1` | 光标当前锚定的词在 `cells` 中的下标；`-1` 表示"无当前词"（光标落在空白/标点处，或输入为空） | FR-011/FR-013/FR-019 |

`toDisplayText()`/`toAnnotatedPromptText()` **不变**（`research.md` §1 已论证新分词产出的标点独立 cell 与现状渲染结果等价，无需修改这两个方法）。

## 四、`PinyinTrie`（`babel/src/fastpath/PinyinTrie.h`）

| 变更 | 内容 | 对应 FR |
|---|---|---|
| 新增 `public static constexpr` 常量 | `kMaxFullPinyinPrefixOverreach = 1`（原 `.cpp` 匿名命名空间值迁移至头文件，数值不变） | 既有行为延续 |
| 新增 `public static constexpr` 常量 | `kMaxPrefixPredictionOverreach = 3`（前缀预测候选池专用，内部调参起点） | FR-022 |
| `queryByFullPinyin()` 签名增量 | 新增带默认值形参 `std::size_t maxOverreach = kMaxFullPinyinPrefixOverreach`（原 2 参调用点不变） | FR-022 |

`queryByAbbrev()`/`TrieMatch`/`build()` 不变。

## 五、`EnglishWordList`（`babel/src/fastpath/EnglishWordList.h`，内部结构重写）

| 变更 | 内容 | 对应 FR |
|---|---|---|
| 新增嵌套类型 | `struct PrefixMatch { std::string word; double weight = 0.0; }` | FR-018 |
| 新增方法 | `std::vector<PrefixMatch> queryByPrefix(const std::string &prefix, std::size_t maxResults = 9) const`，按 `weight` 降序返回 | FR-017/FR-018 |
| 内部存储 | 由 `std::unordered_set<std::string>` 重构为独立前缀树（新增私有 `TrieNode`，不复用 `PinyinTrie::TrieNode`），`research.md` §5 | FR-018 |
| 权重来源 | `weight = totalCount - index`（`index` 为文件行序/词频排名，0-based），取代原"纯存在性、无权重"语义 | FR-018 |

`loadFromFile()`/`fromWords()`/`contains()`/`size()` **签名与对外行为不变**（返回类型、参数列表均不变，仅内部实现由 `unordered_set` 改为 Trie）。

## 六、`RawDraftText`（`babel/src/session/DraftTypes.h`）

**不新增字段**——既有 `int cursorPosition = 0;` 字段此前已声明但从未被任何调用点赋值（纯历史遗留的未使用字段）；本特性首次在 `submitKeystrokes()` 中为其赋值（`research.md` §3），字段定义本身不变。

## 七、`SessionController`（`babel/src/session/SessionController.h`/`.cpp`）

### 1. 方法签名增量

| 方法 | 变更 |
|---|---|
| `Q_INVOKABLE void submitKeystrokes(const QString &text, int cursorPosition = -1)` | 新增带默认值形参；`-1` 退化为 `text.length()`，`research.md` §3（用户已决策方案 B） |

### 2. 新增只读属性

| 属性 | 类型 | 说明 |
|---|---|---|
| `currentCandidates` | `Q_PROPERTY(QVariantList currentCandidates READ currentCandidates NOTIFY currentCandidatesChanged)` | 元素为 `QVariantMap{number:int, text:QString}`，派生自 `m_normalizedCandidate.lattice`（`currentTokenIndex`/`cells[...].candidates`），`research.md` §6；无当前词时为空列表 |

### 3. 新增可调用方法

| 方法 | 说明 |
|---|---|
| `Q_INVOKABLE void selectCandidate(int number)` | 校验 `number` 落在 `[1, cell.candidates.size()]` 且存在当前词后，计算替换文本与新光标位置，`emit candidateReplacementReady(...)`；**不**直接修改 `m_rawDraft`/`m_normalizedCandidate`（`research.md` §6 设计取舍说明） |

### 4. 新增信号

| 信号 | 说明 |
|---|---|
| `void candidateReplacementReady(const QString &newText, int newCursorPosition)` | QML 侧据此回写 `rawInput.text`/`cursorPosition` 并显式重新调用 `submitKeystrokes()`（FR-006/FR-007/FR-020） |
| `void currentCandidatesChanged()` | `Q_PROPERTY` 既有 NOTIFY 惯例，`submitKeystrokes()` 完成一次 `buildLattice()` 后统一 emit |

### 5. 内部状态

不新增成员字段——`m_rawDraft.cursorPosition`（既有字段，六已说明）与 `m_normalizedCandidate.lattice.currentTokenIndex`（三已说明）已足够承载全部所需状态，`selectCandidate()`/`currentCandidates()` 均为基于既有成员的纯派生计算，不引入"格位索引+用户覆盖状态"一类附加状态机制（与 spec Assumptions 第二条完全吻合）。

## 八、`FastCorrector`（`babel/src/fastpath/FastCorrector.h`/`.cpp`）

| 变更 | 内容 | 对应 FR |
|---|---|---|
| `buildLattice()` 签名增量 | 新增带默认值形参 `int cursorPosition = -1`（`research.md` §3，既有 13 处单参调用点不受影响） | FR-011 |
| `kTopKCandidates` | `8` → `9` | FR-010 |
| `buildLatticeCell()` 签名增量（私有方法，无外部兼容性约束） | 新增 `bool enablePrefixPrediction` 形参 | FR-017/FR-022/FR-023 |
| 新增 `public static` 方法 | `static bool isWordBoundaryDelimiter(QChar ch)`——FR-005 分隔符判定的唯一权威实现，供 `SessionController::selectCandidate()` 复用（避免清单重复维护） | FR-005/FR-020 |
| `looksLikeLatin()` | 不变（现在传入的 token 已由新分词器正确剥离标点，判断逻辑本身无需修改） | — |

## 九、`FloatingWindow.qml` 布局增量

| 变更 | 内容 |
|---|---|
| 窗口尺寸 | `480×270` → `480×480`（`research.md` §7） |
| 新增候选区域 | `Column` + `Repeater`，绑定 `SessionController.currentCandidates`，`visible` 绑定其 `length > 0`（同时承担 FR-019 语义） |
| `rawInput.onTextEdited` | `submitKeystrokes(text)` → `submitKeystrokes(text, cursorPosition)` |
| `rawInput.Keys.onPressed` | 新增数字键 1–9 拦截逻辑（FR-012），`research.md` §6 |
| 新增 `Connections` 处理函数 | `onCandidateReplacementReady(newText, newCursorPosition)`，`research.md` §6 |

## 十、与 002 `contracts/llm-prompt-annotation-contract.md` 的关系

该契约"K = 8"数值引用随 FR-010 同步更新为"K = 9"；标注语法本身（`⟦token:候选 权重|...⟧`）**不变**（`research.md` §8）。
