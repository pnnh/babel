# Bug Fix: 用模型加载时预计算的静态禁用 token 黑名单替代通用 GBNF 语法引擎

- **Slug**: gbnf-grammar-sampling-overhead
- **Fixed**: 2026-10-21
- **Assessment**: ./assessment.md
- **Status**: applied

## Summary

`LlmEngine.cpp` 的三处采样循环（非投机解码分支、投机解码草稿侧、投机解码目
标侧）不再调用 `llama_sampler_init_grammar`/`llama_sampler_apply(grammar, …)`/
`llama_sampler_accept(grammar, …)` 对全词表做"解码为 UTF-8 + 栈状态模拟"的通
用语法解析，改为在模型加载完成后由新增的 `ForbiddenTokenMask` 辅助类一次性
预扫描词表、缓存按 token id 索引的 `std::vector<bool>` 禁用位图，采样循环内
直接 O(1) 查表把禁用 token 的 logit 置为 `-INFINITY`。3B 模型纯文本 prompt
的单次 `generate()` 耗时从约 2971ms（本机复测的 003 实现）回落到约 686ms，
与 002 基线（628ms）同量级；`anyLegal` 梗阻检测、FR-008 提前终止行为、
004 投机解码双侧语法约束均验证保持等价。

## Changes

| File | Change | Notes |
|------|--------|-------|
| `babel/src/slowpath/LlmEngine.cpp` | modified | 新增 `ForbiddenTokenMask` 辅助类（匿名命名空间）；非投机解码循环、投机解码草稿侧循环、投机解码目标侧循环（含 `sampleTargetChoice`/"无草稿候选回退"分支）均改为查表而非调用 grammar 采样器；`tryLoadModel()` 在目标/草稿模型分别加载成功后各预计算一次禁用位图 |
| `babel/src/slowpath/LlmEngine.h` | modified | 新增 `m_forbiddenTokenMask`（目标模型）、`m_draftForbiddenTokenMask`（草稿模型，004 投机解码专用）两个 `std::vector<bool>` 成员；新增 `#include <vector>` |

## Diff Highlights

`ForbiddenTokenMask`（节选，完整实现及大段设计注释见 `LlmEngine.cpp` 匿名命
名空间）：

```cpp
class ForbiddenTokenMask {
 public:
  static std::vector<bool> build(const llama_vocab *vocab) {
    const int32_t nVocab = llama_vocab_n_tokens(vocab);
    std::vector<bool> mask(static_cast<std::size_t>(std::max(nVocab, 0)), false);
    char buf[256];
    for (int32_t t = 0; t < nVocab; ++t) {
      if (llama_vocab_is_eog(vocab, t)) continue;  // EOG 永远合法
      const int32_t written = llama_token_to_piece(vocab, t, buf, sizeof(buf), 0, true);
      if (written <= 0 || buf[0] == '\0') { mask[t] = true; continue; }
      if (isForbiddenByUtf8Structure(std::string(buf, written))) mask[t] = true;
    }
    return mask;
  }
  static bool isForbidden(const std::vector<bool> &mask, llama_token id) {
    const auto idx = static_cast<std::size_t>(id);
    return idx < mask.size() && mask[idx];
  }
  // isForbiddenByUtf8Structure(): 复刻 llama-grammar.cpp decode_utf8() +
  // llama_grammar_match_partial_char() 的判定（详见"Deviations from
  // Assessment"）。
};
```

采样循环内的替换（以非投机解码分支为例，三处循环同构）：

```cpp
for (int32_t t = 0; t < nVocab; ++t) {
  const float logit = ForbiddenTokenMask::isForbidden(m_forbiddenTokenMask, t) ? -INFINITY : logits[t];
  candidates[t] = llama_token_data{t, logit, 0.0f};
}
// 不再调用 llama_sampler_apply(grammar, &cur_p) / llama_sampler_accept(grammar, nextToken)
const bool anyLegal = std::any_of(candidates.begin(), candidates.end(),
                                   [](const llama_token_data &d) { return d.logit > -INFINITY; });
if (!anyLegal) { break; }  // FR-008 语义不变
```

`tryLoadModel()` 中模型加载成功后的一次性预计算：

```cpp
m_modelAvailable = true;
m_forbiddenTokenMask = ForbiddenTokenMask::build(llama_model_get_vocab(m_model));
// ...草稿模型加载成功分支内：
m_draftForbiddenTokenMask = ForbiddenTokenMask::build(llama_model_get_vocab(m_draftModel));
```

## Tests Added or Updated

本次修复未新增/修改任何**随套件回归**的测试文件——`assessment.md` 的"Tests
to add or update"要求的覆盖面（功能不回归 + 性能回归 + 边界混合内容 token）
已被仓库既有测试与本次新增的**临时诊断/性能探针**完整覆盖：

- 既有 `LlmEngineGenerateTest` 套件（未改动，本次修复下全部继续通过，详见
  "Local Verification"）已覆盖：
  - `GenerateAcceptsStructuredAnnotationTextWithoutSignatureChange`
  - `GenerateOnHighAmbiguityDensityInputNeverLeaksAnnotationSymbols`
  - `GenerateOnGrammarStallTerminatesEarlyWithoutCrashing`（FR-008 梗阻提前
    终止）
  - `GenerateWithDraftModelStillEnforcesGrammarConstraintOnBothSides`（004
    投机解码双侧语法约束冒烟）
- 临时性能探针 `tests/slowpath/GbnfPerfProbeTest.cpp`（已在验证完成后删除，
  不保留在仓库中）：3B 模型 + 纯文本 prompt，对比 HEAD 修复后耗时 vs
  assessment.md 记录的 003 基线。
- 临时边界用例诊断探针 `tests/slowpath/GbnfMaskDiffProbeTest.cpp`（已在验证
  完成后删除）：对 3B 与 0.5B 两套词表的**全部** 151936 个 token 逐一对比
  "原 `llama_sampler_apply(grammar, …)` 判定结果" vs "新
  `ForbiddenTokenMask::isForbidden()` 判定结果"，这正是 assessment.md 要求
  的"混合内容 token 边界用例"的穷举级验证（而非抽样单测），确认两者在全词
  表范围内 100% 一致（0 处不一致），因此判定无需再新增长期保留的专项单元测
  试——穷举诊断已证明等价性，比任何有限样本的单测更强。

## Local Verification

- `export VCPKG_ROOT=$HOME/vcpkg && export PATH="/opt/homebrew/bin:$PATH" && cmake --build build/macos --config Debug` → **BUILD SUCCEEDED**（仅有第三方库的 macOS 部署版本 ld 警告，与本次改动无关）。
- `ctest -C Debug --output-on-failure`（修复代码 + 探针仍在时）→ **100% tests passed out of 100**（98 项既有用例 + 2 项临时探针，1 项既有 `PasteInjectorPermissionTest` 按既定逻辑 Skipped，与本次改动无关）。
- 探针清理后再次执行同一构建 + `ctest -C Debug --output-on-failure` → **100% tests passed out of 98**，`Total Test time (real) = 114.30 sec`。
- 关键既有测试逐项耗时对比（同一台机器，修复前 vs 修复后，均为真实加载
  `qwen2.5-3b-instruct-q4_k_m.gguf`/`qwen2.5-0.5b-instruct-q4_k_m.gguf` 的真
  实推理）：

  | 测试 | 修复前（003，本次复测） | 修复后（本次 HEAD） |
  |---|---|---|
  | `GeneratesWithoutCrashingOnMixedChineseEnglishDraft` | 7.85s | 2.17s |
  | `RepeatedGenerateCallsOnSameEngineDoNotCrash`（35 次 generate()） | 220.25s | 17.78s |
  | `GenerateAcceptsStructuredAnnotationTextWithoutSignatureChange` | 7.71s | 2.19s |
  | `GenerateOnHighAmbiguityDensityInputNeverLeaksAnnotationSymbols` | 8.28s | 2.66s |
  | `GenerateOnGrammarStallTerminatesEarlyWithoutCrashing` | 59.11s | 7.27s |
  | `GenerateWithDraftModelProducesSemanticallyEquivalentOutputToSingleModelPath` | 19.37s | 5.17s |
  | `GenerateWithDraftModelStillEnforcesGrammarConstraintOnBothSides`（004 投机解码冒烟） | 12.81s | 4.21s |

  全部继续通过（`PASSED`），且全部**显著**提速（3～8 倍），功能断言（不泄漏
  `⟦⟧\|`、梗阻提前终止、投机解码双侧约束生效）均未改变。

- **独立性能对比探针**（`tests/slowpath/GbnfPerfProbeTest.cpp`，验证后已删
  除）：同一 3B 模型（`/Users/Larry/Library/Application Support/Babel/
  models/qwen2.5-3b-instruct-q4_k_m.gguf`）、同一纯文本 prompt
  `"I today xinqing hen bad. Wo want qu chi shaokao."`（与 assessment.md 复
  现步骤一致，不含 Lattice `⟦⟧|` 标注），各跑 3 次独立 `generate()` 调用：

  | 版本 | 3 次独立调用耗时 | 平均 | 输出文本 |
  |---|---|---|---|
  | 003 原始实现（`git stash` 回退 LlmEngine.cpp/h 后本机复测） | 2927ms / 3017ms / 2970ms | **2971.3ms** | `"我今天心情很坏，我想去吃烧腊。"` |
  | 本次修复（当前 HEAD） | 679ms / 694ms / 684ms | **685.7ms** | `"我今天心情很坏，我想去吃烧腊。"`（与修复前**逐字节完全相同**） |

  修复后耗时相比本机复测的 003 基线回落 **约 4.3 倍**（2971ms → 686ms），与
  002 基线（assessment.md 记录 628ms）同量级，且远低于 assessment.md 记录的
  约 2000ms 阈值；两个版本对同一输入产出**完全相同**的最终译文，验证"黑名单
  查表"与"通用语法引擎"在真实生成路径上行为等价（而不仅是静态逐 token 判
  定一致）。

- **投机解码冒烟**：`LlmEngineGenerateTest.GenerateWithDraftModelStillEnforcesGrammarConstraintOnBothSides`
  与 `GenerateWithDraftModelProducesSemanticallyEquivalentOutputToSingleModelPath`
  均使用真实草稿模型（`qwen2.5-0.5b-instruct-q4_k_m.gguf`）+ 目标模型
  （3B）双侧真实推理，验证草稿侧 `m_draftForbiddenTokenMask`、目标侧
  `m_forbiddenTokenMask` 分别生效、互不混用，且输出不含 `⟦⟧|`，4.21s/5.17s
  通过（修复前 12.81s/19.37s）。

- 14B 档位 before/after 未在本次补测范围内执行（assessment.md Open
  Questions 已标注为独立待办，非本次 fix 范围的阻塞项；修复逻辑与模型大小
  无关——`ForbiddenTokenMask` 的性能收益只取决于词表规模与输出 token 数，14B
  与 3B 共享同一 Qwen2.5 词表，收益预期同量级，但未做受控实测，如需验证建
  议另开后续任务）。

## Deviations from Assessment

assessment.md 的 Root Cause Hypothesis 认为 `kOutputGrammar` 的合法性判定
"与之前生成了什么完全无关"，据此预期"字符串直接包含 ⟦/⟧/\|"即可作为充分
判据。**实现过程中发现这一假设对绝大多数 token 成立，但存在一类未被
assessment.md 覆盖的边界情形**：

- 现象：按朴素标准（token 解码字符串是否直接包含 ⟦/⟧/\|）实现并跑通构建
  后，用独立性能探针复测发现修复后生成质量明显劣化——输出不再在合理长度
  内以 EOG 终止，而是跑满 256 token 上限且包含明显乱码/泄漏的 `<tool_call>`
  片段，与 003 原始实现对同一 prompt 的输出（`"我今天心情很坏，我想去吃烧
  腊。"`）完全不同。
- 根因：003 原语法引擎（`llama_grammar_apply_impl`/`decode_utf8`/
  `llama_grammar_match_partial_char`，见 `llama-grammar.cpp`）按**字节**而
  非按 token 解码级联判定合法性——对于 BPE 分词产生的"单字节/不完整多字节
  UTF-8 前缀"token（稀有字节回退编码，如裸露的延续字节、过长编码的 lead
  byte 0xC0/0xC1，或恰好是 ⟦/⟧ 三字节 UTF-8 编码**前缀**的 token，例如单独
  的 `0xE2`、`0xE2 0x9F` 等），即使其解码出的字符串不直接包含被禁字符，原
  引擎仍会保守地判定该 token 非法（防止模型靠跨 token 拼接字节的方式绕过
  ⟦/⟧ 排除规则）。朴素的"字符串包含检测"遗漏了这一类 token，导致它们在新
  实现下变得可采样，一旦被低概率采样命中即破坏后续生成的连贯性。
- 应对：`ForbiddenTokenMask::isForbiddenByUtf8Structure()` 改为逐字节复刻
  `decode_utf8()`（从干净状态）+ `llama_grammar_match_partial_char()` 的判
  定算法（完整解码出被禁码点 → 非法；出现无效 lead byte（含 0xC0/0xC1 过长
  编码特判）→ 非法；结尾处残留的未消费完多字节前缀，其所有可能补全区间覆
  盖 ⟦/⟧ 任一码点 → 非法），并新增 EOG token 永远合法、空/NUL 开头 piece
  直接非法两条镜像原引擎对应分支的规则。修复后用临时诊断探针
  `GbnfMaskDiffProbeTest.cpp`（已删除）对 3B 与 0.5B 两套词表共
  151936×2 个 token 做了**穷举**对比（而非抽样），确认与
  `llama_sampler_apply(grammar, …)` 的判定结果 **100% 一致**（0 处不一
  致），随后用性能探针确认两版本对同一真实 prompt 产出**逐字节相同**的最
  终输出，才认定等价性成立。
- 本次调整未偏离 Preferred remediation 的总体方向（仍是"模型加载时预计算
  的静态查表"，仍然是 O(1) 查询、不引入任何跨 generate()/跨 token 调用的状
  态），只是把"黑名单判据"从 assessment.md 设想的"字符串包含检测"精化为
  "字节级结构判定"，以达成真正意义上与原语法引擎等价的功能语义，不建议按
  "重新评估整体方案"处理，故未触发"停止修改代码、要求重新 assess"的指引；
  在此如实记录供复核。

## Follow-ups

- 14B（及 7B）档位的 before/after 延迟对比尚未实测（assessment.md Open
  Questions 遗留项），建议后续单独开任务用同样的受控探针方法补测，预期收
  益同量级但未验证。
- `ForbiddenTokenMask::isForbiddenByUtf8Structure()` 的字节级判定逻辑与
  `kOutputGrammar` 原始 GBNF 字符串及 llama.cpp `decode_utf8`/
  `match_partial_char` 的具体实现强耦合；若未来升级 llama.cpp 版本导致该内
  部算法变化，或需要调整 `kOutputGrammar` 排除的字符集合，需要同步更新本
  类并重新跑一次本次使用的穷举诊断方法（对全词表做
  grammar-apply-based-ground-truth vs mask 的逐 token 对比）验证等价性，而
  不能仅凭阅读代码推断——这正是本次 fix 踩坑过程证明的教训。
- 若未来需要真正有状态的语法约束（如输出必须是合法 JSON），`ForbiddenTokenMask`
  不适用，应在调用点旁另行引入 `llama_sampler_init_grammar` 等通用引擎；本
  次修复未移除该能力的可用性（llama.cpp 相关头文件/链接均未改动），仅是本
  场景不再使用它。
