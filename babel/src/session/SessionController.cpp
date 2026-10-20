#include "babel/src/session/SessionController.h"

#include <QMetaObject>
#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>
#include <spdlog/spdlog.h>
#include <sqlite3.h>

#include <algorithm>
#include <filesystem>
#include <system_error>
#include <thread>

#include "babel/src/fastpath/DictionaryPaths.h"
#include "babel/src/fastpath/DictionarySchema.h"
#include "babel/src/fastpath/FastCorrector.h"
#include "babel/src/fastpath/RimeDictLoader.h"
#include "babel/src/hotkey/GlobalHotkey.h"
#include "babel/src/inject/ClipboardWriter.h"
#include "babel/src/inject/PasteInjector.h"
#include "babel/src/slowpath/DebounceScheduler.h"
#include "babel/src/slowpath/LlmEngine.h"
#include "babel/src/slowpath/ModelCatalog.h"
#include "babel/src/slowpath/ModelPaths.h"

namespace babel::session {

SessionController::SessionController(QObject *parent) : QObject(parent) {
  m_pollTimer = new QTimer(this);
  m_pollTimer->setInterval(30);
  connect(m_pollTimer, &QTimer::timeout, this, &SessionController::pollSlowPath);
}

SessionController::~SessionController() {
  if (m_globalHotkey) {
    m_globalHotkey->unregisterHotkey();
  }
}

namespace {

/// 002-lattice-dict-model-switch（T011）：打开/建立约定词库 sqlite3 连接 →
/// ensureDictionarySchema() → RimeDictLoader 扫描约定目录写入 →
/// loadAllDictionaryEntries() 读取全部条目；目录为空或全部文件解析失败（条目数
/// 为 0）时回退 builtinSeedDictionary()（FR-005，data-model.md §5）。
std::vector<babel::fastpath::DictionaryEntry> loadProductionDictionaryEntries() {
  const auto dictDir = babel::fastpath::DictionaryPaths::conventionalDictionaryDirectory();
  std::error_code ec;
  std::filesystem::create_directories(dictDir, ec);
  if (ec) {
    spdlog::error("SessionController: 创建约定词库目录失败 {}: {}", dictDir.string(), ec.message());
  }

  // 词典 sqlite3 索引数据库与约定词库目录同级（内部实现细节，宪章原则 IV）。
  const std::filesystem::path dbPath = dictDir.parent_path() / "dictionary.sqlite3";

  std::vector<babel::fastpath::DictionaryEntry> entries;
  sqlite3 *db = nullptr;
  if (sqlite3_open(dbPath.string().c_str(), &db) == SQLITE_OK && db != nullptr &&
      babel::fastpath::ensureDictionarySchema(db)) {
    babel::fastpath::RimeDictLoader::loadDictionaryDirectory(db, dictDir);
    entries = babel::fastpath::loadAllDictionaryEntries(db);
  } else {
    spdlog::error("SessionController: 打开词典 sqlite3 连接/建表失败（{}）", dbPath.string());
  }
  if (db) {
    sqlite3_close(db);
  }

  if (entries.empty()) {
    spdlog::warn(
        "SessionController: 约定词库目录为空或全部文件解析失败，回退 builtinSeedDictionary()（FR-005 最后兜底）");
    entries = babel::fastpath::builtinSeedDictionary();
  } else {
    spdlog::info("SessionController: 已从约定词库目录加载 {} 条真实词库条目（{}）", entries.size(),
                 dictDir.string());
  }
  return entries;
}

/// 004-speculative-decoding（T012，contracts/model-catalog-draft-pairing-
/// internal-contract.md 三）：与 SessionController::pairedDraftModelPathForTier()
/// 共享的核心查表逻辑，提取为自由函数以便 switchModel() 后台线程直接调用（不
/// 跨线程访问 SessionController 实例本身，`this` 指针/成员状态均不涉及，纯
/// 静态 ModelCatalog 查表逻辑，线程安全）。
///
/// 事后修订（用户决策）：新增 `speculativeDecodingEnabled` 参数，默认关闭时
/// 恒返回空路径，即使已存在匹配的草稿模型文件——因实测当前实现在本机反而显
/// 著变慢（见 quickstart.md"实测记录"），故改为用户需在设置页面显式开启后才
/// 会实际启用投机解码，而非"检测到草稿模型文件即自动启用"。
std::filesystem::path lookupPairedDraftModelPath(babel::slowpath::ModelDescriptor::TierHint tier,
                                                  bool speculativeDecodingEnabled) {
  if (!speculativeDecodingEnabled) {
    return {};
  }
  if (tier != babel::slowpath::ModelDescriptor::TierHint::Medium &&
      tier != babel::slowpath::ModelDescriptor::TierHint::High) {
    return {};
  }
  const auto neededDraft = babel::slowpath::ModelCatalog::pairedDraftCandidate(tier);
  for (const auto &descriptor : babel::slowpath::ModelCatalog::scan()) {
    if (babel::slowpath::ModelCatalog::classifyDraftCandidateByFileName(descriptor.fileName) == neededDraft) {
      return descriptor.filePath;
    }
  }
  return {};
}

}  // namespace

void SessionController::bootstrapPlatformDefaults() {
  m_fastCorrector = std::make_unique<babel::fastpath::FastCorrector>(loadProductionDictionaryEntries());

  const auto defaultModelPath = babel::slowpath::ModelPaths::conventionalModelFilePath();
  // 004-speculative-decoding（T013，contracts/llm-engine-speculative-internal-
  // contract.md 四）：默认（低配）档位查表得到草稿模型路径（恒为空路径，
  // FR-005/FR-009），一并传给 LlmEngine 构造函数。
  const auto defaultModelFileName = QString::fromStdString(defaultModelPath.filename().string());
  const auto defaultTier = babel::slowpath::ModelCatalog::classifyTierByFileName(defaultModelFileName);
  const auto defaultDraftModelPath = pairedDraftModelPathForTier(defaultTier);
  auto llmEngine = std::make_shared<babel::slowpath::LlmEngine>(defaultModelPath, defaultDraftModelPath);
  m_modelAvailable = llmEngine->modelAvailable();
  m_llmEngine = std::move(llmEngine);
  m_modelRuntimeState.currentModelPath = defaultModelPath;
  if (m_modelAvailable) {
    m_modelRuntimeState.fallbackHistory.push_back(defaultModelPath);
  }
  emit modelAvailableChanged();
  // 004-speculative-decoding（T023）：默认档位提示文案计算（Low/Unknown 恒为空）。
  updateSpeculativeDecodingNoticeText(defaultTier);

  // 防抖窗口按当前模型档位动态确定（FR-010/FR-011，contracts/debounce-tier-
  // window-internal-contract.md 四"启动调用点"）：默认低配模型恒为 Low/200ms，
  // 与 001/002 既有体验一致；复用 ModelCatalog 分档逻辑，不新增平行判断机制。
  const auto defaultDebounceWindow = babel::slowpath::ModelCatalog::tierDebounceWindow(defaultTier);
  m_debounceScheduler = std::make_unique<babel::slowpath::DebounceScheduler>(
      defaultDebounceWindow, [this](const QString &snapshot) { onSlowPathTriggered(snapshot); });

  m_clipboardWriter = std::make_unique<babel::inject::ClipboardWriter>();
  m_pasteInjector = babel::inject::createPlatformPasteInjector();

  m_globalHotkey = babel::hotkey::createPlatformGlobalHotkey();
  if (m_globalHotkey) {
    const bool hotkeyRegistered =
        m_globalHotkey->registerHotkey(babel::hotkey::HotkeyBinding::defaultBinding(), [this]() {
          // 热键回调运行在平台事件线程上，跨线程安全地切回 Qt 主线程再处理状态变更。
          QMetaObject::invokeMethod(this, "onHotkeyToggled", Qt::QueuedConnection);
        });
    // 契约（global-hotkey-contract.md）MUST NOT 发明用户可见的冲突提示，
    // 此处仅记录内部诊断日志，便于排查"热键无响应"问题（如与系统/其他应用已占用的组合键冲突）。
    if (hotkeyRegistered) {
      spdlog::info("全局热键注册成功（默认组合键）");
    } else {
      spdlog::warn("全局热键注册失败：可能与系统或其他应用已占用的快捷键冲突，浮窗将无法通过热键唤起");
    }
  } else {
    spdlog::warn("当前平台未提供全局热键实现，浮窗将无法通过热键唤起");
  }

  m_pollTimer->start();
}

void SessionController::setFastCorrector(std::unique_ptr<babel::fastpath::FastCorrector> corrector) {
  m_fastCorrector = std::move(corrector);
}

void SessionController::setDebounceScheduler(std::unique_ptr<babel::slowpath::DebounceScheduler> scheduler) {
  m_debounceScheduler = std::move(scheduler);
}

void SessionController::setLlmEngine(std::shared_ptr<babel::slowpath::LlmEngine> engine) {
  m_modelAvailable = engine && engine->modelAvailable();
  m_llmEngine = std::move(engine);
  emit modelAvailableChanged();
}

void SessionController::setClipboardWriter(std::unique_ptr<babel::inject::ClipboardWriter> writer) {
  m_clipboardWriter = std::move(writer);
}

void SessionController::setPasteInjector(std::unique_ptr<babel::inject::PasteInjector> injector) {
  m_pasteInjector = std::move(injector);
}

void SessionController::setGlobalHotkey(std::unique_ptr<babel::hotkey::GlobalHotkey> hotkey) {
  if (m_globalHotkey) {
    m_globalHotkey->unregisterHotkey();
  }
  m_globalHotkey = std::move(hotkey);
}

QString SessionController::currentModelName() const {
  if (m_modelRuntimeState.currentModelPath.empty()) {
    return QString();
  }
  return QString::fromStdString(m_modelRuntimeState.currentModelPath.filename().string());
}

QVariantList SessionController::availableModels() const {
  QVariantList list;
  for (const auto &descriptor : babel::slowpath::ModelCatalog::scan()) {
    QVariantMap entry;
    entry[QStringLiteral("filePath")] = QString::fromStdString(descriptor.filePath.string());
    entry[QStringLiteral("fileName")] = descriptor.fileName;
    entry[QStringLiteral("sizeBytes")] = static_cast<qlonglong>(descriptor.sizeBytes);
    QString tierStr;
    switch (descriptor.tierHint) {
      case babel::slowpath::ModelDescriptor::TierHint::Low:
        tierStr = QStringLiteral("Low");
        break;
      case babel::slowpath::ModelDescriptor::TierHint::Medium:
        tierStr = QStringLiteral("Medium");
        break;
      case babel::slowpath::ModelDescriptor::TierHint::High:
        tierStr = QStringLiteral("High");
        break;
      case babel::slowpath::ModelDescriptor::TierHint::Unknown:
        tierStr = QStringLiteral("Unknown");
        break;
    }
    entry[QStringLiteral("tierHint")] = tierStr;
    entry[QStringLiteral("availability")] =
        descriptor.availability == babel::slowpath::ModelDescriptor::Availability::KnownUnloadable
            ? QStringLiteral("KnownUnloadable")
            : (descriptor.availability == babel::slowpath::ModelDescriptor::Availability::Verified
                   ? QStringLiteral("Verified")
                   : QStringLiteral("Unverified"));
    list.push_back(entry);
  }
  return list;
}

std::vector<babel::slowpath::RecommendedModelProfile> SessionController::recommendedProfiles() const {
  return babel::slowpath::ModelCatalog::recommendedProfiles();
}

QVariantList SessionController::recommendedProfilesVariant() const {
  QVariantList list;
  for (const auto &profile : recommendedProfiles()) {
    QVariantMap entry;
    QString tierStr;
    switch (profile.tier) {
      case babel::slowpath::RecommendedModelProfile::Tier::Low:
        tierStr = QStringLiteral("Low");
        break;
      case babel::slowpath::RecommendedModelProfile::Tier::Medium:
        tierStr = QStringLiteral("Medium");
        break;
      case babel::slowpath::RecommendedModelProfile::Tier::High:
        tierStr = QStringLiteral("High");
        break;
    }
    entry[QStringLiteral("tier")] = tierStr;
    entry[QStringLiteral("modelName")] = profile.modelName;
    entry[QStringLiteral("quantization")] = profile.quantization;
    entry[QStringLiteral("approxSizeDescription")] = profile.approxSizeDescription;
    entry[QStringLiteral("targetHardwareDescription")] = profile.targetHardwareDescription;
    list.push_back(entry);
  }
  return list;
}

std::vector<babel::slowpath::RecommendedDraftModelProfile> SessionController::recommendedDraftProfiles() const {
  // 004-speculative-decoding（T018，contracts/qml-cpp-session-interface-
  // increment.md 一）：直接转发 ModelCatalog::recommendedDraftProfiles()。
  return babel::slowpath::ModelCatalog::recommendedDraftProfiles();
}

QVariantList SessionController::recommendedDraftProfilesVariant() const {
  QVariantList list;
  for (const auto &profile : recommendedDraftProfiles()) {
    QVariantMap entry;
    QString sizeStr;
    switch (profile.size) {
      case babel::slowpath::ModelDescriptor::DraftCandidate::Size0_5B:
        sizeStr = QStringLiteral("Size0_5B");
        break;
      case babel::slowpath::ModelDescriptor::DraftCandidate::Size1_5B:
        sizeStr = QStringLiteral("Size1_5B");
        break;
      case babel::slowpath::ModelDescriptor::DraftCandidate::None:
      default:
        sizeStr = QStringLiteral("None");
        break;
    }
    entry[QStringLiteral("size")] = sizeStr;
    entry[QStringLiteral("modelName")] = profile.modelName;
    entry[QStringLiteral("quantization")] = profile.quantization;
    entry[QStringLiteral("approxSizeDescription")] = profile.approxSizeDescription;
    list.push_back(entry);
  }
  return list;
}

std::filesystem::path SessionController::pairedDraftModelPathForTier(
    babel::slowpath::ModelDescriptor::TierHint tier) const {
  // 004-speculative-decoding（T012，contracts/model-catalog-draft-pairing-
  // internal-contract.md 三，FR-005/FR-009）：转发匿名命名空间内与 switchModel()
  // 后台线程共享的核心查表逻辑，避免重复定义（Low/Unknown 档位或
  // m_speculativeDecodingEnabled == false 时恒返回空路径，不调用
  // ModelCatalog::scan()，不产生任何额外文件系统开销）。
  return lookupPairedDraftModelPath(tier, m_speculativeDecodingEnabled);
}

void SessionController::updateSpeculativeDecodingNoticeText(babel::slowpath::ModelDescriptor::TierHint tier) {
  // 004-speculative-decoding（T023，data-model.md 三/四）：Low/Unknown 恒为空
  // 字符串（FR-009，不展示任何提示）。
  //
  // 事后修订（用户决策）：Medium/High 档位下，提示文案需先区分"实验性功能开关
  // 是否已开启"（m_speculativeDecodingEnabled），再区分"开启后是否已配置对应
  // 草稿模型文件"——两者分别对应不同的用户行动指引（前者需去设置页面开启开关，
  // 后者需去下载对应草稿模型文件），故此处不能直接调用会因开关关闭而恒返回
  // 空路径的 pairedDraftModelPathForTier()，须分别判断。
  QString noticeText;
  if (tier == babel::slowpath::ModelDescriptor::TierHint::Medium ||
      tier == babel::slowpath::ModelDescriptor::TierHint::High) {
    if (!m_speculativeDecodingEnabled) {
      noticeText = QStringLiteral("投机解码为实验性功能，默认未启用（可在本页面开启尝试）");
    } else {
      const auto draftPath = pairedDraftModelPathForTier(tier);
      if (draftPath.empty()) {
        noticeText = QStringLiteral("已开启投机解码，但未检测到匹配的草稿模型文件，当前仍使用普通生成路径");
      }
      // 已配置时 noticeText 保持为空字符串（无需提示，`data-model.md` 四已注明
      // "可选地展示正向提示"，本实现选择不展示以保持设置页面简洁）。
    }
  }
  if (noticeText != m_speculativeDecodingNoticeText) {
    m_speculativeDecodingNoticeText = noticeText;
    emit speculativeDecodingNoticeTextChanged();
  }
}

void SessionController::setSpeculativeDecodingEnabled(bool enabled) {
  if (m_speculativeDecodingEnabled == enabled) {
    return;
  }
  m_speculativeDecodingEnabled = enabled;
  emit speculativeDecodingEnabledChanged();

  // 立即按新开关状态重新计算当前档位的提示文案；若当前已有模型成功加载，
  // 复用既有 switchModel() 热重载流程对当前模型路径"重新加载自己"，使开关
  // 状态对下一次慢流生成立即生效（而非要求用户手动再切换一次模型）。
  const auto currentFileName = QString::fromStdString(m_modelRuntimeState.currentModelPath.filename().string());
  const auto currentTier = babel::slowpath::ModelCatalog::classifyTierByFileName(currentFileName);
  updateSpeculativeDecodingNoticeText(currentTier);
  if (m_modelAvailable && !m_modelRuntimeState.currentModelPath.empty()) {
    switchModel(QString::fromStdString(m_modelRuntimeState.currentModelPath.string()));
  }
}


void SessionController::openSettings() {
  m_settingsVisible = true;
  emit settingsVisibleChanged();
}

void SessionController::closeSettings() {
  m_settingsVisible = false;
  emit settingsVisibleChanged();
}

void SessionController::switchModel(const QString &filePath) {
  // 契约（qml-cpp-session-interface.md §五）：同一时刻只会有一次切换在进行，
  // 切换中再次调用 MUST 被忽略（简单起见选择忽略而非排队，不改变外部可观察
  // 行为——"同一时刻只会有一次切换在进行"本身才是契约要求）。
  if (m_modelRuntimeState.isSwitching) {
    spdlog::warn("SessionController::switchModel() 调用被忽略：已有切换在进行中（{}）", filePath.toStdString());
    return;
  }
  m_modelRuntimeState.isSwitching = true;
  emit modelSwitchingChanged();

  const std::filesystem::path targetPath(filePath.toStdString());
  const QString originalFileName = QString::fromStdString(targetPath.filename().string());
  // 级联回退候选链（research.md §8，FR-013）：此次应用运行期间已知成功加载过
  // 的模型路径（按时间倒序尝试），最终追加低配默认模型兜底。
  std::vector<std::filesystem::path> fallbackCandidates(m_modelRuntimeState.fallbackHistory.rbegin(),
                                                          m_modelRuntimeState.fallbackHistory.rend());
  const auto defaultModelPath = babel::slowpath::ModelPaths::conventionalModelFilePath();
  if (std::find(fallbackCandidates.begin(), fallbackCandidates.end(), defaultModelPath) ==
      fallbackCandidates.end()) {
    fallbackCandidates.push_back(defaultModelPath);
  }

  QPointer<SessionController> self(this);
  const bool speculativeDecodingEnabled = m_speculativeDecodingEnabled;
  std::thread([self, targetPath, originalFileName, fallbackCandidates, speculativeDecodingEnabled]() {
    // 004-speculative-decoding（T014，contracts/llm-engine-speculative-internal-
    // contract.md 四）：对初次尝试加载的 targetPath 与级联回退候选分别查表得到
    // 草稿模型路径一并传给 LlmEngine 构造函数；查表仅依赖静态 ModelCatalog 方法
    // （lookupPairedDraftModelPath()，见本文件匿名命名空间），不跨线程访问
    // SessionController 实例本身——`speculativeDecodingEnabled` 已在主线程按值
    // 拷贝进捕获列表，而非在本线程读取 `self->m_speculativeDecodingEnabled`。
    const auto targetFileName = QString::fromStdString(targetPath.filename().string());
    const auto targetTier = babel::slowpath::ModelCatalog::classifyTierByFileName(targetFileName);
    const auto targetDraftPath = lookupPairedDraftModelPath(targetTier, speculativeDecodingEnabled);
    auto newEngine = std::make_shared<babel::slowpath::LlmEngine>(targetPath, targetDraftPath);
    bool success = newEngine->modelAvailable();
    std::filesystem::path finalPath = targetPath;
    QString noticeText;

    if (!success) {
      for (const auto &candidatePath : fallbackCandidates) {
        if (candidatePath == targetPath) {
          continue;
        }
        const auto candidateFileName0 = QString::fromStdString(candidatePath.filename().string());
        const auto candidateTier = babel::slowpath::ModelCatalog::classifyTierByFileName(candidateFileName0);
        const auto candidateDraftPath = lookupPairedDraftModelPath(candidateTier, speculativeDecodingEnabled);
        auto candidateEngine = std::make_shared<babel::slowpath::LlmEngine>(candidatePath, candidateDraftPath);
        if (candidateEngine->modelAvailable()) {
          newEngine = candidateEngine;
          success = true;
          finalPath = candidatePath;
          const QString candidateFileName = QString::fromStdString(candidatePath.filename().string());
          // FR-013 原文模板文案。
          noticeText = QStringLiteral("模型 %1 当前不可用，已自动切换回 %2 继续使用")
                           .arg(originalFileName, candidateFileName);
          break;
        }
      }
    }

    if (!self) {
      return;
    }
    QMetaObject::invokeMethod(
        self.data(),
        [self, newEngine, success, finalPath, noticeText]() {
          if (!self) {
            return;
          }
          self->applyModelSwitchResult(newEngine, success, success ? finalPath : std::filesystem::path{},
                                        noticeText);
        },
        Qt::QueuedConnection);
  }).detach();
}

void SessionController::applyModelSwitchResult(std::shared_ptr<babel::slowpath::LlmEngine> engine, bool success,
                                                const std::filesystem::path &finalModelPath,
                                                const QString &noticeText) {
  m_llmEngine = std::move(engine);
  m_modelAvailable = success;
  if (success) {
    m_modelRuntimeState.currentModelPath = finalModelPath;
    const auto &history = m_modelRuntimeState.fallbackHistory;
    if (std::find(history.begin(), history.end(), finalModelPath) == history.end()) {
      m_modelRuntimeState.fallbackHistory.push_back(finalModelPath);
    }
    // 热切换完成调用点（contracts/debounce-tier-window-internal-contract.md
    // 四）：按新模型所属档位同步调整防抖窗口时长；success == false 分支 MUST
    // NOT 调用该流程。测试（如 SwitchModelToValidModelUpdatesStateAndEmits-
    // Completion）经 setLlmEngine() 直接构造 SessionController 且未调用
    // setDebounceScheduler()，此时 m_debounceScheduler 为 nullptr，须守卫。
    if (m_debounceScheduler) {
      const auto finalModelFileName = QString::fromStdString(finalModelPath.filename().string());
      const auto tier = babel::slowpath::ModelCatalog::classifyTierByFileName(finalModelFileName);
      m_debounceScheduler->setDebounceWindow(babel::slowpath::ModelCatalog::tierDebounceWindow(tier));
    }
    // 004-speculative-decoding（T023，data-model.md 三/四，Edge Cases 第一条）：
    // 切换成功后按新模型所属档位重新计算投机解码提示文案，确保切换档位后自动
    // 重新匹配、不沿用旧档位状态。
    {
      const auto finalModelFileName = QString::fromStdString(finalModelPath.filename().string());
      const auto tier = babel::slowpath::ModelCatalog::classifyTierByFileName(finalModelFileName);
      updateSpeculativeDecodingNoticeText(tier);
    }
  }
  m_modelRuntimeState.lastFallbackNotice = noticeText;
  m_modelRuntimeState.isSwitching = false;

  emit modelSwitchingChanged();
  emit modelAvailableChanged();
  emit currentModelNameChanged();
  emit fallbackNoticeTextChanged();
  if (!noticeText.isEmpty()) {
    emit modelFallbackTriggered(noticeText);
  }
  emit modelSwitchCompleted(success, currentModelName());
}

QString SessionController::outputPreviewState() const {
  switch (m_outputPreview.generationState) {
    case GenerationState::Idle:
      return QStringLiteral("Idle");
    case GenerationState::Debouncing:
      return QStringLiteral("Debouncing");
    case GenerationState::Generating:
      return QStringLiteral("Generating");
    case GenerationState::Ready:
      return QStringLiteral("Ready");
    case GenerationState::Stale:
      return QStringLiteral("Stale");
  }
  return QStringLiteral("Idle");
}

void SessionController::setState(FloatingSessionState next) { m_state = next; }

void SessionController::resetSessionData() {
  m_rawDraft = RawDraftText{};
  m_normalizedCandidate = NormalizedCandidateText{};
  m_outputPreview = StandardOutputPreview{};
  m_manualPasteHintVisible = false;
  emit rawInputSlotTextChanged();
  emit outputPreviewTextChanged();
  emit outputPreviewStateChanged();
  emit manualPasteHintVisibleChanged();
}

void SessionController::onHotkeyToggled() {
  // 诊断日志：确认热键按下事件确实从平台层传递到此处（排查"注册成功但无响应"问题）。
  spdlog::info("收到全局热键触发回调，当前会话状态 = {}", static_cast<int>(m_state));
  if (m_state == FloatingSessionState::Hidden) {
    m_targetFocus = babel::focus::captureCurrentFocus();
    resetSessionData();
    setState(m_modelAvailable ? FloatingSessionState::Invoked : FloatingSessionState::ModelUnavailable);
    emit sessionInvoked();
  } else {
    cancel();
  }
}

void SessionController::submitKeystrokes(const QString &text) {
  if (m_state == FloatingSessionState::Hidden) {
    return;
  }
  m_rawDraft.content = text;
  m_rawDraft.lastKeystrokeAt = std::chrono::system_clock::now();

  // 002-lattice-dict-model-switch（T020）：快流产出改为完整 PinyinLattice（而非
  // 单一候选字符串），rawInputSlotText 继续展示 lattice.toDisplayText()（对外
  // 行为与 001 等价，FR-008），新增 lattice 字段承载多候选信息供慢流消歧使用。
  const babel::fastpath::PinyinLattice lattice =
      m_fastCorrector ? m_fastCorrector->buildLattice(text) : babel::fastpath::PinyinLattice{};
  m_normalizedCandidate.content = m_fastCorrector ? lattice.toDisplayText() : text;
  m_normalizedCandidate.lattice = lattice;
  m_normalizedCandidate.updatedAt = std::chrono::system_clock::now();
  emit rawInputSlotTextChanged();

  // T022（research.md §6，FR-008）：对每个存在真实歧义的格位输出开发诊断日志，
  // 仅供排查/效果分析使用，不面向终端用户、不写入任何 QML 可见属性。
  if (m_fastCorrector) {
    for (const auto &cell : lattice.cells) {
      if (!cell.isAmbiguous()) {
        continue;
      }
      QStringList parts;
      for (const auto &candidate : cell.candidates) {
        parts << QStringLiteral("%1:%2").arg(candidate.surfaceForm).arg(candidate.normalizedWeight, 0, 'f', 2);
      }
      spdlog::debug("[Lattice] pos={} token={} candidates=[{}]", cell.tokenIndex,
                    cell.originalToken.toStdString(), parts.join(QStringLiteral(", ")).toStdString());
    }
  }

  if (m_state == FloatingSessionState::Invoked) {
    setState(FloatingSessionState::Editing);
  }

  if (m_modelAvailable && m_debounceScheduler) {
    // FR-007：DebounceScheduler 最终转交给 LlmEngine::generate() 的"输入快照"
    // 改为 lattice.toAnnotatedPromptText()（携带 ⟦候选|权重⟧ 结构化标注），
    // 而非规整化展示字符串，供慢流结合上下文消歧（contracts/
    // llm-prompt-annotation-contract.md）。
    const QString slowPathInput = m_fastCorrector ? lattice.toAnnotatedPromptText() : text;
    m_debounceScheduler->onInputChanged(slowPathInput);
    m_outputPreview.generationState = m_debounceScheduler->state();
    emit outputPreviewStateChanged();
  }
}

void SessionController::pollSlowPath() {
  if (!m_debounceScheduler) {
    return;
  }
  const auto before = m_debounceScheduler->state();
  m_debounceScheduler->tick();
  const auto after = m_debounceScheduler->state();
  if (before != after) {
    m_outputPreview.generationState = after;
    emit outputPreviewStateChanged();
  }
}

void SessionController::onSlowPathTriggered(const QString &inputSnapshot) {
  m_outputPreview.generationState = babel::session::GenerationState::Generating;
  m_outputPreview.requestedAt = std::chrono::system_clock::now();
  emit outputPreviewStateChanged();

  if (!m_modelAvailable || !m_llmEngine) {
    // 不应到达（modelAvailable=false 时不驱动慢流，见 bootstrapPlatformDefaults()/
    // submitKeystrokes() 的 m_modelAvailable 判定），此处仅作防御性处理。
    if (m_debounceScheduler) {
      m_debounceScheduler->markGenerationComplete();
    }
    return;
  }

  // 在后台线程执行推理，避免阻塞 UI 线程；结果通过 QueuedConnection 回传主线程，
  // 并以 QPointer 守护 SessionController 可能已被销毁（如浮窗已关闭/应用退出）的情况。
  // 002-lattice-dict-model-switch（research.md §8）：按值捕获 shared_ptr 拷贝
  // （而非裸指针），使该 LlmEngine 实例的生命周期由"是否仍有引用持有"决定，
  // 避免 switchModel() 热切换时旧实例被提前析构导致 use-after-free。
  std::shared_ptr<babel::slowpath::LlmEngine> engine = m_llmEngine;
  QPointer<SessionController> self(this);
  std::thread([self, engine, inputSnapshot]() {
    const QString generated = engine->generate(inputSnapshot);
    if (!self) {
      return;
    }
    QMetaObject::invokeMethod(
        self.data(),
        [self, generated]() {
          if (!self) {
            return;
          }
          self->m_outputPreview.content = generated;
          self->m_outputPreview.readyAt = std::chrono::system_clock::now();
          if (self->m_debounceScheduler) {
            self->m_debounceScheduler->markGenerationComplete();
            self->m_outputPreview.generationState = self->m_debounceScheduler->state();
          } else {
            self->m_outputPreview.generationState = babel::session::GenerationState::Ready;
          }
          emit self->outputPreviewTextChanged();
          emit self->outputPreviewStateChanged();
        },
        Qt::QueuedConnection);
  }).detach();
}

void SessionController::confirm() {
  if (m_state == FloatingSessionState::Hidden) {
    return;
  }
  if (m_clipboardWriter) {
    m_clipboardWriter->write(m_outputPreview.content);
  }

  babel::inject::InjectionOutcome outcome = babel::inject::InjectionOutcome::OtherFailure;
  if (m_pasteInjector) {
    outcome = m_pasteInjector->attemptPaste(m_targetFocus).outcome;
  }

  if (outcome == babel::inject::InjectionOutcome::Succeeded) {
    // 成功路径：浮窗正常关闭，不展示额外提示（contracts/injection-outcome-contract.md）。
    m_manualPasteHintVisible = false;
    emit manualPasteHintVisibleChanged();
    setState(FloatingSessionState::Confirmed);
    emit sessionClosed(CloseReason::Confirmed);
    setState(FloatingSessionState::Hidden);
  } else {
    // 非 Succeeded（UnsupportedEnvironment/PermissionDenied/OtherFailure）：
    // MUST 统一展示"已复制到剪贴板，请手动粘贴"，不区分具体 outcome（FR-009/FR-012）。
    // 浮窗保持可见以便用户看到提示，由用户按 Esc/再次触发热键（cancel()）关闭。
    m_manualPasteHintVisible = true;
    emit manualPasteHintVisibleChanged();
    setState(FloatingSessionState::Confirmed);
  }
}

void SessionController::cancel() {
  if (m_state == FloatingSessionState::Hidden) {
    return;
  }
  setState(FloatingSessionState::Cancelled);
  emit sessionClosed(CloseReason::Cancelled);
  resetSessionData();
  setState(FloatingSessionState::Hidden);
}

}  // namespace babel::session
