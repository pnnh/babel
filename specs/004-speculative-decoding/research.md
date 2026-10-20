# Research: 投机解码（Speculative Decoding）性能优化

**Spec**: [spec.md](./spec.md) ｜ **Plan**: [plan.md](./plan.md)

## 阅读说明

本文件是 Phase 0 产出。按用户本轮指令，每一项调研**必须核实现有代码/已安装依赖的实际状态**，不得凭空假设 API 签名或数值；核实依据逐条注明来源文件路径或命令。本文件同时严格遵守用户本轮的两项明确约束：

1. 若核实后发现当前 llama.cpp 版本不提供"投机解码"命名的现成高层 API，**MUST NOT** 编造不存在的函数；若底层原语（`llama_batch`/`llama_decode`/`llama_memory_seq_*` 等）组合后技术上可行，**MUST** 如实记录"手写草稿-验证循环"这一替代实现路径及其可行性依据。
2. 对于用户明确要求"research 阶段给出候选方案与权衡、不要在 plan 阶段拍板"的两个架构/数据问题（§3 SessionController↔LlmEngine 架构划分、§5 草稿-目标模型档位搭配映射的具体取值），本文件**先**给出候选方案 + 权衡 + Recommendation，供用户决策参考；**用户已在本轮后续对话中就这两项作出显式决策**（§3 采纳方案 B、§5.3 采纳方案①，决策依据与理由见各节新增的"用户决策"小节及 §9 汇总），两项均**不再**是遗留的 `[NEEDS CLARIFICATION]`，`plan.md` 据此可得出"PASS 无遗留"结论。

---

## 1. 本机 llama.cpp 版本是否提供"投机解码"命名的原生高层 API（事实核实，非选型）

**核实方式**：直接阅读本机已构建产物中的头文件，而非凭经验假设或查阅任意网络版本文档。

- 头文件实际路径：`build/macos/vcpkg_installed/arm64-osx/include/llama.h`（1578 行，命令 `wc -l` 已核实）。
- vcpkg port 版本：`vcpkg/ports/llama-cpp/vcpkg.json` `"version": "9246"` → 对应上游 `ggml-org/llama.cpp` `b9246` 标签（与 003 `research.md` §1 核实结论一致，本特性未升级该依赖版本）。
- 根 `vcpkg.json`（仓库根目录）对 `llama-cpp` 的引用为 `"default-features": false` + 仅启用 `metal`（macOS）特性，**未**启用 `tools` 特性——已核实 `vcpkg/ports/llama-cpp/portfile.cmake` 中 `tools` 特性仅用于构建 `llama-cli`/`llama-mtmd-cli` 等示例 CLI 二进制，本仓库未构建/安装这些示例程序，因此也**不**随 vcpkg 附带上游 `examples/speculative/` 示例源码（该示例本身也只是上游仓库中的一个应用程序，不是 `llama.h` 导出的库 API）。

**核实结论（已确认，非假设）**：对 `llama.h` 全文执行关键字搜索——

```bash
grep -n "speculat\|draft" build/macos/vcpkg_installed/arm64-osx/include/llama.h
```

**无任何匹配**。即：本机实际安装的 `llama.h`（b9246）**不提供**任何以 `speculative`/`draft` 命名的高层 API（既无"构造草稿-验证调度器"的封装函数，也无"批量验证并返回接受长度"的便捷函数）。此前 spec.md Assumptions 中"llama.cpp 原生支持投机解码机制"这一表述，核实结论为：**底层推理原语支持手写实现该机制（见下），但不存在开箱即用的高层封装 API**；spec.md 原文本身不因此需要改动（spec 原文已将"具体接入方式留待 research 环节确定"，未断言存在高层 API），仅本研究文件据实记录这一本阶段新确认的技术事实。

---

## 2. 底层原语是否足以手写"草稿-验证"循环（可行性核实，给出手写实现方案）

**核实方式**：逐一确认以下每个 API 在 `llama.h` 中的真实签名与文档注释（行号为 b9246 本机头文件实际行号），不采信记忆中其他版本的签名。

| API | 行号 | 用途（已核实的文档注释原意） |
|---|---|---|
| `llama_batch_init(int32_t n_tokens, int32_t embd, int32_t n_seq_max)` | 922 | 在堆上分配一个可容纳 `n_tokens` 个 token 的批次，**每个 token 可独立设置 `pos`/`seq_id`/`logits` 标志位**（区别于现有 `LlmEngine.cpp` 单 token 场景使用的 `llama_batch_get_one()`，后者固定 `seq_id=0` 且仅自动追踪位置，不支持"一批内多个待验证位置各自要求输出 logits"）。 |
| `llama_batch` 结构体 `logits` 字段 | 240–249 | 文档注释原文："if zero, the logits ... for the respective token will not be output"——即可对批次内任意子集 token 置位，要求 `llama_decode()` 对这些指定位置均产出 logits，这正是"一次 `decode` 调用验证 N 个草稿位置"所需的能力。 |
| `llama_decode(llama_context*, llama_batch)` | 952 | 文档注释原文列出返回值语义：`0` 成功；`1` 未找到 KV slot（警告，非致命）；`2` 被中止；`-1` 非法输入；`< -1` 致命错误。**正值不代表失败**，需按注释区分处理，现有 `LlmEngine.cpp` 当前仅判断 `!= 0`，投机解码路径下批量 decode 需要更细粒度地处理这些返回值（内部实现细节，`/speckit.tasks` 阶段按本行注释落地）。 |
| `llama_get_logits_ith(llama_context*, int32_t i)` | 999 | 取批次中第 `i` 个"被标记需要输出 logits"的 token 对应的 logits 行，现有 `LlmEngine.cpp` 已在用（`i=-1` 取最后一个）；投机解码验证阶段需要对批次内**多个** `i` 值分别调用，取得每个草稿位置"目标模型认为该位置该生成什么"的 logits。 |
| `llama_memory_seq_rm(llama_memory_t, seq_id, pos p0, pos p1)` | 712 | 文档注释原文："Removes all tokens that belong to the specified sequence and have positions in [p0, p1)"——用于**回滚**：当验证在某个草稿位置发现不匹配时，需要从 KV cache 中移除该位置及其后的所有草稿 token，为"用目标模型自己在该位置采样得到的 token 取代"腾出位置，避免 KV cache 残留已失效的草稿假设。 |
| `llama_memory_seq_pos_min/max(llama_memory_t, seq_id)` | 757/764 | 文档注释原文——查询某序列当前 KV cache 中已存在的最小/最大位置，供回滚后重新计算下一次 `decode` 的起始 `pos`（避免手工维护一个可能与 KV cache 实际状态脱节的位置计数器）。 |
| `llama_sampler_apply`/`llama_sampler_accept`/`llama_sampler_sample` | 1234/1278/1480 附近 | 003 `research.md` §1 已核实存在且已在 `LlmEngine.cpp` 中使用，验证阶段"目标模型在每个草稿位置各自做一次独立采样决策"可复用同一套采样器原语，无需新增。 |

**可行性结论（Decision，内部实现细节，按宪章原则 IV 自主决定——不改变 `generate()` 对外签名与 QML↔C++ 契约，仅为引擎内部算法选择）**：

上述原语组合足以手写一个标准的"草稿-验证"（speculative decoding / "draft-then-verify"）循环，概念上分为四步，每一步均已逐一核实对应 API 存在：

1. **草稿阶段**：草稿模型（独立的 `llama_model`/`llama_context`）基于当前已确认的上下文，自回归生成固定数量 `K` 个候选 token（草稿模型自身的生成循环，复用 `LlmEngine.cpp` 现有单 token 自回归模式，仅规模更小、速度更快）。
2. **批量验证阶段**：目标模型使用 `llama_batch_init()` 构造一个含 `K` 个 token（草稿产出的 `K` 个候选）的批次，每个 token 的 `logits` 标志位均置 1（要求全部输出 logits），一次性调用 `llama_decode()`；随后对 `i = 0..K-1` 依次调用 `llama_get_logits_ith()` 取得目标模型在该位置"原本会生成什么"的 logits，经既有采样管线（含 GBNF 语法约束，见 §7）决定目标模型在该位置的"真实"选择。
3. **比较与截断阶段**：从位置 0 开始逐一比较"草稿 token"与"目标模型在该位置的真实选择"是否相同；找到第一个不同的位置 `j` 时停止——位置 `[0, j)` 的草稿 token 视为已被目标模型验证通过，直接采纳（无需目标模型逐一重新计算，这正是投机解码"验证 N 个草稿 token 与生成 1 个 token 权重读取开销相近"的加速来源）；位置 `j` 使用目标模型在该位置的真实采样结果替换草稿猜测（保证 FR-001"语义上等价"）；调用 `llama_memory_seq_rm()` 移除位置 `[j+1, +∞)` 的 KV cache 内容（若草稿产出的 `K` 个 token 全部被验证通过，则 `j = K`，本步骤为空操作）。
4. **继续循环**：以位置 `j`（被替换/追加的那个真实 token）为新的"已确认上下文末尾"，回到步骤 1，草稿模型重新从该位置发起下一轮猜测，直至遇到 EOG 或达到既有 `kMaxOutputTokens` 上限。

**与 FR-001"语义等价"的对应关系**：步骤 3 保证了无论某个位置的草稿猜测是否被采纳，最终写入输出序列的 token 永远是"目标模型在该位置、给定正确前缀上下文时会生成的 token"（草稿猜测仅在与之吻合时才被采纳，不吻合则直接丢弃并替换），与目标模型独立逐 token 生成在每个位置上的决策逻辑完全一致，只是省去了"逐一跑一遍目标模型前向计算"的开销——这正是 spec.md Assumptions 中"验证阶段确保与目标模型独立解码结果等价"这一数学性质的具体实现落点，而非本阶段新引入的产品决策。

**替代方案与否决理由**：
- 等待/要求升级 llama.cpp 版本以获得未来可能新增的高层封装 API：被否决——spec.md 范围声明"不改变 QML↔C++ 契约"之外未授权升级第三方依赖版本，且§1 已确认当前手写方案技术可行，无需承担升级风险（升级 vcpkg overlay port 版本命中宪章判定程序 (d)"新增/变更依赖"，MUST 标记 NEEDS CLARIFICATION，而非在无必要时主动引入）。
- 使用"后端采样 API [EXPERIMENTAL]"（`llama_get_sampled_token_ith()`，003 `research.md` §3 已核实其依赖 `llama_sampler_seq_config`，与现有标准采样管线是两套不同机制）：被否决，理由同 003 §3——现有实现未启用该实验性机制，引入会显著扩大改动面，不符合宪章原则 V"最小必要修改"。

---

## 3. SessionController ↔ LlmEngine 架构划分方案（候选 + 权衡 + **用户决策**）

> 用户本轮明确指令：此项是关键架构决策，**MUST** 在 research 阶段给出候选方案与权衡，**MUST NOT** 在 plan 阶段自行拍板；若候选方案之间的差异会影响 `SessionController` 对外可观察行为，**MUST** 标记为新的 `[NEEDS CLARIFICATION]`。本节按该指令给出候选方案与权衡，**用户已于本轮后续对话中就该项作出显式决策**（见下文"用户决策"小节），不再是遗留待决事项。

### 候选方案 A：`SessionController` 同时持有两个 `LlmEngine`/`llama_context` 实例

`SessionController` 新增一个 `m_draftLlmEngine`（`shared_ptr<LlmEngine>`，与既有 `m_llmEngine` 并列），`switchModel()` 流程在加载目标模型的同时，依据 §5 的档位搭配映射额外尝试加载对应草稿模型；`onSlowPathTriggered()` 调用一个新的、显式接受"目标引擎 + 草稿引擎"两个参数的生成函数（可以是 `LlmEngine` 新增的静态/自由函数，也可以是新增的协调类）。

- **优点**：`LlmEngine` 现有"持有单一 `llama_model*`/`llama_context*`"的类不变（构造函数签名不变），改动集中在 `SessionController`；两个引擎的生命周期（热切换时的新旧实例替换、`use-after-free` 防护）可复用 `SessionController` 已有的 `shared_ptr` + `QPointer` + 异步线程模式（`switchModel()` 现有实现）。
- **缺点**：`SessionController::switchModel()`/`applyModelSwitchResult()` 需要同时管理两条独立的"目标模型加载"与"草稿模型加载"异步流程及其级联回退逻辑（FR-013 既有回退链路专为单一目标模型设计，需要判断"草稿模型加载失败"与"目标模型加载失败"两种独立失败模式——草稿模型失败按 FR-006 静默降级不影响目标模型，目标模型失败才触发既有 FR-013 回退），改动面扩大到 `SessionController.h` 的私有状态与 `.cpp` 的多处流程，违反宪章原则 V"最小必要修改"的比例更高；"验证"逻辑（草稿-验证循环）需要同时访问两个 `LlmEngine` 内部的 `llama_context*`，若 `LlmEngine` 不提供新的公开访问器，`SessionController` 无法实现该循环，这意味着 `LlmEngine.h` 仍需新增新的公开方法（并非真正"签名完全不变"，只是 `generate()` 本身不变）。

### 候选方案 B：`LlmEngine` 内部封装双模型逻辑，对 `SessionController` 完全透明

`LlmEngine` 构造函数扩展为同时接受"目标模型路径"与"（可选的）草稿模型路径"，内部持有两组 `llama_model*`/`llama_context*`（或在草稿模型缺失/加载失败时，第二组保持 `nullptr`，`generate()` 内部据此判断走投机解码路径还是既有单模型路径，天然满足 FR-006"静默降级"）；`generate(const QString&)` 对外签名完全不变，内部按"草稿可用与否"自动选择算法分支。`SessionController` 仍只持有一个 `m_llmEngine`（`shared_ptr<LlmEngine>`），`switchModel()` 现有流程仅需新增"查询 §5 映射表得到草稿模型路径并一并传给 `LlmEngine` 构造函数"这一行为，其余热切换/回退/状态管理逻辑完全不变。

- **优点**：`SessionController.h` 的私有状态与公开契约（`Q_PROPERTY`/`Q_INVOKABLE`/信号）**零改动**（新增的"未启用加速"提示属性见 §5/`data-model.md`，与本架构选择无关，两个方案下都需要）；现有 `switchModel()`/`applyModelSwitchResult()`/级联回退流程完全复用，改动面严格限定在 `LlmEngine.{h,cpp}` 内部，最符合宪章原则 V；`LlmEngine` 作为"慢流推理引擎"这一既有抽象边界的语义保持完整（"如何更快地产出结果"本就是该类的职责）。
- **缺点**：`LlmEngine` 构造函数签名必须变化（新增草稿模型路径参数），所有构造 `LlmEngine` 的调用点（`bootstrapPlatformDefaults()`、`switchModel()` 内部线程、现有测试文件 `LlmEngineGenerateTest.cpp`/`LlmEngineUnavailableTest.cpp`）均需同步更新传参，但由于 `LlmEngine` 本身不是 QML 可见类型（003 `contracts/llm-engine-grammar-internal-contract.md` 已明确"本文件不是对外契约"的先例），该签名变化**不**构成对 `qml-cpp-session-interface.md` 的违反；`LlmEngine` 单个类承担的职责（模型加载 + 投机解码算法 + GBNF 约束）进一步增加，单文件/单类复杂度上升。

### 候选方案 C（折中）：新增一个独立的协调类（如 `SpeculativeDecodingCoordinator`），由 `SessionController` 持有该协调类而非直接持有第二个 `LlmEngine`

该协调类内部持有"目标 `LlmEngine`"的引用/路径 + 独立管理草稿模型的 `llama_model*`/`llama_context*`（草稿模型不复用 `LlmEngine` 类本身，因为草稿模型不需要 GBNF 语法约束之外的 `LlmEngine` 既有职责，如"独立 `modelAvailable()` 判定"语义可能不同）。

- **优点**：避免方案 B 中"`LlmEngine` 职责进一步膨胀"的问题，新增类职责单一；避免方案 A 中"`SessionController` 需要管理两套回退逻辑"的问题（协调类内部处理）。
- **缺点**：是三个方案中**新增文件数量最多**的一个（新增 `.h`/`.cpp` 及对应测试文件），与宪章原则 V"不新增子系统目录、尽量复用既有文件"的既有实践（003 "不新增任何新的源文件/头文件"）相比改动面最大；`SessionController` 仍需决定"持有 `LlmEngine` 还是持有该协调类"，若选择"协调类内部持有 `LlmEngine`"则退化为方案 B 的变体（协调类成为 `LlmEngine` 与 `SessionController` 之间的一层转发），若选择"`SessionController` 同时持有 `LlmEngine` 与协调类"则退化为方案 A 的变体（只是把"第二个引擎"换成了"协调类"），折中收益需结合 `/speckit.tasks` 阶段实际改动量评估，本阶段无法仅凭命名判断其是否真的比 A/B 更优。

### Recommendation（仅供参考，MUST NOT 视为已批准决策——宪章原则 III）

若必须给出一个倾向性意见：方案 B 在"改动面最小""`SessionController` 契约零改动""`LlmEngine` 既有抽象边界语义未被破坏"三项上综合表现最优，是三个候选中最契合宪章原则 V 的选项；方案 A 的主要代价是把"双模型协调"这一本质上属于"推理引擎如何更快产出结果"的关注点泄漏到会话层；方案 C 的"新增文件"代价在当前仓库"倾向复用既有文件、不新增子系统目录"的既往实践（003 Project Structure"不新增任何新文件"）下显得不经济。此 Recommendation 原本不构成决策，最终选择留待用户显式批准——**该批准已取得，见下文"用户决策"**。

### 用户决策（已采纳，取代上述 Recommendation 的"仅供参考"状态）

**Decision**：采纳**方案 B——`LlmEngine` 内部封装双模型逻辑，对 `SessionController` 完全透明**。

**决策来源**：用户在本轮后续对话中明确指示"采纳方案 B（Recommendation）"，构成宪章原则 III 所要求的显式批准，不再是 Agent 自行拍板。

**理由（即方案 B 小节已列出的优点，经用户确认采纳）**：
1. `SessionController.h` 的私有状态与公开契约（`Q_PROPERTY`/`Q_INVOKABLE`/信号）零改动，`switchModel()`/`applyModelSwitchResult()`/级联回退流程完全复用，改动面严格限定在 `LlmEngine.{h,cpp}` 内部，最符合宪章原则 V"最小必要修改"。
2. `LlmEngine` 作为"慢流推理引擎"既有抽象边界的语义保持完整——"如何更快地产出结果"本就是该类的职责，双模型协调逻辑不泄漏到会话层。
3. 相比方案 A（`SessionController` 需同时管理两套独立的加载/回退流程）与方案 C（需新增协调类文件，改动面最大、与既往"不新增子系统目录"的实践相悖），方案 B 的代价（`LlmEngine` 构造函数签名需扩展以接受可选的草稿模型路径）是三者中最小且最可控的一项，调用点明确（`bootstrapPlatformDefaults()`、`switchModel()` 内部线程、`LlmEngineGenerateTest.cpp`/`LlmEngineUnavailableTest.cpp`），不构成对 `qml-cpp-session-interface.md` 的违反（`LlmEngine` 本非 QML 可见类型）。

**对后续设计产物的影响（已回填，详见对应文件）**：
- `data-model.md` 三"具体宿主位置"由【待决】回填为"`LlmEngine` 内部"。
- `contracts/llm-engine-speculative-internal-contract.md` 四"依赖待决架构问题的部分"回填为方案 B 对应的确定结论。
- `data-model.md`/`contracts/`/`quickstart.md` 中原先注明"取决于 §3 待决架构方案"的字样均已回填为方案 B 下的确定表述。

---

## 4. 草稿模型与目标模型内存/显存占用叠加评估——测量方法论（非数值断言）

spec.md Assumptions 已明确"具体占用数值…留待 research/实现阶段以实测数据记录，本 spec 不对具体占用数值做强约束性断言"；本节按用户指令仅给出**如何测量**的验证步骤建议，不在本阶段编造任何具体 MB/GB 数值。

**建议测量步骤（供 `/speckit.implement` 完成后实际执行，记录到本文件或 `quickstart.md` 对应场景的实测结果栏）**：

1. **基线测量（仅目标模型，不启用投机解码）**：分别加载中配（Qwen2.5-7B-Instruct GGUF Q4_K_M）、高配（Qwen2.5-14B-Instruct GGUF Q4_K_M）模型后，在 macOS 上使用 `Activity Monitor`/`vm_stat`/`sudo powermetrics --samplers gpu_power` 或终端 `ps -o rss -p <pid>`（常驻内存，RSS）读数作为基线；Metal 统一内存架构下进程 RSS 即可近似反映"模型权重+KV cache"占用的统一内存总量（macOS 为本仓库当前唯一已构建验证的平台，`research.md` 延续 002/003 对该平台的实测口径）。
2. **叠加测量（同时加载目标模型 + 对应草稿模型）**：在 1 的同一测量方式下，启动投机解码路径（目标 + 草稿模型均加载后，执行一次实际生成请求使二者 KV cache 均被填充），记录同一进程 RSS 的新读数。
3. **叠加量计算**：步骤 2 读数减去步骤 1 读数，即为"引入草稿模型"这一行为本身带来的额外占用增量；该增量理论上应与 `RecommendedModelProfile`/`RecommendedDraftModelProfile`（`data-model.md`）中草稿模型的 `approxSizeDescription` 量级相近（草稿模型权重本身 + 其独立 KV cache，远小于目标模型）。
4. **记录位置**：实测完成后，数值结果应追加到 `quickstart.md` 对应验证场景的"实测记录"小节（仿照既有 003 `research.md` 对"具体加速倍数不预先断言、留待实测"类条款的处理方式），不在本次 `/speckit.plan` 阶段填入任何假定数值。

**本节结论**：不构成本阶段的 `[NEEDS CLARIFICATION]`——spec.md FR-005 已经把"是否启用"这一产品决策（仅中/高配启用，不按实测内存门槛动态启用/禁用）确定为固定按档位判断，不依赖运行时内存检测，故"具体占用多少"不影响本特性任何已确定的行为分支，只是一项需要实测记录的事实，留待实现完成后补充。

---

## 5. ModelCatalog 草稿模型数据结构与档位搭配映射方案（候选 + 权衡，**不在本阶段拍板具体映射取值**）

### 5.1 核实：现有 `classifyTierByFileName()` 关键字表是否会把草稿模型文件误判为目标档位

**核实方式**：对 FR-004 已确定的两档候选草稿模型规格的推荐文件名（延续 002/003 既有"文件名含版本号关键字"约定，如 `qwen2.5-0.5b-instruct-q4_k_m.gguf`/`qwen2.5-1.5b-instruct-q4_k_m.gguf`），逐一核对 `ModelCatalog.cpp` 现有 `classifyTierByFileName()`（`babel/src/slowpath/ModelCatalog.cpp`）的四个子串匹配分支：

| 候选草稿文件名子串 | 是否包含 `"3b"` | 是否包含 `"7b"` | 是否包含 `"14b"` | 是否包含 `"32b"` |
|---|---|---|---|---|
| `0.5b`（`qwen2.5-0.5b-instruct...`） | 否 | 否 | 否 | 否 |
| `1.5b`（`qwen2.5-1.5b-instruct...`） | 否 | 否 | 否 | 否 |

**核实结论**：按当前既有四个关键字分支，两档候选草稿文件名均**不会**命中任何一个分支，天然落入 `Unknown`，**不存在**"被误判为 Medium/High 目标档位"的冲突风险——用户提出的担忧在当前既定候选文件名下经核实并不成立，故**不需要修改** `classifyTierByFileName()` 既有四个分支的匹配逻辑或 `TierHint` 枚举本身（避免无必要变更，符合宪章原则 V）。

**Decision（内部实现细节，按宪章原则 IV 自主决定）**：尽管不存在误判风险，FR-007"应用自动识别并搭配草稿模型"仍需要一个独立于 `TierHint` 目标档位分类的"是否为已知草稿模型候选"识别机制（供 §5.2 的搭配映射查表使用）。新增一个**不影响** `ModelDescriptor`/`TierHint` 既有结构的独立静态方法（建议签名，最终命名留待 `/speckit.tasks`）：

```cpp
// 建议新增（ModelCatalog.h），与既有 classifyTierByFileName() 并列、互不影响：
enum class DraftCandidate { None, Size0_5B, Size1_5B };
static DraftCandidate classifyDraftCandidateByFileName(const QString &fileName);
```

该方法与 `classifyTierByFileName()` 同构（文件名子串匹配："0.5b"/"0-5b" → `Size0_5B`，"1.5b"/"1-5b" → `Size1_5B`，否则 `None`），供 §5.2 搭配查表与 `SessionController` 判定"当前目标档位所需草稿模型是否已被用户下载"使用；`ModelCatalog::scan()` 既有行为**不变**（仍返回全部 `*.gguf` 文件、含草稿模型文件在内，不从 `availableModels` 列表中过滤——保持 002 `contracts/qml-cpp-session-interface.md` "FR-011 全部 `*.gguf` 文件条目"既有契约不变；用户若手动在设置页面选中草稿模型文件触发 `switchModel()` 使其成为"目标模型"，因其 `tierHint` 落入 `Unknown`，按既有 `tierDebounceWindow()` 查表复用低配 200ms、且后续讨论的"仅 Medium/High 启用投机解码"判断自然不会命中，不会产生错误行为，只是用户会得到一个较弱的翻译模型，这不是本特性引入的新风险）。

### 5.2 新增数据结构：`RecommendedDraftModelProfile`（草稿模型推荐规格，静态数据）

与既有 `RecommendedModelProfile`（目标模型三档推荐）并列、结构同构但语义独立（不复用同一个 `Tier` 枚举，避免把"目标档位"与"草稿规格"两个不同维度的概念混用到同一个枚举类型里）：

```cpp
// 建议新增（ModelCatalog.h）：
struct RecommendedDraftModelProfile {
  ModelDescriptor::DraftCandidate size;  // Size0_5B / Size1_5B（§5.1）
  QString modelName;                      // 如 "Qwen2.5-0.5B-Instruct"
  QString quantization;                   // "GGUF Q4_K_M"（延续既有量化精度约定）
  QString approxSizeDescription;          // 待 /speckit.tasks 阶段按实际 GGUF 文件大小填入，本阶段不编造数值
};

static std::vector<RecommendedDraftModelProfile> recommendedDraftProfiles();
```

### 5.3 档位搭配映射——候选方案（**用户决策**）

FR-007 已确定"机制"（应用自动固定搭配，用户无需手动选择），本节此前仅列出候选方案供用户决策，**用户已于本轮后续对话中就具体映射取值作出显式决策**（见下文"用户决策"小节）：

- **方案 ①（等比例缩放）**：Medium（Qwen2.5-7B-Instruct）↔ 0.5B 草稿；High（Qwen2.5-14B-Instruct）↔ 1.5B 草稿。理由：目标模型越大，草稿模型"猜得准"所需的相对能力也可以适当更强（1.5B 相对 14B 的参数比例与 0.5B 相对 7B 的比例接近，维持两档"相对差距"的一致性直觉）。
- **方案 ②（统一使用较大草稿模型）**：Medium、High 均 ↔ 1.5B 草稿。理由：草稿模型越大，候选 token 与目标模型最终选择的吻合率通常越高（验证通过率更高），加速效果更稳定；代价是 Medium 档位下"目标模型本身解码一个 token 的开销"与"1.5B 草稿模型解码一个 token 的开销"的相对差距比搭配 0.5B 时更小，理论加速比收益可能不如方案①/③明显（草稿模型本身也需要时间，过大的草稿模型会侵蚀收益）。
- **方案 ③（统一使用较小草稿模型）**：Medium、High 均 ↔ 0.5B 草稿。理由：额外内存/显存占用（§4）最小、且对任意档位而言草稿模型本身的生成开销都最低，若验证通过率仍可接受则加速比上限最高；代价是验证通过率可能因"0.5B 草稿模型猜测 14B 目标模型行为"的难度更高而下降，对 SC-002（高配可测量降低）的达成存在不确定性，具体需实测验证（§4 方法论同样适用于验证通过率的实测，非本阶段断言）。
- **方案 ④（用户在候选规格之外无从知晓搭配结果，故两档候选均对两个目标档位同时生效，不做强制一对一映射）**：即不设"固定映射"，而是"用户下载了哪个草稿模型文件，就对中/高配统一使用那一个"（若两个草稿模型文件都下载了，则按某种优先级，如文件修改时间/体积，择一使用）。理由：进一步简化 FR-007 的实现（无需查表），用户只需下载"任意一个"候选规格即可，不需要理解"应该为哪个档位下载哪一个"。**但**：此方案与 FR-007 原文"应用根据当前目标模型档位（中配/高配）自动固定搭配**一个默认草稿模型**"的措辞存在张力——FR-007 的"固定搭配"表述更贴近"每个目标档位对应唯一默认草稿模型"的语义（即方案①/②/③中的某一个），方案④更接近"不区分档位、统一搭配"，需要用户确认该方案是否仍符合 FR-007 原意，还是 FR-007 已经排除了这种"不区分档位"的解读。

**Recommendation（原仅供参考，现已被用户采纳为最终决策——见下文）**：方案①（等比例缩放）在"直觉对应关系清晰""两档候选规格均有明确用武之地（不会出现'用户下载了 1.5B 却从未被用到'的情况）"两点上相对占优，但其验证通过率/实际加速效果仍需 `/speckit.implement` 后实测验证（SC-001/SC-002 本身也要求"以实测数据证明有可测量降低"，不要求规格阶段预判）。

### 用户决策（已采纳，取代上述 Recommendation 的"仅供参考"状态）

**Decision**：采纳**方案①（等比例缩放）**——

```text
pairedDraftCandidate(Medium) = DraftCandidate::Size0_5B   # Medium(Qwen2.5-7B-Instruct) ↔ Qwen2.5-0.5B-Instruct
pairedDraftCandidate(High)   = DraftCandidate::Size1_5B   # High(Qwen2.5-14B-Instruct)  ↔ Qwen2.5-1.5B-Instruct
```

**决策来源**：用户在本轮后续对话中明确指示"采纳方案①（Recommendation）：Medium(7B)↔Qwen2.5-0.5B-Instruct、High(14B)↔Qwen2.5-1.5B-Instruct，档位越高草稿模型也越强"，构成宪章原则 III 所要求的显式批准。

**理由（即方案①小节已列出的理由，经用户确认采纳）**：
1. 目标模型越大，草稿模型"猜得准"所需的相对能力也可以适当更强；1.5B 相对 14B 的参数比例与 0.5B 相对 7B 的比例接近，维持两档"相对差距"的一致性直觉，符合"档位越高、草稿模型也越强"的用户预期。
2. 两档候选草稿规格（FR-004 已确定的 0.5B/1.5B）均有明确对应的目标档位用途，不会出现"用户下载了某一档草稿模型却从未被应用实际搭配使用"的情况（对比方案②/③"统一使用同一档"会使另一档候选规格形同虚设）。
3. 方案④"不区分档位、按已下载文件统一搭配"与 FR-007"固定搭配"措辞存在张力，用户未选择该方案，不再需要进一步确认其是否符合原意。
4. 具体验证通过率/实际加速效果仍需 `/speckit.implement` 完成后按 `quickstart.md`"实测记录"小节实测验证，本决策仅确定映射关系本身，不预判实测数值（SC-001/SC-002 本身即要求以实测数据证明，不要求规格阶段预判）。

**对后续设计产物的影响（已回填，详见对应文件）**：
- `data-model.md` 三"SpeculativeDecodingProfile.pairedDraft"由【待决】回填为上表映射规则。
- `contracts/model-catalog-draft-pairing-internal-contract.md` 三"档位搭配映射表"由留空回填为上表具体返回值，并补充对应的行为契约断言。
- `quickstart.md` 前置条件/场景一/场景二中"以回填后的最终映射为准""示例措辞"等表述已回填为上表具体映射，不再是示例。

**本节结论**：§5.3 的具体映射取值已由用户显式决策为方案①（见上），不再是遗留的 `[NEEDS CLARIFICATION]`，已从 §9 清单移除。

---

## 6. GBNF 语法约束在投机解码路径下的应用方式（Decision，非待决事项）

**已有约束（非本阶段决策，FR-003 原文）**："草稿模型产出的候选 token 序列在被目标模型批量验证时，MUST 同样遵循既有语法约束规则，不得使约束失效或被绕过"——即**目标模型验证阶段必须应用 GBNF 约束**这一点已由 spec.md 明文规定，不是本阶段的待决问题。本阶段需要判断的是：**草稿模型自身的候选生成阶段**是否也需要应用同一套 GBNF 约束（spec.md 未明确规定这一子问题，但它不改变 FR-003 已保证的"最终输出安全性"，纯粹是影响"投机解码是否真的能带来加速"的工程实现细节，按宪章原则 IV 由 Agent 自主决定）。

**Decision：目标模型验证阶段与草稿模型候选生成阶段均需应用 GBNF 约束**，理由：

1. **正确性层面（已由 FR-003 保证，非本阶段新增）**：无论草稿阶段是否应用语法约束，目标模型验证阶段（§2 步骤 2）始终基于其自身的语法约束采样管线（003 既有 `llama_sampler_init_grammar()` + 梗阻检测循环）决定"真实"选择，因此最终写入输出的 token 序列必然满足语法约束——这一点与草稿阶段是否应用约束**无关**，是 FR-003 字面要求的直接满足。
2. **效率层面（本阶段新发现的工程必要性，解释"为什么草稿阶段也应该应用"）**：若草稿模型生成阶段完全不受语法约束，其产出的候选 token 中包含 `⟦`/`⟧`/`|` 等被禁符号的概率不为零；这类候选在目标模型验证阶段**必然**被判定为"与目标模型真实选择不符"（因为目标模型的语法掩码会将这些符号的 logit 置为 `-inf`，不可能被目标模型自身选中），从而使该位置及其后的全部草稿 token 一并失效（§2 步骤 3 的"从第一个不同位置截断"逻辑）。若草稿模型频繁猜出被禁符号，验证通过率会系统性降低，直接削弱甚至抵消投机解码本应带来的加速收益，与 FR-001"降低整句生成延迟"的核心目的相悖，也使 SC-001/SC-002"可测量的降低"更难达成。
3. **两个语法采样器实例需要状态同步**：GBNF 语法采样器（`llama_sampler_init_grammar()`）是**有状态**的——`llama_sampler_accept(token)` 会推进其内部语法解析堆栈（003 `research.md` §3 已核实："不可省略，否则下一轮 apply() 会基于过期堆栈状态误判合法性"）。草稿模型与目标模型是两个独立的 `llama_context`/采样器实例，各自需要一份**独立**的语法采样器对象（语法规则字符串相同，但内部堆栈状态各自维护，不能跨 `llama_context` 共享同一个 `llama_sampler*` 指针）；且无论某个位置的 token 最终是"草稿猜中被采纳"还是"验证失败被目标模型结果替换"，**两侧**的语法采样器都必须对该"最终确定的 token"调用一次 `accept()`，以保证草稿模型下一轮猜测时，其内部语法堆栈状态与"实际已生成的序列"保持一致（否则草稿模型会基于一个从未真正发生过的历史状态继续猜测，即使猜测本身枚举出合法字符也可能方向性跑偏，进一步降低验证通过率）。

**结论**：此项为内部算法实现细节（不改变 FR-003 已规定的外部可观察保证、不改变 `generate()` 签名），按宪章原则 IV 由 Agent 自主决定，记录如上，不构成 `[NEEDS CLARIFICATION]`。

---

## 7. 固定窗口大小（草稿 token 数量 K）是否存在技术必需的自适应需求（呼应 spec.md Out of Scope 核实）

spec.md Out of Scope 已声明"动态自动调整投机解码窗口大小"默认不实施，**除非** research 阶段发现"固定窗口大小在技术上不可行，必须引入某种基础的自适应机制才能完成已声明的 FR-001"。

**核实结论**：§2 描述的手写草稿-验证循环中，`K`（每轮草稿 token 数量）是一个可以**硬编码为固定常量**的参数（类比现有 `kMaxOutputTokens = 256` 的既有实现风格，`LlmEngine.cpp` 已有此类固定常量先例）；循环的正确性（§2 步骤 3 的比较-截断逻辑）**不依赖** `K` 取值大小——无论 `K` 取多少，验证逻辑都能正确工作（`K` 过大只会导致"验证不通过时浪费了更多草稿 token 的生成算力"，`K` 过小只会导致"加速收益相对变小"，两者都只影响性能表现、不影响 FR-001"语义等价"的正确性要求）。因此，**不存在**"固定窗口大小技术上不可行"的情形，spec.md Out of Scope 条款的排除范围**原样有效，无需推翻**；`K` 的具体固定数值（如 4/8/16）留待 `/speckit.tasks`/`/speckit.implement` 阶段按宪章原则 IV 作为内部实现细节确定（不改变外部可观察行为），本阶段不预先断言。

---

## 8. 构建 Qt 构建/测试基础设施是否需要变更（核实结论：不需要）

核实 `tests/CMakeLists.txt` 当前 `GLOB` 规则（`*Test.cpp` 自动发现）与 POST_BUILD ad-hoc codesign 配置——`/speckit.tasks` 阶段新增的测试文件（如针对 `classifyDraftCandidateByFileName()`/`recommendedDraftProfiles()` 的新用例）只需按既有命名约定追加到既有测试文件（`ModelCatalogTest.cpp` 等）或新建同样匹配 `*Test.cpp` 通配符的文件，现有 CMake 配置**无需任何改动**即可自动纳入；GoogleTest/vcpkg 依赖版本不变，不新增测试框架。

---

## 9. 本阶段新发现的待决事项（已全部由用户显式决策，无遗留 `[NEEDS CLARIFICATION]`）

按宪章原则 II"未决策不得默认"，以下两项此前均命中判定程序 (a)（可能改变外部可观察行为/实现复杂度分布）或属用户本轮明确要求"不得拍板"的范围，均**未**由 Agent 自行选定，而是以候选方案 + Recommendation 的形式交由用户决策；**用户已于本轮后续对话中就这两项分别作出显式决策**，故此处不再汇总为 `[NEEDS CLARIFICATION]` 清单，而是记录最终决策结果：

1. **SessionController ↔ LlmEngine 架构划分**（§3）：候选方案 A（双实例，`SessionController` 持有）/ B（`LlmEngine` 内部封装，推荐）/ C（新增独立协调类）。**用户决策：采纳方案 B**——`LlmEngine` 内部封装双模型逻辑，对 `SessionController` 完全透明。
2. **草稿-目标模型档位搭配映射具体取值**（§5.3）：候选方案 ①（7B↔0.5B / 14B↔1.5B，推荐）/ ②（统一 1.5B）/ ③（统一 0.5B）/ ④（不区分档位，按用户已下载的文件统一搭配）。**用户决策：采纳方案①**——Medium(7B)↔Qwen2.5-0.5B-Instruct、High(14B)↔Qwen2.5-1.5B-Instruct。

上述两项的最终决策已回填至 `data-model.md`/`contracts/model-catalog-draft-pairing-internal-contract.md`/`contracts/llm-engine-speculative-internal-contract.md`/`quickstart.md` 对应小节的"最终"字段（原"【待决】"/"故意留空"占位处均已替换为具体内容），`plan.md` Constitution Check 复查结论据此更新为无条件 PASS。
