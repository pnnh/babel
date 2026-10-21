# Bug Verification: GBNF 语法约束解码性能开销

- **Slug**: gbnf-grammar-sampling-overhead
- **Tested**: 2026-10-21
- **Assessment**: ./assessment.md
- **Fix**: ./fix.md
- **Result**: verified

## Summary

修复有效：独立重新构建 + 全量回归 98/98 通过（1 项按设计跳过）；独立重跑
`LlmEngineGenerateTest` 全部 10 项真实模型推理测试，耗时与 fix.md 报告的
"修复后"数字高度吻合（误差 <5%），确认提速幅度真实可信（3～8 倍），且全部
功能断言（标注符号不泄漏、语法梗阻提前终止、004 投机解码双侧约束生效）继
续通过。fix.md 中记录的"字节级 UTF-8 结构判定"这一关键偏离（从朴素字符串
包含检测精化而来）经独立推导验证（手工核算 ⟦ U+27E6 的 3 字节 UTF-8 编码
`E2 9F A6` 在截断前缀场景下的判定逻辑），数学上自洽，与穷举对比声称的"100%
一致"结论方向吻合。

## Checks Performed

| Check | Command / Action | Result | Notes |
|-------|------------------|--------|-------|
| 构建 | `cmake --build build/macos --config Debug`（独立重新执行，非沿用子代理构建产物） | pass | `** BUILD SUCCEEDED **` |
| 回归套件 | `ctest -C Debug --output-on-failure`（全量，独立重新执行） | pass | 100% tests passed out of 98；`PasteInjectorPermissionTest` 按既有设计跳过，与本次改动无关；总耗时 109.14s（fix.md 报告 114.30s，同量级） |
| LlmEngineGenerateTest 专项+计时复核 | `ctest -C Debug -R "LlmEngineGenerateTest"`（独立重新执行并计时） | pass | 10/10 通过，关键耗时：`RepeatedGenerateCallsOnSameEngineDoNotCrash` 18.35s（fix.md 报告 17.78s）、`GenerateOnGrammarStallTerminatesEarlyWithoutCrashing` 7.24s（报告 7.27s）、`GenerateWithDraftModelStillEnforcesGrammarConstraintOnBothSides` 4.26s（报告 4.21s）——均高度吻合，确认提速数据非子代理单方面声称 |
| 代码逻辑独立复核 | 手工重新推导 `isForbiddenByUtf8Structure()` 对 ⟦(U+27E6, UTF-8 `E2 9F A6`) 的截断前缀（单独 `E2`、`E2 9F`）判定区间计算 | pass | 单独 `E2`：`value=0x02, nRemain=2`，区间 `[0x2000,0x2FFF]` 覆盖 `0x27E6`→正确判非法；`E2 9F`：区间 `[0x27C0,0x27FF]` 覆盖 `0x27E6/0x27E7`→正确判非法，与 fix.md 描述的"保守拦截可能补全为禁用符号的前缀"逻辑一致 |
| 禁用位图与模型可用标志绑定关系 | `grep` 检查 `m_forbiddenTokenMask`/`m_draftForbiddenTokenMask` 的赋值位置 | pass | 均紧邻 `m_modelAvailable = true` / `m_draftModelAvailable = true` 之后构建，不存在"草稿模型加载成功但禁用位图仍为空、导致黑名单静默失效"的时序漏洞 |
| 工作区残留检查 | `git status --short` + `find tests -iname "*Probe*"` | pass | 仅 `LlmEngine.{h,cpp}` 改动 + 新增 `fix.md`，子代理描述的临时探针（`GbnfPerfProbeTest.cpp`/`GbnfMaskDiffProbeTest.cpp`）均已清理，未遗留 |

## Output Excerpts

```
100% tests passed out of 98
Total Test time (real) = 109.14 sec
The following tests did not run:
     43 - PasteInjectorPermissionTest.UngrantedAccessibilityYieldsPermissionDenied (Skipped)
```

```
 2/10 Test #76: LlmEngineGenerateTest.RepeatedGenerateCallsOnSameEngineDoNotCrash ... Passed   18.35 sec
 6/10 Test #80: LlmEngineGenerateTest.GenerateOnGrammarStallTerminatesEarlyWithoutCrashing ... Passed    7.24 sec
 8/10 Test #82: LlmEngineGenerateTest.GenerateWithDraftModelStillEnforcesGrammarConstraintOnBothSides ... Passed    4.26 sec
100% tests passed out of 10
```

## Residual Risks

- **穷举级 token 对比未独立重跑**：fix.md 声称对 3B/0.5B 两套词表共
  151936×2 个 token 做了逐一穷举对比（而非抽样），且探针已删除不可复现。
  本次验证未重新实现该探针独立重跑这一穷举过程，仅通过（a）手工推导两个
  边界案例的数学正确性、（b）既有功能回归测试（含真实模型推理、断言不泄
  漏标注符号）全部通过这两项间接证据建立信心，未达到"完全独立重现穷举结
  论"的严格程度。若需要更高确信度，建议后续复现该探针方法并重新跑一次。
- **14B/7B 档位未补测**：assessment.md 与 fix.md 均已如实标注这是遗留待办，
  本次验证同样未覆盖，修复逻辑本身与模型大小无关（词表规模决定收益），但
  未经受控实测验证于更大模型上同样显著。
- **与通用语法引擎的功能等价性仅覆盖当前 `kOutputGrammar` 定义**：若未来
  `kOutputGrammar` 排除的字符集合发生变化，或升级 llama.cpp 版本改变
  `decode_utf8`/`match_partial_char` 内部实现，`ForbiddenTokenMask` 需要同
  步更新并重新验证，fix.md Follow-ups 已记录此耦合风险。

## Recommendation

Close the bug — verified（独立重新构建、全量回归、关键测试计时复核均通过，
核心的字节级判定逻辑经手工数学推导确认自洽；穷举对比结论未独立重跑，已在
Residual Risks 中如实标注为未完全独立复现的部分，不影响整体判定）。
