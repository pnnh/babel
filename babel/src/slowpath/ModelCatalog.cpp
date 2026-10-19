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

/// 档位标签启发式分类（research.md §7.2，内部实现细节）：文件名包含 "3b"/"3-b"
/// 归类 Low；包含 "14b" 归类 Medium；包含 "32b" 归类 High；均不匹配归类 Unknown。
ModelDescriptor::TierHint classifyTier(const std::string &fileName) {
  const std::string lower = toLower(fileName);
  if (lower.find("3b") != std::string::npos || lower.find("3-b") != std::string::npos) {
    return ModelDescriptor::TierHint::Low;
  }
  if (lower.find("14b") != std::string::npos) {
    return ModelDescriptor::TierHint::Medium;
  }
  if (lower.find("32b") != std::string::npos) {
    return ModelDescriptor::TierHint::High;
  }
  return ModelDescriptor::TierHint::Unknown;
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
    descriptor.tierHint = classifyTier(fileName);
    descriptor.availability = checkGgufMagic(entry.path());
    result.push_back(std::move(descriptor));
  }

  std::sort(result.begin(), result.end(),
            [](const ModelDescriptor &a, const ModelDescriptor &b) { return a.fileName < b.fileName; });
  return result;
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
          QStringLiteral("Qwen2.5-14B-Instruct"),
          QStringLiteral("GGUF Q4_K_M"),
          QStringLiteral("约 9GB"),
          QStringLiteral("Apple M1 Pro + 32GB 统一内存 / Windows + RTX 4090 24GB 显存"),
      },
      RecommendedModelProfile{
          RecommendedModelProfile::Tier::High,
          QStringLiteral("Qwen2.5-32B-Instruct"),
          QStringLiteral("GGUF Q4_K_M"),
          QStringLiteral("约 19.85GB"),
          QStringLiteral("Apple M1 Pro + 32GB 统一内存 / Windows + RTX 4090 24GB 显存"),
      },
  };
}

}  // namespace babel::slowpath
