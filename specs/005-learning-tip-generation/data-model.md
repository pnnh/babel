# Data Model: 学习提示气泡内容生成

**Spec**: [spec.md](./spec.md) ｜ **Research**: [research.md](./research.md)

## 阅读说明

本文件记录本特性对既有实体（`specs/001-floating-ime-converter/data-model.md`、`specs/002-lattice-dict-model-switch/data-model.md`、`specs/003-model-tier-perf-tuning/data-model.md`、`specs/004-speculative-decoding/data-model.md`）的**增量变更**。既有实体中未被下表提及的部分（`RawDraftText`/`NormalizedCandidateText`/`StandardOutputPreview`/`PinyinLattice` 系列/`DictionaryEntry`/`ActiveModelRuntimeState`/`RecommendedModelProfile`/`TierDebounceProfile`/`SpeculativeDecodingProfile`/`RecommendedDraftModelProfile`）**原样有效，未变更**。本文件全部字段均已回填最终值（唯一一项架构决策点——分隔符格式——已由用户在 `research.md` §3 显式决策），**不存在**任何"【待决】"占位。

---

## 一、学习提示建议（Learning Tip Suggestion）—— spec.md Key Entities 原文实体

spec.md 原文已声明该实体"不引入独立的持久化数据模型，生命周期与本轮会话的标准中文输出预览保持一致"。本特性据此**不新增**独立的实体类/结构体，而是：

1. 复用 001 已声明、此前从未被赋值的 `SessionController::learningTipText`/`learningTipVisible` 两个只读属性作为**唯一的对外呈现载体**（`contracts/qml-cpp-session-interface-increment.md` 重申二者语义不变，只是首次被赋予真实内容来源）。
2. 新增一个**纯内部、不对外暴露**的解析结果结构体 `ParsedLearningTipOutput`（定义于 `SessionController.cpp` 匿名命名空间，不进入任何头文件、不作为 QML 可见类型），承载"本轮从 `LlmEngine::generate()` 组合输出中解析切分出的两部分内容"这一过渡态数据：

```cpp
// SessionController.cpp 匿名命名空间（内部实现细节，非对外契约）
struct ParsedLearningTipOutput {
  QString standardOutput;       // 标准中文译写结果（始终非空，解析失败时回退为原始组合文本）
  QString learningTip;          // 英文表达建议；为空字符串表示本轮未产出有效建议
  bool learningTipPresent = false;  // 是否判定为"有效建议"（非空且解析成功）
};
```

| 字段 | 类型 | 取值规则 | 来源 |
|---|---|---|---|
| `standardOutput` | `QString` | 恒非空字符串（`generate()` 本身返回空字符串的既有失败场景除外，与本特性无关）；双段格式解析成功时为分隔符之前的 trimmed 文本，解析失败/畸形时原样整段文本 | FR-001（同一次调用产出的标准中文译写结果部分） |
| `learningTip` | `QString` | 解析成功且非空时为分隔符之后的 trimmed 文本，否则为空字符串 | FR-001/FR-003（英文表达建议本身，不附加语法点讲解） |
| `learningTipPresent` | `bool` | `learningTip` 非空时为 `true`，否则为 `false` | 驱动 `SessionController::m_learningTipVisible` 赋值 |

---

## 二、`LlmEngine` 接口增量（`babel/src/slowpath/LlmEngine.h`）

### 1. 新增公开常量

```cpp
class LlmEngine {
 public:
  /// 学习提示双段输出分隔符（005-learning-tip-generation，research.md §3
  /// 用户决策：方案 3）。includeLearningTip=true 时系统提示词要求模型按
  /// "<标准中文译写结果>\n<<<EN_TIP>>>\n<英文表达建议>" 顺序输出，调用方
  /// （SessionController）按本常量解析切分。纯 ASCII 字符序列，不在
  /// ForbiddenTokenMask 黑名单范围内（黑名单仅排除 ⟦/⟧/|），无需扩展黑名单
  /// （research.md §4）。
  static constexpr const char *kLearningTipSeparator = "<<<EN_TIP>>>";
  ...
};
```

### 2. `generate()` 签名变更（新增带默认值参数，返回类型不变）

```cpp
/// `includeLearningTip` 为 true 时（仅由 SessionController 在中/高配档位且
/// 用户设置开关开启时传入，research.md §5），系统提示词额外要求模型在同一次
/// 输出中产出与标准中文译写结果对应的英文表达建议，两部分之间以
/// kLearningTipSeparator 分隔；本方法仍只返回单一拼接后的 QString，不在
/// LlmEngine 内部解析切分（research.md §7：职责边界，解析逻辑由调用方负责）。
/// 默认参数 false 保持与 001～004 既有调用点完全源码兼容。
QString generate(const QString &normalizedInput, bool includeLearningTip = false) const;
```

### 3. 内部新增常量（`LlmEngine.cpp` 匿名命名空间，非公开接口）

```cpp
constexpr int kMaxOutputTokensWithLearningTip = 400;  // includeLearningTip=true 时的采样上限（research.md §6）
```

### 4. 系统提示词增量（`LlmEngine.cpp` 内部实现细节）

既有 `kSystemPrompt` 常量内容**不变**；新增一段仅在 `includeLearningTip == true` 时追加拼接的指令后缀常量（暂命名 `kLearningTipInstructionSuffix`），具体文案由 `/speckit.tasks`/`/speckit.implement` 落地时最终确定措辞，但 MUST 满足以下要素（均可从 spec.md 原文 FR-001/FR-003 直接推导，非新发明产品语义）：

- 明确要求"标准中文译写结果"在前、分隔符 `<<<EN_TIP>>>` 其次、"英文表达建议"在后的固定顺序。
- 明确要求英文表达建议"只能是地道英文表达本身，不附加语法点讲解或其他说明文字"（FR-003 原文）。
- 明确要求除上述三段内容外不得输出任何其他文字（延续既有 `kSystemPrompt` "不要添加任何解释或前后缀"的既定风格）。

---

## 三、`SessionController` 接口增量（`babel/src/session/SessionController.h`）

### 1. 新增 Q_PROPERTY

```cpp
Q_PROPERTY(bool learningTipEnabled READ learningTipEnabled WRITE setLearningTipEnabled NOTIFY learningTipEnabledChanged)
```

| 成员 | 类型 | 默认值 | 持久化 | 说明 |
|---|---|---|---|---|
| `m_learningTipEnabled` | `bool` | `true`（FR-008：开关默认开启） | **不持久化**（research.md §8，沿用 002/004 既有模式，每次应用启动恢复为 `true`） | 设置页面开关当前状态 |

```cpp
bool learningTipEnabled() const { return m_learningTipEnabled; }
/// 用户在设置页面切换开关时调用。关闭时 MUST 立即将 learningTipVisible 置为
/// 不可见（FR-008 字面要求"用户关闭该开关后，系统 MUST 保持 learningTipVisible
/// 为不可见"，非"从下一轮生成起才生效"），不等待下一轮生成完成；开启时不产生
/// 任何立即副作用（气泡内容仍需等待下一轮慢流生成完成才会出现，与 001 既定的
/// "仅在生成完成时更新"节奏一致，research.md §2/§9）。
void setLearningTipEnabled(bool enabled);
```

### 2. 新增信号

```cpp
void learningTipEnabledChanged();
```

### 3. `resetSessionData()` 增量（跨会话清空，research.md §2）

```cpp
void SessionController::resetSessionData() {
  ...（既有字段重置不变）...
  m_learningTipText = QString();
  m_learningTipVisible = false;
  ...
  emit learningTipTextChanged();
  emit learningTipVisibleChanged();
}
```

### 4. `onSlowPathTriggered()` 增量（核心生成-解析-回填流程）

```text
触发时（主线程，调用 generate() 之前）：
  currentTier = ModelCatalog::classifyTierByFileName(m_modelRuntimeState.currentModelPath 文件名)
                （既有 003/004 查表方式，不新增缓存字段）
  wantLearningTip = m_learningTipEnabled
                    && (currentTier == Medium || currentTier == High)

后台线程：
  generated = engine->generate(inputSnapshot, wantLearningTip)
  若 wantLearningTip == true：
    parsed = parseLearningTipOutput(generated)   // 新增内部辅助函数，纯字符串处理
    standardOutput = parsed.standardOutput
    learningTip = parsed.learningTip
    learningTipPresent = parsed.learningTipPresent
  否则：
    standardOutput = generated
    learningTip = QString()
    learningTipPresent = false

主线程完成回调（与既有 outputPreviewText 回填同一次回调，research.md §2）：
  m_outputPreview.content = standardOutput
  m_learningTipText = learningTipPresent ? learningTip : QString()
  m_learningTipVisible = learningTipPresent
  emit outputPreviewTextChanged(); emit learningTipTextChanged(); emit learningTipVisibleChanged();
  （其余既有逻辑——generationState 回填等——不变）
```

### 5. 新增内部辅助函数（`SessionController.cpp` 匿名命名空间）

```cpp
/// 按 LlmEngine::kLearningTipSeparator 解析 generate(includeLearningTip=true)
/// 的组合输出；仅在 wantLearningTip == true 时调用。
ParsedLearningTipOutput parseLearningTipOutput(const QString &raw);
```

解析规则（data-model.md 一的取值规则表已给出字段语义，此处给出算法）：

1. 查找 `raw` 中 `kLearningTipSeparator` 首次出现的位置 `idx`。
2. `idx < 0`（未找到分隔符）→ `standardOutput = raw`（原样整段），`learningTipPresent = false`（FR-004：判定为无法产出有效建议）。
3. `idx >= 0`：
   - `candidateStandard = raw.left(idx).trimmed()`
   - `candidateTip = raw.mid(idx + 分隔符长度).trimmed()`
   - 若 `candidateStandard` 为空 → 判定为畸形输出，`standardOutput = raw`（整段原文兜底，避免核心译文功能因格式异常而显示为空），`learningTipPresent = false`。
   - 否则 → `standardOutput = candidateStandard`；若 `candidateTip` 非空 → `learningTip = candidateTip`、`learningTipPresent = true`；否则 `learningTipPresent = false`（`standardOutput` 仍使用解析出的有效中文部分，不因英文建议为空而连带丢弃已解析出的正确译文）。

---

## 四、档位 → 是否请求学习提示 映射表（FR-009，非新增持久化实体，查表逻辑）

| `TierHint` | 是否启用学习提示生成（`wantLearningTip` 的档位侧条件） |
|---|---|
| `Low` | 恒为 `false`（FR-009，低配默认档位） |
| `Medium` | 取决于 `m_learningTipEnabled` |
| `High` | 取决于 `m_learningTipEnabled` |
| `Unknown` | 恒为 `false`（与 003/004 既有"Unknown 复用 Low 档位行为"的惯例一致） |

本表**不**新增独立的 `ModelCatalog` 静态方法（与 003 的 `tierDebounceWindow()`、004 的 `pairedDraftCandidate()` 不同，本表逻辑足够简单——仅 "Medium/High 为 true，其余为 false"——直接以内联布尔表达式写在 `SessionController::onSlowPathTriggered()` 中，不构成需要独立查表函数的复杂度，宪章原则 V 最小必要修改）。

---

## 五、QML 侧新增数据（`babel/content/SettingsPanel.qml`，非 C++ 数据模型，列于此处便于追溯）

设置页面新增一个开关控件（复用 004 `speculativeDecodingEnabled` 开关完全相同的纯 `Rectangle` + `MouseArea` 实现模式，不引入 `QtQuick.Controls` 依赖），绑定 `SessionController.learningTipEnabled`，点击时执行 `SessionController.learningTipEnabled = !SessionController.learningTipEnabled`。不新增任何 QML 文件，不新增任何数据结构。
