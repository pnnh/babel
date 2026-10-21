# Contract: LlmEngine 学习提示双段输出内部实现契约（非 QML↔C++ 对外契约）

**Spec**: [spec.md](../spec.md) ｜ **Data Model**: [data-model.md](../data-model.md) ｜ **Research**: [research.md](../research.md)

## 适用范围

本文件记录本特性为落实 FR-001～FR-004 在 `LlmEngine`（生成侧）与 `SessionController`（解析/消费侧）之间新增的内部实现约束。`LlmEngine` 不是 QML 可见类型（003 `contracts/llm-engine-grammar-internal-contract.md` 已确立此先例），本文件**不是**对 `contracts/qml-cpp-session-interface.md` 的补充，而是与 003 `llm-engine-grammar-internal-contract.md`、004 `llm-engine-speculative-internal-contract.md` 并列的第三份 `LlmEngine` 内部契约。

## 一、对外签名变更（research.md §7）

```cpp
class LlmEngine {
 public:
  static constexpr const char *kLearningTipSeparator = "<<<EN_TIP>>>";

  /// 新增 includeLearningTip 参数，默认 false；返回类型保持 QString 不变。
  QString generate(const QString &normalizedInput, bool includeLearningTip = false) const;
};
```

单参数调用形式 `generate(text)` 的行为与 001～004 现状**逐字节一致**（`includeLearningTip` 取默认值 `false` 时，系统提示词不附加任何双段格式指令，采样上限沿用既有 `kMaxOutputTokens = 256`）。

## 二、行为契约（可被测试断言的外部可观察效果）

1. **`includeLearningTip == false` 时的等价性**：`generate(text, false)` 与既有单参数 `generate(text)` 产出的结果在"是否包含双段格式痕迹"这一维度上**必须等价**——即输出中不应出现 `kLearningTipSeparator` 字符串（因为系统提示词根本没有要求模型产出这段标记），本质上与 001～004 行为完全相同。
2. **`includeLearningTip == true` 时的格式指令**：系统提示词 MUST 要求模型按"标准中文译写结果 → `kLearningTipSeparator` → 英文表达建议"的固定顺序输出，且英文表达建议部分 MUST 被要求"只能是地道英文表达本身，不附加语法点讲解或其他说明文字"（FR-003 原文直接转译为 prompt 指令）。
3. **不保证语法级强制**：`includeLearningTip == true` 不改变 `ForbiddenTokenMask` 的黑名单内容（仍仅排除 ⟦/⟧/|），不引入任何语法级"必须包含分隔符"的强制保证——模型未遵循格式指令（分隔符缺失）是一种**预期内、需由调用方处理**的情形，不是 bug，不应抛出异常或返回空字符串代表失败（除非既有"模型整体不可用"语义本身成立，与本特性无关）。
4. **采样上限**：`includeLearningTip == true` 时采样循环上限为 `kMaxOutputTokensWithLearningTip(400)`，`includeLearningTip == false` 时为既有 `kMaxOutputTokens(256)`，两者互不影响、互不改变。
5. **与投机解码的正交性（004）**：无论 `m_draftModelAvailable` 是否为 `true`（是否启用投机解码），`includeLearningTip` 的语义与行为均保持一致——`includeLearningTip` 只影响 prompt 文本本身与采样上限常量，两条采样分支（既有单模型分支 / 投机解码分支）均需使用同一份按 `includeLearningTip` 条件拼接后的 prompt 文本与同一个采样上限常量，不需要为投机解码分支单独适配。
6. **语法梗阻/EOG 终止行为不变**（003/004 既有契约延续）：003 已确立的"语法梗阻提前终止"与既有自然终止（EOG）逻辑在双段输出场景下延续，若模型在尚未输出分隔符前就触发这两种终止条件之一，产出的部分文本交由调用方按"分隔符缺失"分支处理（契约二.3），不属于 `LlmEngine` 自身职责范围。

## 三、`LlmEngine` 职责边界（research.md §7 理由，供后续特性参考）

`LlmEngine::generate()` **不**在内部解析/切分双段输出——只负责按 `includeLearningTip` 决定拼接何种 prompt、使用何种采样上限，返回单一拼接后的 `QString`。解析切分逻辑（基于 `kLearningTipSeparator` 的字符串查找与兜底规则）由调用方（`SessionController.cpp` 内部辅助函数 `parseLearningTipOutput()`，参见 `data-model.md` 三.5）负责，不下沉到 `LlmEngine`。

## 四、与 003/004 既有契约的关系

- 不改变 003 `contracts/llm-engine-grammar-internal-contract.md` 记录的 `ForbiddenTokenMask` 黑名单内容与性能特性（research.md §4）。
- 不改变 004 `contracts/llm-engine-speculative-internal-contract.md` 记录的降级透明性/语义等价性/仅中高配启用等全部既有行为契约——两份契约的判定条件（`m_draftModelAvailable`、档位）与本契约的判定条件（`includeLearningTip`）完全独立，可同时成立，互不冲突。
