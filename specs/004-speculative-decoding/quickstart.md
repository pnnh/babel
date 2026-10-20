# Quickstart: 投机解码（Speculative Decoding）性能优化 验证指南

**Spec**: [spec.md](./spec.md) ｜ **Plan**: [plan.md](./plan.md) ｜ **Data Model**: [data-model.md](./data-model.md) ｜ **Contracts**: [contracts/](./contracts/)

本指南用于在 `/speckit.implement` 完成本特性对应的 `tasks.md` 任务后，手工验证端到端行为是否满足 `spec.md` 的验收场景与成功标准。本文件延续 001/002/003 `quickstart.md` 的既有验证场景（核心闭环、手动粘贴降级、异常提示、Lattice 多候选消歧、模型热切换、防抖节奏、GBNF 语法约束）作为回归基线，本文件**仅新增**本特性引入的验证场景，**不重复**既有内容。

## 前置条件

- `research.md` §9 列出的两项本阶段新发现的事项（SessionController↔LlmEngine 架构划分、草稿-目标模型档位搭配映射具体取值）**已由用户显式决策并回填**至 `data-model.md`/`contracts/model-catalog-draft-pairing-internal-contract.md`：架构划分采纳方案 B（`LlmEngine` 内部封装双模型逻辑，对 `SessionController` 完全透明），搭配映射采纳方案①（Medium(7B)↔Qwen2.5-0.5B-Instruct、High(14B)↔Qwen2.5-1.5B-Instruct）；`/speckit.implement` MUST 据此最终决策完成实现，本指南中"具体搭配哪一个草稿模型"的步骤均已按该最终映射描述，不再是示例措辞。
- 已按 FR-004 候选规格，由用户手动下载 Qwen2.5-0.5B-Instruct（GGUF Q4_K_M）与/或 Qwen2.5-1.5B-Instruct（GGUF Q4_K_M）草稿模型文件，连同既有中配（Qwen2.5-7B-Instruct）/高配（Qwen2.5-14B-Instruct）目标模型文件，放置于约定本地模型目录（`ModelPaths::conventionalModelDirectory()`），文件名建议包含 `0.5b`/`1.5b` 关键字（如 `qwen2.5-0.5b-instruct-q4_k_m.gguf`/`qwen2.5-1.5b-instruct-q4_k_m.gguf`），以便被 `ModelCatalog::classifyDraftCandidateByFileName()` 正确识别。
- 构建与测试运行方式延续既有工作流（未因本特性改变构建入口）：

```bash
# macOS
cmake --preset macos
cmake --build --preset macos
ctest --preset macos --output-on-failure
```

---

## 验证场景一：中/高配已正确配置草稿模型时，生成延迟可测量降低且翻译质量不下降（User Story 1，对应 SC-001/SC-002/SC-003）

**目的**：验证 FR-001——投机解码路径相较既有单模型路径，整句生成延迟有可测量降低，且最终译文内容语义一致。

### 1.1 准备同一测试样例的两种路径对照

1. 准备一组覆盖多种输入场景的测试语料（至少涵盖：纯中文草稿、中英混杂草稿、含同音字歧义需 Lattice 消歧的草稿，延续 002 既有测试语料约定）。
2. 临时将约定模型目录中对应档位的草稿模型文件移出目录（或重命名使其不再被 `classifyDraftCandidateByFileName()` 识别），确保"不启用投机解码"路径下系统静默降级为既有单模型生成（FR-006）。
3. 对每条语料，在设置页面切换为中配模型，触发一次慢流生成，记录：
   - 整句生成延迟（`outputPreviewState` 从 `Generating` 变为 `Ready` 所经过的时间，可用既有日志时间戳或手动秒表计时）；
   - 最终 `outputPreviewText` 文本内容。
4. 将草稿模型文件放回约定目录，重复步骤 3（此时 FR-007 自动搭配生效，`speculativeDecodingNoticeText` 应变为空/正向提示，见场景二）。
5. 对比步骤 3（禁用）与步骤 4（启用）的延迟与文本：
   - **预期（SC-001）**：步骤 4 记录的延迟相较步骤 3 有可测量的降低（无需达到具体倍数，只需确认数值上更低）。
   - **预期（SC-003）**：步骤 3 与步骤 4 的 `outputPreviewText` 在语义上一致（用户确认上屏的内容不因投机解码引入而发生可感知的质量下降；允许因采样随机性产生的措辞级差异，但不允许出现语义错误、未消歧歧义字、或标注符号 `⟦`/`⟧`/`|` 泄漏）。

### 1.2 高配档位重复验证（SC-002）

6. 切换为高配模型，重复 1.1 步骤 2～5（使用高配对应的草稿模型文件）。
   - **预期（SC-002）**：与中配同理，高配档位下启用投机解码后的延迟相较禁用时有可测量的降低。

### 1.3 低配档位不受影响（Edge Cases，FR-009）

7. 切换回低配默认档位，重复一次生成。
   - **预期**：系统行为与 003 交付时完全一致（不触发任何草稿模型加载尝试、`speculativeDecodingNoticeText` 恒为空字符串，不展示任何投机解码相关提示）。

---

## 验证场景二：未配置草稿模型时设置页面正确提示（User Story 3，对应 SC-004）

**目的**：验证 FR-006——草稿模型缺失时静默降级 + 设置页面明确提示。

### 2.1 中配档位未配置草稿模型

1. 确保约定模型目录中**不存在**中配档位对应搭配的草稿模型文件——按最终映射（方案①），中配对应 Qwen2.5-0.5B-Instruct（如 `qwen2.5-0.5b-instruct-q4_k_m.gguf`）。
2. 切换为中配目标模型，进入设置页面查看模型信息展示区域。
   - **预期**：`speculativeDecodingNoticeText` 展示非空提示文案（如"未检测到草稿模型，投机解码未启用"，FR-006 原文示例），`recommendedDraftProfiles` 列表仍正常展示两档候选规格供用户了解下载方式（User Story 2）。
3. 返回主视图，输入一段草稿文本触发慢流生成。
   - **预期**：生成成功完成，产出与 003 既有单模型生成路径一致的翻译结果（FR-006：不因草稿模型缺失导致生成失败或应用崩溃）。

### 2.2 高配档位同理

4. 重复 2.1 步骤 1～3，目标模型档位换为高配。
   - **预期**：行为与 2.1 一致。

### 2.3 草稿模型文件损坏/格式不兼容（Edge Cases）

5. 在约定目录中放置一个文件名匹配草稿模型关键字、但内容损坏（非合法 GGUF 头）的文件。
6. 切换为中配或高配目标模型。
   - **预期**：系统行为与 2.1/2.2（未配置）一致——静默降级 + 提示，不得因草稿模型加载失败导致目标模型本身也无法完成生成（Edge Cases 第二条）。

### 2.4 配置成功后提示消失/变为正向提示

7. 放回有效的草稿模型文件，重新切换一次目标模型档位（触发 FR-007 自动重新匹配）。
   - **预期**：`speculativeDecodingNoticeText` 变为空字符串或正向提示文案（具体措辞见 `contracts/qml-cpp-session-interface-increment.md` 三，留待 `/speckit.tasks` 最终确定）。

---

## 验证场景三：切换目标模型档位后自动重新匹配草稿模型（Edge Cases 第一条）

1. 在中配档位下已正确配置对应草稿模型（场景一已验证投机解码生效）。
2. 在设置页面将目标模型切换至高配档位，且约定目录中**尚未**放置高配档位对应搭配的草稿模型文件。
   - **预期**：切换成功完成（`modelSwitchCompleted(true, ...)`），`speculativeDecodingNoticeText` 自动变为非空提示（按 FR-006 静默降级行为，即使此前中配下投机解码曾经生效，高配下因草稿模型未配置也应正确降级，不得错误地沿用中配阶段的"已启用"状态）。
3. 放置高配对应的草稿模型文件后，不切换模型，直接触发一次生成。
   - **预期**：此时是否立即生效取决于是否重新执行了 `switchModel()`/应用重启；若实现中"检测时机"选择为仅在 `switchModel()`/启动时重新计算（`data-model.md` 三"生命周期"），则本步骤预期提示仍为未启用，需用户重新触发一次模型切换（如选中同一文件再次调用 `switchModel()`）才会重新检测到草稿模型已就绪——具体检测时机属内部实现细节，`/speckit.tasks` 阶段确定后，本场景以最终实现行为为准进行验证。

---

## 实测记录（`/speckit.implement` 完成后填写，本阶段不预先编造数值）

**实测环境**：macOS（Apple Silicon，Metal 后端），本机已放置真实权重文件：Qwen2.5-3B-Instruct（低配，完整）、Qwen2.5-14B-Instruct（高配，完整）、Qwen2.5-0.5B-Instruct（草稿，完整）、Qwen2.5-1.5B-Instruct（草稿，完整）。**Qwen2.5-7B-Instruct（中配）用户仍在下载中，实测时文件仅 ~5.9MB（远未达到完整 Q4_K_M 体积），无法加载，故中配档位的 SC-001 实测**暂缺，需用户完成下载后补测**（不影响本特性其余结论）。

- **正确性验证（FR-001"语义等价"）**：使用真实 Qwen2.5-14B-Instruct（高配）+ Qwen2.5-1.5B-Instruct（高配对应草稿）组合，对同一输入分别执行"仅目标模型"与"目标+草稿"两条路径，两次返回文本逐字完全一致（如"我今天不是非常忙，很高兴能和你们一起讨论这个项目的技术方案，我们需要在下午的会议中确认所有项目的进度和下一步的计划。"）。该验证同时覆盖低配（3B）+ Qwen2.5-0.5B-Instruct 组合（`tests/slowpath/LlmEngineGenerateTest.cpp` 自动化用例 `GenerateWithDraftModelProducesSemanticallyEquivalentOutputToSingleModelPath`/`GenerateWithDraftModelStillEnforcesGrammarConstraintOnBothSides`，均通过）。
  - **实现修正记录**：实测初期发现投机解码路径产出"词汇本身合法但顺序错乱/重复"的异常文本（如"我不是不太今天我很能你们一起项目我们需要下午你们讨论项目的和的下一步计划确认所有的进度下一步"），定位为批量验证阶段的 logits 索引偏移错误——`llama_decode()` 对批次内位置 i（已输入草稿 token d_i）产出的 logits 预测的是"紧跟在 d_0..d_i 之后"的下一个 token，而非"d_i 本身是否合理"；验证 d_0 须使用本轮验证之前（上一轮末尾或首次提示词解码）已产出的 logits，验证 d_i（i>=1）须使用批次内位置 i-1 的输出 logits。修复该索引偏移后，复测确认投机解码路径与单模型路径产出完全一致的文本，问题已解决（`babel/src/slowpath/LlmEngine.cpp` `generate()` 投机解码分支，相关说明已写入代码注释）。
- **SC-001/SC-002 实测延迟对比数值（高配档位，Qwen2.5-14B-Instruct + Qwen2.5-1.5B-Instruct，`kSpeculativeDraftWindow = 4`）**：
  - 短输入（约 7 个输出 token）：单模型 3.48s；投机解码 6.20s（**慢约 1.8 倍**，非预期方向）。
  - 长输入（约 58 字输出）：单模型 11.4s～11.7s；投机解码 28.3s～29.6s（**慢约 2.4～2.6 倍**，非预期方向）。
  - **结论（诚实记录，不回避不利结果）**：在本机实测环境下，当前实现（固定 `K=4`、Qwen2.5-14B-Instruct 搭配 Qwen2.5-1.5B-Instruct）**未能达成 SC-001/SC-002 期望的"可测量降低"，反而显著变慢**。初步诊断（`spdlog::debug` 诊断日志复核多轮验证结果，详见实现过程记录）显示：每轮草稿窗口内验证不通过（草稿猜测与目标模型真实选择不一致）的频率较高，导致草稿模型生成的候选 token 中有相当比例被丢弃，而每轮"草稿生成 + 批量验证 + 失败回滚 + 单 token 替换解码"引入的调用次数（尤其是 `llama_decode()` 调用次数）显著多于单模型路径逐 token 生成，抵消了批量验证本应带来的收益；加之本机 Apple Silicon + Metal 后端下 14B 模型单 token 解码本身延迟已较低（基线不高），进一步压缩了投机解码的收益空间。**该发现不构成本特性实现错误**（FR-001"语义等价"正确性已独立确认），而是 `research.md` §9/§4 本就要求"留待实测、不预先断言"的开放问题的真实（且不利）答案，建议后续迭代可尝试更小的 `K`、更换草稿模型搭配、或针对本应用"短句翻译"场景的特性重新评估投机解码是否值得默认启用。
- **验证通过率（草稿 token 被目标模型接受的比例）**：诊断观察到的若干轮次样例为 `k=4` 窗口下接受数 `j∈{0,1,2}`（即每轮平均接受约 0.75～2 个草稿 token，约 20%～50% 接受率），与上述延迟结论相互印证（接受率不够高，不足以覆盖多次调用的固定开销）。
- **内存/显存叠加占用实测结果（高配档位，`research.md` §4 方法论，`ps -o rss= -p <pid>` 读数）**：
  - 基线（仅加载 Qwen2.5-14B-Instruct，不启用投机解码，已执行一次生成使 KV cache 填充）：RSS ≈ **4517 MB**（约 4.4GB）。
  - 叠加（同时加载 Qwen2.5-14B-Instruct + Qwen2.5-1.5B-Instruct，投机解码路径，已执行一次生成）：RSS ≈ **10137 MB**（约 9.9GB）。
  - **增量 ≈ 5621 MB（约 5.5GB）**——**显著高于**预期量级（Qwen2.5-1.5B-Instruct Q4_K_M 文件本身仅约 1.1GB，`research.md` §4 步骤 3 原预期增量应"与草稿模型 `approxSizeDescription` 量级相近"）。诚实记录该偏差，不回避：初步推测可能与 Apple Silicon + Metal 后端下每个独立 `llama_context` 均会分配自己的计算图 compute buffer（001 实施记录曾观察到 14B 级别模型 compute buffer 可达数百 MB 量级）、以及投机解码路径下目标模型 KV cache 配置（`n_ctx`）与草稿模型 KV cache 独立分配有关，具体根因**未**在本次实测中进一步拆解定位，留待后续调优迭代确认；该发现进一步印证"当前实现在本机既未提速、又显著增加内存占用"的双重不利结论。
- **中配档位（Qwen2.5-7B-Instruct + Qwen2.5-0.5B-Instruct）SC-001 实测**：**阻塞**——用户两次尝试下载 Qwen2.5-7B-Instruct 均未成功（文件仅 ~5.9MB，远未达到完整 Q4_K_M 体积约 4.4~4.7GB，疑似下载中断），需用户确认下载完整后使用 `tests/session/SessionControllerTest.cpp` 的 `SwitchModelToMediumOrHighTierModelLengthensDebounceWindow`（003 既有测试，当前因此被 `GTEST_SKIP()`）与本场景一的步骤补测。

### 事后修订：默认关闭为实验性开关

基于以上延迟不达标的实测结果，用户决策：功能代码保留，但**默认不启用**，改为设置页面可见的实验性开关（默认关闭），由用户自行决定是否开启尝试。本手册中"验证场景一"的全部步骤 MUST 在执行前先于设置页面开启"投机解码加速（实验性，默认关闭）"开关，否则无论草稿模型文件是否就位，均会走既有单模型生成路径（提示文案会显示"投机解码为实验性功能，默认未启用"而非"未检测到草稿模型"）。
