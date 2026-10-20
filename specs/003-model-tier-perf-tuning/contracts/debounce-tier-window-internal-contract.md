# Contract: DebounceScheduler 运行时窗口调整 与 ModelCatalog 档位识别（内部实现契约）

**Spec**: [spec.md](../spec.md) ｜ **Data Model**: [data-model.md](../data-model.md) ｜ **Research**: [research.md](../research.md)

## 适用范围

本文件记录 `babel::slowpath::DebounceScheduler`、`babel::slowpath::ModelCatalog`、`babel::session::SessionController` 三者之间为落实 FR-010～FR-014 新增的内部协作契约。**不涉及**任何 QML 可见属性/信号/方法的新增或变更——`specs/002-lattice-dict-model-switch/contracts/qml-cpp-session-interface.md` 中定义的 `switchModel(filePath)`/`currentModelName`/`availableModels` 等现有对外接口签名与语义均**不变**。

## 一、`DebounceScheduler` 新增方法

```cpp
class DebounceScheduler {
 public:
  // 既有构造函数签名不变：
  DebounceScheduler(std::chrono::milliseconds debounceWindow, GenerateCallback onTrigger);

  // 新增：
  void setDebounceWindow(std::chrono::milliseconds window);
};
```

**前置/后置条件**：
- 可在任意 `state()` 下调用（`Idle`/`Debouncing`/`Generating`/`Ready`/`Stale` 均可），MUST NOT 因调用时机不同而产生不同副作用。
- 调用后 MUST NOT 改变 `state()` 当前取值、`m_pendingInput`、`m_generationInFlight`、`m_timerArmed` 等既有状态（FR-013）。
- 调用后，下一次 `tick()` 对"是否已过防抖窗口"的判定 MUST 使用新窗口值；调用前已经基于旧窗口值排定的触发不受影响（即窗口调整不追溯既有等待中的判定，只影响调用时刻之后的判定逻辑，自然满足 FR-014"下一次防抖触发周期生效"）。

## 二、`ModelCatalog` 新增公开静态方法

```cpp
class ModelCatalog {
 public:
  // 既有方法签名不变：
  static std::vector<ModelDescriptor> scan();
  static std::vector<RecommendedModelProfile> recommendedProfiles();

  // 新增：由原 .cpp 匿名命名空间私有函数 classifyTier() 提升而来，关键字表按
  // research.md §5 更新（新增 "7b"→Medium，"14b" 由 Medium 改判 High）。
  static ModelDescriptor::TierHint classifyTierByFileName(const QString &fileName);
};
```

`scan()` 内部实现从调用私有 `classifyTier()` 改为调用该公开静态方法，行为对 `scan()` 的既有调用方（`SessionController::availableModels()`）透明、无感知差异。

## 三、新增查表函数（防抖窗口映射，落位留待 `/speckit.tasks` 按就近原则确定）

```cpp
std::chrono::milliseconds tierDebounceWindow(ModelDescriptor::TierHint tier);
```

| 输入 `tier` | 返回值 |
|---|---|
| `Low` | `200ms` |
| `Medium` | `500ms` |
| `High` | `800ms` |
| `Unknown` | `200ms`（复用 `Low` 档位数值，用户决策，`research.md` §5） |

## 四、`SessionController` 新增调用点（不新增 QML 可见方法/属性）

| 调用点 | 时机 | 行为 |
|---|---|---|
| `bootstrapPlatformDefaults()` | 应用启动、加载默认低配模型后 | 对 `ModelPaths::conventionalModelFilePath()` 文件名调用 `classifyTierByFileName()` → `tierDebounceWindow()` → `m_debounceScheduler->setDebounceWindow(...)`（预期恒为 `Low`/200ms，与 FR-011 一致） |
| `applyModelSwitchResult(..., bool success, ...)` | `switchModel()` 热切换流程结束、`success == true` 时 | 对 `finalModelPath` 文件名执行同样的"分类 → 查表 → `setDebounceWindow()`"流程；`success == false`（理论上仅当连低配默认模型都不可用时发生）时 MUST NOT 调用 `setDebounceWindow()`（此时 `modelAvailable() == false`，`SessionController::submitKeystrokes()` 既有逻辑本就不会再驱动 `m_debounceScheduler`，窗口值是否更新不产生可观察差异，但显式不调用可避免对一个"已知不可用"状态做无意义计算） |

## 五、行为契约（可被测试断言的外部可观察效果）

1. 应用启动后、未进行任何手动切换时，防抖窗口 MUST 为 200ms（与 001/002 既有体验一致，FR-011）。
2. 用户成功切换至文件名含 `"7b"` 关键字的模型后，下一次防抖触发使用的等待时长 MUST 为 500ms（FR-012）。
3. 用户成功切换至文件名含 `"14b"` 关键字的模型后，下一次防抖触发使用的等待时长 MUST 为 800ms（FR-002/FR-012，与"14b 现对应高配"的分类表变更联动）。
4. 用户从中/高配切回文件名含 `"3b"` 的低配模型后，防抖窗口 MUST 恢复为 200ms（FR-011 原文"保持既有数值不变"的另一种测试角度：切回后同样生效）。
5. 切换过程中（`modelSwitching() == true` 期间），`DebounceScheduler` 仍使用切换前的旧窗口值，直至切换完成的那一刻才更新（FR-014）。
6. 用户成功切换至文件名**不含**任何已识别关键字（`"3b"`/`"7b"`/`"14b"`/`"32b"`）的自定义命名模型后（`classifyTierByFileName()` 返回 `Unknown`），下一次防抖触发使用的等待时长 MUST 为 200ms（与 `Low` 档位数值相同，用户决策，`research.md` §5）。

## 六、本契约未决定的事项

- `tierDebounceWindow()`/`classifyTierByFileName()` 具体落位于哪个 `.h`/`.cpp` 文件、是否再拆出独立文件：内部实现细节，留待 `/speckit.tasks`/`/speckit.implement` 按就近原则自主决定，不改变上述对外行为契约。
