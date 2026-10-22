# Research: 候选词列表手动纠偏

**Spec**: [spec.md](./spec.md)

## 阅读说明

本文件记录 `/speckit.plan` 阶段对 spec.md 全部 23 条 FR（两轮澄清）的技术落地方式研究，均基于**直接阅读现有代码**（`FastCorrector.{h,cpp}`、`PinyinTrie.{h,cpp}`、`EnglishWordList.{h,cpp}`、`PinyinLattice.{h,cpp}`、`SessionController.{h,cpp}`、`FloatingWindow.qml`、`DraftTypes.h`）得出，而非网络搜索/最佳实践消解。本阶段与用户已完成 1 轮交互式架构决策（`submitKeystrokes()` 签名扩展策略，见 §3，已选定方案 B），其余事项经宪章判定程序 (a)–(e) 核对后均落入"内部细节"（原则 IV），由 Agent 自主决定并在本文件记录依据，不升级为新的 `[NEEDS CLARIFICATION]`。

---

## §1 FR-005：分词器重写——从"仅按空白字符切分"扩展为"空白 + 标点双重边界识别"

### 关键发现：`FastCorrector::buildLattice()` 当前用 `std::istringstream stream(input); while (stream >> token)` 分词，存在两个此前未被显式记录的副作用

1. **空白本身从不被物化为格位（cell）**：`istringstream >> token` 会静默跳过任意长度的前导空白，不为其产生任何 token；`PinyinLattice::toDisplayText()` 依赖这一既有事实，用"相邻两个输出 token 均为英文时才插入 1 个空格"的启发式规则重建分隔符（见该方法注释），而不是逐字保留原始空白。
2. **标点此前从未与相邻词断开**：`,`/`.`/`!` 等字符不是空白，因此如"hen,"会被当作**一个完整 token**（"hen,"）参与 `looksLikeLatin()` 判断——该函数要求全部字符为字母，含逗号的字符串必然判定为假，导致整个"hen,"落入非拼音透传分支原样回显，"hen"本身从未进入拼音/英文匹配管线。这正是 spec Clarifications 中"用户已输入'hen,'"示例与 FR-005 要解决的根本问题。

### 设计决策：新增统一分词器，按"连续空白 / 连续标点 / 连续非分隔符"三态扫描，不改变 `PinyinLattice::toDisplayText()`/`toAnnotatedPromptText()` 本身

新分词算法（`FastCorrector.cpp` 内部实现，对外签名变化见 §6）：

1. 跳过连续空白字符（含空格/制表符/换行符，延续既有 `istringstream` 对空白的定义）——**不为空白产生 cell**（与现状完全一致，`toDisplayText()` 的既有空格重建启发式不受影响，验证见下文）。
2. 遇到连续"分隔标点"字符（见下表）：消费整段连续标点为**一个独立 cell**，`source = Passthrough`（复用既有机制，非拼音/无字母内容必然 `looksLikeLatin()==false`）。
3. 其余字符（字母/汉字/数字/连字符 `-`/下划线 `_`/全角 `＿`/未在上表中列出的任何其他字符）视为"词字符"，连续消费为一个"候选格位 token"，交由既有 `buildLatticeCell()` 处理（签名增量见 §6）。
4. 重复 1–3 直至字符串结束。

**验证"不改变 `toDisplayText()`/`toAnnotatedPromptText()` 行为"**：这两个方法均已对 `isLatin` 做逐 cell 判断决定是否插入空格；标点 cell 的 `surfaceForm`（如 `","`）本身 `looksLikeLatin()` 恒为假，与"标点此前粘连在词尾、同样判定 `isLatin=false`"的历史行为完全等价——唯一区别是现在"hen"能被**单独、正确**地送入拼音/英文匹配管线，"," 作为独立 cell 原样回显，两者拼接顺序不变。经手工推演"hen, shi"/"hen shi"/"hen want" 三个样例（分别验证标点分隔、纯空白分隔、两个英文候选间保留空格的既有规则），结果与预期肉眼一致，**两个既有渲染方法本身无需任何代码改动**。

### FR-005 分隔标点完整清单（半角 → 全角对应，均为"分隔标点"；连字符/下划线及其全角对应明确排除）

| 半角 | Unicode | 全角对应 | Unicode |
|---|---|---|---|
| `.` | U+002E | `。` | U+3002 |
| `,` | U+002C | `，` | U+FF0C |
| `;` | U+003B | `；` | U+FF1B |
| `:` | U+003A | `：` | U+FF1A |
| `!` | U+0021 | `！` | U+FF01 |
| `?` | U+003F | `？` | U+FF1F |
| `'` | U+0027 | `‘` `’` | U+2018 / U+2019 |
| `"` | U+0022 | `“` `”` | U+201C / U+201D |
| `(` | U+0028 | `（` | U+FF08 |
| `)` | U+0029 | `）` | U+FF09 |
| `[` | U+005B | `【` | U+3010 |
| `]` | U+005D | `】` | U+3011 |
| `{` | U+007B | `｛` | U+FF5B |
| `}` | U+007D | `｝` | U+FF5D |
| `=` | U+003D | `＝` | U+FF1D |
| `+` | U+002B | `＋` | U+FF0B |
| `/` | U+002F | `／` | U+FF0F |
| `\` | U+005C | `＼` | U+FF3C |
| `\|` | U+007C | `｜` | U+FF5C |
| `@` | U+0040 | `＠` | U+FF20 |
| `#` | U+0023 | `＃` | U+FF03 |
| `$` | U+0024 | `＄` | U+FF04 |
| `%` | U+0025 | `％` | U+FF05 |
| `^` | U+005E | `＾` | U+FF3E |
| `&` | U+0026 | `＆` | U+FF06 |
| `*` | U+002A | `＊` | U+FF0A |
| `~` | U+007E | `～` | U+FF5E |
| `` ` `` | U+0060 | `｀` | U+FF40 |

**明确排除（MUST NOT 作为分隔符）**：`-`（U+002D）、`_`（U+005F）及全角 `＿`（U+FF3F）——按 FR-005 原文，不计入上表，视为普通"词字符"参与 token 消费（如 "state-of-the-art" 保持为一个不可分割 token，交由既有 `looksLikeLatin()` 判定——含连字符的 token 该函数恒判定为假，落入既有"非拼音透传"兜底分支原样回显，不要求额外匹配该类复合词，与 Assumptions"视觉样式/未尽边界由实现自主确定"同一授权层级，不构成新契约）。

**可复用性设计**：该分隔符判定逻辑封装为一个跨文件共享的纯函数 `FastCorrector::isWordBoundaryDelimiter(QChar ch)`（新增 `public static` 方法），供 `SessionController::selectCandidate()`（§6/§7）在计算"替换位置后是否已存在分隔符"（FR-020）时复用**同一份定义**，避免两处清单各自维护导致后续不同步。

---

## §2 FR-011/FR-006/FR-020：光标锚定定位、格位偏移量与替换算法

### 决策：`PinyinLatticeCell` 新增 `tokenStart`/`tokenEnd` 字段（原始 `QString` 中的字符偏移区间 `[tokenStart, tokenEnd)`），分词阶段一并产出

当前 `buildLattice()` 先 `rawInput.toStdString()` 转为 UTF-8 `std::string` 再用 `istringstream` 切分，完全丢失字符偏移信息。新实现改为**直接在 `QString` 上逐 `QChar` 扫描**（而非转 `std::string` 后扫描），原因：

1. QML `TextInput.cursorPosition` 是基于 `QString`/UTF-16 code unit 的位置，必须与分词偏移量使用同一坐标系，否则 FR-011 的光标锚定会在含非 ASCII 字符（汉字、全角标点）时发生错位。
2. 常用汉字与本清单全部全角标点均落在 BMP 内，每个字符对应单个 UTF-16 code unit/单个 `QChar`，不涉及代理对（surrogate pair），现有代码库其余部分亦未处理超出 BMP 的字符（如 emoji），本次不扩大该既有简化范围。

每个 token 扫描完成后，转 `token.toStdString()`（UTF-8）交给既有 `buildLatticeCell(tokenIndex, token, enablePrefixPrediction)`（签名增量见 §6），`tokenStart`/`tokenEnd` 直接记录在返回的 `PinyinLatticeCell` 上。标点/空白判定、cell 产出流程与 §1 一致。

### 光标锚定目标词（`currentTokenIndex`）判定规则

`PinyinLattice` 新增顶层字段 `int currentTokenIndex = -1`，由 `buildLattice(rawInput, cursorPosition)`（§6）在分词完成后一次性计算：

- 遍历全部"词字符"产生的 cell（**不含**标点 passthrough cell——光标落在标点上或标点与下一个词之间时不应展示该标点自身的"候选列表"，这不是候选词交互的产品意图所在；此判断不需要额外字段，分词器内部已经知道某个 span 是"词字符扫描"产生还是"标点扫描"产生，仅后者会被跳过），找到满足 `cell.tokenStart <= cursorPosition <= cell.tokenEnd` 的 cell，取其 `tokenIndex`。
- 若光标落在纯空白区间内（两侧均无覆盖该位置的词 cell）、或输入为空、或光标恰好落在标点 cell 内部：`currentTokenIndex` 保持 `-1`（无当前词）。这与 User Story 2 Edge Case 原文"候选列表切换为展示空白后新词（初始为空）对应的候选集合**或**进入无候选可展示状态"中后一种已被显式允许的表述完全一致，是本阶段的具体选择（内部细节，原则 IV）。
- `cursorPosition` 参数本身先做防御性钳制：`< 0` 或 `> rawInput.length()` 时钳制为 `rawInput.length()`（视作光标在文本末尾，与 `cursorPosition` 参数缺省值 `-1` 的退化行为完全一致，见 §6）。

### "前缀模式"（prefix mode）判定规则（FR-017 核心分支条件）

一个词 cell 是否处于"用户尚未输入完整"的前缀预测模式，判定依据**不是光标是否在该词内部**，而是——**该 token 结束位置 `tokenEnd` 之后，原始字符串中是否已经存在分隔符（空白或标点）**：

- 若 `tokenEnd == rawInput.length()`（token 后再无任何字符）**或** `tokenEnd < rawInput.length()` 但该处尚未分词完成（不会发生，分词已扫描到字符串末尾）→ 实际上只有"token 正好是整个输入的末尾"这一种情况成立 → 视为"尚未输入完整"，启用前缀预测分支。
- 若 `tokenEnd` 位置存在字符且该字符被 `isWordBoundaryDelimiter()` 判定为分隔符（空白或标点）→ 该词已有明确的结束边界 → 视为"已完整输入"，按既有默认规则处理，不启用前缀预测分支，**无论光标当前是否仍停留在该词内部编辑**（用户移动光标回去修改早先已完整的词时，该词依旧只应用默认保守 overreach，这与 FR-022"前缀预测候选池...MUST NOT 影响默认自动应用候选池"的字面表述完全吻合——"是否前缀模式"与"是否光标锚定词"是两个独立维度，一个词可以同时是"光标锚定的当前词"又是"已完整输入的词"）。

该规则同时解释了"前缀预测"与"光标锚定"两个独立 FR（FR-017 vs FR-011）如何在同一次 `buildLatticeCell()` 调用中共存：**是否启用前缀预测**取决于该 token 在原始字符串中是否已被分隔符终结（与光标位置无关）；**该 cell 是否作为 `currentTokenIndex` 展示给用户**则完全取决于光标位置（与是否前缀模式无关）。二者常见组合是"光标在最后一个尚未分隔的词内 → 二者同时成立"，但移动光标回编辑中段已完整词时，只有"光标锚定"成立、"前缀模式"不成立，行为合理。

### FR-006/FR-020 替换与自动分隔符算法（`SessionController::selectCandidate()`，详见 §7）

利用上述 `tokenStart`/`tokenEnd`：

```text
chosenText      = cell.candidates[number-1].surfaceForm
needsSeparator  = tokenEnd >= text.length() || !isWordBoundaryDelimiter(text.at(tokenEnd))
newText         = text.left(tokenStart) + chosenText + (needsSeparator ? " " : "") + text.mid(tokenEnd)
newCursorPos    = tokenStart + chosenText.length() + (needsSeparator ? 1 : 0)
```

`needsSeparator` 的判断即 FR-020"若替换位置紧邻的后一个字符已经是分隔符（空格或标点）则不重复插入"的字面实现；追加字符固定为半角空格（FR-020 原文"如空格"，内部细节，原则 IV）。

---

## §3 FR-011：`submitKeystrokes()`/`buildLattice()` 光标参数——用户已决策方案 B

**本轮 `/speckit.plan` 对话中已就该项架构分歧向用户提问并取得显式决策**（原则 III"用户委派"的反面——用户未委派、而是直接选择了编号方案，视为显式批准）：

- `SessionController::submitKeystrokes(const QString &text, int cursorPosition = -1)`——新增**带默认值**的形参，`-1` 表示"未显式提供光标位置"，内部退化为 `text.length()`（等价于当前"始终视作光标在末尾"的既有行为）。
- 对称地，`FastCorrector::buildLattice(const QString &rawInput, int cursorPosition = -1) const`——同一退化规则，保证 `tests/fastpath/*.cpp` 现有 13 处 `buildLattice(text)` 单参调用无需修改即可编译通过、行为不变（§1 已论证新分词器不改变既有渲染输出）。
- `tests/session/SessionControllerTest.cpp` 现有 2 处 `submitKeystrokes(QStringLiteral("草稿"))` 单参调用同理不受影响。

**选择理由（用户采纳的 Recommendation）**：文本与光标位置在同一次调用中原子传递，避免"分离两次调用"方案的时序竞争风险（用户同时移动光标与编辑文本时，分离调用可能导致候选基于过期光标位置计算）；同时符合宪章原则 V"最小必要修改"，不强制立即触碰现有测试断言。

`FloatingWindow.qml` 调用点相应从 `SessionController.submitKeystrokes(text)` 改为 `SessionController.submitKeystrokes(text, rawInput.cursorPosition)`（见 §6 QML 增量）。

---

## §4 FR-008/FR-009/FR-017/FR-018：候选合并与"英文优先"规则在多候选场景下的精确化

### 取消英文词表短路——合并逻辑变化点

`buildLatticeCell()` 现状：`m_commonEnglishWords.contains(token)` 命中后**立即 return**（短路，跳过 Trie/编辑距离管线）。FR-008 要求取消该短路，改为：无论是否命中英文词表，均继续执行既有 Trie 全拼/简拼/编辑距离合并流程；若命中英文词表（`contains()`），额外把该英文候选（`source = EnglishWordListPassthrough`，复用既有枚举值语义不变——现在只是不再触发提前返回）加入同一个 `merged` 候选池参与排序与 Top-K 截断。

### 新增前缀模式分支（仅当 §2 判定为"前缀模式"且 `token.size() >= 2`，FR-023）

1. 英文前缀：调用新增的 `m_commonEnglishWords.queryByPrefix(token, kTopKCandidates)`（§5），结果逐条转换为 `CandidateEntry{source = EnglishPrefixTrie}`（`CandidateEntry::Source` 新增枚举值，供诊断区分"精确命中"与"前缀联想"，不影响展示），按既有 `seen` 去重集合合并（与精确匹配用同一字符串去重，避免同一词同时以两种 source 重复出现在列表中）。
2. 拼音前缀放宽：全拼 Trie 查询改为显式传入 `PinyinTrie::kMaxPrefixPredictionOverreach`（§5）而非默认的 `kMaxFullPinyinPrefixOverreach`。
3. 非前缀模式（已完整输入的词，含光标回头编辑场景）：行为与现状完全一致（默认 `kMaxFullPinyinPrefixOverreach`，不触发英文前缀联想，仍执行既有精确命中合并）。

### "英文优先"在多候选场景下的精确化规则（新 Assumption，原则 II 允许记录客观推导，非臆测新需求）

FR-009 原文用单数"该候选"描述"英文候选固定排第 1 位"；FR-017/018 新引入后，同一格位可能同时产生**多个**英文来源候选（如 `token="co"` 的前缀预测可能命中 "co"/"con"/"cost"等多个英文词）。spec 未给出针对"多个英文候选"的排序细则，此处记录客观推导作为内部实现决策（原则 IV，未改变任何已有验收场景的可观察结果——1 对 1 "hen"场景下只有 1 个 EnglishWordListPassthrough 候选，新旧规则结果完全一致）：

- 对 `merged` 候选池中全部 `source ∈ {EnglishWordListPassthrough, EnglishPrefixTrie}` 的候选，取其中 `rawWeight` 最高者（即原始词频排名最靠前者，见 §5 权重换算），**强制**移至列表第 1 位（FR-009 字面行为）。
- 其余候选（含未被选中的"次优"英文候选）**不**额外加权、**不**整体前移，按各自 `rawWeight` 与全部拼音候选一起正常降序排序，占据第 2 位及以后——避免"多个英文候选集体霸占列表前排"这一 FR-009 原文未授权的扩张解释，保持"英文优先"例外规则的最小化（仅豁免单个候选，不豁免一类候选）。

### 编号与候选展示的最终裁决优先级（综合 FR-003/FR-009/FR-010）

1. 若存在英文来源候选：最佳者固定编号 1。
2. 其余全部候选（拼音 Trie 全拼/简拼/编辑距离回退/次优英文候选）按 `rawWeight` 降序依次编号 2…N。
3. 整体截断至 Top-K=9（FR-010），归一化 `normalizedWeight` 基于截断后子集计算（既有规则不变）。

---

## §5 FR-018/FR-022：`EnglishWordList` 前缀树重构 + `PinyinTrie` 双溢出上限参数

### `EnglishWordList`：新建独立 `TrieNode`（不复用 `PinyinTrie::TrieNode`）

`PinyinTrie::TrieNode` 是该类的 `private` 嵌套结构，承载"多候选+权重"的拼音专属语义（`std::vector<std::pair<std::string, double>> candidates`，因为多个不同汉字可共享同一拼音 key）；`EnglishWordList` 的语义是"单词 → 自身"的 1:1 关系（不存在"多个词共享同一拼写"的情形），复用会引入不必要的 `vector` 包装与跨类私有访问耦合。故新建一个结构更简单的独立 `TrieNode { children; bool isWord; double weight; }`，仅服务 `EnglishWordList` 内部。

**权重换算（词频排名 → `weight`）**：`loadFromFile()`/`fromWords()` 保持原静态工厂签名/返回类型完全不变，内部实现改为：① 按行序收集去重后的小写单词到一个 `vector`，得到 `totalCount`；② 以 `weight = totalCount - index`（`index` 为该词在文件中的 0-based 出现顺序，即原始词频排名）插入 Trie。该换算把英文词的权重映射到与真实拼音词库 `frequencyWeight`（实测范围约 1～27000+，见 `base.dict.yaml` 抽样）同一数量级，使 §4 "次优英文候选与拼音候选公平排序"的合并结果有意义，而非让权重普遍极小的英文候选永远沉底（内部调参决策，原则 IV，不改变任何已声明的外部契约/验收场景结果）。

```cpp
class EnglishWordList {
 public:
  struct PrefixMatch {
    std::string word;
    double weight = 0.0;
  };
  EnglishWordList() = default;
  static EnglishWordList loadFromFile(const std::filesystem::path &path);   // 签名不变
  static EnglishWordList fromWords(const std::vector<std::string> &words); // 签名不变
  bool contains(const std::string &token) const;                          // 行为不变（基于 Trie 精确匹配）
  std::vector<PrefixMatch> queryByPrefix(const std::string &prefix, std::size_t maxResults = 9) const;  // 新增
  std::size_t size() const;                                               // 行为不变
 private:
  struct TrieNode { std::unordered_map<char, std::unique_ptr<TrieNode>> children; bool isWord = false; double weight = 0.0; };
  std::unique_ptr<TrieNode> m_root;
  std::size_t m_wordCount = 0;
};
```

`queryByPrefix()` **不**引入类似 `PinyinTrie` 的"溢出上限"概念——该上限原本是为规避"英文单词被误判为拼音前缀"的假阳性风险（`want`→"万台"问题），这是拼音 Trie 特有的跨语义碰撞场景；`EnglishWordList::queryByPrefix()` 是"英文前缀→英文全词"的同语义联想，不存在对应的假阳性风险，只需 `maxResults` 控制结果规模（类比既有 `PinyinTrie::queryByAbbrev()` 对简拼场景同样不设溢出上限的既有先例）。

### `PinyinTrie` 双上限参数：新增带默认值形参 + 两个具名常量迁移至头文件

```cpp
class PinyinTrie {
 public:
  /// 默认（非前缀预测）场景全拼溢出上限（既有值，原 .cpp 匿名命名空间迁移至此，
  /// 供 FastCorrector 显式引用）。
  static constexpr std::size_t kMaxFullPinyinPrefixOverreach = 1;
  /// FR-022 新增：前缀预测候选池专用、单独放宽的溢出上限（内部调参起点，类比
  /// 既有 kTopKCandidates 先例，待 SC-006 实测结果调整，不影响默认候选池）。
  static constexpr std::size_t kMaxPrefixPredictionOverreach = 3;

  std::vector<TrieMatch> queryByFullPinyin(const std::string &pinyin, std::size_t maxResults = 5,
                                            std::size_t maxOverreach = kMaxFullPinyinPrefixOverreach) const;
  ...
};
```

`queryByAbbrev()` 签名/行为不变。既有 `tests/fastpath/PinyinTrieTest.cpp` 中全部 1 参/2 参调用形式因新增形参带默认值而继续编译通过、行为不变。

---

## §6 QML ↔ C++ 接口增量（候选展示 + 数字键选择 + 替换回写）

### 新增只读属性 `currentCandidates`（`QVariantList`，元素为 `QVariantMap{number:int, text:QString}`）

沿用既有 `availableModels()`/`recommendedProfilesVariant()` 的 `QVariantList<QVariantMap>` 既有风格（显式具名字段，优于隐式依赖 QML `Repeater` 的 `index`，与代码库既有惯例一致，原则 II）：

```cpp
QVariantList SessionController::currentCandidates() const {
  QVariantList list;
  const auto &lattice = m_normalizedCandidate.lattice;
  if (lattice.currentTokenIndex < 0 || lattice.currentTokenIndex >= static_cast<int>(lattice.cells.size())) {
    return list;  // 无当前词：空列表，QML 据此隐藏候选区域（FR-019 自然满足，见下）
  }
  const auto &cell = lattice.cells[lattice.currentTokenIndex];
  for (std::size_t i = 0; i < cell.candidates.size(); ++i) {
    QVariantMap entry;
    entry[QStringLiteral("number")] = static_cast<int>(i + 1);
    entry[QStringLiteral("text")] = cell.candidates[i].surfaceForm;
    list.push_back(entry);
  }
  return list;
}
```

### 新增 `Q_INVOKABLE void selectCandidate(int number)`（算法见 §2，内部读取 `m_rawDraft.content`/`m_rawDraft.cursorPosition`，不改变自身状态，仅 `emit candidateReplacementReady(newText, newCursorPosition)`）

**设计取舍**：`selectCandidate()` 本身**不**直接调用 `submitKeystrokes()` 更新内部状态——`rawInput`（QML 本地编辑缓冲区，参见 `FloatingWindow.qml` 既有注释"不与外部数据绑定"）是文本的唯一事实来源，C++ 无法单方面改写它；设计为"C++ 只计算结果、QML 负责落地并显式重新提交"，与"用户手动键入触发 `onTextEdited → submitKeystrokes()`"走同一条既有路径，不新增第二条状态更新通路，避免状态来源产生分歧。

### 新增信号 `void candidateReplacementReady(const QString &newText, int newCursorPosition)`

QML 侧 `Connections` 新增处理函数：

```qml
function onCandidateReplacementReady(newText, newCursorPosition) {
    rawInput.text = newText;
    rawInput.cursorPosition = newCursorPosition;
    SessionController.submitKeystrokes(newText, newCursorPosition);
}
```

（`TextInput.text` 程序化赋值不会触发 `onTextEdited`，故需显式调用 `submitKeystrokes()`，与 006 之前既有代码对 Qt 信号语义的既有认知一致。）

### `rawInput` 的 `onTextEdited`/`Keys.onPressed` 增量

```qml
onTextEdited: SessionController.submitKeystrokes(text, cursorPosition)

Keys.onPressed: (event) => {
    if (event.key >= Qt.Key_1 && event.key <= Qt.Key_9 && event.modifiers === Qt.NoModifier) {
        const num = event.key - Qt.Key_0;
        if (num <= SessionController.currentCandidates.length) {
            SessionController.selectCandidate(num);
            event.accepted = true;
        }
        // 未 accepted：回落到默认插入行为（FR-012"超出候选数量时数字正常插入"）。
    }
}
```

`event.modifiers === Qt.NoModifier` 守卫为新增公共入口的防御性校验（宪章原则 VI 第 1 条"为新增的公共入口做输入校验"），防止误劫持未来可能引入的 `Ctrl+数字`/`Cmd+数字` 组合键（当前代码库无此类既有绑定，纯前瞻性防御，不改变任何已声明行为）。

### 候选区域 QML 结构（竖排编号展示）

```qml
Column {
    width: parent.width
    visible: SessionController.currentCandidates.length > 0
    spacing: 2
    Repeater {
        model: SessionController.currentCandidates
        delegate: Text {
            width: parent.width
            color: "#cccccc"
            font.pixelSize: 13
            text: modelData.number + ". " + modelData.text
        }
    }
}
```

`visible: ... .length > 0` 同时承担 FR-019 的"非输入状态不展示"语义（见下）。

### FR-019"输入/编辑进行中"状态边界——决策：不新增空闲超时定时器，复用既有状态与"是否存在当前词"派生状态

FR-019 原文明确将"是否需要空闲超时阈值"这一具体判定细节的决定权交给 `/speckit.plan` 阶段（"具体判定规则...留待 `/speckit.plan`/`/speckit.tasks` 设计阶段...确定，不改变本条...这一外部可观察行为本身"）。核对现有机制后决定**不引入新的计时器/状态**，理由：

- 浮窗整体隐藏（`FloatingSessionState::Hidden`）时 `floatingWindow.visible = false`，候选区域随整个窗口一起不可见，天然满足。
- 设置页面打开时（`SessionController.settingsVisible == true`）`mainView.visible = false`，候选区域（属于 `mainView` 子树）随之不可见，天然满足"非输入焦点场景不展示"。
- 无当前词时（`currentTokenIndex == -1`，见 §2，覆盖"刚唤起浮窗尚未输入""光标停留在纯空白/标点处"等场景）`currentCandidates` 为空列表，候选区域 `visible` 绑定自动为 `false`。
- FR-013"始终展示候选列表(即便唯一候选)"与 FR-015"用户可以忽略候选列表、直接按回车/空格确认"均未附带任何"闲置多久后消失"的条款，若额外引入独立空闲计时器反而会与这两条产生"浮窗仍打开、确实存在当前词，候选列表却因计时器到期而消失"的新行为分歧，不在 FR-019 字面授权范围内。

该决策**不改变**"非输入状态 MUST NOT 持续展示候选列表"这一 FR-019 核心外部断言本身，仅是"如何达成"的具体内部实现路径选择（原则 IV），与 FR-019 原文"MUST NOT 改变这一外部可观察行为本身"的限定完全兼容。

---

## §7 浮窗默认尺寸（FR-014）

当前 480×270（`width`/`height` 固定值，`Column` 内 `spacing=8`、外层 `margins=12`）。既有 5 行内容（原始输入槽 64px + 规整化预览文本约 20px + 输出预览 90px + 学习提示约 20px + 手动粘贴提示约 20px）之和 174px，加 4 个行间距 `8×4=32px`，加上下边距 `12×2=24px`，合计 174+32+24=230px——与实际 270px 相比预留了约 40px 冗余（高频行文本换行等场景缓冲）。

新增候选区域：9 行候选文本（`font.pixelSize: 13`，单行约 18–20px）+ `spacing: 2`（区域内部）+ 1 个与既有 `Column` 的新增行间距 `8px`，合计约 `9×20 + 8×2 + 8 ≈ 204px`。叠加后理论最小高度 `270 + 204 ≈ 474px`；取整为 **480×480**（高度取整数、预留少量缓冲，符合 Assumptions"候选区域具体视觉样式/尺寸留待实现阶段自主确定"的既有授权范围，内部细节，原则 IV）。宽度维持 480 不变——候选文本本身较短（单个汉字/拼音词/英文单词），竖排布局不构成横向空间瓶颈。

---

## §8 对 002 `contracts/llm-prompt-annotation-contract.md` 的影响

该契约"本契约已回填的事项"一节明确："每个歧义格位候选数量的规模上限...K = 8...不改变本契约定义的标注语法本身，仅改变参与渲染的候选数量上界"，并显式授权"`/speckit.implement` 阶段 MAY 在不改变'`generate()` 输入仍为字符串'这一 FR-007 硬性约束的前提下调整具体标注符号/排版...只需同步更新本文件"。FR-010 将 `kTopKCandidates` 从 8 调整为 9 后，**仅需**将该文件中"K = 8"的数值引用同步改为"K = 9"，**不改变**标注语法本身（`⟦token:候选1 权重1|...⟧` 格式不变）。该项已记录于本特性 `plan.md` 触碰文件清单。

---

## §9 测试影响汇总（供 `/speckit.tasks` 拆分参考，非本阶段产物）

- `tests/fastpath/FastCorrectorTest.cpp`：`EnglishWordListInterceptsCommonWordBeforePinyinPipeline`/`EnglishWordListLookupIsCaseInsensitive` 两个既有测试断言"命中词表后 `candidates.size()==1`"的短路行为，需按 FR-008 更新为"合并后候选池含该英文候选且固定排位 1，同时仍含其余拼音候选"（行为按 spec 明确要求变化，非"为通过测试而放宽断言"，符合原则 V 例外）。
- `tests/fastpath/PinyinTrieTest.cpp`：新增验证"传入 `kMaxPrefixPredictionOverreach` 时返回比默认上限更多前缀溢出候选"的用例。
- 新增 `tests/fastpath/EnglishWordListTest.cpp`（独立新文件，类比既有 `PinyinTrieTest.cpp` 的既定先例——`EnglishWordList` 新增的 Trie 实现与 `queryByPrefix()` 是独立可测试单元，原则 V"implement MAY 新增该任务模块下必需的测试文件"）：覆盖 `queryByPrefix()` 排序/`maxResults` 截断/大小写不敏感/空前缀等场景。
- `tests/session/SessionControllerTest.cpp`：新增 `selectCandidate()`/`candidateReplacementReady`/`currentCandidates`/光标位置穿透 的用例。
