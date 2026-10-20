#include <gtest/gtest.h>

#include <QCoreApplication>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

#include "babel/src/slowpath/LlmEngine.h"
#include "babel/src/slowpath/ModelPaths.h"

// 回归测试：真实调用 LlmEngine::generate() 验证"用户确认后进程崩溃
// （llama_sampler_dist_apply 断言失败：assert(found) 因 KV cache 跨调用累积、
// 数值溢出导致 logits 出现 NaN）"问题已修复（见 LlmEngine.cpp generate() 开头的
// llama_memory_clear() 调用）。依赖用户手动下载的真实模型文件（research.md §7），
// 文件不存在的环境中自动跳过，不影响 CI。

TEST(LlmEngineGenerateTest, GeneratesWithoutCrashingOnMixedChineseEnglishDraft) {
  // ModelPaths::conventionalModelDirectory() 依赖 QCoreApplication::applicationName()
  // 推导约定目录（生产环境由 main.cpp 的可执行文件名隐式决定为 "Babel"）；独立测试
  // 进程无 QApplication 实例时需显式设置，否则会解析到错误的路径。
  QCoreApplication::setApplicationName(QStringLiteral("Babel"));

  const auto modelPath = babel::slowpath::ModelPaths::conventionalModelFilePath();
  babel::slowpath::LlmEngine engine(modelPath);
  if (!engine.modelAvailable()) {
    GTEST_SKIP() << "本地未放置真实模型文件（" << modelPath.string()
                 << "），跳过真实推理生成验证，见 research.md §7。";
  }

  const QString output = engine.generate(QStringLiteral("wo today bushi very e"));
  EXPECT_FALSE(output.isEmpty());
}

// 回归测试：SessionController 的 DebounceScheduler 每次防抖窗口到期都会在
// **同一个** LlmEngine/llama_context 上调用一次 generate()（见
// SessionController::onSlowPathTriggered），模拟用户持续输入时产生的连续多次调用，
// 验证每次调用前清空 KV cache 后不再出现跨调用状态累积导致的崩溃/生成失败。
TEST(LlmEngineGenerateTest, RepeatedGenerateCallsOnSameEngineDoNotCrash) {
  QCoreApplication::setApplicationName(QStringLiteral("Babel"));

  const auto modelPath = babel::slowpath::ModelPaths::conventionalModelFilePath();
  babel::slowpath::LlmEngine engine(modelPath);
  if (!engine.modelAvailable()) {
    GTEST_SKIP() << "本地未放置真实模型文件，跳过真实推理生成验证。";
  }

  // 模拟用户逐步打字、每次防抖触发时草稿文本不断增长的真实场景；重复多轮调用，
  // 验证每次调用前清空 KV cache 后各次调用彼此独立、不受此前调用残留状态影响。
  const QStringList baseDrafts = {
      QStringLiteral("wo"),
      QStringLiteral("wo today"),
      QStringLiteral("wo today bushi"),
      QStringLiteral("wo today bushi very"),
      QStringLiteral("wo today bushi very e"),
      QStringLiteral("wo today bushi very e if you e"),
      QStringLiteral("wo today bushi very e if you e we go eat malatang"),
  };
  for (int round = 0; round < 5; ++round) {
    for (const QString &draft : baseDrafts) {
      const QString output = engine.generate(draft);
      EXPECT_FALSE(output.isEmpty()) << "round=" << round << " draft=" << draft.toStdString();
    }
  }
}

// T016 [US2]：generate() 接收含 ⟦⟧ 结构化标注的输入文本时签名仍为单一 QString
// 参数不变（FR-007 硬性约束，contracts/llm-prompt-annotation-contract.md）。

TEST(LlmEngineGenerateTest, GenerateAcceptsStructuredAnnotationTextWithoutSignatureChange) {
  QCoreApplication::setApplicationName(QStringLiteral("Babel"));

  const auto modelPath = babel::slowpath::ModelPaths::conventionalModelFilePath();
  babel::slowpath::LlmEngine engine(modelPath);
  if (!engine.modelAvailable()) {
    GTEST_SKIP() << "本地未放置真实模型文件，跳过真实推理生成验证。";
  }

  // contracts/llm-prompt-annotation-contract.md 示例：含 ⟦token:候选 权重|...⟧
  // 标注片段的标准输入文本，generate() 签名仍为单一 QString 参数，调用方式与
  // 普通规整化字符串完全一致。
  const QString annotatedInput = QStringLiteral("我⟦chi:吃 0.42|持 0.35|迟 0.15|池 0.05|尺 0.02|齿 0.01⟧了晚饭");
  const QString output = engine.generate(annotatedInput);
  EXPECT_FALSE(output.isEmpty());
  // 配套系统提示词约束 + GBNF 语法层面强制约束（FR-005）：模型输出 MUST NOT
  // 包含 ⟦/⟧/| 三个 Lattice 内部标注符号中的任意一个。
  EXPECT_FALSE(output.contains(QStringLiteral("⟦")));
  EXPECT_FALSE(output.contains(QStringLiteral("⟧")));
  EXPECT_FALSE(output.contains(QStringLiteral("|")));
}

// T020 [US2]：高歧义密度输入（多个 ⟦候选1 权重1|候选2 权重2|...⟧ 结构化标注片段）
// 场景下，GBNF 语法约束仍 MUST 保证最终生成结果不含 ⟦/⟧/| 任意字符（FR-005，
// quickstart.md 场景三 3.1/3.2，SC-004）。
TEST(LlmEngineGenerateTest, GenerateOnHighAmbiguityDensityInputNeverLeaksAnnotationSymbols) {
  QCoreApplication::setApplicationName(QStringLiteral("Babel"));

  const auto modelPath = babel::slowpath::ModelPaths::conventionalModelFilePath();
  babel::slowpath::LlmEngine engine(modelPath);
  if (!engine.modelAvailable()) {
    GTEST_SKIP() << "本地未放置真实模型文件，跳过真实推理生成验证。";
  }

  // 多个高歧义拼音位置（chi/shi）均附带结构化标注片段，标注密度显著高于既有
  // GenerateAcceptsStructuredAnnotationTextWithoutSignatureChange 用例。
  const QString highDensityInput = QStringLiteral(
      "我⟦chi:吃 0.42|持 0.35|迟 0.15|池 0.05|尺 0.02|齿 0.01⟧了晚饭，"
      "然后⟦shi:是 0.40|时 0.30|事 0.15|市 0.08|式 0.05|使 0.02⟧的时候觉得"
      "⟦chi:吃 0.42|持 0.35|迟 0.15|池 0.05|尺 0.02|齿 0.01⟧得太饱了。");
  const QString output = engine.generate(highDensityInput);
  EXPECT_FALSE(output.contains(QStringLiteral("⟦")));
  EXPECT_FALSE(output.contains(QStringLiteral("⟧")));
  EXPECT_FALSE(output.contains(QStringLiteral("|")));
}

// T021 [US2]：合法混排内容（英文品牌名/技术缩写）不被 GBNF 语法约束误伐
// （FR-007"最宽边界"，quickstart.md 场景三 3.2 第 5 步）。不强制断言模型逐字
// 复现特定词汇本身，仅验证语法约束未因这些合法字符导致异常梗阻/空输出。
TEST(LlmEngineGenerateTest, GenerateDoesNotFilterLegalMixedContentLikeBrandNamesOrEmoji) {
  QCoreApplication::setApplicationName(QStringLiteral("Babel"));

  const auto modelPath = babel::slowpath::ModelPaths::conventionalModelFilePath();
  babel::slowpath::LlmEngine engine(modelPath);
  if (!engine.modelAvailable()) {
    GTEST_SKIP() << "本地未放置真实模型文件，跳过真实推理生成验证。";
  }

  const QString mixedContentInput =
      QStringLiteral("wo gang mai le yi tai xin de iPhone，上面用 GPT 写了一段测试代码 😀");
  const QString output = engine.generate(mixedContentInput);
  EXPECT_FALSE(output.isEmpty());
}

// T022 [US2]：语法梗阻（候选集合全部为 -inf）情形下的健壮性（FR-008，
// quickstart.md 场景三 3.3，"较难稳定复现，尽力而为"）：复用既有
// RepeatedGenerateCallsOnSameEngineDoNotCrash 的多轮调用手法，验证即便某次调用
// 触发语法梗阻提前终止，也不抛出异常、不使进程崩溃，返回值始终为合法
// QString（不强制断言梗阻必然被触发，仅验证该路径被触发时的既有健壮性）。
TEST(LlmEngineGenerateTest, GenerateOnGrammarStallTerminatesEarlyWithoutCrashing) {
  QCoreApplication::setApplicationName(QStringLiteral("Babel"));

  const auto modelPath = babel::slowpath::ModelPaths::conventionalModelFilePath();
  babel::slowpath::LlmEngine engine(modelPath);
  if (!engine.modelAvailable()) {
    GTEST_SKIP() << "本地未放置真实模型文件，跳过真实推理生成验证。";
  }

  const QStringList highAmbiguityDrafts = {
      QStringLiteral("我⟦chi:吃 0.42|持 0.35|迟 0.15|池 0.05|尺 0.02|齿 0.01⟧了晚饭"),
      QStringLiteral("我⟦chi:吃 0.42|持 0.35|迟 0.15|池 0.05|尺 0.02|齿 0.01⟧了晚饭，"
                     "然后⟦shi:是 0.40|时 0.30|事 0.15|市 0.08|式 0.05|使 0.02⟧的时候很饱"),
      QStringLiteral("wo gang mai le yi tai xin de iPhone，上面用 GPT 写了一段测试代码 😀"),
  };
  for (int round = 0; round < 3; ++round) {
    for (const QString &draft : highAmbiguityDrafts) {
      EXPECT_NO_THROW({
        const QString output = engine.generate(draft);
        // generate() MUST NOT 返回 null QString（即便梗阻提前终止，已生成部分
        // 内容仍是合法 QString；"模型不可用"情形已在上方 modelAvailable() 检查
        // 中提前返回，不会到达此处）。
        EXPECT_FALSE(output.isNull()) << "round=" << round << " draft=" << draft.toStdString();
      }) << "round=" << round << " draft=" << draft.toStdString();
    }
  }
}

// T015 [US1]：004-speculative-decoding（contracts/llm-engine-speculative-
// internal-contract.md 二，FR-001/FR-003/FR-006）。

namespace {

/// 在约定模型目录下查找文件名含 "0.5b"/"0-5b" 或 "1.5b"/"1-5b" 关键字的真实
/// 草稿模型文件（research.md §5.1/§5.3，用户本轮尚未下载，见 quickstart.md 前
/// 置条件）；本机未放置时返回空路径，供调用方 GTEST_SKIP()。
std::filesystem::path findRealDraftModelFile() {
  const auto dir = babel::slowpath::ModelPaths::conventionalModelDirectory();
  std::error_code ec;
  if (!std::filesystem::exists(dir, ec) || ec || !std::filesystem::is_directory(dir, ec) || ec) {
    return {};
  }
  for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
    if (ec) {
      break;
    }
    if (!entry.is_regular_file()) {
      continue;
    }
    std::string lower = entry.path().filename().string();
    std::transform(lower.begin(), lower.end(), lower.begin(),
                    [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (lower.find("0.5b") != std::string::npos || lower.find("0-5b") != std::string::npos ||
        lower.find("1.5b") != std::string::npos || lower.find("1-5b") != std::string::npos) {
      return entry.path();
    }
  }
  return {};
}

}  // namespace

TEST(LlmEngineGenerateTest, GenerateWithDraftModelProducesSemanticallyEquivalentOutputToSingleModelPath) {
  QCoreApplication::setApplicationName(QStringLiteral("Babel"));

  const auto modelPath = babel::slowpath::ModelPaths::conventionalModelFilePath();
  const auto draftModelPath = findRealDraftModelFile();
  if (draftModelPath.empty()) {
    GTEST_SKIP() << "本地未放置真实草稿模型文件（Qwen2.5-0.5B-Instruct/Qwen2.5-1.5B-Instruct，"
                    "research.md §5.1/§5.3，用户本轮尚未下载），跳过投机解码语义等价性验证，"
                    "见 quickstart.md 前置条件。";
  }

  babel::slowpath::LlmEngine singleModelEngine(modelPath);
  babel::slowpath::LlmEngine speculativeEngine(modelPath, draftModelPath);
  if (!singleModelEngine.modelAvailable() || !speculativeEngine.modelAvailable()) {
    GTEST_SKIP() << "本地未放置真实目标模型文件，跳过投机解码语义等价性验证。";
  }

  const QString input = QStringLiteral("wo today bushi very e");
  const QString singleModelOutput = singleModelEngine.generate(input);
  const QString speculativeOutput = speculativeEngine.generate(input);
  // FR-001"语义等价"：自动化断言至少确认均非空且不含标注符号，措辞级差异
  // （采样随机性导致）留待人工复核，不强制逐字节相等。
  EXPECT_FALSE(singleModelOutput.isEmpty());
  EXPECT_FALSE(speculativeOutput.isEmpty());
  EXPECT_FALSE(speculativeOutput.contains(QStringLiteral("⟦")));
  EXPECT_FALSE(speculativeOutput.contains(QStringLiteral("⟧")));
  EXPECT_FALSE(speculativeOutput.contains(QStringLiteral("|")));
}

TEST(LlmEngineGenerateTest, GenerateWithDraftModelStillEnforcesGrammarConstraintOnBothSides) {
  QCoreApplication::setApplicationName(QStringLiteral("Babel"));

  const auto modelPath = babel::slowpath::ModelPaths::conventionalModelFilePath();
  const auto draftModelPath = findRealDraftModelFile();
  if (draftModelPath.empty()) {
    GTEST_SKIP() << "本地未放置真实草稿模型文件，跳过投机解码 GBNF 双侧约束验证，"
                    "见 quickstart.md 前置条件。";
  }

  babel::slowpath::LlmEngine engine(modelPath, draftModelPath);
  if (!engine.modelAvailable()) {
    GTEST_SKIP() << "本地未放置真实目标模型文件，跳过投机解码 GBNF 双侧约束验证。";
  }

  // 复用既有 003 高密度标注测试样例（GenerateOnHighAmbiguityDensityInputNever-
  // LeaksAnnotationSymbols）。
  const QString highDensityInput = QStringLiteral(
      "我⟦chi:吃 0.42|持 0.35|迟 0.15|池 0.05|尺 0.02|齿 0.01⟧了晚饭，"
      "然后⟦shi:是 0.40|时 0.30|事 0.15|市 0.08|式 0.05|使 0.02⟧的时候觉得"
      "⟦chi:吃 0.42|持 0.35|迟 0.15|池 0.05|尺 0.02|齿 0.01⟧得太饱了。");
  const QString output = engine.generate(highDensityInput);
  EXPECT_FALSE(output.contains(QStringLiteral("⟦")));
  EXPECT_FALSE(output.contains(QStringLiteral("⟧")));
  EXPECT_FALSE(output.contains(QStringLiteral("|")));
}

TEST(LlmEngineGenerateTest, GenerateWithMissingDraftModelPathFallsBackSilentlyToSingleModelPath) {
  QCoreApplication::setApplicationName(QStringLiteral("Babel"));

  const auto modelPath = babel::slowpath::ModelPaths::conventionalModelFilePath();
  const std::filesystem::path missingDraftPath =
      std::filesystem::temp_directory_path() / "babel_test_missing_draft_model.gguf";
  std::filesystem::remove(missingDraftPath);  // 确保确实不存在

  babel::slowpath::LlmEngine engine(modelPath, missingDraftPath);
  if (!engine.modelAvailable()) {
    GTEST_SKIP() << "本地未放置真实目标模型文件，跳过草稿模型路径缺失静默降级验证。";
  }
  // FR-006：草稿模型路径不存在 MUST NOT 影响目标模型本身的加载结果。
  EXPECT_TRUE(engine.modelAvailable());
  EXPECT_NO_THROW({
    const QString output = engine.generate(QStringLiteral("wo today bushi very e"));
    EXPECT_FALSE(output.isEmpty());
  });
}

TEST(LlmEngineGenerateTest, GenerateWithCorruptDraftModelFileFallsBackSilentlyToSingleModelPath) {
  QCoreApplication::setApplicationName(QStringLiteral("Babel"));

  const auto modelPath = babel::slowpath::ModelPaths::conventionalModelFilePath();
  // Edge Cases 第二条：文件名匹配草稿关键字但内容损坏（非法 GGUF 头）。
  const std::filesystem::path corruptDraftPath =
      std::filesystem::temp_directory_path() / "qwen2.5-0.5b-instruct-corrupt-test.gguf";
  {
    std::ofstream ofs(corruptDraftPath, std::ios::binary);
    ofs << "NOTAGGUFFILE_______________________";
  }

  babel::slowpath::LlmEngine engine(modelPath, corruptDraftPath);
  if (!engine.modelAvailable()) {
    std::filesystem::remove(corruptDraftPath);
    GTEST_SKIP() << "本地未放置真实目标模型文件，跳过草稿模型文件损坏静默降级验证。";
  }
  // FR-006：草稿模型文件损坏 MUST NOT 影响目标模型本身的加载结果。
  EXPECT_TRUE(engine.modelAvailable());
  EXPECT_NO_THROW({
    const QString output = engine.generate(QStringLiteral("wo today bushi very e"));
    EXPECT_FALSE(output.isEmpty());
  });
  std::filesystem::remove(corruptDraftPath);
}


