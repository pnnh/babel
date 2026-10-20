#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QStringList>
#include <chrono>
#include <filesystem>
#include <fstream>

#include "babel/src/slowpath/ModelCatalog.h"
#include "babel/src/slowpath/ModelPaths.h"

using babel::slowpath::ModelCatalog;
using babel::slowpath::ModelDescriptor;
using babel::slowpath::RecommendedModelProfile;

// T025 [US3]：ModelCatalog 单元测试（data-model.md §6/§7，research.md §7.1/7.2/7.3）。
// 先写测试，确认其在 ModelCatalog::scan() 实现完成前处于 FAIL 状态。

namespace {

void writeFile(const std::filesystem::path &path, const std::string &content) {
  std::ofstream ofs(path, std::ios::binary);
  ofs << content;
}

/// 为每个测试用例设置独一无二的 applicationName，使
/// ModelPaths::conventionalModelDirectory() 解析到隔离的临时目录，避免测试相互
/// 干扰、也避免误写入真实用户数据目录。
std::filesystem::path isolatedModelDirectoryFor(const char *testCaseName) {
  QCoreApplication::setApplicationName(QStringLiteral("BabelModelCatalogTest_%1").arg(testCaseName));
  const auto dir = babel::slowpath::ModelPaths::conventionalModelDirectory();
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  return dir;
}

}  // namespace

TEST(ModelCatalogTest, ScanEnumeratesAllGgufFilesInConventionalDirectory) {
  const auto dir = isolatedModelDirectoryFor("Enumerate");
  writeFile(dir / "qwen2.5-3b-instruct-q4_k_m.gguf", std::string("GGUF") + std::string(100, '\0'));
  writeFile(dir / "qwen2.5-14b-instruct-q4_k_m.gguf", std::string("GGUF") + std::string(100, '\0'));
  writeFile(dir / "not-a-model.txt", "ignored, not .gguf");

  const auto models = ModelCatalog::scan();
  ASSERT_EQ(models.size(), 2u);

  std::filesystem::remove_all(dir);
}

TEST(ModelCatalogTest, TierHintClassifiesByFilenameKeyword) {
  const auto dir = isolatedModelDirectoryFor("TierHint");
  writeFile(dir / "qwen2.5-3b-instruct-q4_k_m.gguf", std::string("GGUF") + std::string(100, '\0'));
  writeFile(dir / "qwen2.5-7b-instruct-q4_k_m.gguf", std::string("GGUF") + std::string(100, '\0'));
  writeFile(dir / "qwen2.5-14b-instruct-q4_k_m.gguf", std::string("GGUF") + std::string(100, '\0'));
  writeFile(dir / "qwen2.5-32b-instruct-q4_k_m.gguf", std::string("GGUF") + std::string(100, '\0'));
  writeFile(dir / "mystery-model.gguf", std::string("GGUF") + std::string(100, '\0'));

  const auto models = ModelCatalog::scan();
  ASSERT_EQ(models.size(), 5u);

  const auto findByName = [&models](const std::string &name) -> const ModelDescriptor * {
    for (const auto &m : models) {
      if (m.fileName.toStdString() == name) return &m;
    }
    return nullptr;
  };

  ASSERT_NE(findByName("qwen2.5-3b-instruct-q4_k_m.gguf"), nullptr);
  EXPECT_EQ(findByName("qwen2.5-3b-instruct-q4_k_m.gguf")->tierHint, ModelDescriptor::TierHint::Low);
  EXPECT_EQ(findByName("qwen2.5-7b-instruct-q4_k_m.gguf")->tierHint, ModelDescriptor::TierHint::Medium);
  // 本特性变更（research.md §5）：14B 现为新高配推荐，原 Medium 判定已不再适用。
  EXPECT_EQ(findByName("qwen2.5-14b-instruct-q4_k_m.gguf")->tierHint, ModelDescriptor::TierHint::High);
  EXPECT_EQ(findByName("qwen2.5-32b-instruct-q4_k_m.gguf")->tierHint, ModelDescriptor::TierHint::High);
  EXPECT_EQ(findByName("mystery-model.gguf")->tierHint, ModelDescriptor::TierHint::Unknown);

  std::filesystem::remove_all(dir);
}

TEST(ModelCatalogTest, GgufMagicHeaderValidationDeterminesAvailability) {
  const auto dir = isolatedModelDirectoryFor("MagicHeader");
  // 合法 GGUF 文件头魔数 "GGUF" -> Unverified（研究阶段已决策，仍需真正加载才能
  // 升级为 Verified）。
  writeFile(dir / "valid-looking.gguf", std::string("GGUF") + std::string(100, '\0'));
  // 文件头魔数不匹配 -> KnownUnloadable（仍可被选中以触发 FR-013 回退链路）。
  writeFile(dir / "corrupted.gguf", "NOTAGGUFFILE_______________________");
  // 文件过短（不足 4 字节）同样视为校验失败 -> KnownUnloadable。
  writeFile(dir / "too-short.gguf", "AB");

  const auto models = ModelCatalog::scan();
  ASSERT_EQ(models.size(), 3u);

  const auto findByName = [&models](const std::string &name) -> const ModelDescriptor * {
    for (const auto &m : models) {
      if (m.fileName.toStdString() == name) return &m;
    }
    return nullptr;
  };

  EXPECT_EQ(findByName("valid-looking.gguf")->availability, ModelDescriptor::Availability::Unverified);
  EXPECT_EQ(findByName("corrupted.gguf")->availability, ModelDescriptor::Availability::KnownUnloadable);
  EXPECT_EQ(findByName("too-short.gguf")->availability, ModelDescriptor::Availability::KnownUnloadable);

  std::filesystem::remove_all(dir);
}

TEST(ModelCatalogTest, ScanOnMissingDirectoryReturnsEmptyWithoutError) {
  QCoreApplication::setApplicationName(QStringLiteral("BabelModelCatalogTest_Missing"));
  const auto dir = babel::slowpath::ModelPaths::conventionalModelDirectory();
  std::filesystem::remove_all(dir);

  const auto models = ModelCatalog::scan();
  EXPECT_TRUE(models.empty());
}

TEST(ModelCatalogTest, RecommendedProfilesReturnFixedThreeTiers) {
  const auto profiles = ModelCatalog::recommendedProfiles();
  ASSERT_EQ(profiles.size(), 3u);

  bool hasLow = false, hasMedium = false, hasHigh = false;
  for (const auto &p : profiles) {
    if (p.tier == RecommendedModelProfile::Tier::Low) {
      hasLow = true;
      EXPECT_TRUE(p.modelName.contains(QStringLiteral("3B")));
    }
    if (p.tier == RecommendedModelProfile::Tier::Medium) {
      hasMedium = true;
      EXPECT_TRUE(p.modelName.contains(QStringLiteral("7B")));
    }
    if (p.tier == RecommendedModelProfile::Tier::High) {
      hasHigh = true;
      EXPECT_TRUE(p.modelName.contains(QStringLiteral("14B")));
    }
    EXPECT_FALSE(p.quantization.isEmpty());
    EXPECT_FALSE(p.approxSizeDescription.isEmpty());
    EXPECT_FALSE(p.targetHardwareDescription.isEmpty());
  }
  EXPECT_TRUE(hasLow);
  EXPECT_TRUE(hasMedium);
  EXPECT_TRUE(hasHigh);
}

// T011 [US1]：classifyTierByFileName() 公开静态方法，不经由 scan() 亦可直接调用
// （contracts/debounce-tier-window-internal-contract.md 二）。
TEST(ModelCatalogTest, ClassifyTierByFileNameIsPubliclyCallable) {
  EXPECT_EQ(ModelCatalog::classifyTierByFileName(QStringLiteral("qwen2.5-3b-instruct-q4_k_m.gguf")),
            ModelDescriptor::TierHint::Low);
  EXPECT_EQ(ModelCatalog::classifyTierByFileName(QStringLiteral("qwen2.5-7b-instruct-q4_k_m.gguf")),
            ModelDescriptor::TierHint::Medium);
  EXPECT_EQ(ModelCatalog::classifyTierByFileName(QStringLiteral("qwen2.5-14b-instruct-q4_k_m.gguf")),
            ModelDescriptor::TierHint::High);
  EXPECT_EQ(ModelCatalog::classifyTierByFileName(QStringLiteral("qwen2.5-32b-instruct-q4_k_m.gguf")),
            ModelDescriptor::TierHint::High);
  EXPECT_EQ(ModelCatalog::classifyTierByFileName(QStringLiteral("mystery-model.gguf")),
            ModelDescriptor::TierHint::Unknown);
}

// T012 [US1]：tierDebounceWindow() 四档完整映射（data-model.md 二
// TierDebounceProfile，用户已决策数值，含 Unknown 复用 Low 档位 200ms）。
TEST(ModelCatalogTest, TierDebounceWindowMapsEachTierToDocumentedMilliseconds) {
  EXPECT_EQ(ModelCatalog::tierDebounceWindow(ModelDescriptor::TierHint::Low), std::chrono::milliseconds(200));
  EXPECT_EQ(ModelCatalog::tierDebounceWindow(ModelDescriptor::TierHint::Medium), std::chrono::milliseconds(500));
  EXPECT_EQ(ModelCatalog::tierDebounceWindow(ModelDescriptor::TierHint::High), std::chrono::milliseconds(800));
  EXPECT_EQ(ModelCatalog::tierDebounceWindow(ModelDescriptor::TierHint::Unknown), std::chrono::milliseconds(200));
}

// T004 [Foundational]：004-speculative-decoding（contracts/model-catalog-draft-
// pairing-internal-contract.md 二.1/二.2/二.3/二.4/二.5）。

TEST(ModelCatalogTest, ClassifyDraftCandidateByFileNameMatchesDocumentedKeywords) {
  EXPECT_EQ(ModelCatalog::classifyDraftCandidateByFileName(QStringLiteral("qwen2.5-0.5b-instruct-q4_k_m.gguf")),
            ModelDescriptor::DraftCandidate::Size0_5B);
  EXPECT_EQ(ModelCatalog::classifyDraftCandidateByFileName(QStringLiteral("qwen2.5-1.5b-instruct-q4_k_m.gguf")),
            ModelDescriptor::DraftCandidate::Size1_5B);
  EXPECT_EQ(ModelCatalog::classifyDraftCandidateByFileName(QStringLiteral("qwen2.5-7b-instruct-q4_k_m.gguf")),
            ModelDescriptor::DraftCandidate::None);
  EXPECT_EQ(ModelCatalog::classifyDraftCandidateByFileName(QStringLiteral("mystery-model.gguf")),
            ModelDescriptor::DraftCandidate::None);
}

TEST(ModelCatalogTest, ClassifyDraftCandidateByFileNameDoesNotInterfereWithClassifyTierByFileName) {
  const QStringList fileNames = {
      QStringLiteral("qwen2.5-3b-instruct-q4_k_m.gguf"),
      QStringLiteral("qwen2.5-7b-instruct-q4_k_m.gguf"),
      QStringLiteral("qwen2.5-14b-instruct-q4_k_m.gguf"),
      QStringLiteral("qwen2.5-32b-instruct-q4_k_m.gguf"),
  };
  const std::vector<ModelDescriptor::TierHint> expectedTiers = {
      ModelDescriptor::TierHint::Low,
      ModelDescriptor::TierHint::Medium,
      ModelDescriptor::TierHint::High,
      ModelDescriptor::TierHint::High,
  };
  for (int i = 0; i < fileNames.size(); ++i) {
    // 先调用新方法，再调用既有方法，确认既有方法的分类结果不受新方法引入影响
    // （research.md §5.1 回归断言）。
    (void)ModelCatalog::classifyDraftCandidateByFileName(fileNames[i]);
    EXPECT_EQ(ModelCatalog::classifyTierByFileName(fileNames[i]), expectedTiers[static_cast<std::size_t>(i)])
        << fileNames[i].toStdString();
  }
}

TEST(ModelCatalogTest, RecommendedDraftProfilesReturnsFixedTwoCandidates) {
  const auto profiles = ModelCatalog::recommendedDraftProfiles();
  ASSERT_EQ(profiles.size(), 2u);

  bool hasSize0_5B = false, hasSize1_5B = false;
  for (const auto &p : profiles) {
    if (p.size == ModelDescriptor::DraftCandidate::Size0_5B) {
      hasSize0_5B = true;
      EXPECT_EQ(p.modelName, QStringLiteral("Qwen2.5-0.5B-Instruct"));
    }
    if (p.size == ModelDescriptor::DraftCandidate::Size1_5B) {
      hasSize1_5B = true;
      EXPECT_EQ(p.modelName, QStringLiteral("Qwen2.5-1.5B-Instruct"));
    }
    EXPECT_FALSE(p.quantization.isEmpty());
    EXPECT_FALSE(p.approxSizeDescription.isEmpty());
  }
  EXPECT_TRUE(hasSize0_5B);
  EXPECT_TRUE(hasSize1_5B);
}

TEST(ModelCatalogTest, PairedDraftCandidateMapsLowToNoneMediumToSize0_5BHighToSize1_5B) {
  EXPECT_EQ(ModelCatalog::pairedDraftCandidate(ModelDescriptor::TierHint::Low), ModelDescriptor::DraftCandidate::None);
  EXPECT_EQ(ModelCatalog::pairedDraftCandidate(ModelDescriptor::TierHint::Unknown),
            ModelDescriptor::DraftCandidate::None);
  EXPECT_EQ(ModelCatalog::pairedDraftCandidate(ModelDescriptor::TierHint::Medium),
            ModelDescriptor::DraftCandidate::Size0_5B);
  EXPECT_EQ(ModelCatalog::pairedDraftCandidate(ModelDescriptor::TierHint::High),
            ModelDescriptor::DraftCandidate::Size1_5B);
}
