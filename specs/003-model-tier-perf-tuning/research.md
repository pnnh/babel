# Research: 模型档位调整、GBNF 语法约束解码与防抖节奏优化

**Spec**: [spec.md](./spec.md) ｜ **Plan**: [plan.md](./plan.md)

## 阅读说明

本文件是 Phase 0 产出，对应 `spec.md` 四个方向中**方向一、三、四**（方向二投机解码已在 spec 中明确不实施，不产生任何研究任务）。按用户本轮指令，每一项调研**必须核实现有代码/已安装依赖的实际状态**，不得凭空假设库 API 签名；核实依据逐条注明来源文件路径或命令。

---

## 1. 当前 vcpkg 安装的 llama-cpp 版本是否支持 GBNF 语法采样 API（事实核实，非选型）

**核实方式**：直接阅读本机已构建产物中的头文件，而非查阅网络上任意版本的文档。

- 头文件实际路径：`build/macos/vcpkg_installed/arm64-osx/include/llama.h`。
- vcpkg port 清单：`vcpkg/ports/llama-cpp/vcpkg.json` 的 `"version": "9246"`，`portfile.cmake` 第 4 行 `REF b${VERSION}` → 对应上游 `ggml-org/llama.cpp` 的 `b9246` 发布标签。

**核实结论（已确认，非假设）**：该版本 `llama.h` 第 1361–1388 行确实声明了 GBNF 语法采样 API，具体函数签名如下（直接摘录，未做任何改写）：

```c
/// @details Initializes a GBNF grammar, see grammars/README.md for details.
/// @param vocab The vocabulary that this grammar will be used with.
/// @param grammar_str The production rules for the grammar, encoded as a string.
///        Returns an empty grammar if empty. Returns NULL if parsing of grammar_str fails.
/// @param grammar_root The name of the start symbol for the grammar.
LLAMA_API struct llama_sampler * llama_sampler_init_grammar(
        const struct llama_vocab * vocab,
                      const char * grammar_str,
                      const char * grammar_root);
```

另有 `llama_sampler_init_grammar_lazy_patterns()`（触发式语法，仅在匹配到特定前缀/token 后才启用语法约束）与已标记 `DEPRECATED` 的 `llama_sampler_init_grammar_lazy()`。本特性场景是"全程约束"而非"触发式约束"（FR-005 要求"生成过程中的每一个 token 均受语法规则约束"），因此采用非 lazy 版本 `llama_sampler_init_grammar()`，不考虑 lazy 变体。

**配套确认的采样管线原语**（`llama.h` 第 1234/1278/1279/1480 行）：
- `llama_sampler_apply(llama_sampler *smpl, llama_token_data_array *cur_p)`：对候选数组就地应用一个采样器（掩码/重排/截断 logits）。
- `llama_sampler_accept(llama_sampler *smpl, llama_token token)`：告知采样器"刚才实际选中了该 token"，驱动采样器内部状态前进（语法采样器用它来推进内部语法堆栈）。
- `llama_sampler_sample(llama_sampler *smpl, llama_context *ctx, int32_t idx)`：便捷封装，等价于"取 logits → 建 `cur_p` → `apply` → 取 `cur_p.data[cur_p.selected].id` → `accept` → 返回"（注释原文摘录于 `llama.h` 第 1470–1479 行）。

**结论**：本特性方向三在技术上可行，`LlmEngine::generate()` 现有的 `llama_sampler_chain_init`/`llama_sampler_chain_add` 组装方式（`LlmEngine.cpp` 当前实现）需要新增一个 `llama_sampler_init_grammar()` 构造的语法采样器并入采样管线；具体并入方式见 §3（涉及"梗阻检测"需求，不能简单套用现成的 `llama_sampler_chain_*` 封装，见下文）。

---

## 2. GBNF 语法规则设计：表达"最宽边界"（FR-007 已澄清）

**已澄清的产品决策（来自 spec.md Clarifications，非本阶段决策）**：合法字符集 = 汉字 + 中文标点 + 阿拉伯数字 + 英文字母 + 全角/半角标点 + emoji（即"几乎所有字符"），仅强制排除 `⟦`（U+27E6）、`⟧`（U+27E7）、`|`（U+007C，ASCII half-width vertical line）三个符号。

**核实**：三个被排除符号的 Unicode 码点，核实依据为 `LlmEngine.cpp` 现有系统提示词中的字节字面量 `"\xe2\x9f\xa6"`/`"\xe2\x9f\xa7"`（UTF-8 字节序列），解码得：
- `\xe2\x9f\xa6` = U+27E6（MATHEMATICAL LEFT WHITE SQUARE BRACKET，即 `⟦`）
- `\xe2\x9f\xa7` = U+27E7（MATHEMATICAL RIGHT WHITE SQUARE BRACKET，即 `⟧`）
- `|` = U+007C（ASCII VERTICAL LINE，代码中直接以字面量 `|` 分隔候选项）

**GBNF 是否支持"负向字符类 + 任意 Unicode（含 emoji 补充平面）"——核实而非假设**：本机环境未随 vcpkg 附带 `grammars/README.md` 源码（仅安装了编译产物头文件，未附带上游仓库文档/示例 `.gbnf` 文件，已核实 `vcpkg/ports/llama-cpp/` 目录与 `build/**/vcpkg_installed/**` 均无 `grammars/` 目录）。因此该项结论改为引用上游官方文档（`ggml-org/llama.cpp` 仓库 `grammars/README.md`，与本机 b9246 版本的语法引擎实现同源，语法 DSL 自该特性引入后未发生过破坏性变更）：
- 字符类终结符支持完整 Unicode，可直接书写原始字符，或用转义：8 位 `\xXX`、16 位（BMP）`\uXXXX`、32 位（含 emoji 所在的补充平面）`\UXXXXXXXX`。
- 字符类支持 `[^...]` 形式取反。
- 官方示例：`single-line ::= [^\n]+\n`。

**Decision（语法设计方案）**：

```gbnf
root      ::= safe-char*
safe-char ::= [^\u27E6\u27E7\x7C]
```

- `root` 定义为"零个或多个合法字符"（Kleene 星号），而非"一个或多个"：这使得语法状态机在**任意生成位置**都处于"可接受结束"状态（空字符串本身就是 `root` 的一个合法推导），从而不会与既有的 `llama_vocab_is_eog()` 自然终止逻辑冲突——语法层面从不强制生成必须达到某个最小长度。
- `safe-char` 用**单一负向字符类**而非枚举"汉字范围 + 标点范围 + 数字范围 + 字母范围 + emoji 范围"：因为 FR-007 的"最宽边界"决策的本质是"除三个符号外一切皆合法"，枚举正向范围存在遗漏某个 emoji/全角标点子范围、从而反向收窄字符集的风险（与"最宽边界"决策矛盾）；取反写法在语义上精确等价于"排除且仅排除这三个符号"，不存在遗漏风险，也是最简实现。
- `|` 使用 8 位转义 `\x7C` 而非直接书写字面量 `|`：`|` 在 GBNF 顶层规则体中是候选分支分隔符元字符，虽然字符类 `[...]` 内部按官方文档未将其列为需转义的元字符（元字符仅 `]`/开头的 `^`/`-`/`\`），直接书写大概率同样有效，但使用转义可消除对解析器实现细节的依赖、提高可读性与跨版本兼容性，内部实现细节，按宪章原则 IV 自主决定。

**替代方案与否决理由**：
- 枚举正向范围（如 `[\u4e00-\u9fff\u3000-\u303f0-9a-zA-Z...]` 列举十余个 Unicode 区段）：被否决，理由见上（遗漏风险、维护成本高，且与 FR-007"最宽边界"的决策意图相悖）。
- 仅在系统提示词层面做字符串后处理过滤（生成完成后用正则删除三个符号）：被否决，这不是"语法层面强制约束"（FR-005 明确要求是生成过程中逐 token 的语法约束，而非事后清洗），且与 FR-008"梗阻时提前终止并输出已生成部分"的语义不兼容（事后过滤不存在"梗阻"这个状态）。

---

## 3. 语法梗阻（找不到合法延续 token）检测与提前终止方案（FR-008 已澄清，本节为实现方案）

**已澄清的产品决策（非本阶段决策）**：梗阻时提前终止生成、输出已生成的部分内容，不放宽约束续写、不判定失败、不要求重试。

**核实：header 中是否存在现成的"梗阻"哨兵返回值？**——核实结论为**否**。`llama.h` 中唯一出现 `LLAMA_TOKEN_NULL`（第 39 行 `#define LLAMA_TOKEN_NULL -1`）哨兵语义的 API 是实验性的"后端采样 API”`llama_get_sampled_token_ith()`（第 1470 行附近注释"backend sampling API [EXPERIMENTAL]... note: use only if the llama_context was created with at least one llama_sampler_seq_config"），与本引擎当前使用的标准 `llama_sampler_sample()` 管线是**两套不同机制**，`LlmEngine.cpp` 现有实现未启用 `llama_sampler_seq_config`，不适用该哨兵值。标准 `llama_sampler_sample()` 的文档注释（`llama.h` 第 1470–1479 行）显示其内部固定执行 `cur_p.data[cur_p.selected].id`，若语法把全部候选的 logit 都压到 `-inf`，`cur_p.selected` 的值将取决于其后继采样器（如 `dist`）在"全 `-inf`"输入下的行为（softmax 对全 `-inf` 输入在数值上是未定义/退化情况），**不能依赖该便捷函数自动识别梗阻**。

**候选方案**：

1. **方案 A（否决）**：继续使用现有 `llama_sampler_chain_init`/`llama_sampler_chain_add` 一体式链条（语法 + temp + dist 全部加入同一条 chain），通过 `llama_sampler_sample()` 一次性采样。
   - 否决理由：链条内部各采样器的 `apply()` 依次调用，调用方无法在"语法采样器 apply 完、dist 采样器 apply 之前"这个中间时刻介入检查 `cur_p`，因此无法可靠探测梗阻，只能在采样完成后做"猜测性"事后判断（如检查选中 token 对应的原始未掩码 logit 是否为 `-inf`，但该值已被链条中前序采样器就地覆盖，不一定仍可取得原始值）。
2. **方案 B（Recommendation）**：不使用 `llama_sampler_chain_*` 封装，手动分步调用 `llama_sampler_apply()`，在"语法采样器单独 apply 之后、temp/dist 采样器 apply 之前"插入一次显式检查。伪代码（供 `/speckit.tasks`/`/speckit.implement` 参考，非最终实现代码）：

   ```cpp
   llama_sampler *grammar = llama_sampler_init_grammar(vocab, kOutputGrammar, "root");
   llama_sampler *temp    = llama_sampler_init_temp(0.3f);
   llama_sampler *dist    = llama_sampler_init_dist(/*seed=*/0);

   for (int i = 0; i < kMaxOutputTokens; ++i) {
     const float *logits = llama_get_logits_ith(m_ctx, -1);
     const int32_t nVocab = llama_vocab_n_tokens(vocab);
     std::vector<llama_token_data> candidates(nVocab);
     for (int32_t t = 0; t < nVocab; ++t) candidates[t] = {t, logits[t], 0.0f};
     llama_token_data_array cur_p{candidates.data(), candidates.size(), -1, false};

     llama_sampler_apply(grammar, &cur_p);  // 语法掩码：非法 token 的 logit 被置为 -inf

     const bool anyLegal = std::any_of(candidates.begin(), candidates.end(),
                                        [](const auto &d) { return d.logit > -INFINITY; });
     if (!anyLegal) {
       break;  // FR-008：语法梗阻，提前终止，result 已累积的部分内容即为本次返回值
     }

     llama_sampler_apply(temp, &cur_p);
     llama_sampler_apply(dist, &cur_p);     // 设置 cur_p.selected
     const llama_token nextToken = cur_p.data[cur_p.selected].id;
     if (llama_vocab_is_eog(vocab, nextToken)) break;  // 既有自然终止逻辑不变

     llama_sampler_accept(grammar, nextToken);  // 推进语法内部状态机（栈）
     // ……后续 llama_token_to_piece()/llama_decode() 与现有实现一致
   }
   ```

   - 检测依据：`cur_p` 中全部候选的 `logit` 字段在语法采样器 `apply()` 之后是否**全部**为 `-inf`（即候选集合在数值上等价于"空集"）。这是"通过检查候选 token 集合是否为空"这一候选思路的具体化。
   - `llama_sampler_accept(grammar, nextToken)` 这一步**不可省略**：语法采样器内部按已接受的 token 序列维护一个解析堆栈，若不调用 `accept()`，下一轮 `apply()` 仍会基于上一次的堆栈状态判断合法性，导致状态与实际已生成文本不同步。

**Decision**：采纳方案 B。属于 `LlmEngine::generate()` 内部实现细节（不改变对外签名/行为契约之外的任何可观察效果——FR-006），按宪章原则 IV 由 Agent 自主决定，无需用户确认；此处记录供 `/speckit.tasks` 任务拆分与 `/speckit.implement` 落地参考。

**系统提示词是否需要调整**：现有系统提示词末句"最终输出的句子中绝对不能包含 ⟦、⟧、| 等标注符号本身"（`LlmEngine.cpp` `kSystemPrompt`）在引入硬性语法约束后，从"保证正确性的必要条件"降级为"非必要的软性提示"（正确性已由语法层面保证）。**Decision：保留该句不变**。理由：该软性提示会引导模型在采样阶段本身就更倾向于不生成这三个符号对应的 token（降低其原始 logit 排名），从而降低"语法掩码后恰好无合法候选"（即触发 FR-008 梗阻提前终止、损失部分生成内容）的概率；移除该提示不会带来任何新增能力，反而可能实际增加梗阻发生频率。此为内部实现细节（不改变对外契约），按宪章原则 IV 自主决定。

---

## 4. DebounceScheduler 支持运行时调整窗口时长的改造方案（FR-010/FR-013/FR-014）

**现状核实**：`DebounceScheduler.h` 当前构造函数为 `DebounceScheduler(std::chrono::milliseconds debounceWindow, GenerateCallback onTrigger)`，`m_debounceWindow` 为私有成员，构造后无任何修改入口；`tests/slowpath/DebounceSchedulerTest.cpp` 现有 7 个测试用例均直接以固定毫秒数构造实例并断言状态机迁移，未涉及窗口动态调整。

**候选方案**：

1. **方案 A（Recommendation）**：新增一个轻量 setter 方法 `void setDebounceWindow(std::chrono::milliseconds window)`，仅更新 `m_debounceWindow` 这一个成员；构造函数签名保持不变。`SessionController` 在 `bootstrapPlatformDefaults()`（初始化时）与 `applyModelSwitchResult()`（热切换成功后）调用该方法。
   - 优点：完全不触碰 `m_state`/`m_pendingInput`/`m_lastChangeAt`/`m_timerArmed`/`m_generationInFlight` 等既有状态字段，天然满足 FR-013"MUST NOT 改变既有状态机本身"；现有 7 个测试用例的构造方式无需修改（新增测试用例针对 setter 本身即可）；FR-014"下一次防抖触发周期生效"天然成立——下一次 `tick()` 读取的 `m_debounceWindow` 已是新值，无需额外"生效时机"控制逻辑。
2. **方案 B（否决）**：模型切换时由 `SessionController` 整体 `reset()`/重新 `make_unique<DebounceScheduler>(newWindow, callback)` 构造新实例。
   - 否决理由：需要手动搬运 `m_state`/`m_pendingInput`/`m_generationInFlight` 等运行时状态到新实例（否则一次正在进行中的 `Generating`/`Stale` 会被静默丢弃，可能导致用户当前正在等待的一次生成结果凭空消失），复杂度与风险均高于方案 A，且没有换来任何额外收益。
3. **方案 C（否决）**：构造函数改为接收 `std::function<std::chrono::milliseconds()>` 回调（每次 `tick()` 动态查询当前窗口值），而非存储具体数值。
   - 否决理由：本特性"模型档位 → 窗口时长"的对应关系在"切换完成的那一刻"就已确定（FR-014），不存在"需要在每次 `tick()` 时重新查询外部状态"的场景，引入回调只是增加间接层与被测试覆盖的复杂度，未带来实际收益。

**Decision**：采纳方案 A。属于内部实现细节（不改变外部可观察的状态机行为，只改变触发所需等待的时长这一数值本身，而"该数值应为多少"已由 FR-011/FR-012 的产品决策给定），按宪章原则 IV 自主决定。

---

## 5. SessionController 如何获知"当前模型属于哪个档位"（FR-010/FR-014）

**现状核实**：`ModelCatalog.cpp` 匿名命名空间内已存在 `classifyTier(const std::string &fileName)` 函数（私有，仅供 `scan()` 内部调用），依据文件名关键字子串判定：`"3b"`/`"3-b"` → `Low`；`"14b"` → `Medium`；`"32b"` → `High`；均不匹配 → `Unknown`。`tests/slowpath/ModelCatalogTest.cpp` 的 `TierHintClassifiesByFilenameKeyword` 用例即覆盖该行为。

**关键冲突点（必须在本阶段澄清，否则会产生错误行为）**：FR-001/FR-002 将"中配推荐"由 14B 改为 7B、"高配推荐"由 32B 改为 14B。若不同步调整 `classifyTier()` 的关键字—档位对应表，会出现：用户下载**新高配推荐模型**（文件名含 `"14b"`）后，现有逻辑仍会将其误判为 `Medium`（旧版"中配"关键字），进而在 FR-012 中被分配 500ms 窗口而非其实际应得的 800ms 高配窗口——与 FR-002"高配推荐调整为 Qwen2.5-14B-Instruct"的决策矛盾。

**核实：这是否恰好是"同一模型文件"从而不存在冲突？**——核实结论为**是，可以零冲突对齐**：002 旧版"中配"推荐型号与本特性新版"高配"推荐型号字面量完全相同，均为 **Qwen2.5-14B-Instruct（GGUF Q4_K_M）**（对照 `spec.md` 背景小节原文："中配推荐从 Qwen2.5-14B-Instruct 调整为 Qwen2.5-7B-Instruct、高配从 Qwen2.5-32B-Instruct 调整为 Qwen2.5-14B-Instruct"）。也就是说，此前被归为 `Medium` 的 14B 文件，在本特性的新档位语义下**本就应该**归为 `High`——这不是一个全新发明的产品判断，而是"同一关键字—现在对应新的正确档位"这一必然推论，与 FR-004"旧版 14B/32B 文件仍可被扫描识别并使用"完全兼容（可用性不变，仅重新归类到与其实际参数规模相符的档位标签）。

**Decision（分类表更新，内部实现细节 + 直接由 FR-001/FR-002 推导，非新增产品决策）**：

| 文件名关键字 | 原 `classifyTier()`（002） | 新 `classifyTier()`（本特性） |
|---|---|---|
| `"3b"` / `"3-b"` | `Low` | `Low`（不变） |
| `"7b"` / `"7-b"` | （未覆盖） | `Medium`（**新增**，对应 FR-001 新中配推荐） |
| `"14b"` | `Medium` | `High`（**变更**，14B 现为新高配推荐，见上文核实） |
| `"32b"` | `High` | `High`（不变，旧版高配文件仍可用，复用"大体量旧档位统一计入 High"的既有分类粒度，见下方关于 Unknown/32B 的说明） |
| 均不匹配 | `Unknown` | `Unknown`（不变） |

**获知当前档位的方案**：

1. **方案 1（Recommendation）**：复用上表更新后的分类函数，将其从 `ModelCatalog.cpp` 匿名命名空间中提升为 `ModelCatalog` 的公开静态方法（如 `ModelCatalog::classifyTierByFileName(const QString &fileName)`），供 `SessionController` 在 `bootstrapPlatformDefaults()`/`applyModelSwitchResult()` 成功路径中对 `m_modelRuntimeState.currentModelPath.filename()` 调用，得到 `ModelDescriptor::TierHint` 后查表（见 `data-model.md` 新增的 `TierDebounceProfile`）得到对应窗口时长。
   - 优点：完全复用 002 已验证、已有测试覆盖的同一套分类逻辑与数据来源（`std::filesystem::path` 文件名），不新增任何平行的"第二套档位判断机制"，符合宪章原则 II"已存在的同类实现取值 MAY 沿用，MUST 在 plan.md 中写明来源"（来源：`ModelCatalog.cpp` 现有 `classifyTier()`，经上表关键字—档位映射更新后行为与新版 FR-001/FR-002 推荐数值完全一致）。
2. **方案 2（否决）**：改为按**精确文件名白名单**匹配（而非关键字子串），如仅当文件名精确等于某个预设常量才判定档位，其余一律 `Unknown`。
   - 否决理由：与 002 已确立并测试覆盖的"关键字子串启发式"行为不一致，属于不必要的行为收紧，且需要用户手动下载的真实文件名可能与"推荐"文案存在大小写/分隔符等微小差异（如 `qwen2.5-14b-instruct-q4_k_m.gguf` vs 用户自行重命名），精确匹配会降低鲁棒性。
3. **方案 3（否决）**：`switchModel(filePath)` 由 QML 侧在调用前从其本已持有的 `availableModels` 列表条目中取出 `tierHint` 字段，一并作为新参数传入。
   - 否决理由：需要改变 `switchModel()` 现有对外签名（002 契约 `contracts/qml-cpp-session-interface.md` 已固定为 `switchModel(filePath: string)` 单参数），本特性 Assumptions/FR 均未授权修改该契约，按宪章原则 V"最小必要修改"与"未在 plan.md 中确认的依赖变更"的禁止项，不采纳。

**Decision**：采纳方案 1。

**`Unknown` 档位防抖窗口数值（本阶段曾发现 `[NEEDS CLARIFICATION]`，现已由用户显式决策，不再待定）**：

> **背景**：当前生效模型的 `tierHint` 为 `Unknown`（文件名不含 `"3b"`/`"7b"`/`"14b"`/`"32b"` 等任何已识别关键字，即用户自行放置的自定义命名模型文件）时，`DebounceScheduler` 应使用的防抖窗口时长，FR-010～FR-012 仅针对"低配/中配/高配"三个官方命名档位给出了明确数值，未覆盖 `Unknown` 这一第四种运行时可能出现的状态；该取值改变用户可感知的交互节奏（判定程序 (a) 命中），曾存在以下三个互不等价的候选，本计划此前未自行拍板：
>
> 1. **方案①（Recommendation，已采纳）**：复用低配 200ms。
> 2. 方案②（未采纳）：复用高配 800ms。
> 3. 方案③（未采纳）：固定取三档窗口的中间值 500ms。
>
> **用户决策**：Unknown 档位复用低配 200ms（采纳方案①/Recommendation）。
>
> **理由**（用户决策 + 既有论证一并记录）：
> - 与引入本特性前的原始固定行为（001/002 固定 200ms）最接近，变更面最小，符合"未被本特性新语义覆盖的情形应尽量维持原状"的保守直觉，不对自定义命名模型的实际参数规模做任何无依据的主观假设（方案②"文件名偏离官方命名即推测参数规模较大"缺乏可验证依据，本计划不采纳该推测作为决策基础）。
> - 与 FR-011"低配档位 200ms 窗口不受本特性调整影响"的既有基线保持数值一致，`Unknown` 分支因此不构成需要单独测试覆盖一套"全新窗口数值"的额外实现分支，降低实现与测试复杂度。
> - 为运行时可能出现的全部四种 `TierHint` 取值（`Low`/`Medium`/`High`/`Unknown`）提供了完整、无遗漏的窗口映射，`tierDebounceWindow()` 查表函数不再存在未定义分支（`data-model.md` 二、`contracts/debounce-tier-window-internal-contract.md` 三均已据此回填具体数值）。
>
> 本项**不再阻塞** `/speckit.tasks` 为"`Unknown` 档位防抖窗口赋值"这一子任务给出最终验收标准——该子任务 MUST 直接采纳本节记录的 200ms 作为验收数值，无需再等待用户进一步确认/委派。

---

## 6. ModelCatalog 推荐档位静态数据（`RecommendedModelProfile`）更新内容（FR-001/FR-002，事实更新非选型）

**现状核实**：`ModelCatalog.cpp::recommendedProfiles()` 当前硬编码三条静态数据（低配 3B ≈2GB、中配 14B ≈9GB、高配 32B ≈19.85GB）。

**Decision（按 FR-001/FR-002 原文数值更新，附带经网络检索核实的近似文件体积，非产品层决策）**：

| 档位 | 原数值（002） | 新数值（本特性，FR-001/FR-002） | 近似体积（GGUF Q4_K_M，公开模型仓库核实） |
|---|---|---|---|
| 低配 Low | Qwen2.5-3B-Instruct | **不变** | 约 2GB（不变） |
| 中配 Medium | Qwen2.5-14B-Instruct | **Qwen2.5-7B-Instruct** | 约 4.6GB（原中配 9GB 估计已不适用，新中配模型参数规模更小） |
| 高配 High | Qwen2.5-32B-Instruct | **Qwen2.5-14B-Instruct** | 约 9GB（与 002 原"中配"体积估计相同数值，因新高配模型与 002 旧中配模型是同一模型） |

量化方案字符串（`"GGUF Q4_K_M"`）、`targetHardwareDescription`（目标硬件描述文案）均不变，仅 `modelName`/`approxSizeDescription` 两个字段随新档位数值更新。

---

## 结论汇总（供 `/speckit.tasks` 排期参考）

| 编号 | 事项 | 结论 |
|---|---|---|
| §1 | llama-cpp GBNF API 是否存在 | 已核实存在，`llama_sampler_init_grammar(vocab, grammar_str, grammar_root)`（`llama.h:1365`） |
| §2 | 最宽边界语法设计 | `root ::= safe-char*` / `safe-char ::= [^\u27E6\u27E7\x7C]`（否定字符类，不枚举正向范围） |
| §3 | 梗阻检测与提前终止 | 手动分步 `llama_sampler_apply()`，语法掩码后检查 `cur_p` 全体候选是否均为 `-inf`；系统提示词维持不变 |
| §4 | DebounceScheduler 动态窗口改造 | 新增 `setDebounceWindow(std::chrono::milliseconds)` setter，构造函数签名不变，状态机字段不受影响 |
| §5 | SessionController 档位识别 | 复用并更新 `ModelCatalog::classifyTier()`（提升为公开静态方法），关键字表新增 `7b`→Medium、`14b`→High（变更） |
| §5 | `Unknown` 档位防抖窗口数值（用户决策） | 用户决策：复用低配 200ms（方案①/Recommendation 已采纳，不再待定，见 §5 详述） |
| §6 | `RecommendedModelProfile` 静态数据更新 | 按 FR-001/FR-002 更新 `modelName`/`approxSizeDescription` |
