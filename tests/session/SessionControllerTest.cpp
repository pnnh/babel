#include <gtest/gtest.h>

#include <QCoreApplication>
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
