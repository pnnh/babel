# Implementation Plan: AI 辅助双语浮窗转换器（MVP）

**Branch**: `001-floating-ime-converter`（Git 当前实际所在分支为 `main`；本仓库未对该特性单独建立 feature branch，特性产物以 `specs/001-floating-ime-converter/` 目录隔离）

**Date**: 2026-10-16

**Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `/specs/001-floating-ime-converter/spec.md`

**Note**: This template is filled in by the `/speckit.plan` command. See `.specify/templates/plan-template.md` for the execution workflow.

## Summary

在既有空壳 Qt6 Quick 桌面工程（`babel/`）之上，交付一个跨平台（Windows/macOS/Linux）的全局热键浮窗转换器：用户按下默认热键 `Ctrl+Alt+Space`（macOS 为 `Control+Option+Space`）唤起无边框浮窗并记录当前系统焦点窗口；浮窗内置"快流"（本地词典/拼音/简拼 Trie + QWERTY 键位邻近编辑距离，纯内存、无网络、无大模型，目标延迟 <15ms）与"慢流"（本地 llama.cpp 加载 Qwen2.5-3B-Instruct GGUF Q4_K_M，防抖触发，目标首批内容 <300ms、整句 <800ms）双管道；用户按回车确认后，标准中文文本写入系统剪贴板并尝试平台原生模拟粘贴注入（Windows SendInput / macOS CGEventPost / Linux X11 XTest），注入不可用或失败时统一降级为"已复制到剪贴板，请手动粘贴"提示，不区分具体原因。

本计划严格以 `spec.md` 已确认的 FR-001~FR-013、SC-001~SC-007、Assumptions、Out of Scope 为唯一授权范围；对于 spec 未显式钦定、且会新增依赖/影响实现方向的 5 项事项（快流编辑距离库的自研与否、全局热键绑定库、模拟注入库、单元测试框架、模型文件供给方式），本计划在首版中**未**代为决策，仅在 `research.md` 中给出候选方案与权衡并标记 `[NEEDS CLARIFICATION]`，留待 `/speckit.clarify` 或用户显式委派。**本次更新**：用户已明确委派"全部委托 Agent 按 `research.md` 中各自的 Recommendation 选项执行，不需要用户逐项选择"，Agent 已据此在 `research.md` 中逐项采纳 Recommendation（或在 Recommendation 缺失时按既有证据自主补全判断）定稿为最终 Decision，5 项 `[NEEDS CLARIFICATION]` 已全部解除，详见下文 Technical Context 与"用户委派及 Agent 实际选择"一节。

## Technical Context

**Language/Version**: C++23（根 `CMakeLists.txt` 已设 `CMAKE_CXX_STANDARD 23`；macOS 平台另启用 `OBJC`/`OBJCXX` 与 Swift 6.1 用于原生桥接——均为仓库既有配置，非本特性新增）。

**Primary Dependencies**:
- Qt6（`Core`/`Qml`/`Quick`/`Gui`/`QuickControls2`/`Widgets`/`ShaderTools`/`QuickDialogs2`），既有骨架 QML 模块名 `quick`（`babel/CMakeLists.txt`、`babel/content/Main.qml`）。浮窗 UI、剪贴板写入（`QClipboard`，属 `Qt6::Gui` 既有依赖）均复用该技术栈，不新增 UI 框架。
- **慢流推理引擎 — 已确认**：`llama.cpp`，经 vcpkg overlay port `vcpkg/ports/llama-cpp`（已存在于仓库但尚未被 `vcpkg.json` 引用）接入；模型固定为 **Qwen2.5-3B-Instruct，GGUF Q4_K_M** 量化（spec FR-013 已明确，本轮 Technical Context 进一步确认引擎为 llama.cpp，因 GGUF 为 llama.cpp/ggml 生态原生格式，无需也不引入额外推理运行时）。macOS 建议启用 overlay port 的 `metal` feature 以辅助达成 SC-003 延迟目标；`download`/`tools` feature 默认不启用（避免引入 curl 等 spec 未要求的能力，属内部细节，单行记录）。
- **快流编辑距离/词典实现 — 已解决（用户委派，Agent 采纳 Recommendation）**：采纳 **方案 1：仓库内自研**——编辑距离前缀索引（SymSpell 思路）+ 拼音/简拼 Trie + QWERTY 邻近权重表，全部手写 C++，不引入第三方源码/overlay。理由与排除的替代方案见 `research.md` §2。
- **全局热键绑定库 — 已解决（用户委派，Agent 采纳 Recommendation）**：采纳 **方案 1：分平台自研薄封装**，直接调用已确认的原生 API（Windows `RegisterHotKey` / macOS Carbon `RegisterEventHotKey` 或 Cocoa 全局事件监听 / Linux 经 `libxcb` 或 `libX11` 的 `XGrabKey`），不引入 `JaderoChan/global_hotkey`、`Skycoder42/QHotkey` 等第三方库。理由见 `research.md` §3。
- **模拟粘贴注入库 — 已解决（用户委派，Agent 采纳 Recommendation）**：采纳 **方案 1：分平台自研薄封装**，直接调用已确认的原生 API（Windows `SendInput` / macOS `CGEventPost`，需 Accessibility 权限 / Linux X11 `XTest` 扩展；Wayland 预期失败走统一降级路径），不引入 `antelle/keyboard-auto-type`、`libxdo` 等第三方库。理由见 `research.md` §4.2。
- **本地 LLM 模型文件供给/部署方式 — 已解决（用户委派，Agent 自主补全判断）**：采纳**用户手动下载模型文件后放置于应用约定的本地目录**，不随安装包分发、应用内不提供自动下载入口（与"全程本地离线运行"Assumption 一致，避免模型权重再分发许可评估）。理由见 `research.md` §7。
- 既有依赖 `spdlog`（日志）、`boost-filesystem`（模型/词典文件路径处理）沿用，不变更。
- **单元测试框架 — 已解决（用户委派，Agent 自主补全判断）**：采纳 **GoogleTest**（经 vcpkg 官方 port `gtest` 引入），详见下文 Testing 字段与 `research.md` §6。

**Storage**: 复用既有 `sqlite3`（vcpkg 依赖 `unofficial-sqlite3`）承载快流词典/拼音 Trie 的本地索引数据；具体表结构见 `data-model.md`。按用户本轮技术上下文明确要求，不引入新存储技术。

**Testing**: **已解决（用户委派，Agent 自主补全判断）** 采纳 **GoogleTest**（vcpkg 官方 port `gtest`，非 overlay/vendor），配合根 `CMakeLists.txt` 既有的 `enable_testing()`，通过 CMake `gtest_discover_tests` 注册 CTest 用例；`vcpkg.json` 需在 `/speckit.tasks`→`/speckit.implement` 阶段补充 `gtest` 依赖声明（本阶段仍不改动构建文件，遵循原则 V）。理由见 `research.md` §6。

**Target Platform**: 桌面三平台 Windows / macOS / Linux（Linux 以 X11 为主；Wayland 下自动注入不保证支持，走 spec 已定义的统一降级提示路径），对应 `CMakePresets.json` 既有的 `windows`/`macos`/`linux` 三套 configure/build preset（均为既有配置，未改动）。

**Project Type**: desktop-app（单一常驻 Qt6 Quick 进程：托盘 + 浮窗；不做跨进程 IPC；不接入系统级输入法框架 TSF/IMKit/Fcitx5——spec Out of Scope 已明确排除，本计划不扩展此边界）。

**Performance Goals**（引用 spec Success Criteria，非本阶段新定义指标）:
- 热键唤起浮窗：用户主观体验即时，无可感知卡顿（SC-001）。
- 原始输入槽规整化展示延迟 < 15ms（SC-002，亦为 FR-003 端到端目标）。
- 普通消费级 CPU 下，慢流首批内容 < 300ms、整句生成完毕 < 800ms（SC-003）。

**Constraints**:
- 快流、慢流 MUST 全程本地离线运行，不依赖网络连接（spec Assumptions）。
- 自动粘贴注入 MUST 至少在 macOS 与 Linux X11 可用；Windows 与其余 Linux 合成器（含 Wayland）以平台能力为准，不支持/失败时统一提示"已复制到剪贴板，请手动粘贴"，不区分具体原因（FR-009/FR-010/FR-012）。
- 单一常驻进程架构，不新增跨进程 IPC、不接入系统级输入法框架（spec Out of Scope）。
- 本地 LLM 模型文件 MUST 由用户手动下载后放置于应用约定的本地目录；应用不随安装包分发模型权重，亦不提供应用内自动下载入口（用户委派、Agent 采纳，见 `research.md` §7）；文件缺失/损坏/无法加载时的提示行为仍遵循 FR-011。

**Scale/Scope**: MVP 面向单机单用户本地离线场景；浮窗同一时刻仅处理一个转换会话，不要求并发多会话（spec Assumptions）。

**本阶段最初遗留、现已全部解决的 `[NEEDS CLARIFICATION]` 清单**（均为本次规划新发现，非 spec 遗留——spec 的 4 项既有澄清已在 `checklists/requirements.md` 中全部解决）：

| # | 事项 | 判定程序命中项 | 处理方式（历史） | 当前状态 |
|---|------|----------------|------------------|----------|
| 1 | 快流编辑距离/拼音 Trie：自研 vs 开源移植 | (d) 新增依赖/实现范围 | `research.md` §2 列候选，不选型 | **已解决**（用户委派，见下文） |
| 2 | 全局热键绑定：自研薄封装 vs 现成库 | (d) 新增依赖 | `research.md` §3 列候选，不选型 | **已解决**（用户委派，见下文） |
| 3 | 模拟粘贴注入：自研薄封装 vs 现成库 | (d) 新增依赖 | `research.md` §4.2 列候选，不选型 | **已解决**（用户委派，见下文） |
| 4 | C++ 单元测试框架选型 | (d) 新增依赖 | `research.md` §6 列候选，不选型 | **已解决**（用户委派，见下文） |
| 5 | 本地 LLM 模型文件的供给/部署方式（随包打包 / 用户手动放置 / 首次运行引导） | (d) 部署方式变更 | `research.md` §7 仅记录问题，不选型 | **已解决**（用户委派，见下文） |

### 用户委派及 Agent 实际选择

用户已显式委派："全部委托 Agent 按 `research.md` 中各自的 Recommendation 选项执行，不需要用户逐项选择"。该委派本身即构成原则 II 要求的显式用户决策（决策权被用户明确转移给 Agent 执行，而非 Agent 自行发明默认值）；Agent 据此对上述 5 项逐一定稿，结果如下（完整理由见 `research.md` 对应小节）：

| # | 事项 | Agent 实际选择 | Recommendation 来源 |
|---|------|----------------|----------------------|
| 1 | 快流编辑距离/拼音 Trie | **方案 1：仓库内自研**（编辑距离前缀索引 + 拼音/简拼 Trie + QWERTY 邻近权重表，全部手写 C++） | `research.md` §2 明文 Recommendation 的"追求许可证最简单"分支 |
| 2 | 全局热键绑定库 | **方案 1：分平台自研薄封装**（Windows `RegisterHotKey` / macOS Carbon 或 Cocoa / Linux `XGrabKey`） | `research.md` §3 明文 Recommendation 的"保留完全控制权"分支 |
| 3 | 模拟粘贴注入库 | **方案 1：分平台自研薄封装**（Windows `SendInput` / macOS `CGEventPost` / Linux X11 `XTest`） | `research.md` §4.2 明文 Recommendation 的"保持完全可控"分支 |
| 4 | C++ 单元测试框架 | **GoogleTest**（经 vcpkg 官方 port `gtest` 引入） | `research.md` §6 原未录入明文 Recommendation，Agent 在委派范围内按既有证据自主补全判断 |
| 5 | 本地 LLM 模型文件供给/部署方式 | **用户手动下载后放置于约定本地目录**（不随安装包分发、不提供应用内自动下载入口） | `research.md` §7 原未录入候选/Recommendation，Agent 在委派范围内按 FR-011 字面前提与 Assumptions 约束自主补全判断 |

说明：第 4、5 两项 `research.md` 原文未录入明文 Recommendation 句（§6 仅列候选未排序取舍，§7 仅记录问题未列候选）；Agent 在用户"全部委托"的委派范围内，为完成委派目标而自主补全判断，而非超出委派范围擅自新增产品语义——判断依据均已在 `research.md` 对应小节以"采纳理由"形式记录，可追溯、可审查。

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

*Language (宪章《语言与文档规范》)：本 plan 及同特性 research/data-model/quickstart/contracts 的说明性文字 MUST 使用简体中文；API/路径/库名等标识符可保留英文。本文件已满足该要求。*

**原则 I（显式需求是唯一授权范围）**：Technical Context 中每一条"已确认"表述均可追溯至 spec.md 的 FR/SC/Assumptions 条款、用户本轮 Technical Context 原话，或用户后续显式委派（"全部委托 Agent 按 Recommendation 执行"）（均已在正文以"spec FR-xxx"/"本轮 Technical Context"/"用户委派"标注来源）；原 5 项未显式授权的事项现已全部经由用户显式委派转化为可追溯的决策，未出现 Agent 超出委派范围自行发明新需求的情况。**PASS**。

**原则 II（未决策不得默认，Assumption 不得充当决策）**：原 5 项曾在 (d)（新增依赖/部署方式变更）命中判定程序并标记 `[NEEDS CLARIFICATION]`；现已通过用户显式委派解除——委派指令本身即为显式决策行为（用户明确将选型执行权转移给 Agent，而非 Agent 自行以"最佳实践"消解为默认值），5 项的具体选型结果均在 `research.md`/本文件"用户委派及 Agent 实际选择"一节中可追溯记录。**PASS**。

**原则 III（推荐不是批准）**：`research.md` 中对候选方案给出的 Recommendation 原为建议而非决策；经用户显式委派后，Agent 采纳 Recommendation（或在缺失处自主补全）的行为已构成被用户授权的决策执行，而非 Agent 单方面将 Recommendation 当作既定选型；该委派关系与采纳结果均已逐项记录在案。**PASS**。

**原则 IV（内部细节低摩擦，外部行为高控制）**：新增的目录划分（见下文 Project Structure）、内部状态机字段命名、Qt `metal` feature 是否启用、模型文件约定本地路径的具体常量等均属不改变外部可观察行为的内部细节，已由本计划单行自主记录，未就此提问用户。**PASS**。

**原则 V（最小必要修改与实施边界）**：本阶段（`/speckit.plan`）仅更新 `specs/001-floating-ime-converter/{plan.md, research.md, data-model.md, contracts/*, quickstart.md}` 五类设计产物，未改动任何既有源码（`babel/**`）或构建文件（`CMakeLists.txt`、`vcpkg.json`、`CMakePresets.json`）；`vcpkg.json` 中补充 `llama-cpp`/`gtest` 依赖、新增 `src/` 子目录等均留待 `/speckit.tasks` → `/speckit.implement` 阶段按 Project Structure 执行。**PASS**。

**原则 VI（预授权质量基线不得发明产品语义）**：本阶段未产出代码，暂不适用；已在 `data-model.md` 中明确"粘贴注入结果的内部区分（成功/环境不支持/权限不足/其他）MUST NOT 泄露为差异化用户文案"，防止实现阶段借内部诊断发明对外错误语义，符合 FR-012；5 项选型落地（含自研注入/热键实现）均未改变该约束。**PASS**。

**判定程序门禁**：对本 plan 中每一项用户未显式指定的事项已逐条执行 (a)–(e) 判断；原 5 项命中 (d) 的事项已经由用户显式委派解决（见上文"用户委派及 Agent 实际选择"）；其余内部细节（目录结构、QML/C++ 属性命名、内部状态机标签、模型文件路径常量等）判定结果全部为"否"，已按原则 IV 自主决定并单行记录于 Project Structure 与 `data-model.md`。

**Gate 结论**：**PASS**——设计产物已生成，原 5 项 `[NEEDS CLARIFICATION]` 均已经由用户显式委派、Agent 采纳 Recommendation（或自主补全判断）全部解决，无遗留未决策事项；`/speckit.tasks` 可据此直接为快流实现、热键绑定、注入实现、单元测试、模型文件部署等模块排期，无需再经 `/speckit.clarify`。

### Post-Design Re-check（Phase 1 完成后）

`data-model.md` 与 `contracts/` 中定义的实体字段、状态机迁移、QML/C++ 接口方法均逐条对照 spec 的 FR-001~FR-013 与 Key Entities 核对来源，未发现设计产物引入 spec 未授权的新外部行为；`TargetFocusContext` 在"目标窗口被切换/最小化/关闭"场景下的具体处理规则（spec Edge Cases 第 3 条，未转化为 FR）在 `data-model.md` 中仅如实记录字段与问题本身，**未**代为定义该场景下的具体降级行为，避免违反原则 II。**PASS，无新增违规**。

**本次（委派采纳后）二次复查**：`data-model.md` §7（`DictionaryEntry`/`PinyinTrieNode`）、`contracts/global-hotkey-contract.md`、`contracts/injection-outcome-contract.md` 已按 5 项选型结果回填具体实现路径描述（自研 C++、GoogleTest 测试目标、模型文件约定路径等），核对后确认均未引入 spec 未授权的新外部行为——回填内容均为"如何实现已授权需求"的内部细节或委派范围内的选型结果，不改变 FR-001~FR-013 定义的外部可观察行为。**PASS，无新增违规**。

## Project Structure

### Documentation (this feature)

```text
specs/001-floating-ime-converter/
├── spec.md               # 已有：特性规格
├── plan.md               # 本文件（/speckit.plan 输出）
├── research.md           # Phase 0 输出
├── data-model.md         # Phase 1 输出
├── quickstart.md         # Phase 1 输出
├── contracts/            # Phase 1 输出
│   ├── qml-cpp-session-interface.md
│   ├── global-hotkey-contract.md
│   └── injection-outcome-contract.md
├── checklists/
│   └── requirements.md   # 已有：规格质量检查单
└── tasks.md               # Phase 2 输出（由 /speckit.tasks 生成，本命令不创建）
```

### Source Code (repository root)

本特性延续仓库既有的单一桌面应用工程布局（非 web/mobile 多项目结构）。以下为**本计划针对该特性建议的目录扩展**，供 `/speckit.tasks` 排期与 `/speckit.implement` 落地参考；本阶段仅做结构规划，不创建这些源文件/目录。

```text
babel/
├── CMakeLists.txt              # 既有：需在 /speckit.implement 阶段补充 src/ 子目录与 llama-cpp 链接（本计划不改动）
├── main.cpp                    # 既有：常驻进程入口，后续接入全局热键监听与托盘
├── content/                    # 既有：QML 资源根目录
│   ├── Main.qml                 # 既有入口，后续承载托盘/常驻窗口角色
│   └── FloatingWindow.qml       # 新增：无边框浮窗，三段式布局（原始输入槽/输出预览/学习提示气泡，对应 FR-002）
└── src/                         # 新增：纯 C++ 业务逻辑目录，与 QML 展示解耦
    ├── fastpath/                  # 快流：对应 FR-003（编辑距离/拼音实现：仓库内自研，已选型，见 research.md §2）
    │   ├── FastCorrector.{h,cpp}        # 对外统一入口：输入击键 → 规整化候选文本
    │   ├── PinyinTrie.{h,cpp}           # 拼音/简拼词典 Trie（数据层见 data-model.md）
    │   └── KeyboardProximityMap.{h,cpp} # QWERTY 物理键位邻近编辑距离加权表
    ├── slowpath/                  # 慢流：对应 FR-004/FR-005，封装 llama.cpp
    │   ├── LlmEngine.{h,cpp}            # 加载/调用 Qwen2.5-3B-Instruct GGUF Q4_K_M
    │   └── DebounceScheduler.{h,cpp}    # 输入防抖 → 触发/取消慢流生成
    ├── hotkey/                    # 全局热键：对应 FR-001（分平台自研薄封装，已选型，见 research.md §3）
    │   ├── GlobalHotkey.h                # 跨平台抽象接口
    │   ├── GlobalHotkey_win.cpp
    │   ├── GlobalHotkey_mac.mm
    │   └── GlobalHotkey_x11.cpp
    ├── inject/                    # 剪贴板写入 + 模拟粘贴注入：对应 FR-007/FR-008/FR-009/FR-012（分平台自研薄封装，已选型，见 research.md §4.2）
    │   ├── ClipboardWriter.{h,cpp}       # 基于 Qt6::Gui QClipboard
    │   ├── PasteInjector.h               # 跨平台抽象接口
    │   ├── PasteInjector_win.cpp
    │   ├── PasteInjector_mac.mm
    │   └── PasteInjector_x11.cpp
    └── focus/                     # 目标应用焦点上下文：对应 FR-001 记录、FR-008 使用
        └── FocusContext.{h,cpp}

tests/                           # 单元测试框架：GoogleTest（vcpkg 官方 port `gtest`），已选型，见 research.md §6；`gtest_discover_tests` 与根 CMakeLists.txt 既有 enable_testing() 集成，具体测试目标由 /speckit.tasks 排期
```

**Structure Decision**：沿用仓库既有单工程布局（`babel/` 可执行目标 + `content/` QML 资源），仅新增 `babel/src/` 作为纯 C++ 业务逻辑目录，按"快流/慢流/热键+浮窗 UI/剪贴板与注入"四个设计分区拆分子目录，与 `content/` 下的 QML UI 解耦，便于分平台代码（`*_win.cpp`/`*_mac.mm`/`*_x11.cpp`）并行开发与裁剪编译。未采用 web/mobile 等多项目结构选项（不适用于本特性）。

## Complexity Tracking

> **Fill ONLY if Constitution Check has violations that must be justified**

无宪章违规需要豁免，本表留空。
