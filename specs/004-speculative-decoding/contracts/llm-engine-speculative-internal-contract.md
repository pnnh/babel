# Contract: LlmEngine 投机解码内部实现契约（非 QML↔C++ 对外契约）

**Spec**: [spec.md](../spec.md) ｜ **Data Model**: [data-model.md](../data-model.md) ｜ **Research**: [research.md](../research.md)

## 适用范围

本文件记录本特性为落实 FR-001～FR-003 在 `LlmEngine`（`research.md` §3 用户已决策采纳方案 B，宿主即为 `LlmEngine` 本身，不新增协调类）**内部**新增的实现约束。`LlmEngine` 不是 QML 可见类型（003 `contracts/llm-engine-grammar-internal-contract.md` 已确立此先例），本文件**不是**对 `contracts/qml-cpp-session-interface-increment.md` 的补充。

**本契约的架构基础已确定（`research.md` §3 用户决策：方案 B）**：以下第二、三节的行为契约适用于 `LlmEngine` 这一确定的宿主类型；第四节此前"依赖待决架构问题"的部分已按方案 B 回填为确定结论。

## 一、对外签名（重申不变，FR-002）

```cpp
class LlmEngine {
 public:
  QString generate(const QString &normalizedInput) const;  // 签名不变，无论内部是否启用投机解码
};
```

调用方（`SessionController::onSlowPathTriggered()`）**无需**、也**不应**因本特性修改任何调用代码。

## 二、行为契约（可被测试断言的外部可观察效果，与宿主架构无关）

1. **降级透明性（FR-006）**：当草稿模型未配置、加载失败、或文件损坏/格式不兼容时，`generate()` 的返回结果 MUST 与"不启用投机解码的既有单模型生成路径"完全等价（003 交付时的行为），不因此抛出异常、不返回空字符串代表"失败"（除非既有"模型整体不可用"语义本身成立，即 `modelAvailable() == false` 的既有情形，与投机解码无关，不得混淆）。
2. **语义等价性（FR-001）**：启用投机解码路径时，`generate()` 返回的最终文本 MUST 与"假设关闭投机解码、仅用目标模型独立逐 token 生成"在语义上一致（`research.md` §2 步骤 3 已给出手写验证循环如何保证这一点：任何草稿猜测与目标模型真实决策不符的位置均被直接替换，不采纳草稿猜测）。
3. **语法约束不被绕过（FR-003）**：无论是否启用投机解码，`generate()` 返回结果 MUST NOT 包含 `⟦`（U+27E6）、`⟧`（U+27E7）、`|`（U+007C）三个字符中的任意一个（003 既有 GBNF 约束在投机解码路径下同样生效，`research.md` §6：目标模型验证阶段与草稿模型候选生成阶段均应用该约束，且两侧语法采样器状态随"最终采纳的 token 序列"同步前进）。
4. **仅中/高配启用（FR-005/FR-009）**：低配默认档位（Qwen2.5-3B-Instruct）的 `generate()` 路径 MUST 不发生任何变化——不尝试加载草稿模型、不进入投机解码分支判断逻辑本身（而非"判断后发现不满足条件才退回"，以避免低配档位承担任何额外的初始化开销，呼应 FR-009"既有生成路径 MUST 保持 003 交付时的状态不变"）。
5. **梗阻行为不变（003 既有契约延续）**：003 已确立的"语法梗阻提前终止"行为（`contracts/llm-engine-grammar-internal-contract.md` 三.2）在投机解码路径下延续——若验证阶段发现目标模型在某位置的语法候选集合全部为 `-inf`，MUST 返回该位置之前已生成的部分内容，不得因投机解码的引入而改变这一既有退出路径的语义。

## 三、本契约确认的技术可行性结论（`research.md` §1/§2，事实性，非待决）

- 本机 `llama.h`（b9246）**不提供**任何以 `speculative`/`draft` 命名的高层封装 API（已核实，`research.md` §1）。
- `llama_batch_init()`/`llama_decode()`/`llama_get_logits_ith()`/`llama_memory_seq_rm()`/`llama_memory_seq_pos_min/max()` 等既有底层原语组合后，技术上足以手写"草稿-验证"循环（已核实，`research.md` §2），本特性按此实现，不依赖任何未经核实的 API。
- `K`（每轮草稿 token 数量）MAY 固定为内部常量，不存在"必须自适应调整"的技术强制性（`research.md` §7），spec.md Out of Scope"不实现自适应窗口"条款原样有效。

## 四、本契约依据用户决策（`research.md` §3：方案 B）确定的架构细节

- **构造函数签名**：用户已决策采纳候选方案 B（`LlmEngine` 内部封装双模型），`LlmEngine` 构造函数签名 MUST 扩展以接受一个可选的草稿模型路径参数（路径为空或对应文件不存在/加载失败时，内部第二组 `llama_model*`/`llama_context*` 保持 `nullptr`，`generate()` 走既有单模型路径，天然满足 FR-006"静默降级"）。所有构造 `LlmEngine` 的调用点（`bootstrapPlatformDefaults()`、`switchModel()` 内部线程、`LlmEngineGenerateTest.cpp`/`LlmEngineUnavailableTest.cpp`）均需同步更新传参，`/speckit.tasks` 阶段据此排期。
- **生命周期管理的归属**：草稿模型对应的 `llama_model*`/`llama_context*` 由 `LlmEngine` 内部统一负责析构/热切换时的 use-after-free 防护，与既有目标模型的 `llama_model*`/`llama_context*` 生命周期管理方式一致（`LlmEngine` 既有析构逻辑的直接延伸）；`SessionController` 侧继续沿用既有 `shared_ptr` + `QPointer` + 异步线程模式管理 `LlmEngine` 实例本身，无需感知其内部持有几组模型。

## 五、本契约与档位搭配映射取值的关系（`research.md` §5.3，用户已决策：方案①）

- `research.md` §5.3 草稿-目标模型档位搭配映射的具体取值：用户已决策采纳方案①（`contracts/model-catalog-draft-pairing-internal-contract.md` 三：Medium→Size0_5B / High→Size1_5B）。本契约的"行为契约"（第二节）与具体映射取值无关（无论搭配哪个草稿模型，第二节 1～5 条均成立），故本契约的成立**不依赖**该项决策；`LlmEngine` 实际加载"哪一个"草稿模型文件这一运行时行为，由 `SessionController` 按该映射表查表后将对应路径传入 `LlmEngine` 构造函数决定。
