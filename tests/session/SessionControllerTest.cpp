#include <gtest/gtest.h>

#include <QCoreApplication>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "babel/src/fastpath/FastCorrector.h"
#include "babel/src/inject/ClipboardWriter.h"
#include "babel/src/inject/PasteInjector.h"
#include "babel/src/session/SessionController.h"
#include "babel/src/slowpath/DebounceScheduler.h"
#include "babel/src/slowpath/LlmEngine.h"
#include "babel/src/slowpath/ModelPaths.h"

using babel::inject::ClipboardWriter;
using babel::inject::InjectionAttemptResult;
using babel::inject::InjectionOutcome;
using babel::inject::PasteInjector;
using babel::session::FloatingSessionState;
using babel::session::SessionController;

namespace {

/// 确保测试进程存在一个 QCoreApplication 实例（Qt 事件循环所需），供
/// switchModel() 内部 QMetaObject::invokeMethod(..., Qt::QueuedConnection) 的
/// 跨线程回调得以被 pumpEventsUntil() 实际投递、执行。本文件内全部用例共享同一
/// 实例（进程生命周期内只构造一次）。
void ensureCoreApplication() {
  if (!QCoreApplication::instance()) {
    static int argc = 1;
    static char argv0[] = "SessionControllerTest";
    static char *argv[] = {argv0, nullptr};
    static QCoreApplication app(argc, argv);
  }
}

/// 反复驱动 Qt 事件循环直至 predicate 为真或超时，供测试同步等待
/// switchModel() 后台线程经 QueuedConnection 回传主线程的结果。
template <typename Predicate>
bool pumpEventsUntil(Predicate predicate, std::chrono::milliseconds timeout = std::chrono::seconds(10)) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return predicate();
}

/// 构造内容为合法 GGUF 文件头魔数的最小占位文件（非真实可用模型，仅用于驱动
/// ModelCatalog/LlmEngine 加载路径的结构性测试，不要求真实推理）。
std::filesystem::path writeFakeFile(const std::filesystem::path &path, const std::string &content) {
  std::ofstream ofs(path, std::ios::binary);
  ofs << content;
  return path;
}

}  // namespace

namespace {

/// 记录调用顺序的剪贴板写入测试替身（不依赖真实 QClipboard/QGuiApplication）。
class RecordingClipboardWriter : public ClipboardWriter {
 public:
  explicit RecordingClipboardWriter(std::vector<std::string> *log) : m_log(log) {}
  void write(const QString &text) override { m_log->push_back("clipboard:" + text.toStdString()); }

 private:
  std::vector<std::string> *m_log;
};

/// 记录调用顺序、返回预设 outcome 的粘贴注入测试替身。
class RecordingPasteInjector : public PasteInjector {
 public:
  RecordingPasteInjector(std::vector<std::string> *log, InjectionOutcome outcome) : m_log(log), m_outcome(outcome) {}
  InjectionAttemptResult attemptPaste(const babel::focus::TargetFocusContext & /*targetFocus*/) override {
    m_log->push_back("paste");
    InjectionAttemptResult result;
    result.outcome = m_outcome;
    return result;
  }

 private:
  std::vector<std::string> *m_log;
  InjectionOutcome m_outcome;
};

/// 构造一个已"唤起"（非 Hidden）的 SessionController，仅注入
/// ClipboardWriter/PasteInjector 测试替身；不装配 GlobalHotkey/FastCorrector/
/// DebounceScheduler/LlmEngine（measurement 聚焦 confirm()/cancel() 本身）。
std::unique_ptr<SessionController> buildInvokedController(std::vector<std::string> *log, InjectionOutcome outcome) {
  auto controller = std::make_unique<SessionController>();
  controller->setClipboardWriter(std::make_unique<RecordingClipboardWriter>(log));
  controller->setPasteInjector(std::make_unique<RecordingPasteInjector>(log, outcome));
  controller->onHotkeyToggled();  // Hidden -> Invoked/ModelUnavailable
  controller->submitKeystrokes(QStringLiteral("草稿"));
  return controller;
}

}  // namespace

// T019 [US1]：SessionController.confirm()/cancel() 状态迁移与"剪贴板写入先于
// 粘贴注入"调用顺序测试（contracts/injection-outcome-contract.md）。

TEST(SessionControllerTest, ConfirmWritesClipboardBeforeAttemptingPaste) {
  std::vector<std::string> log;
  auto controller = buildInvokedController(&log, InjectionOutcome::Succeeded);
  controller->confirm();
  ASSERT_EQ(log.size(), 2u);
  EXPECT_EQ(log[0].rfind("clipboard:", 0), 0u);  // 剪贴板写入 MUST 先于粘贴注入
  EXPECT_EQ(log[1], "paste");
}

TEST(SessionControllerTest, ConfirmSucceededClosesWithoutManualPasteHint) {
  std::vector<std::string> log;
  auto controller = buildInvokedController(&log, InjectionOutcome::Succeeded);
  controller->confirm();
  EXPECT_EQ(controller->sessionState(), FloatingSessionState::Hidden);
  EXPECT_FALSE(controller->manualPasteHintVisible());
}

TEST(SessionControllerTest, ConfirmNonSucceededShowsUnifiedManualPasteHint) {
  for (const auto outcome :
       {InjectionOutcome::UnsupportedEnvironment, InjectionOutcome::PermissionDenied, InjectionOutcome::OtherFailure}) {
    std::vector<std::string> log;
    auto controller = buildInvokedController(&log, outcome);
    controller->confirm();
    EXPECT_TRUE(controller->manualPasteHintVisible());
    // 剪贴板写入仍 MUST 已完成（FR-012：即便注入失败，用户仍可手动粘贴）。
    ASSERT_FALSE(log.empty());
    EXPECT_EQ(log.front().rfind("clipboard:", 0), 0u);
  }
}

TEST(SessionControllerTest, CancelProducesNoClipboardOrPasteAction) {
  std::vector<std::string> log;
  auto controller = buildInvokedController(&log, InjectionOutcome::Succeeded);
  controller->cancel();
  EXPECT_TRUE(log.empty());
  EXPECT_EQ(controller->sessionState(), FloatingSessionState::Hidden);
}

// T026 [US3]：switchModel()/openSettings()/closeSettings() 行为与
// modelSwitching/currentModelName/availableModels/recommendedProfiles/
// fallbackNoticeText/settingsVisible 属性、modelSwitchCompleted/
// modelFallbackTriggered 信号（contracts/qml-cpp-session-interface.md）。

TEST(SessionControllerTest, OpenCloseSettingsToggleVisibilityWithoutSideEffects) {
  ensureCoreApplication();
  SessionController controller;
  EXPECT_FALSE(controller.settingsVisible());
  controller.openSettings();
  EXPECT_TRUE(controller.settingsVisible());
  controller.closeSettings();
  EXPECT_FALSE(controller.settingsVisible());
}

TEST(SessionControllerTest, RecommendedProfilesExposesFixedThreeTiers) {
  ensureCoreApplication();
  SessionController controller;
  const auto profiles = controller.recommendedProfiles();
  EXPECT_EQ(profiles.size(), 3);
}

// T020 [US2]：004-speculative-decoding（contracts/qml-cpp-session-interface-
// increment.md 一，FR-004）。
TEST(SessionControllerTest, RecommendedDraftProfilesExposesFixedTwoCandidates) {
  ensureCoreApplication();
  SessionController controller;
  const auto profiles = controller.recommendedDraftProfiles();
  ASSERT_EQ(profiles.size(), 2u);
  bool hasSize0_5B = false, hasSize1_5B = false;
  for (const auto &p : profiles) {
    if (p.modelName == QStringLiteral("Qwen2.5-0.5B-Instruct")) {
      hasSize0_5B = true;
    }
    if (p.modelName == QStringLiteral("Qwen2.5-1.5B-Instruct")) {
      hasSize1_5B = true;
    }
  }
  EXPECT_TRUE(hasSize0_5B);
  EXPECT_TRUE(hasSize1_5B);
}

TEST(SessionControllerTest, SwitchModelToValidModelUpdatesStateAndEmitsCompletion) {
  ensureCoreApplication();
  const auto tempDir = std::filesystem::temp_directory_path() / "babel_switchmodel_test_valid";
  std::filesystem::remove_all(tempDir);
  std::filesystem::create_directories(tempDir);
  // 合法 GGUF 文件头魔数但非真实可加载权重数据：babel::slowpath::LlmEngine 对
  // "文件存在且非空但并非真正可用的模型文件" 的处理属于真实 llama.cpp 加载失败
  // 范畴（modelAvailable()==false），用于驱动"切换目标不可用 -> 级联回退"路径
  // （与下一用例区分：本用例改为验证"初始模型缺失 + 切换目标也不可用"时仍能
  // 正确收尾且不崩溃、不挂起）。
  const auto fakeModelPath = writeFakeFile(tempDir / "fake-target.gguf", std::string("GGUF") + std::string(64, '\0'));

  SessionController controller;
  controller.setFastCorrector(std::make_unique<babel::fastpath::FastCorrector>());
  const auto missingInitialPath = tempDir / "missing-initial.gguf";
  controller.setLlmEngine(std::make_unique<babel::slowpath::LlmEngine>(missingInitialPath));
  ASSERT_FALSE(controller.modelAvailable());

  bool completedSignalFired = false;
  QObject::connect(&controller, &SessionController::modelSwitchCompleted, &controller,
                    [&completedSignalFired](bool, const QString &) { completedSignalFired = true; });

  controller.switchModel(QString::fromStdString(fakeModelPath.string()));
  EXPECT_TRUE(controller.modelSwitching());
  ASSERT_TRUE(pumpEventsUntil([&controller]() { return !controller.modelSwitching(); }));
  EXPECT_TRUE(completedSignalFired);
  // 占位 GGUF 文件头正确但无真实可加载权重数据，llama.cpp 加载 MUST 失败
  // （modelAvailable()==false），因初始模型本就缺失、无其余可回退历史，
  // 最终 modelAvailable 应保持 false 但不崩溃、不挂起（FR-013 终止条件）。
  EXPECT_FALSE(controller.modelAvailable());

  std::filesystem::remove_all(tempDir);
}

TEST(SessionControllerTest, SwitchModelFailureFallsBackToPreviouslyWorkingModel) {
  ensureCoreApplication();
  // 本用例验证"初始模型本身可用 -> 记为 fallbackHistory -> 切换至不可用目标 ->
  // 自动回退至此前可用模型"链路（FR-013，research.md §8）。依赖本机已放置的
  // 真实模型文件；若不可用则跳过（不影响 CI，与既有 LlmEngineGenerateTest 跳过
  // 策略一致）。显式使用 applicationName="Babel" 以解析到生产环境实际放置真实
  // 模型文件的约定目录（而非本文件其余用例使用的隔离测试目录名）。
  QCoreApplication::setApplicationName(QStringLiteral("Babel"));
  const auto workingModelPath = babel::slowpath::ModelPaths::conventionalModelFilePath();
  {
    babel::slowpath::LlmEngine probe(workingModelPath);
    if (!probe.modelAvailable()) {
      GTEST_SKIP() << "本地未放置真实模型文件，跳过回退链路验证。";
    }
  }

  const auto tempDir = std::filesystem::temp_directory_path() / "babel_switchmodel_test_fallback";
  std::filesystem::remove_all(tempDir);
  std::filesystem::create_directories(tempDir);
  const auto unloadablePath = writeFakeFile(tempDir / "unloadable.gguf", std::string("GGUF") + std::string(32, '\0'));

  SessionController controller;
  controller.setFastCorrector(std::make_unique<babel::fastpath::FastCorrector>());
  controller.setLlmEngine(std::make_unique<babel::slowpath::LlmEngine>(workingModelPath));
  ASSERT_TRUE(controller.modelAvailable());

  QString fallbackNotice;
  QObject::connect(&controller, &SessionController::modelFallbackTriggered, &controller,
                    [&fallbackNotice](const QString &notice) { fallbackNotice = notice; });

  controller.switchModel(QString::fromStdString(unloadablePath.string()));
  ASSERT_TRUE(pumpEventsUntil([&controller]() { return !controller.modelSwitching(); }, std::chrono::seconds(30)));

  // 切换目标不可用，应自动回退至此前已知可用的模型，modelAvailable 恢复 true。
  EXPECT_TRUE(controller.modelAvailable());
  EXPECT_FALSE(fallbackNotice.isEmpty());

  std::filesystem::remove_all(tempDir);
}

TEST(SessionControllerTest, SecondSwitchModelCallWhileSwitchingIsIgnored) {
  ensureCoreApplication();
  const auto tempDir = std::filesystem::temp_directory_path() / "babel_switchmodel_test_concurrent";
  std::filesystem::remove_all(tempDir);
  std::filesystem::create_directories(tempDir);
  const auto targetA = writeFakeFile(tempDir / "target-a.gguf", std::string("GGUF") + std::string(32, '\0'));
  const auto targetB = writeFakeFile(tempDir / "target-b.gguf", std::string("GGUF") + std::string(32, '\0'));

  SessionController controller;
  controller.setFastCorrector(std::make_unique<babel::fastpath::FastCorrector>());
  controller.setLlmEngine(
      std::make_unique<babel::slowpath::LlmEngine>(tempDir / "missing-initial-concurrent.gguf"));

  controller.switchModel(QString::fromStdString(targetA.string()));
  ASSERT_TRUE(controller.modelSwitching());
  // 契约（qml-cpp-session-interface.md §五）：同一时刻只会有一次切换在进行，
  // 切换中再次调用 MUST 被忽略或排队，不崩溃、不产生第二次并发切换。
  EXPECT_NO_THROW(controller.switchModel(QString::fromStdString(targetB.string())));

  ASSERT_TRUE(pumpEventsUntil([&controller]() { return !controller.modelSwitching(); }));

  std::filesystem::remove_all(tempDir);
}

// T015 [US1]：switchModel() 成功后根据新模型所属档位同步调整防抖窗口时长
// （contracts/debounce-tier-window-internal-contract.md 四/五 2/3）。依赖本机
// 已放置的文件名含 "7b"（新中配）/"14b"（新高配）关键字的真实可加载模型文件
// （research.md §6/quickstart.md 前置条件要求用户预先下载 Qwen2.5-7B-Instruct/
// Qwen2.5-14B-Instruct）；本机未放置时 GTEST_SKIP()，与既有
// SwitchModelFailureFallsBackToPreviouslyWorkingModel 用例的跳过策略一致，不
// 影响 CI。
TEST(SessionControllerTest, SwitchModelToMediumOrHighTierModelLengthensDebounceWindow) {
  ensureCoreApplication();
  QCoreApplication::setApplicationName(QStringLiteral("Babel"));
  const auto modelDir = babel::slowpath::ModelPaths::conventionalModelDirectory();

  std::filesystem::path mediumTierModelPath;
  std::filesystem::path highTierModelPath;
  std::error_code ec;
  if (std::filesystem::exists(modelDir, ec) && !ec && std::filesystem::is_directory(modelDir, ec) && !ec) {
    for (const auto &entry : std::filesystem::directory_iterator(modelDir, ec)) {
      if (ec) {
        break;
      }
      if (!entry.is_regular_file()) {
        continue;
      }
      std::string lower = entry.path().filename().string();
      std::transform(lower.begin(), lower.end(), lower.begin(),
                      [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
      if (lower.find("7b") != std::string::npos && mediumTierModelPath.empty()) {
        mediumTierModelPath = entry.path();
      }
      if (lower.find("14b") != std::string::npos && highTierModelPath.empty()) {
        highTierModelPath = entry.path();
      }
    }
  }
  if (mediumTierModelPath.empty() || highTierModelPath.empty()) {
    GTEST_SKIP() << "本地未放置文件名含 7b（新中配 Qwen2.5-7B-Instruct）/14b（新高配 "
                    "Qwen2.5-14B-Instruct）关键字的真实可加载模型文件（research.md §6/"
                    "quickstart.md 前置条件），跳过档位切换防抖窗口动态调整验证。";
  }
  {
    // 确认两者确实可被 llama.cpp 真实加载（而非仅文件名匹配但内容损坏/占位），
    // 否则 switchModel() 会触发级联回退，掩盖本用例要验证的窗口值变化。
    babel::slowpath::LlmEngine mediumProbe(mediumTierModelPath);
    babel::slowpath::LlmEngine highProbe(highTierModelPath);
    if (!mediumProbe.modelAvailable() || !highProbe.modelAvailable()) {
      GTEST_SKIP() << "本地 7b/14b 命名模型文件存在但未能真实加载，跳过验证。";
    }
  }

  SessionController controller;
  controller.setFastCorrector(std::make_unique<babel::fastpath::FastCorrector>());
  controller.setLlmEngine(
      std::make_shared<babel::slowpath::LlmEngine>(babel::slowpath::ModelPaths::conventionalModelFilePath()));

  int triggerCount = 0;
  auto scheduler = std::make_unique<babel::slowpath::DebounceScheduler>(
      std::chrono::milliseconds(200), [&triggerCount](const QString &) { ++triggerCount; });
  babel::slowpath::DebounceScheduler *schedulerPtr = scheduler.get();
  controller.setDebounceScheduler(std::move(scheduler));

  // 切换至中配（文件名含 "7b"）：窗口 MUST 变为 500ms（FR-012）。
  controller.switchModel(QString::fromStdString(mediumTierModelPath.string()));
  ASSERT_TRUE(
      pumpEventsUntil([&controller]() { return !controller.modelSwitching(); }, std::chrono::seconds(60)));
  ASSERT_TRUE(controller.modelAvailable());

  const auto t0 = std::chrono::steady_clock::now();
  schedulerPtr->onInputChanged(QStringLiteral("a"), t0);
  // 旧 200ms 窗口下本应已触发；新 500ms 窗口下尚未到期，不应触发。
  schedulerPtr->tick(t0 + std::chrono::milliseconds(250));
  EXPECT_EQ(triggerCount, 0);
  schedulerPtr->tick(t0 + std::chrono::milliseconds(550));
  EXPECT_EQ(triggerCount, 1);
  schedulerPtr->markGenerationComplete();

  // 切换至高配（文件名含 "14b"）：窗口 MUST 变为 800ms（FR-002/FR-012）。
  triggerCount = 0;
  controller.switchModel(QString::fromStdString(highTierModelPath.string()));
  ASSERT_TRUE(
      pumpEventsUntil([&controller]() { return !controller.modelSwitching(); }, std::chrono::seconds(60)));
  ASSERT_TRUE(controller.modelAvailable());

  const auto t1 = std::chrono::steady_clock::now();
  schedulerPtr->onInputChanged(QStringLiteral("a"), t1);
  // 旧 500ms 窗口下本应已触发；新 800ms 窗口下尚未到期，不应触发。
  schedulerPtr->tick(t1 + std::chrono::milliseconds(600));
  EXPECT_EQ(triggerCount, 0);
  schedulerPtr->tick(t1 + std::chrono::milliseconds(850));
  EXPECT_EQ(triggerCount, 1);
}

// T025 [US3]：004-speculative-decoding（data-model.md 三/四，FR-006/FR-009，
// Edge Cases 第一条，quickstart.md 验证场景二/三）。

namespace {

/// 为每个测试用例设置独一无二的 applicationName，使
/// ModelPaths::conventionalModelDirectory() 解析到隔离的临时目录，避免测试
/// 相互干扰、也避免误写入真实用户数据目录（与 ModelCatalogTest.cpp 的
/// isolatedModelDirectoryFor() 同构）。
std::filesystem::path isolatedSpeculativeTestModelDirectoryFor(const char *testCaseName) {
  QCoreApplication::setApplicationName(QStringLiteral("BabelSpeculativeDecodingTest_%1").arg(testCaseName));
  const auto dir = babel::slowpath::ModelPaths::conventionalModelDirectory();
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  return dir;
}

/// 获取真实可加载的低配模型文件路径（生产环境 "Babel" 约定目录下的
/// Qwen2.5-3B-Instruct，本机已放置真实权重）。`classifyTierByFileName()` 仅按
/// 文件名子串判定档位、与文件实际内容/参数规模无关（既有既定设计，本特性未
/// 变更该既有行为），故可将其符号链接为"文件名含中/高配关键字"的测试替身，
/// 既能被 llama.cpp 真实加载（modelAvailable()==true）又不需下载额外的真实
/// 7B/14B 测试夹具。
std::filesystem::path realLoadableLowTierModelPathForSymlink() {
  const auto previousAppName = QCoreApplication::applicationName();
  QCoreApplication::setApplicationName(QStringLiteral("Babel"));
  const auto path = babel::slowpath::ModelPaths::conventionalModelFilePath();
  QCoreApplication::setApplicationName(previousAppName);
  return path;
}

}  // namespace

TEST(SessionControllerTest, SpeculativeDecodingNoticeTextEmptyForLowTierDefaultModel) {
  ensureCoreApplication();
  const auto realLowTierPath = realLoadableLowTierModelPathForSymlink();
  {
    babel::slowpath::LlmEngine probe(realLowTierPath);
    if (!probe.modelAvailable()) {
      GTEST_SKIP() << "本地未放置真实低配模型文件（Qwen2.5-3B-Instruct），跳过投机解码提示文案验证。";
    }
  }

  const auto dir = isolatedSpeculativeTestModelDirectoryFor("LowTierEmpty");
  const auto lowTierTestPath = dir / "qwen2.5-3b-instruct-symlink-test.gguf";
  std::error_code symlinkEc;
  std::filesystem::create_symlink(realLowTierPath, lowTierTestPath, symlinkEc);
  ASSERT_FALSE(symlinkEc) << symlinkEc.message();

  SessionController controller;
  controller.setFastCorrector(std::make_unique<babel::fastpath::FastCorrector>());
  controller.switchModel(QString::fromStdString(lowTierTestPath.string()));
  ASSERT_TRUE(
      pumpEventsUntil([&controller]() { return !controller.modelSwitching(); }, std::chrono::seconds(60)));
  ASSERT_TRUE(controller.modelAvailable());
  // FR-009：低配档位恒为空字符串，不展示任何提示。
  EXPECT_TRUE(controller.speculativeDecodingNoticeText().isEmpty());

  std::filesystem::remove_all(dir);
}

TEST(SessionControllerTest, SpeculativeDecodingNoticeTextNonEmptyWhenDraftModelMissingForMediumOrHighTier) {
  ensureCoreApplication();
  const auto realLowTierPath = realLoadableLowTierModelPathForSymlink();
  {
    babel::slowpath::LlmEngine probe(realLowTierPath);
    if (!probe.modelAvailable()) {
      GTEST_SKIP() << "本地未放置真实低配模型文件（Qwen2.5-3B-Instruct），跳过投机解码提示文案验证。";
    }
  }

  const auto dir = isolatedSpeculativeTestModelDirectoryFor("DraftMissing");
  // 文件名含 "7b" 关键字 -> 分类为 Medium；隔离目录中不放置任何草稿模型文件。
  const auto mediumTierTestPath = dir / "qwen2.5-7b-instruct-symlink-test.gguf";
  std::error_code symlinkEc;
  std::filesystem::create_symlink(realLowTierPath, mediumTierTestPath, symlinkEc);
  ASSERT_FALSE(symlinkEc) << symlinkEc.message();

  SessionController controller;
  controller.setFastCorrector(std::make_unique<babel::fastpath::FastCorrector>());
  // 事后修订（用户决策）：显式开启开关，确保本用例验证的是"未配置草稿模型
  // 文件"这一原因导致的提示，而非默认关闭开关本身导致的提示（两者文案不同）。
  controller.setSpeculativeDecodingEnabled(true);
  controller.switchModel(QString::fromStdString(mediumTierTestPath.string()));
  ASSERT_TRUE(
      pumpEventsUntil([&controller]() { return !controller.modelSwitching(); }, std::chrono::seconds(60)));
  ASSERT_TRUE(controller.modelAvailable());
  // FR-006：中配档位未配置对应草稿模型文件时为非空提示。
  EXPECT_FALSE(controller.speculativeDecodingNoticeText().isEmpty());

  std::filesystem::remove_all(dir);
}

TEST(SessionControllerTest, SpeculativeDecodingNoticeTextClearsWhenMatchingDraftModelFilePresent) {
  ensureCoreApplication();
  const auto realLowTierPath = realLoadableLowTierModelPathForSymlink();
  {
    babel::slowpath::LlmEngine probe(realLowTierPath);
    if (!probe.modelAvailable()) {
      GTEST_SKIP() << "本地未放置真实低配模型文件（Qwen2.5-3B-Instruct），跳过投机解码提示文案验证。";
    }
  }

  const auto dir = isolatedSpeculativeTestModelDirectoryFor("DraftPresent");
  const auto mediumTierTestPath = dir / "qwen2.5-7b-instruct-symlink-test.gguf";
  std::error_code symlinkEc;
  std::filesystem::create_symlink(realLowTierPath, mediumTierTestPath, symlinkEc);
  ASSERT_FALSE(symlinkEc) << symlinkEc.message();
  // 草稿模型文件（Medium 搭配 Size0_5B）：仅需文件名匹配 + 合法 GGUF 文件头
  // 魔数即可被 ModelCatalog::scan()/classifyDraftCandidateByFileName() 识别为
  // "已配置"，speculativeDecodingNoticeText 的计算不依赖该文件是否真实可加载
  // （该文件本身是否可加载由 LlmEngine::tryLoadModel() 静默降级处理，二者相
  // 互独立，data-model.md 三）。
  writeFakeFile(dir / "qwen2.5-0.5b-instruct-placeholder.gguf", std::string("GGUF") + std::string(64, '\0'));

  SessionController controller;
  controller.setFastCorrector(std::make_unique<babel::fastpath::FastCorrector>());
  // 事后修订（用户决策）：实验性开关默认 false，须显式开启后"已配置草稿模型
  // 则提示清空"的断言才成立；未开启时即使文件存在也会提示"未启用"。
  controller.setSpeculativeDecodingEnabled(true);
  controller.switchModel(QString::fromStdString(mediumTierTestPath.string()));
  ASSERT_TRUE(
      pumpEventsUntil([&controller]() { return !controller.modelSwitching(); }, std::chrono::seconds(60)));
  ASSERT_TRUE(controller.modelAvailable());
  EXPECT_TRUE(controller.speculativeDecodingNoticeText().isEmpty());

  std::filesystem::remove_all(dir);
}

TEST(SessionControllerTest, SwitchingTargetModelTierAutomaticallyRematchesDraftModel) {
  ensureCoreApplication();
  const auto realLowTierPath = realLoadableLowTierModelPathForSymlink();
  {
    babel::slowpath::LlmEngine probe(realLowTierPath);
    if (!probe.modelAvailable()) {
      GTEST_SKIP() << "本地未放置真实低配模型文件（Qwen2.5-3B-Instruct），跳过投机解码提示文案验证。";
    }
  }

  const auto dir = isolatedSpeculativeTestModelDirectoryFor("TierSwitchRematch");
  const auto mediumTierTestPath = dir / "qwen2.5-7b-instruct-symlink-test.gguf";
  const auto highTierTestPath = dir / "qwen2.5-14b-instruct-symlink-test.gguf";
  std::error_code symlinkEc;
  std::filesystem::create_symlink(realLowTierPath, mediumTierTestPath, symlinkEc);
  ASSERT_FALSE(symlinkEc) << symlinkEc.message();
  std::filesystem::create_symlink(realLowTierPath, highTierTestPath, symlinkEc);
  ASSERT_FALSE(symlinkEc) << symlinkEc.message();
  // 仅为 Medium 档位放置匹配的草稿模型文件（Size0_5B），High 档位（搭配
  // Size1_5B）故意不放置，驱动"切换档位后自动重新匹配"断言。
  writeFakeFile(dir / "qwen2.5-0.5b-instruct-placeholder.gguf", std::string("GGUF") + std::string(64, '\0'));

  SessionController controller;
  controller.setFastCorrector(std::make_unique<babel::fastpath::FastCorrector>());
  // 事后修订（用户决策）：实验性开关默认 false，须显式开启后"档位切换自动
  // 重新匹配草稿模型"的断言（而非"开关本身"）才是本用例实际要验证的内容。
  controller.setSpeculativeDecodingEnabled(true);

  controller.switchModel(QString::fromStdString(mediumTierTestPath.string()));
  ASSERT_TRUE(
      pumpEventsUntil([&controller]() { return !controller.modelSwitching(); }, std::chrono::seconds(60)));
  ASSERT_TRUE(controller.modelAvailable());
  EXPECT_TRUE(controller.speculativeDecodingNoticeText().isEmpty())
      << "中配档位已配置草稿模型，提示文案应为空";

  controller.switchModel(QString::fromStdString(highTierTestPath.string()));
  ASSERT_TRUE(
      pumpEventsUntil([&controller]() { return !controller.modelSwitching(); }, std::chrono::seconds(60)));
  ASSERT_TRUE(controller.modelAvailable());
  // Edge Cases 第一条：切换档位后 MUST 自动重新匹配新档位对应的默认草稿模型，
  // 不错误沿用中配阶段"已启用"状态。
  EXPECT_FALSE(controller.speculativeDecodingNoticeText().isEmpty())
      << "高配档位未配置对应草稿模型，提示文案应自动变为非空，不沿用中配阶段的空字符串状态";

  std::filesystem::remove_all(dir);
}

// 事后修订（用户决策）：实测发现投机解码在本机反而显著变慢后，改为默认关闭
// 的实验性开关，新增以下两个测试覆盖该决策引入的新行为。

TEST(SessionControllerTest, SpeculativeDecodingDisabledByDefault) {
  SessionController controller;
  controller.setFastCorrector(std::make_unique<babel::fastpath::FastCorrector>());
  EXPECT_FALSE(controller.speculativeDecodingEnabled());
}

TEST(SessionControllerTest, SpeculativeDecodingNoticeShowsDisabledReasonEvenWhenDraftModelFilePresent) {
  ensureCoreApplication();
  const auto realLowTierPath = realLoadableLowTierModelPathForSymlink();
  {
    babel::slowpath::LlmEngine probe(realLowTierPath);
    if (!probe.modelAvailable()) {
      GTEST_SKIP() << "本地未放置真实低配模型文件（Qwen2.5-3B-Instruct），跳过投机解码提示文案验证。";
    }
  }

  const auto dir = isolatedSpeculativeTestModelDirectoryFor("DisabledByDefaultDespiteDraftPresent");
  const auto mediumTierTestPath = dir / "qwen2.5-7b-instruct-symlink-test.gguf";
  std::error_code symlinkEc;
  std::filesystem::create_symlink(realLowTierPath, mediumTierTestPath, symlinkEc);
  ASSERT_FALSE(symlinkEc) << symlinkEc.message();
  // 与 SpeculativeDecodingNoticeTextClearsWhenMatchingDraftModelFilePresent
  // 使用相同的"已配置草稿模型文件"前提，但本用例刻意不调用
  // setSpeculativeDecodingEnabled(true)——验证默认关闭开关时，即使草稿模型
  // 文件客观存在，提示文案仍反映"开关未启用"这一原因，而非静默视为已启用。
  writeFakeFile(dir / "qwen2.5-0.5b-instruct-placeholder.gguf", std::string("GGUF") + std::string(64, '\0'));

  SessionController controller;
  controller.setFastCorrector(std::make_unique<babel::fastpath::FastCorrector>());
  ASSERT_FALSE(controller.speculativeDecodingEnabled());
  controller.switchModel(QString::fromStdString(mediumTierTestPath.string()));
  ASSERT_TRUE(
      pumpEventsUntil([&controller]() { return !controller.modelSwitching(); }, std::chrono::seconds(60)));
  ASSERT_TRUE(controller.modelAvailable());
  EXPECT_FALSE(controller.speculativeDecodingNoticeText().isEmpty())
      << "默认关闭开关时，即使草稿模型文件存在，仍应提示未启用（原因为开关而非文件缺失）";

  std::filesystem::remove_all(dir);
}

// T026：ctest --tests-regex "SessionControllerTest|LlmEngineGenerateTest|LlmEngineUnavailableTest"
// 见本文件顶部注释与 tasks.md T026 执行记录。

// ---------------------------------------------------------------------------
// 005-learning-tip-generation（Phase 3 User Story 1）：learningTipEnabled 属性
// 读写/NOTIFY、resetSessionData()/onHotkeyToggled() 对学习提示字段的清空与信号
// 触发——不依赖 onSlowPathTriggered() 私有接线的确定性子集（tasks.md T012
// Notes：方案 C 已与用户确认的测试边界）。
// ---------------------------------------------------------------------------

TEST(SessionControllerTest, LearningTipEnabledDefaultsToTrue) {
  SessionController controller;
  controller.setFastCorrector(std::make_unique<babel::fastpath::FastCorrector>());
  EXPECT_TRUE(controller.learningTipEnabled());
}

TEST(SessionControllerTest, LearningTipEnabledWriteUpdatesValueAndEmitsNotifyOnlyOnActualChange) {
  SessionController controller;
  controller.setFastCorrector(std::make_unique<babel::fastpath::FastCorrector>());

  int notifyCount = 0;
  QObject::connect(&controller, &SessionController::learningTipEnabledChanged, [&notifyCount]() { ++notifyCount; });

  controller.setLearningTipEnabled(false);
  EXPECT_FALSE(controller.learningTipEnabled());
  EXPECT_EQ(notifyCount, 1);

  // 取值未变化时不应重复 emit。
  controller.setLearningTipEnabled(false);
  EXPECT_EQ(notifyCount, 1);

  controller.setLearningTipEnabled(true);
  EXPECT_TRUE(controller.learningTipEnabled());
  EXPECT_EQ(notifyCount, 2);
}

TEST(SessionControllerTest, ResetSessionDataClearsLearningTipFieldsAndEmitsNotifySignalsOnEachSessionInvocation) {
  SessionController controller;
  controller.setFastCorrector(std::make_unique<babel::fastpath::FastCorrector>());

  int textNotifyCount = 0;
  int visibleNotifyCount = 0;
  QObject::connect(&controller, &SessionController::learningTipTextChanged, [&textNotifyCount]() { ++textNotifyCount; });
  QObject::connect(&controller, &SessionController::learningTipVisibleChanged,
                    [&visibleNotifyCount]() { ++visibleNotifyCount; });

  // 调用 onHotkeyToggled()（Hidden -> Invoked，内部触发一次 resetSessionData()）。
  // 本用例验证的是"每次新会话唤起都会执行清空分支并正确 emit 信号"这一结构性
  // 正确性，不验证"确实清除了上一会话残留的真实非空内容"——该效果需真实走通
  // 一次 onSlowPathTriggered() 生成回合才能构造前置状态，受限于
  // onSlowPathTriggered() 为 private 且仅在生产专用的 bootstrapPlatformDefaults()
  // 内接线，测试进程无法安全触发，留待 quickstart.md 场景三人工验证
  // （Polish 阶段 T019）。
  controller.onHotkeyToggled();

  EXPECT_GE(textNotifyCount, 1);
  EXPECT_GE(visibleNotifyCount, 1);
  EXPECT_TRUE(controller.learningTipText().isEmpty());
  EXPECT_FALSE(controller.learningTipVisible());
}

// ---------------------------------------------------------------------------
// 005-learning-tip-generation（Phase 4 User Story 2）：模型不可用时学习提示
// 恒不可见；learningTipVisible()/learningTipText() 一致性不变量。
// ---------------------------------------------------------------------------

TEST(SessionControllerTest, LearningTipStaysInvisibleAndEmptyWhenModelUnavailable) {
  const std::filesystem::path missingPath =
      std::filesystem::temp_directory_path() / "babel_test_learningtip_modelunavailable.gguf";
  std::filesystem::remove(missingPath);

  SessionController controller;
  controller.setLlmEngine(std::make_shared<babel::slowpath::LlmEngine>(missingPath));
  controller.setFastCorrector(std::make_unique<babel::fastpath::FastCorrector>());

  ASSERT_FALSE(controller.modelAvailable());
  controller.onHotkeyToggled();
  // 既有 FR-011 行为无回归：模型不可用时迁移至 ModelUnavailable 状态。
  EXPECT_EQ(controller.sessionState(), FloatingSessionState::ModelUnavailable);
  // onSlowPathTriggered() 既有的 !m_modelAvailable 早退路径本就不会触碰这两个
  // 字段，本用例断言其保持默认初始值（FR-004 语义）。
  EXPECT_FALSE(controller.learningTipVisible());
  EXPECT_TRUE(controller.learningTipText().isEmpty());
}

TEST(SessionControllerTest, LearningTipVisibleAndTextConsistencyInvariantHoldsAfterConstruction) {
  SessionController controller;
  controller.setFastCorrector(std::make_unique<babel::fastpath::FastCorrector>());

  // FR-004"不得出现标准中文译写失败、但学习提示仍展示内容的不一致状态"对应
  // 的最小可确定性断言子集：learningTipVisible() 为 true 当且仅当
  // learningTipText() 非空。
  EXPECT_EQ(controller.learningTipVisible(), !controller.learningTipText().isEmpty());
}
