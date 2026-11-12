# Contract: 本地模型路径解析契约

**适用范围**: `babel_core` 中 `ModelPaths` 及所有大模型定位与加载模块。

---

## 1. 解析优先级与规则

解析本地模型绝对路径时，**必须**严格遵循以下优先级：

1. **环境变量覆盖**：
   * 检查环境变量 `BABEL_MODEL_PATH`。
   * 若非空，直接展开路径（若以 `~` 开头则展开为 `$HOME`），并将其作为最终模型路径返回。
2. **标准用户目录**：
   * 默认目标目录定义为 `$HOME/.local/models`。
   * 默认目标文件为 `$HOME/.local/models/qwen2.5-3b-instruct-q4_k_m.gguf`。
   * 若该文件存在，作为最终路径返回。
3. **历史路径回退（兼容期）**：
   * 检查平台 AppData 路径：`~/Library/Application Support/Babel/models/qwen2.5-3b-instruct-q4_k_m.gguf`。
   * 若存在，记录 Warning 级别日志提示用户手动迁移，并允许加载。
4. **缺失处理**：
   * 若上述位置均不存在模型文件，`ModelCatalog::scan()` 返回空列表，`LlmEngine` 保持 `modelAvailable = false` 状态，输出指导日志（包含标准放置路径），应用不崩溃。

---

## 2. 跨平台标准目录定义
* **macOS / Linux**: `$HOME/.local/models`
* **Windows**: `%USERPROFILE%/.local/models`

