# Bug Verification: 英文单词误判为走音拼音

- **Slug**: english-word-pinyin-misdetection
- **Tested**: 2026-10-21
- **Assessment**: ./assessment.md
- **Fix**: ./fix.md
- **Result**: verified

## Summary

修复有效：全量回归 98/98 通过（1 项按设计跳过），新增/更新的 30 项专项测试（FastCorrectorTest/PinyinLatticeTest/PinyinTrieTest）全部通过；真实部署链路（main.cpp 首次部署 → SessionController 生产加载 → FastCorrector 构造）在"删除已部署词表模拟全新安装"场景下重新验证通过，应用全程无崩溃，全局热键正常注册。验证过程中额外发现词表拦截方案存在远超预期的碰撞规模（2170 个真实拼音/英文碰撞），已与用户确认为符合产品设计意图（英文优先）而非缺陷，详见 fix.md 新增记录，不计入本次回归失败。

## Checks Performed

| Check | Command / Action | Result | Notes |
|-------|------------------|--------|-------|
| Reproduction（post-fix，真实部署全链路） | 删除 `~/Library/Application Support/Babel/dictionaries/common-english-words.txt` 模拟全新安装 → 实际运行 `build/macos/babel/Debug/Babel.app` → 检查启动日志 | pass | 日志确认：①首次部署词表文件（60022 行）；②SessionController 加载"53968 条高频英文常用词" + "891061 条真实词库条目"；③全局热键注册成功；全程无崩溃、无段错误 |
| 新增/更新测试 | `ctest -C Debug -R "FastCorrectorTest\|PinyinLatticeTest\|PinyinTrieTest"` | pass | 30/30 通过，含 `EnglishWordListInterceptsCommonWordBeforePinyinPipeline`、`PinyinTrieOverreachMatchIsMarkedAsLatinOriginLengthMismatch`、`ToAnnotatedPromptTextAnnotatesRiskySingleCandidateCollapse` 等直接针对本 bug 的用例 |
| 回归套件 | `ctest -C Debug --output-on-failure`（全量） | pass | 100% tests passed out of 98；1 项（`PasteInjectorPermissionTest`）按既有设计因环境权限原因跳过，与本次改动无关 |
| 构建 | `cmake --build build/macos --config Debug` | pass | `** BUILD SUCCEEDED **`，无新增警告 |
| Lint / type-check | — | not-run | 项目未配置独立 lint/type-check 工具链，构建本身即走 Xcode/Clang 诊断，已随构建检查 |

## Output Excerpts

```
100% tests passed out of 98
The following tests did not run:
     43 - PasteInjectorPermissionTest.UngrantedAccessibilityYieldsPermissionDenied (Skipped)
```

```
[info] main: 已将随包高频英文常用词表首次部署到约定目录 .../dictionaries/common-english-words.txt
[info] SessionController: 已从约定词库目录加载 891061 条真实词库条目（.../dictionaries）
[info] SessionController: 已加载 53968 条高频英文常用词（.../dictionaries/common-english-words.txt）
[info] 全局热键注册成功（默认组合键）
```

## Residual Risks

- **词表拦截的碰撞规模（本次验证阶段新发现）**：独立量化统计发现真实词库与英文词表之间存在 **2170 个**精确拼音/英文碰撞（单字 1334 + 多字 836），远超 assessment.md 原先仅提及的"姓氏/专有名词碰撞"类别，包含 `hen`(很)/`chi`(吃) 以及更极端的 `name`(那么)/`like`(立刻) 等双向高频词。经与用户确认，这是产品"英文优先、拼音为辅"设计哲学下的**预期行为**（用户决策，详见 fix.md），不作为本 bug 的遗留缺陷处理；根治方案（候选词列表手动纠偏）已规划为独立后续特性，需另行 `speckit.specify` 立项。
- **GUI 层真实键盘触发的端到端复现未覆盖**：本次验证复现的是"应用启动 → 生产部署 → FastCorrector 构造"这条链路，未通过真实键盘输入 + 全局热键触发浮窗 + 可见候选文本这一完整 UI 交互路径做像素级复现（该路径此前已由用户在实机测试中确认過，且不属于本 bug 修复涉及的代码范围）。
- **许可/合规未决**：`common-english-words.txt`（COCA 衍生词表）来源与许可未正式确认，按用户 `accept_risk` 决策维持现状，参见 fix.md Follow-ups。

## Recommendation

Close the bug — verified end-to-end（功能回归、专项测试、真实部署链路三方面均通过；碰撞规模问题已确认为设计决策而非缺陷，不影响结案）。
