#include "babel/src/logging/DaemonLogger.h"

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <cstdlib>
#include <filesystem>
#include <vector>

namespace babel::logging {

void DaemonLogger::initialize(const std::string &logFilePath) {
  std::string targetPath = logFilePath;
  if (targetPath.empty()) {
    const char *home = std::getenv("HOME");
#if defined(_WIN32)
    if (!home) {
      home = std::getenv("USERPROFILE");
    }
#endif
    if (home) {
      targetPath = (std::filesystem::path(home) / ".local" / "state" / "babel" / "daemon.log").string();
    } else {
      targetPath = "/tmp/babel-daemon.log";
    }
  }

  std::error_code ec;
  std::filesystem::create_directories(std::filesystem::path(targetPath).parent_path(), ec);

  try {
    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    // 10 MB 单文件，保留 3 份历史轮转
    auto fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(targetPath, 10 * 1024 * 1024, 3);

    std::vector<spdlog::sink_ptr> sinks{consoleSink, fileSink};
    auto logger = std::make_shared<spdlog::logger>("babel_daemon", sinks.begin(), sinks.end());
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] [%n] %v");
    logger->set_level(spdlog::level::debug);
    logger->flush_on(spdlog::level::info);

    spdlog::set_default_logger(logger);
    spdlog::info("DaemonLogger: Initialized logging to {}", targetPath);
  } catch (const spdlog::spdlog_ex &ex) {
    // 降级使用控制台日志
    spdlog::error("DaemonLogger: Log init failed: {}", ex.what());
  }
}

void DaemonLogger::flush() {
  spdlog::default_logger()->flush();
}

}  // namespace babel::logging

