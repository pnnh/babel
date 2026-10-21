#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <filesystem>
#include <memory>

#include "babel/src/focus/FocusContext.h"
#include "babel/src/hotkey/HotkeyBinding.h"
#include "babel/src/inject/InjectionResult.h"
#include "babel/src/session/DraftTypes.h"
#include "babel/src/session/OutputPreviewTypes.h"
#include "babel/src/slowpath/ModelCatalog.h"

class QTimer;

namespace babel::fastpath {
class FastCorrector;
}
namespace babel::slowpath {
class DebounceScheduler;
class LlmEngine;
}  // namespace babel::slowpath
namespace babel::hotkey {
class GlobalHotkey;
}
namespace babel::inject {
class ClipboardWriter;
class PasteInjector;
}  // namespace babel::inject

namespace babel::session {

/// 浮窗会话状态机（data-model.md §6），是 RawDraftText/StandardOutputPreview 等
/// 实体的宿主上下文。对应 User Story 1/2/3 的整体交互流程。
enum class FloatingSessionState {
  Hidden,          ///< 初始状态 / 确认或取消后
  Invoked,         ///< 用户按下全局热键（FR-001），捕获 TargetFocusContext，浮窗显示
  Editing,         ///< 浮窗可输入状态下用户持续击键
  Confirmed,       ///< 用户按回车（FR-006）
  Cancelled,       ///< 用户按 Esc 或再次触发热键（FR-006）
  ModelUnavailable,  ///< 本地 LLM 模型文件缺失/损坏/无法加载（FR-011）
};

/// 暴露给 QML 的浮窗会话控制器（contracts/qml-cpp-session-interface.md）。
/// 承载一次浮窗会话（FloatingSessionState）的全部可观察状态。
///
/// 本阶段（Foundational，T011）仅声明只读属性/信号/可调用方法骨架，
/// submitKeystrokes()/confirm()/cancel() 的业务逻辑由 User Story 1 阶段
/// （T036/T037/T038）在 SessionController.cpp 中补全实现。
/// 注册方式（T014）：在 main.cpp 中通过 qmlRegisterSingletonInstance() 手动注册为
/// QML 单例"SessionController"（classic 注册方式，不依赖 QML_ELEMENT/qmltyperegistrar
/// 跨 babel_core 静态库边界的构建期扫描，兼容性更可预期）。
class SessionController : public QObject {
  Q_OBJECT

  Q_PROPERTY(QString rawInputSlotText READ rawInputSlotText NOTIFY rawInputSlotTextChanged)
  Q_PROPERTY(QString outputPreviewText READ outputPreviewText NOTIFY outputPreviewTextChanged)
  Q_PROPERTY(QString outputPreviewState READ outputPreviewState NOTIFY outputPreviewStateChanged)
  Q_PROPERTY(QString learningTipText READ learningTipText NOTIFY learningTipTextChanged)
  Q_PROPERTY(bool learningTipVisible READ learningTipVisible NOTIFY learningTipVisibleChanged)
  Q_PROPERTY(bool modelAvailable READ modelAvailable NOTIFY modelAvailableChanged)
  Q_PROPERTY(bool manualPasteHintVisible READ manualPasteHintVisible NOTIFY manualPasteHintVisibleChanged)
  // 002-lattice-dict-model-switch（contracts/qml-cpp-session-interface.md 二/三/四）：
  // 设置页面/模型热切换新增只读属性。
  Q_PROPERTY(QString currentModelName READ currentModelName NOTIFY currentModelNameChanged)
  Q_PROPERTY(bool modelSwitching READ modelSwitching NOTIFY modelSwitchingChanged)
  Q_PROPERTY(QVariantList availableModels READ availableModels NOTIFY availableModelsChanged)
  Q_PROPERTY(QVariantList recommendedProfiles READ recommendedProfilesVariant CONSTANT)
  Q_PROPERTY(QString fallbackNoticeText READ fallbackNoticeText NOTIFY fallbackNoticeTextChanged)
  Q_PROPERTY(bool settingsVisible READ settingsVisible NOTIFY settingsVisibleChanged)
  // 004-speculative-decoding（contracts/qml-cpp-session-interface-increment.md
  // 一）：唯一的对外契约增量——草稿模型推荐规格展示 + 投机解码未启用提示。
  Q_PROPERTY(QVariantList recommendedDraftProfiles READ recommendedDraftProfilesVariant CONSTANT)
  Q_PROPERTY(QString speculativeDecodingNoticeText READ speculativeDecodingNoticeText NOTIFY
                 speculativeDecodingNoticeTextChanged)
  // 004-speculative-decoding 事后修订（用户决策：实验性功能默认关闭，设置页面
  // 提供可见开关，由用户显式开启后才会在中/高配档位实际启用投机解码加速）。
  Q_PROPERTY(bool speculativeDecodingEnabled READ speculativeDecodingEnabled WRITE
                 setSpeculativeDecodingEnabled NOTIFY speculativeDecodingEnabledChanged)
  // 005-learning-tip-generation（contracts/qml-cpp-session-interface-increment.md
  // 一，data-model.md 三.1）：学习提示气泡开关，默认开启（FR-008），不跨应用
  // 重启持久化。
  Q_PROPERTY(
      bool learningTipEnabled READ learningTipEnabled WRITE setLearningTipEnabled NOTIFY learningTipEnabledChanged)

 public:
  /// 浮窗关闭原因（qml-cpp-session-interface.md 的 sessionClosed(reason) 信号参数）。
  enum class CloseReason { Confirmed, Cancelled };
  Q_ENUM(CloseReason)

  explicit SessionController(QObject *parent = nullptr);
  ~SessionController() override;

  /// 装配生产环境的真实依赖（FastCorrector/LlmEngine/DebounceScheduler/
  /// ClipboardWriter/PasteInjector/GlobalHotkey）并启动慢流轮询定时器。
  /// 仅由 main.cpp 在生产环境调用；单元测试 MUST NOT 调用本方法，
  /// 应改用 setXxx() 注入测试替身，避免测试进程意外抢占系统全局热键等副作用。
  void bootstrapPlatformDefaults();

  QString rawInputSlotText() const { return m_normalizedCandidate.content; }
  QString outputPreviewText() const { return m_outputPreview.content; }
  QString outputPreviewState() const;
  QString learningTipText() const { return m_learningTipText; }
  bool learningTipVisible() const { return m_learningTipVisible; }
  bool modelAvailable() const { return m_modelAvailable; }
  bool manualPasteHintVisible() const { return m_manualPasteHintVisible; }

  /// 当前慢流实际加载模型的文件名（不含路径），模型不可用时为空字符串。
  QString currentModelName() const;
  bool modelSwitching() const { return m_modelRuntimeState.isSwitching; }
  /// 约定本地模型目录下扫描到的全部模型文件条目（每次读取时重新扫描，
  /// `ModelCatalog::scan()` 为轻量级文件系统枚举+4 字节头校验，FR-011）。
  QVariantList availableModels() const;
  /// 固定三档推荐模型说明性数据（FR-012），非扫描结果、不持久化。
  std::vector<babel::slowpath::RecommendedModelProfile> recommendedProfiles() const;
  QVariantList recommendedProfilesVariant() const;
  QString fallbackNoticeText() const { return m_modelRuntimeState.lastFallbackNotice; }
  bool settingsVisible() const { return m_settingsVisible; }

  /// 004-speculative-decoding（T017，data-model.md 二，FR-004）：固定两档草稿
  /// 模型推荐说明性数据，与 recommendedProfiles() 同构，非扫描结果、不持久化。
  std::vector<babel::slowpath::RecommendedDraftModelProfile> recommendedDraftProfiles() const;
  QVariantList recommendedDraftProfilesVariant() const;
  /// 004-speculative-decoding（T022，data-model.md 四，FR-006/FR-009）：当前
  /// 目标模型档位是否启用投机解码加速及原因的说明文案——低配档位恒为空字符串
  /// （不展示任何提示）；中/高配档位未配置对应草稿模型时为非空提示；已配置时
  /// 为空字符串。
  QString speculativeDecodingNoticeText() const { return m_speculativeDecodingNoticeText; }

  /// 004-speculative-decoding 事后修订：实验性功能开关当前状态，默认 false
  /// （不随应用重启持久化，与 002/003 既有"模型选择不跨重启持久化"约定一致）。
  bool speculativeDecodingEnabled() const { return m_speculativeDecodingEnabled; }
  /// 用户在设置页面切换开关时调用。若当前已有模型加载成功，立即按新的开关
  /// 状态触发一次与 switchModel() 相同的热重载流程（重新查表决定是否传入草稿
  /// 模型路径给 LlmEngine），使开关状态对下一次慢流生成立即生效。
  void setSpeculativeDecodingEnabled(bool enabled);

  /// 005-learning-tip-generation（T006，FR-008 默认开启，不持久化）。
  bool learningTipEnabled() const { return m_learningTipEnabled; }
  /// 用户在设置页面切换开关时调用（实现见 T007）：当且仅当切换为 false 且
  /// 当前已展示内容时，立即清空已展示的学习提示内容（FR-008"立即生效"）。
  void setLearningTipEnabled(bool enabled);

  FloatingSessionState sessionState() const { return m_state; }

  /// 供测试/集成装配注入依赖（非 QML 可见），均为非拥有裸指针语义以外的所有权转移。
  void setFastCorrector(std::unique_ptr<babel::fastpath::FastCorrector> corrector);
  void setDebounceScheduler(std::unique_ptr<babel::slowpath::DebounceScheduler> scheduler);
  void setLlmEngine(std::shared_ptr<babel::slowpath::LlmEngine> engine);
  void setClipboardWriter(std::unique_ptr<babel::inject::ClipboardWriter> writer);
  void setPasteInjector(std::unique_ptr<babel::inject::PasteInjector> injector);
  void setGlobalHotkey(std::unique_ptr<babel::hotkey::GlobalHotkey> hotkey);

 public slots:
  /// 每次击键后由 QML 调用（FR-003/FR-004/FR-005）。
  Q_INVOKABLE void submitKeystrokes(const QString &text);
  /// 回车确认（FR-006/FR-007/FR-008）。
  Q_INVOKABLE void confirm();
  /// Esc 或再次触发热键（FR-006）。
  Q_INVOKABLE void cancel();

  /// 浮窗右上角设置按钮触发；置 settingsVisible = true（FR-010）。
  Q_INVOKABLE void openSettings();
  /// 设置页面"返回"按钮触发；置 settingsVisible = false，回到主视图；MUST NOT
  /// 影响既有会话数据（FR-010/FR-014）。
  Q_INVOKABLE void closeSettings();
  /// 热切换至 `filePath` 指定的模型文件（research.md §8）：非 UI 线程构造新
  /// `LlmEngine`，加载失败时按 FR-013 执行级联回退直至低配默认模型。
  Q_INVOKABLE void switchModel(const QString &filePath);

  /// 由 GlobalHotkey 回调触发：首次唤起浮窗 / 已显示时再次按下则取消。
  void onHotkeyToggled();

  /// 周期性检查 DebounceScheduler 是否应触发慢流生成（由 bootstrapPlatformDefaults()
  /// 启动的轮询定时器驱动，默认构造不自动启动）。
  void pollSlowPath();

 signals:
  void sessionInvoked();
  void sessionClosed(babel::session::SessionController::CloseReason reason);

  void rawInputSlotTextChanged();
  void outputPreviewTextChanged();
  void outputPreviewStateChanged();
  void learningTipTextChanged();
  void learningTipVisibleChanged();
  void modelAvailableChanged();
  void manualPasteHintVisibleChanged();

  // 002-lattice-dict-model-switch（contracts/qml-cpp-session-interface.md）。
  void currentModelNameChanged();
  void modelSwitchingChanged();
  void availableModelsChanged();
  void fallbackNoticeTextChanged();
  void settingsVisibleChanged();
  /// switchModel() 流程结束时发出：是否成功、最终生效的模型名称（FR-014）。
  void modelSwitchCompleted(bool success, const QString &modelName);
  /// 自动回退发生时发出：提示文案（FR-013）。
  void modelFallbackTriggered(const QString &noticeText);
  /// 004-speculative-decoding（T022，contracts/qml-cpp-session-interface-
  /// increment.md 一）：speculativeDecodingNoticeText 取值变化时发出。
  void speculativeDecodingNoticeTextChanged();
  /// 004-speculative-decoding 事后修订：speculativeDecodingEnabled 取值变化时
  /// 发出（用户切换开关，或构造函数设置初始默认值 false 时均不 emit——仅属性
  /// 变化后才通知，构造期无 QML 绑定可供通知）。
  void speculativeDecodingEnabledChanged();

  /// 005-learning-tip-generation（T006）：learningTipEnabled 取值变化时发出
  /// （构造函数设置初始默认值 true 时不 emit，仅属性变化后才通知）。
  void learningTipEnabledChanged();

 private:
  void setState(FloatingSessionState next);
  void onSlowPathTriggered(const QString &inputSnapshot);
  void resetSessionData();
  void applyModelSwitchResult(std::shared_ptr<babel::slowpath::LlmEngine> engine, bool success,
                              const std::filesystem::path &finalModelPath, const QString &noticeText);
  /// 004-speculative-decoding（T011，data-model.md 三"具体宿主位置"）：目标
  /// 档位 -> 固定搭配草稿模型文件路径查表（供 switchModel()/
  /// bootstrapPlatformDefaults() 调用 LlmEngine 构造函数，以及
  /// speculativeDecodingNoticeText 提示文案计算共同复用）。Low/Unknown 档位、
  /// 或 m_speculativeDecodingEnabled == false 时恒返回空路径（事后修订：默认
  /// 关闭时不产生任何额外文件系统开销，与原 FR-005/FR-009 的"低配恒不启用"
  /// 判定逻辑共用同一条早退路径）。
  std::filesystem::path pairedDraftModelPathForTier(babel::slowpath::ModelDescriptor::TierHint tier) const;
  /// 004-speculative-decoding（T023，data-model.md 三"取值来源与计算规则"）：
  /// 按 `tier`/`draftModelPath` 重新计算 m_speculativeDecodingNoticeText，取值
  /// 变化时 emit speculativeDecodingNoticeTextChanged()。
  void updateSpeculativeDecodingNoticeText(babel::slowpath::ModelDescriptor::TierHint tier);

  FloatingSessionState m_state = FloatingSessionState::Hidden;
  RawDraftText m_rawDraft;
  NormalizedCandidateText m_normalizedCandidate;
  StandardOutputPreview m_outputPreview;
  babel::focus::TargetFocusContext m_targetFocus;

  QString m_learningTipText;
  bool m_learningTipVisible = false;
  bool m_modelAvailable = false;
  bool m_manualPasteHintVisible = false;
  bool m_settingsVisible = false;
  babel::slowpath::ActiveModelRuntimeState m_modelRuntimeState;
  /// 004-speculative-decoding（T022）：当前目标档位是否启用投机解码加速及
  /// 原因的说明文案（data-model.md 四）。
  QString m_speculativeDecodingNoticeText;
  /// 004-speculative-decoding 事后修订：实验性功能开关，默认 false（用户决策：
  /// 实测发现当前实现在本机反而变慢 1.8~2.6 倍，故不默认启用，仅保留为用户
  /// 可自行尝试的可选项，不跨应用重启持久化）。
  bool m_speculativeDecodingEnabled = false;

  /// 005-learning-tip-generation（T006，FR-008 默认开启，research.md §8 同类
  /// 先例，不跨应用重启持久化）。
  bool m_learningTipEnabled = true;

  std::unique_ptr<babel::fastpath::FastCorrector> m_fastCorrector;
  std::unique_ptr<babel::slowpath::DebounceScheduler> m_debounceScheduler;
  // 002-lattice-dict-model-switch（research.md §8）：由 unique_ptr 改为
  // shared_ptr，使 onSlowPathTriggered() 后台线程按值捕获的拷贝可延长旧实例
  // 生命周期至"在飞"调用结束，避免 switchModel() 热切换时发生 use-after-free。
  std::shared_ptr<babel::slowpath::LlmEngine> m_llmEngine;
  std::unique_ptr<babel::inject::ClipboardWriter> m_clipboardWriter;
  std::unique_ptr<babel::inject::PasteInjector> m_pasteInjector;
  std::unique_ptr<babel::hotkey::GlobalHotkey> m_globalHotkey;
  QTimer *m_pollTimer = nullptr;
};

}  // namespace babel::session
