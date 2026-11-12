#pragma once

#include <spdlog/spdlog.h>
#include <string>

namespace babel::logging {

/// 守护进程结构化日志工具
class DaemonLogger {
 public:
  /// 初始化日志输出：同时输出至控制台及 $HOME/.local/state/babel/daemon.log 滚动文件
  static void initialize(const std::string &logFilePath = "");

  /// 刷新日志
  static void flush();
};

}  // namespace babel::logging

