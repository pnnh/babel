# Bug Assessment: GBNF 语法约束解码引入未被量化的巨大延迟开销

- **Slug**: gbnf-grammar-sampling-overhead
- **Created**: 2026-10-21
- **Source**: pasted text（用户实测描述）+ 本会话内独立复现探针（探测脚本已清理，数据保留于本报告）
- **Verdict**: valid
- **Severity**: high

## Report（概述）

用户实际运行 7B/14B/3B 三档模型，输入同一句"I today xinqing hen bad. Wo want qu chi shaokao."，反馈：

1. 翻译理解质量普遍不佳（与另一个独立 bug `english-word-pinyin-misdetection` 共同造成，已分案处理）；
2. 延迟体感异常："即使 3B 模型需要生成的时间体感上也有 2 秒钟以上，14B 似乎要 5 秒以上"，用户要求分析是否正常。

针对第 2 点延迟异常，本次会话在修复英文误判 bug 的同时独立插入探针做了对比测量（探针代码已清理，数值如下）：

| 配置 | 输入 | 3B 实测延迟 |
|---|---|---|
| 002 基线（**无** GBNF，`llama_sampler_chain_init` 一体式温度+采样链） | 纯文本 | **628ms** |
| 003（**含** GBNF `llama_sampler_apply(grammar, …)`） | 纯文本（无 Lattice 标注符号） | **约 2000ms**（约 3.2 倍） |
| 003 真实生产管道（Lattice 标注 + GBNF） | 含 `⟦⟧\|` 标注的真实 prompt | **5404ms**（约 8.6 倍） |

002 的 628ms 在 SC-003（见 `specs/001-*/spec.md` 第 122 行：低配默认模型 Qwen2.5-3B 一整句生成 **MUST** < 800ms）阈值内；003 引入 GBNF 后，低配默认模型的同类请求已稳定超出该阈值 **2.5～6.8 倍**。SC-003 明确规定该阈值仅对低配默认模型强制适用（中/高配已被 Clarification 豁免），因此这不是"中/高配用户已知情接受更长延迟"的范畴，而是**对仍需强制达标的默认模型路径的真实回归**。

## Symptom

002 特性交付时，低配 Qwen2.5-3B 的整句生成延迟（628ms）满足 SC-003 的 800ms 硬性阈值。003 特性引入 GBNF 语法约束解码后，**同一模型、同一类输入**的延迟上涨到约 2～5.4 秒，使默认模型不再满足自身规格明确要求的延迟上限。003 的 `tasks.md`/`research.md` 在交付时验证了 GBNF 的**功能正确性**（标注符号不泄漏、语法梗阻提前终止），但从未测量其**性能代价**，该开销因此从未被纳入 003 的验收决策。

## Reproduction

1. 使用 002 基线代码（`git show c7ee534:babel/src/slowpath/LlmEngine.cpp`）对 3B 模型发起一次 `generate()`，记录整句生成耗时 → 628ms。
2. 使用当前 HEAD（含 003 GBNF）代码对同一 3B 模型、**仅替换 prompt 为无 Lattice 标注的纯文本**发起 `generate()`，记录耗时 → 约 2000ms。
3. 使用当前 HEAD 代码、真实生产 prompt（含 `⟦⟧\|` Lattice 标注）发起 `generate()`，记录耗时 → 5404ms。
4. 对比三组耗时，差值可完全归因于「引入 GBNF」与「引入 Lattice 标注导致 prompt token 数增加 + 标注符号对语法判定产生更多边界检查」两个独立变量的叠加。

[NEEDS CLARIFICATION: 14B "体感 5 秒以上"的数值尚未用同样的受控探针方法独立量化（仅有 3B 的精确数据），但报告中引用的用户原话与 3B 的相对regression幅度方向一致，可作为高置信度的间接佐证。]

## Suspected Code Paths

- `babel/src/slowpath/LlmEngine.cpp:257-304`（非投机解码分支）与 `:313-464`（投机解码分支，草稿/目标双侧）— 两处循环体均在**每个输出 token** 的采样前执行：
  - 新建 `std::vector<llama_token_data> candidates(nVocab)`（`nVocab = 151936`，已用 GGUF 元数据直接解析 `tokenizer.ggml.tokens` 数组长度验证，见下方 Root Cause），逐项填充 logits；
  - 调用 `llama_sampler_apply(grammar, &cur_p)` 对**全部 151936 个候选 token** 执行 llama.cpp 内置 GBNF 栈式语法机解析（逐 token 解码为 UTF-8 字节串、针对 `safe-char` 字符类做逐字符栈状态转移校验）；
  - 调用 `llama_sampler_accept(grammar, nextToken)` 推进语法解析器内部堆栈。
- 对照 002 原始实现（`git show c7ee534:babel/src/slowpath/LlmEngine.cpp:133-141`）：仅用 `llama_sampler_chain_init` 封装 `temp`+`dist` 两个采样器，`llama_sampler_sample(sampler, m_ctx, -1)` 一次调用完成，**没有** grammar 相关的全词表扫描。003 新增的唯一结构性差异就是 `llama_sampler_apply(grammar, &cur_p)` 这一步。

## Root Cause Hypothesis

**高置信度**。`kOutputGrammar`（`LlmEngine.cpp:218`）定义为 `root ::= safe-char*` / `safe-char ::= [^\u27E6\u27E7\x7C]`——这是一个**无递归、无状态依赖**的字符类过滤器：每个 token 是否合法只取决于它自身解码出的字符是否命中 3 个被禁字符，与之前生成了什么完全无关（Kleene 星号展开后不产生需要跨步骤记忆的语法状态）。但当前实现把这个本质上"静态黑名单"的过滤规则，通过 `llama_sampler_init_grammar` 接入了 llama.cpp 通用的 GBNF 栈式语法引擎，导致**每一个输出 token**，都要对**全部 151936 个词表候选** token 逐一做"解码为 UTF-8 字节串 + 栈状态转移模拟"这一通用但昂贵的语法校验路径——而该校验结果对于给定模型的词表其实是**跨 generate() 调用、跨 session 永久不变的静态属性**（某个 token id 的字符串里是否含有 `⟦`/`⟧`/`|`，只取决于模型词表本身，不取决于当前生成到第几步）。

用通用语法引擎实现一个静态黑名单过滤器，是"规则正确但引擎选型过重"的典型性能反模式：003 阶段只验证了"标注符号不泄漏""语法梗阻正确终止"两项功能正确性（FR-005～FR-008），完全没有测量这一全词表逐 token 语法解析在 256 个输出步 × 151936 候选规模下的真实耗时代价，该代价直到用户本次实测才被发现。

## Proposed Remediation

**Preferred（预计算静态禁用 token 黑名单，绕开通用语法引擎）**：

在模型加载完成后（而非每次 `generate()` 内、更不是每个输出 token 内）**一次性**遍历词表全部 token id，用 `llama_token_to_piece` 解码每个 token 的字符串，判定其是否包含 `⟦`(U+27E6)/`⟧`(U+27E7)/`|`(U+007C) 三者之一，结果缓存为一个 `std::vector<bool>` 或 `std::unordered_set<llama_token>` 静态禁用集合（对 151936 词表规模，预期禁用 token 数量极小，仅为包含这三个字符的词条）。采样循环内，不再调用 `llama_sampler_init_grammar`/`llama_sampler_apply(grammar, …)`，而是在构建 `candidates` 时直接对禁用集合中的 token 把 `logit` 置为 `-INFINITY`（与当前语法掩码效果等价），其余流程（`anyLegal` 梗阻检测、`temp`/`dist` 采样）不变。

此方案与当前实现在**功能语义上完全等价**（同一组字符被禁止、同一梗阻检测逻辑），但把"每 token 全词表语法解析"降级为"每 token 全词表查表"（O(1) 哈希/位图查询 × 151936，无字符串解码、无栈模拟），预期可把 003 新增的开销压低到接近 002 基线水平。

**Alternatives**：
- **（备选 A）仅优化候选数组复用**：避免每个输出 token 重新 `new` 一次 `std::vector<llama_token_data>`，改为在循环外分配一次、循环内重置 logit 值。收益有限——探针数据显示主要耗时在 `llama_sampler_apply(grammar, …)` 本身的语法解析，而非数组内存分配，预计只能省下毫秒级开销，不建议作为主修复方向，可作为次要微优化顺带做。
- **（备选 B）生成后过滤而非生成中阻断**：放弃"生成时主动禁止"，改为生成完毕后用字符串 `replace`/正则剥离 `⟦⟧|`。性能最优（零采样期开销），但改变了 FR-005～FR-009 的设计意图（从"模型物理上不可能生成"降级为"生成后事后清理"，`anyLegal`/语法梗阻提前终止语义也随之消失），**需要先征得用户对原 003 需求的让步确认**，不建议作为默认选项。

**Files likely to change**：
- `babel/src/slowpath/LlmEngine.cpp`（两处采样循环：非投机解码分支 `:257-304`，投机解码分支草稿侧 `:313-359` 与目标侧 `:382-464`）
- 可能新增一个小型辅助类/函数（如 `ForbiddenTokenMask`）封装"词表预扫描+禁用集合"逻辑，供三处循环复用，避免重复实现三份

**Tests to add or update**：
- 性能回归断言：复现本报告的受控对比探针（002 基线 vs 003 当前 vs 003 修复后），确认修复后 3B 纯文本延迟回落到基线量级（无需精确匹配 628ms，但应显著低于当前约 2000ms）
- 功能不回归：003 既有的"标注符号不泄漏到输出""语法梗阻提前终止"相关测试必须继续通过，证明新的静态黑名单掩码与原 GBNF 语法引擎行为等价
- 边界用例：若某个 token 字符串恰好同时包含合法字符与被禁字符（如一个 token 解码出的多字符片段里混有 `|`），黑名单判定逻辑需要和原语法引擎的"整个 token 若含非法字符即判非法"行为保持一致，建议补充一个针对此类"混合内容 token"的单元测试

## Risks & Considerations

- 预计算黑名单依赖 `llama_token_to_piece` 对全部 151936 个 token 做一次性解码，这一步本身也有成本，但只需在**模型加载时**执行一次（而非每次 generate() 或每个输出 token），即使耗时几十到上百毫秒也完全可接受，不应放入请求关键路径。
- 投机解码（004）引入了草稿模型与目标模型**两套独立词表**（`draftVocab`/`vocab`），黑名单预计算需要对两个词表分别构建缓存，不能共用同一份禁用集合——这是容易遗漏的细节，fix 阶段需要显式覆盖。
- 本修复只解决"用通用语法引擎实现静态过滤器"这一个性能反模式；若未来 003/004 之外的特性需要**真正有状态**的语法约束（例如要求输出必须是合法 JSON），静态黑名单方案不适用，届时仍需引入 `llama_sampler_init_grammar` 这类通用引擎，本次修复不构成对该能力的永久移除，建议封装为可替换的策略而不是硬编码删除 grammar 相关代码路径。
- 14B 档位的精确延迟回归幅度本报告未独立测量（见 Open Questions），fix 完成后建议至少补测一次 14B 的 before/after 对比，确认修复在更大模型上同样有效（理论上收益应更明显，因为全词表语法解析的相对占比在更大模型的更长总耗时中可能占比更低，也可能因为更大模型本身推理更慢导致该项开销占比反而被稀释，需要实测佐证而非假设）。

## Open Questions

- [NEEDS CLARIFICATION: 14B 档位"体感 5 秒以上"尚未用相同受控探针精确量化，fix 完成后建议补测]
- [NEEDS CLARIFICATION: 是否需要在本 bug 范围内，把 002 原本更快的 `llama_sampler_chain_init` 一体式封装（而非 003 引入的手动分步 apply）在非语法相关的 temp/dist 环节上也一并恢复，还是保持现状仅替换 grammar 这一步——倾向后者（最小改动），但留待 fix 阶段确认]
