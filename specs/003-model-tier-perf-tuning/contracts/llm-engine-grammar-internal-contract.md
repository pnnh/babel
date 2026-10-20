# Contract: LlmEngine GBNF 语法采样（内部实现契约，非 QML↔C++ 对外契约）

**Spec**: [spec.md](../spec.md) ｜ **Data Model**: [data-model.md](../data-model.md) ｜ **Research**: [research.md](../research.md)

## 适用范围

本文件记录 `babel::slowpath::LlmEngine` 为落实 FR-005～FR-009 在**内部**新增的实现约束。`LlmEngine` 不是 QML 可见类型（`SessionController` 才是），因此本文件**不是**对 `specs/002-lattice-dict-model-switch/contracts/qml-cpp-session-interface.md` 的增量——该契约本身**无任何变更**（FR-006 明文要求）。本文件的读者是 `/speckit.tasks`/`/speckit.implement`，而非 QML 层开发者。

## 一、对外签名（重申不变，FR-006）

```cpp
class LlmEngine {
 public:
  QString generate(const QString &normalizedInput) const;  // 签名不变
};
```

调用方（`SessionController::onSlowPathTriggered()` 等）**无需**、也**不应**因本特性修改任何调用代码。

## 二、内部新增状态（私有，不反映为类的公开成员）

| 内部常量/逻辑 | 说明 | 来源 |
|---|---|---|
| GBNF 语法产生式字符串常量 | `"root ::= safe-char*\nsafe-char ::= [^\\u27E6\\u27E7\\x7C]\n"` | `data-model.md` 三、Output Grammar Rule |
| 语法根符号名 | `"root"` | 同上 |
| 梗阻检测循环 | 手动分步 `llama_sampler_apply()` + 候选集合全 `-inf` 判定 | `research.md` §3 |

## 三、行为契约（可被测试断言的外部可观察效果）

1. **正常情形**：输入含或不含 `⟦...|...⟧` 结构化标注片段，`generate()` 返回的 `QString` 结果 MUST NOT 包含 `⟦`（U+27E6）、`⟧`（U+27E7）、`|`（U+007C）三个字符中的任意一个（FR-005，对应测试：现有 `LlmEngineGenerateTest.GenerateAcceptsStructuredAnnotationTextWithoutSignatureChange` 已包含对 `⟦`/`⟧` 的断言，`/speckit.tasks` 阶段 MUST 补充对 `|` 的同等断言）。
2. **梗阻情形**：若生成过程中某一步骤语法约束导致候选集合为空，`generate()` MUST 返回梗阻点之前已生成的部分内容（非空字符串，除非梗阻发生在第一个 token 之前）；MUST NOT 抛出异常、MUST NOT 返回空字符串代表"失败"语义（FR-008，与"模型不可用返回空字符串"的既有语义——`modelAvailable() == false` 时返回空——是两种不同情形，不得混淆：梗阻是"模型可用但本次生成提前结束"，与"模型整体不可用"无关）。
3. **合法字符集**：除上述三个排除码点外，生成结果 MAY 包含任意其他 Unicode 字符（汉字/中文标点/数字/字母/全半角标点/emoji），语法约束 MUST NOT 额外过滤/替换/转义这些字符（FR-007，对应测试：构造包含品牌名"iPhone"、技术缩写"GPT"等英文字母内容的测试样例，验证语法约束不会误伐这些合法字符）。
4. **最大输出长度**：现有 `kMaxOutputTokens = 256` 硬上限不变，梗阻提前终止与达到 token 上限提前终止是两种独立的循环退出路径，MUST 分别可测试（梗阻通常发生在远小于 256 token 处）。

## 四、本契约未决定的事项

- `research.md` §3 已将"系统提示词维持不变"列为 Decision，本契约据此确认——`kSystemPrompt` 常量 MUST NOT 因本特性被删除或弱化其"不要包含标注符号"的措辞。
- 梗阻发生频率的实测数据（是否罕见/常见）留待 `/speckit.implement` 完成后以实测记录，本契约不预先断言。
