# Research: AI 辅助双语浮窗转换器（MVP）

**Spec**: [spec.md](./spec.md) ｜ **Plan**: [plan.md](./plan.md)

## 阅读说明

本文件按"快流纠错模块"“慢流/LLM 推理模块”“全局热键+浮窗 UI 模块”“剪贴板写入与模拟粘贴注入模块”四个设计分区组织，另附本地存储与测试框架两个支撑性调研条目。

- 标注 **Decision** 的条目：选型已由 `spec.md` 的 FR/SC 或用户本轮 Technical Context 显式确认，本文件仅记录理由与被排除的替代方案，不构成新决策。
- 标注 **`[NEEDS CLARIFICATION]`** 的条目：仅列出候选方案与权衡（Recommendation 仅为建议，不是批准），最终选型留待 `/speckit.clarify` 或用户显式委派，`/speckit.plan` 本阶段 MUST NOT 自行拍板（宪章原则 II、判定程序 (d)）。
- 标注 **Decision（用户委派，Agent 采纳 Recommendation）** 的条目：原为 `[NEEDS CLARIFICATION]`，用户已于后续轮次明确委派——"全部委托 Agent 按 research.md 中各自的 Recommendation 选项执行，不需要用户逐项选择"。Agent 据此将对应 Recommendation（§2/§3/§4.2 有明文 Recommendation 句；§6/§7 本文件原未录入明文 Recommendation，Agent 在委派范围内按既有证据与约束自主补全判断并同样标注为本类）采纳为最终 Decision，已不再是候选或建议。该委派本身不构成宪章原则 II/III 违规：决策仍由用户显式授权（委派本身即显式指令），Agent 仅执行既定委派范围内的选型落地，未超出委派边界自行发明新需求。

---

## 0. 委派决策总览（本轮新增）

以下 5 项原 `[NEEDS CLARIFICATION]` 事项，已按用户显式委派、由 Agent 采纳 `research.md` 对应 Recommendation（或在 Recommendation 缺失时按既有证据自主判断）定稿为最终 Decision，详见各自小节：

| # | 事项 | 对应小节 | 最终 Decision（摘要） | Recommendation 来源 |
|---|------|----------|------------------------|----------------------|
| 1 | 快流编辑距离/拼音 Trie：自研 vs 开源移植 | §2 | 方案 1：仓库内自研（编辑距离 + 拼音/简拼 Trie + QWERTY 邻近权重，全部手写 C++） | §2 明文 Recommendation 的"追求许可证最简单"分支 |
| 2 | 全局热键绑定库选型 | §3 | 方案 1：分平台自研薄封装 | §3 明文 Recommendation 的"保留完全控制权"分支 |
| 3 | 模拟粘贴注入库选型 | §4.2 | 方案 1：分平台自研薄封装 | §4.2 明文 Recommendation 的"保持完全可控"分支 |
| 4 | C++ 单元测试框架选型 | §6 | GoogleTest（经 vcpkg 官方 port `gtest` 引入） | §6 原未录入明文 Recommendation，Agent 按委派自主补全 |
| 5 | 本地 LLM 模型文件供给/部署方式 | §7 | 用户手动下载后放置于约定本地目录（不随安装包分发、应用内不提供自动下载入口） | §7 原未录入候选/Recommendation，Agent 按委派自主补全 |

---

## 1. 慢流模块：本地 LLM 推理引擎

**Decision**：`llama.cpp` 作为推理引擎，加载 **Qwen2.5-3B-Instruct** 的 **GGUF Q4_K_M** 量化权重。

**Rationale**：
- spec FR-013 已明确模型与量化精度；GGUF 是 `llama.cpp`/`ggml` 生态的原生权重格式，业界事实标准运行时即为 `llama.cpp`（或其绑定），不存在无需转换即可直接消费该格式的替代主流运行时。
- 仓库内已存在对应的 vcpkg overlay port `vcpkg/ports/llama-cpp`（`vcpkg.json` 尚未引用），具备 `metal`（Apple GPU 加速）、`download`（URL 下载模型）、`tools`（CLI 工具）三个可选 feature。
- 用户本轮 Technical Context 已将"使用 llama.cpp"列为既定事实，非本阶段新决策。

**Feature 启用建议**（内部实现细节，按宪章原则 IV 自主决定，单行记录）：
- 仅链接核心推理库，不启用 `tools`（CLI 工具对嵌入式集成无必要）。
- macOS 启用 `metal`，以利用 Apple Silicon GPU 加速辅助达成 SC-003（首批内容 <300ms、整句 <800ms）；该 feature 的硬件可用性已由 `CMakePresets.json` 的 `macos` preset（`arm64-osx` 三元组）佐证。
- 不启用 `download`：spec 未要求应用具备"从 URL 自动下载模型"的能力，引入该 feature 会额外引入 `curl` 依赖且扩大攻击面/网络行为，超出 FR-013 字面授权范围，若未来需要该能力应先补充 FR 再启用。

**Alternatives considered（已排除）**：
- ONNX Runtime GenAI / 其他通用推理运行时：需先将 GGUF 转换或改用其原生格式，偏离 FR-013 已确认的"GGUF Q4_K_M"字面约束，且会引入额外转换与运行时依赖，无额外收益。
- 直接裸用 `ggml` 而不经 `llama.cpp` 封装层：需要自行实现 Qwen2.5 的分词、采样、KV-cache 管理等已由 `llama.cpp` 提供的能力，收益不明显、维护成本更高。

---

## 2. 快流模块：本地词典/拼音纠错实现

**背景**：`docs/IME.md`（产品愿景文档）§3.2/§5.1 中提到过"SymSpell (C++)"+"拼音音节 Trie 树"作为产品愿景侧的既有技术描述，但 `spec.md` 的 FR-003 仅规定了**功能目标与延迟指标**（本地词典/拼音简拼匹配 + 物理键位邻近编辑距离，<15ms），**未**在 `spec.md` 中把具体实现库钦定为强制依赖。因此本模块的"自研 vs 引入开源库"仍属未决项（判定程序 (d) 新增依赖）。

**候选方案**：

| # | 方案 | 优点 | 缺点 |
|---|------|------|------|
| 1 | 仓库内自研：编辑距离前缀索引（SymSpell 思路）+ 拼音/简拼 Trie + QWERTY 邻近权重表，全部手写 C++ | 无许可证顾虑；可完全贴合中英拼音混排场景定制；不引入第三方源码到构建系统 | 开发与正确性验证成本更高；需自行覆盖性能基准（<15ms） |
| 2 | 引入开源 C++ 移植 `erhanbaris/SymSpellPlusPlus`（基于 SymSpell v6.1 思路，LGPL-3.0，可选 `sparsepp` 加速） | 编辑距离算法已被广泛验证；性能优化（`sparsepp`）现成可用 | LGPL-3.0 对静态链接桌面应用的合规边界需法务/发布策略评估；无 vcpkg 包，需以 overlay/vendor 方式引入；仅覆盖纯文本编辑距离，拼音/简拼 Trie 与 QWERTY 邻近层仍需自行叠加开发 |
| 3 | 引入开源 C++ 移植 `tcamin/SymSpell-cpp`（MIT License） | 许可证宽松，法务顾虑小；接入成本低 | 项目活跃度/维护状态需在选型时进一步核实；同样仅覆盖编辑距离部分，拼音/简拼层仍需自研叠加 |

**Recommendation（非批准，原文）**：若法务/发布流程能接受 LGPL-3.0 静态链接，方案 2 可降低编辑距离核心算法的实现风险；若追求许可证最简单，方案 1 或"方案 3 + 自研拼音层"更合适。无论选择何种编辑距离基础库，**拼音/简拼 Trie 词典与 QWERTY 键位邻近加权均为本特性独有需求，需自行实现**，这部分工作量在三个方案下相近。

**Decision（用户委派，Agent 采纳 Recommendation）**：采纳方案 1——**仓库内自研**：编辑距离前缀索引（SymSpell 思路）+ 拼音/简拼 Trie + QWERTY 邻近权重表，全部手写 C++，不引入第三方源码到构建系统。

**采纳理由**：
- 用户已明确委派"全部委托 Agent 按 Recommendation 执行"；本项 Recommendation 为条件分支，Agent 在委派范围内选择"追求许可证最简单"分支（方案 1），依据：仓库现有依赖（`spdlog` MIT、`boost-filesystem` BSL-1.0、`sqlite3` Public Domain，见 `vcpkg.json`）均为许可证宽松依赖，无 LGPL-3.0 静态链接先例，引入方案 2 需额外进行法务/发布策略评估，超出本次委派的选型范围；方案 1/3 均需自行实现拼音/简拼 Trie 层，工作量与方案 2 相近（Recommendation 原文已指出），故不因引入第三方库而获得明显净收益。
- 同时排除方案 3（`tcamin/SymSpell-cpp`，MIT）：虽许可证同样宽松，但该库无 vcpkg 官方 port，仍需 overlay/vendor 方式引入且需额外核实其维护活跃度，综合成本与方案 1 自研相近，优先选择零新增第三方依赖的方案 1，与 §3/§4.2 的"自研"选型风格保持一致，降低"部分自研+部分第三方"的混合维护复杂度。

**标记**：~~`[NEEDS CLARIFICATION]`~~ → **已解决（用户委派，Agent 采纳 Recommendation，见上）**；`data-model.md` §7（`DictionaryEntry`/`PinyinTrieNode`）字段定义据此选型保持不变，供自研实现直接落地。

---

## 3. 全局热键+浮窗 UI 模块：热键绑定实现

**已确认事实**（来自 spec FR-001 与用户本轮 Technical Context，非本阶段决策）：
- 默认组合键：`Ctrl+Alt+Space`（macOS 为 `Control+Option+Space`）。
- 各平台底层机制：Windows `RegisterHotKey`；macOS Carbon `RegisterEventHotKey` 或 Cocoa 全局事件监听；Linux X11 经 `libxcb` 或 `libX11` 的 `XGrabKey`。
- Linux Wayland 下不保证支持，按 spec 统一降级路径处理（FR-009/FR-012），非本模块需要"解决"的问题。

**待决项**：是否在上述已确定的平台机制之上，自研一层跨平台薄封装，还是直接引入现成的跨平台热键库。

**候选方案**：

| # | 方案 | 优点 | 缺点 |
|---|------|------|------|
| 1 | 分平台自研薄封装（直接调用 spec 已指定的原生 API） | 零新增第三方依赖；完全可控，便于按 Wayland 场景定制降级检测逻辑；与 `src/hotkey/` 目录按平台拆分的既定结构天然契合 | 需自行维护三套平台代码与边界情况（修饰键归一化、重复注册检测、与系统既有热键冲突的探测） |
| 2 | 引入 `JaderoChan/global_hotkey`（MIT，C++，无第三方依赖，覆盖 Windows `RegisterHotKey`/macOS Carbon/Linux X11 `XGrabKey`） | 开箱即用，减少自研与跨平台调试成本；机制与 spec 已确认的原生 API 一致 | 无 vcpkg 包，需 overlay/vendor 方式引入；需评估其事件循环模型与 Qt 事件循环的集成方式；外部维护节奏需核实 |
| 3 | 引入 `Skycoder42/QHotkey`（基于 Qt） | 与现有 Qt6 技术栈天然契合，API 简单，集成摩擦小 | 无 vcpkg 包；对 Wayland 无特殊处理（与自研方案一致，仍需同样的降级路径） |

**Recommendation（非批准，原文）**：若希望减少跨平台原生 API 的自研与调试工作量，方案 2/3 更省力；若希望对热键注册失败、冲突探测等边界行为保留完全控制权（便于未来扩展，如自定义热键），方案 1 更合适。

**Decision（用户委派，Agent 采纳 Recommendation）**：采纳方案 1——**分平台自研薄封装**，直接调用 spec 已指定的原生 API（Windows `RegisterHotKey`、macOS Carbon `RegisterEventHotKey` 或 Cocoa 全局事件监听、Linux `libxcb`/`libX11` 的 `XGrabKey`）。

**采纳理由**：
- Agent 在 Recommendation 的条件分支中选择"保留完全控制权"分支：方案 2（`JaderoChan/global_hotkey`）、方案 3（`Skycoder42/QHotkey`）均无 vcpkg 官方 port，需以 overlay/vendor 方式引入，其集成成本并不显著低于方案 1 的自研成本（仍需评估事件循环集成、维护节奏等不确定因素）；而方案 1 零新增第三方依赖，且能完全掌控热键注册失败、Wayland 降级检测等边界行为，便于与 FR-009/FR-012 的统一降级路径精确对齐。
- 与 `plan.md` Project Structure 中已预留的 `src/hotkey/`（按 `*_win.cpp`/`*_mac.mm`/`*_x11.cpp` 平台拆分）目录结构天然契合，无需因引入第三方库而调整既定目录规划。
- 与 §2、§4.2 的自研选型保持一致风格，避免"部分自研+部分第三方"的混合维护模式。

**已知边界场景（来自 spec Edge Cases，未转化为 FR，本阶段不新增需求）**：
- "全局热键与系统已有其他快捷键冲突，用户如何察觉并处理"——spec 仅在 Edge Cases 中提出问题，未定义具体行为规则；本计划不代为设计该行为，留待该问题被转化为正式 FR 后再设计。方案 1 的"完全可控"特性为该问题未来被转化为 FR 后的扩展预留了空间，但本身不构成提前实现该行为的理由。

**标记**：~~`[NEEDS CLARIFICATION]`~~ → **已解决（用户委派，Agent 采纳 Recommendation，见上）**；`contracts/global-hotkey-contract.md` 定义的行为契约与该选型完全兼容，无需修改契约文本本身（契约定义本即与具体库选型解耦）。

---

## 4. 剪贴板写入与模拟粘贴注入模块

### 4.1 剪贴板写入

**Decision**：使用 Qt6 自带 `QClipboard`（`Qt6::Gui`，既有依赖，无需新增第三方库）。

**Rationale**：FR-007 仅要求"将最终中文文本写入系统剪贴板"，Qt 已提供跨平台封装；属于实现已授权需求（FR-007）所必需的内部步骤，不改变外部行为，按宪章原则 IV 自主决定并单行记录，无需用户确认。

### 4.2 模拟粘贴注入

**已确认事实**（来自 spec FR-008/FR-010，非本阶段决策）：各平台机制为 Windows `SendInput`、macOS `CGEventPost`（需 Accessibility 权限）、Linux X11 `XTest` 扩展；Wayland 下预期失败，统一走"已复制到剪贴板，请手动粘贴"降级提示（FR-009/FR-012）。

**候选方案**：

| # | 方案 | 优点 | 缺点 |
|---|------|------|------|
| 1 | 分平台自研薄封装（直接调用 spec 已指定的原生 API） | 零新增第三方依赖；可精确控制"注入失败"判定逻辑，便于与 FR-012 的统一提示规则对齐 | 需自行维护三套平台代码，尤其 macOS Accessibility 权限状态检测、X11 `XTest` 可用性探测 |
| 2 | 引入 `antelle/keyboard-auto-type`（MIT，C++，覆盖 Windows `SendInput`/macOS `CGEventPost`/Linux X11 `XTest`，明确不支持 Wayland） | 已封装跨平台按键模拟与 Unicode 文本输入，机制与 spec 已确认的原生 API 完全一致；由 KeeWeb 项目实际使用、相对成熟 | 无 vcpkg 包，需 overlay/vendor 方式引入；其"注入失败"判定粒度是否能精确映射到 FR-012 所需的统一降级提示，需要在选型时验证 |
| 3 | 仅 Linux 侧引入 `libxdo`（`xdotool` 底层库，基于 XTest），Windows/macOS 仍自研 | Linux X11 侧可复用成熟实现，降低该平台自研成本 | 引入"部分自研+部分第三方"的混合维护模式，三平台代码风格不统一；`libxdo` 同样无 vcpkg 包 |

**Recommendation（非批准，原文）**：若优先降低多平台自研与调试成本，方案 2 更省力；若优先保持对"失败判定→统一提示"链路的完全可控，方案 1 更直接。

**Decision（用户委派，Agent 采纳 Recommendation）**：采纳方案 1——**分平台自研薄封装**，直接调用 spec 已指定的原生 API（Windows `SendInput`、macOS `CGEventPost`、Linux X11 `XTest` 扩展）。

**采纳理由**：
- Agent 在 Recommendation 的条件分支中选择"保持完全可控"分支：FR-012 要求无论 `InjectionAttemptResult.outcome` 取何值（`UnsupportedEnvironment`/`PermissionDenied`/`OtherFailure`）均统一展示同一条提示文案，这要求对"注入是否成功、因何失败"的判定逻辑有精确控制；方案 2（`antelle/keyboard-auto-type`）的失败判定粒度是否能精确映射到该统一降级要求需额外验证，方案 1 自研可从实现起点就确保判定粒度与 `contracts/injection-outcome-contract.md` 完全对齐，不存在第三方库语义落差风险。
- 排除方案 3（混合模式：仅 Linux 引入 `libxdo`，Windows/macOS 自研）：Recommendation 原文及候选表均已指出该方案会造成"部分自研+部分第三方"的风格不统一，且 `libxdo` 同样无 vcpkg 包，综合成本与收益不优于统一自研。
- 与 §2、§3 的自研选型保持一致风格，`plan.md` Project Structure 中已预留的 `src/inject/`（`PasteInjector_win.cpp`/`_mac.mm`/`_x11.cpp`）目录结构无需因引入第三方库而调整。

**标记**：~~`[NEEDS CLARIFICATION]`~~ → **已解决（用户委派，Agent 采纳 Recommendation，见上）**；`contracts/injection-outcome-contract.md` 定义的抽象契约（调用顺序、`outcome` 枚举、统一提示文案强约束）与该选型完全兼容，契约文本本身无需修改。

---

## 5. 本地持久化

**Decision**：复用既有 `sqlite3`（vcpkg 依赖 `unofficial-sqlite3`）存储快流词典/拼音 Trie 的本地索引数据；不引入新的存储技术（如 LevelDB、自定义二进制格式等）。

**Rationale**：用户本轮 Technical Context 明确要求"持续使用 sqlite3 ... 优先复用现有依赖，避免引入新存储技术"；具体表结构见 `data-model.md`。

---

## 6. 单元测试框架

**现状**：根 `CMakeLists.txt` 已调用 `enable_testing()`，但仓库目前没有任何测试目标，`vcpkg.json` 未声明 `gtest`/`catch2`/`doctest` 等测试依赖。

**判定**：引入任一测试框架都构成判定程序 (d)（新增依赖）命中项，按宪章原则 II MUST NOT 由 `/speckit.plan` 自行选定。

**候选（原文，仅供后续 `/speckit.clarify` 参考，不在本阶段比较优劣）**：GoogleTest、Catch2、doctest；或 MVP 阶段暂不引入独立测试框架、仅以 `quickstart.md` 中的手工验收场景把关（该选项本身也是一项需要用户确认的产品/工程决策，而非默认项）。

**Recommendation 说明**：本节原文**未**录入明文 Recommendation（仅罗列候选，未排序取舍）。用户本轮已明确委派"全部委托 Agent 按 Recommendation 选项执行"；在本节无明文 Recommendation 可直接采纳的情况下，Agent 在委派范围内按既有候选与项目既有证据自主补全判断，而非超出委派范围擅自新增需求。

**Decision（用户委派，Agent 自主补全判断）**：采纳 **GoogleTest**，经 vcpkg 官方 port `gtest` 引入（非 overlay/vendor），MVP 阶段即启用，并通过 CMake 的 `gtest_discover_tests` 与既有 `enable_testing()`/CTest 集成。

**采纳理由**：
- 候选的三个框架（GoogleTest/Catch2/doctest）均为业界成熟方案，均有 vcpkg 官方 port，集成摩擦相近；GoogleTest 是 C++ 生态中使用面最广、文档与社区资源最完善的单元测试框架，内置 mock（gMock）能力可覆盖本特性中热键/注入等平台相关模块的接口桩测试需求，降低后续按 `src/hotkey/`、`src/inject/` 等模块拆分测试目标时的选型不确定性。
- 排除"MVP 阶段暂不引入独立测试框架"选项：该选项本身被候选说明明确标注为"也是一项需要用户确认的产品/工程决策"，与用户本轮委派的范围（"按 Recommendation 选型执行"，隐含前提是存在一个被采纳的测试框架）不符；且根 `CMakeLists.txt` 已调用 `enable_testing()`，表明仓库已有引入自动化测试的既定意图，不采纳"不引入框架"更贴合这一既有信号。

**标记**：~~`[NEEDS CLARIFICATION]`~~ → **已解决（用户委派，Agent 自主补全判断，见上）**。

---

## 7. 本地 LLM 模型文件的供给/部署方式

**现状**：FR-011 规定"当本地大语言模型文件缺失、损坏或无法加载时"系统必须给出可理解提示且不崩溃，这隐含了模型文件**可能缺失**这一前提，但 spec 全文未定义模型文件应如何被首次获取/部署（例如：随安装包一同打包、要求用户手动下载后指定路径、首次运行时引导下载等）。

**判定**：模型文件的供给/部署方式属于判定程序 (d)（部署方式变更）命中项，涉及安装包体积、是否需要网络下载（可能与"本地优先/离线运行"的 Assumption 产生张力）等外部可观察的运维行为，MUST NOT 由本计划代为决定。

**Recommendation 说明**：本节原文**未**录入任何候选方案表或 Recommendation（仅记录问题本身，待 `/speckit.clarify`）。用户本轮已明确委派"全部委托 Agent 按 Recommendation 执行"；本节无明文 Recommendation 可供采纳，Agent 在委派范围内，依据 spec 既有条款与 Assumptions 的字面约束自主补全判断，不超出委派范围新增产品语义。

**候选方案（Agent 为完成委派而补充列出）**：

| # | 方案 | 优点 | 缺点 |
|---|------|------|------|
| 1 | 随安装包一同打包分发 | 开箱即用，无需用户额外操作 | GGUF Q4_K_M 量化后的 Qwen2.5-3B-Instruct 权重通常仍有 GB 级体积，显著增大安装包；再分发该模型权重涉及额外许可证评估，超出本次委派可自主决定的范围 |
| 2 | 用户手动下载模型文件后，放置于应用约定的本地目录 | 安装包体积不受影响；不涉及应用内再分发模型权重的许可证问题；与 FR-011"模型文件可能缺失"的预期前提直接吻合 | 用户首次使用前需额外手动操作；应用需在约定路径处检测文件是否存在并给出清晰指引 |
| 3 | 应用首次运行时引导下载 | 用户体验上比方案 2 更"自动化" | 直接与 Assumptions 中"快流、慢流 MUST 全程本地离线运行，不依赖网络连接"产生张力（research.md 原文已指出）；需引入下载相关依赖（如 `curl`），与 §1 中"不启用 `download` feature"的既有内部决策矛盾 |

**Decision（用户委派，Agent 自主补全判断）**：采纳方案 2——**用户手动下载模型文件后，放置于应用约定的本地目录**（不随安装包分发、应用内不提供自动下载入口）。

**采纳理由**：
- FR-011 将"模型文件缺失、损坏或无法加载"作为需要被友好提示、不崩溃的**预期可能状态**来规定，而非仅作为安装损坏等异常态处理，这一字面表述本身即暗示 spec 假设模型文件并非强制随应用一同存在，与方案 2 的前提吻合度最高。
- 方案 1（随包打包）涉及 GB 级安装包体积增长，且需评估 Qwen2.5 模型权重的再分发许可条款，均超出本次委派授权 Agent 自主决定的范围（判定程序 (d)/(e) 命中，这类涉及许可证再分发与显著资源成本的决策需更高层级确认，但鉴于用户已要求"全部委托、不需逐项选择"，Agent 选择风险最低、与既有约束无冲突的方案，而非选择涉及额外许可证评估的方案 1）。
- 方案 3（首次运行引导下载）与 Assumptions 的"全程本地离线运行，不依赖网络连接"直接冲突，且与 §1 中"不启用 llama-cpp overlay port 的 `download` feature"这一已记录的内部决策矛盾，排除。
- 约定本地目录的具体路径（如平台标准用户数据目录下的 `models/` 子目录）属于不改变本决策外部行为（"用户需手动获取并放置模型文件"这一事实）的内部实现细节，留待 `/speckit.tasks`/`/speckit.implement` 阶段按宪章原则 IV 自主确定并单行记录，不在本文件中预先规定具体路径常量。

**标记**：~~`[NEEDS CLARIFICATION]`~~ → **已解决（用户委派，Agent 自主补全判断，见上）**；`data-model.md`/`quickstart.md` 中涉及模型文件路径的部分，据此选型回填为"用户需手动将模型文件放置于约定本地目录"的具体描述（见对应文件更新）。
