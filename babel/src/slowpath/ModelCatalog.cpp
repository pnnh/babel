#include "babel/src/slowpath/ModelCatalog.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <system_error>

#include "babel/src/slowpath/ModelPaths.h"

namespace babel::slowpath {

namespace {

std::string toLower(const std::string &s) {
  std::string out = s;
  std::transform(out.begin(), out.end(), out.begin(),
                  [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return out;
}

/// GGUF 文件头魔数校验（research.md §7.3，用户委派 Agent 采纳 Recommendation
/// 方案 3 的补充定义）：读取文件前 4 字节，判断是否等于 "GGUF"（0x47 0x47 0x55
/// 0x46）。校验失败（文件过短/头 4 字节不匹配）→ KnownUnloadable；校验通过 →
/// Unverified（真正加载后才可能升级为 Verified 或降级为 KnownUnloadable，该部
/// 分由 LlmEngine 实际加载流程负责，ModelCatalog::scan() 仅做静态头部校验）。
ModelDescriptor::Availability checkGgufMagic(const std::filesystem::path &path) {
  std::ifstream ifs(path, std::ios::binary);
  if (!ifs) {
    return ModelDescriptor::Availability::KnownUnloadable;
  }
  char header[4] = {0, 0, 0, 0};
  ifs.read(header, sizeof(header));
  if (ifs.gcount() != static_cast<std::streamsize>(sizeof(header))) {
    return ModelDescriptor::Availability::KnownUnloadable;
  }
  static constexpr char kGgufMagic[4] = {'G', 'G', 'U', 'F'};
  if (std::equal(std::begin(header), std::end(header), std::begin(kGgufMagic))) {
    return ModelDescriptor::Availability::Unverified;
  }
  return ModelDescriptor::Availability::KnownUnloadable;
}

}  // namespace

std::vector<ModelDescriptor> ModelCatalog::scan() {
  std::vector<ModelDescriptor> result;
  const std::filesystem::path dir = ModelPaths::conventionalModelDirectory();
  std::error_code ec;
  if (!std::filesystem::exists(dir, ec) || ec || !std::filesystem::is_directory(dir, ec) || ec) {
    return result;
  }

  for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
    if (ec) {
      break;
    }
    if (!entry.is_regular_file()) {
      continue;
    }
    const std::string fileName = entry.path().filename().string();
    static constexpr char kSuffix[] = ".gguf";
    static constexpr std::size_t kSuffixLen = std::char_traits<char>::length(kSuffix);
    if (fileName.size() <= kSuffixLen || toLower(fileName).compare(fileName.size() - kSuffixLen, kSuffixLen,
                                                                     kSuffix) != 0) {
      continue;
    }

    ModelDescriptor descriptor;
    descriptor.filePath = entry.path();
    descriptor.fileName = QString::fromStdString(fileName);
    std::error_code sizeEc;
    descriptor.sizeBytes = static_cast<int64_t>(std::filesystem::file_size(entry.path(), sizeEc));
    if (sizeEc) {
      descriptor.sizeBytes = 0;
    }
    descriptor.tierHint = ModelCatalog::classifyTierByFileName(descriptor.fileName);
    descriptor.availability = checkGgufMagic(entry.path());
    result.push_back(std::move(descriptor));
  }

  std::sort(result.begin(), result.end(),
            [](const ModelDescriptor &a, const ModelDescriptor &b) { return a.fileName < b.fileName; });
  return result;
}

ModelDescriptor::TierHint ModelCatalog::classifyTierByFileName(const QString &fileName) {
  // 档位标签启发式分类（research.md §5，由原 .cpp 匿名命名空间私有 classifyTier()
  // 提升为公开静态方法）：文件名包含 "3b"/"3-b" 归类 Low；包含 "7b"/"7-b" 归类
  // Medium（新增，对应 FR-001 新中配推荐）；包含 "14b" 归类 High（变更：14B 现为
  // 新高配推荐，原 Medium 判定已不再适用）；包含 "32b" 归类 High（不变）；均不
  // 匹配归类 Unknown。
  const std::string lower = toLower(fileName.toStdString());
  if (lower.find("3b") != std::string::npos || lower.find("3-b") != std::string::npos) {
    return ModelDescriptor::TierHint::Low;
  }
  if (lower.find("7b") != std::string::npos || lower.find("7-b") != std::string::npos) {
    return ModelDescriptor::TierHint::Medium;
  }
  if (lower.find("14b") != std::string::npos) {
    return ModelDescriptor::TierHint::High;
  }
  if (lower.find("32b") != std::string::npos) {
    return ModelDescriptor::TierHint::High;
  }
  return ModelDescriptor::TierHint::Unknown;
}

std::chrono::milliseconds ModelCatalog::tierDebounceWindow(ModelDescriptor::TierHint tier) {
  // data-model.md 二 TierDebounceProfile / contracts/debounce-tier-window-
  // internal-contract.md 三：Unknown 复用 Low 档位数值 200ms（用户已显式决策，
  // research.md §5）。
  switch (tier) {
    case ModelDescriptor::TierHint::Low:
      return std::chrono::milliseconds(200);
    case ModelDescriptor::TierHint::Medium:
      return std::chrono::milliseconds(500);
    case ModelDescriptor::TierHint::High:
      return std::chrono::milliseconds(800);
    case ModelDescriptor::TierHint::Unknown:
    default:
      return std::chrono::milliseconds(200);
  }
}

ModelDescriptor::DraftCandidate ModelCatalog::classifyDraftCandidateByFileName(const QString &fileName) {
  // 004-speculative-decoding（research.md §5.1，contracts/model-catalog-draft-
  // pairing-internal-contract.md 二.1）：与 classifyTierByFileName() 同构的独立
  // 分类方法，文件名子串匹配不区分大小写；均不匹配 -> None。已核实（research.md
  // §5.1 表格）两档候选草稿文件名均不会命中 classifyTierByFileName() 既有四个
  // 分支，二者互不冲突，故本方法不修改、不复用后者任何分支逻辑。
  const std::string lower = toLower(fileName.toStdString());
  if (lower.find("0.5b") != std::string::npos || lower.find("0-5b") != std::string::npos) {
    return ModelDescriptor::DraftCandidate::Size0_5B;
  }
  if (lower.find("1.5b") != std::string::npos || lower.find("1-5b") != std::string::npos) {
    return ModelDescriptor::DraftCandidate::Size1_5B;
  }
  return ModelDescriptor::DraftCandidate::None;
}

std::vector<RecommendedDraftModelProfile> ModelCatalog::recommendedDraftProfiles() {
  // 004-speculative-decoding（data-model.md 二，FR-004）：固定两档静态数据，不
  // 依赖文件系统状态。approxSizeDescription 按实际 GGUF Q4_K_M 量化文件的典型
  // 大小估算（0.5B/1.5B 参数规模的合理量级，不编造夸张数值）。
  return {
      RecommendedDraftModelProfile{
          ModelDescriptor::DraftCandidate::Size0_5B,
          QStringLiteral("Qwen2.5-0.5B-Instruct"),
          QStringLiteral("GGUF Q4_K_M"),
          QStringLiteral("约 0.4GB"),
      },
      RecommendedDraftModelProfile{
          ModelDescriptor::DraftCandidate::Size1_5B,
          QStringLiteral("Qwen2.5-1.5B-Instruct"),
          QStringLiteral("GGUF Q4_K_M"),
          QStringLiteral("约 1GB"),
      },
  };
}

ModelDescriptor::DraftCandidate ModelCatalog::pairedDraftCandidate(ModelDescriptor::TierHint targetTier) {
  // 004-speculative-decoding（research.md §5.3 用户决策：方案①，contracts/
  // model-catalog-draft-pairing-internal-contract.md 三）：固定查表，MUST NOT
  // 考虑候选方案②～④。
  switch (targetTier) {
    case ModelDescriptor::TierHint::Medium:
      return ModelDescriptor::DraftCandidate::Size0_5B;
    case ModelDescriptor::TierHint::High:
      return ModelDescriptor::DraftCandidate::Size1_5B;
    case ModelDescriptor::TierHint::Low:
    case ModelDescriptor::TierHint::Unknown:
    default:
      return ModelDescriptor::DraftCandidate::None;
  }
}

std::vector<RecommendedModelProfile> ModelCatalog::recommendedProfiles() {
  // FR-012 原文数值（data-model.md §7），固定说明性数据，不涉及本阶段新决策。
  return {
      RecommendedModelProfile{
          RecommendedModelProfile::Tier::Low,
          QStringLiteral("Qwen2.5-3B-Instruct"),
          QStringLiteral("GGUF Q4_K_M"),
          QStringLiteral("约 2GB"),
          QStringLiteral("通用低配设备"),
      },
      RecommendedModelProfile{
          RecommendedModelProfile::Tier::Medium,
          QStringLiteral("Qwen2.5-7B-Instruct"),
          QStringLiteral("GGUF Q4_K_M"),
          QStringLiteral("约 4.6GB"),
          QStringLiteral("Apple M1 Pro + 32GB 统一内存 / Windows + RTX 4090 24GB 显存"),
      },
      RecommendedModelProfile{
          RecommendedModelProfile::Tier::High,
          QStringLiteral("Qwen2.5-14B-Instruct"),
          QStringLiteral("GGUF Q4_K_M"),
          QStringLiteral("约 9GB"),
          QStringLiteral("Apple M1 Pro + 32GB 统一内存 / Windows + RTX 4090 24GB 显存"),
      },
  };
}

}  // namespace babel::slowpath
