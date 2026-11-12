#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace babel::ipc {

enum class MessageType {
  Unknown,
  Hello,
  KeyDown,
  Reset,
  Commit,
  Heartbeat
};

std::string messageTypeToString(MessageType type);
MessageType stringToMessageType(const std::string &str);

struct KeyDownPayload {
  int keyCode = 0;
  int modifiers = 0;
  std::string characters;
};

struct KeyDownResponsePayload {
  bool consumed = false;
  std::string action;  // "NONE", "UPDATE_PREEDIT", "COMMIT_TEXT"
  std::string markedText;
  std::string commitText;
};

struct HelloResponsePayload {
  std::string version = "0.1.0";
  std::string daemonStatus = "READY";
  bool modelAvailable = false;
  std::string modelPath;
};

struct IpcMessage {
  uint64_t seq = 0;
  MessageType type = MessageType::Unknown;
  std::string rawPayloadJson = "{}";

  /// 序列化为 [4字节网络大端序长度] + [JSON 字节流]
  std::vector<uint8_t> serialize() const;

  /// 从 JSON 字符串解析消息实体
  static std::optional<IpcMessage> fromJson(const std::string &jsonStr);

  /// 快捷构造响应报文
  static IpcMessage makeKeyDownResponse(uint64_t seq, const KeyDownResponsePayload &payload);
  static IpcMessage makeHelloResponse(uint64_t seq, const HelloResponsePayload &payload);
};

/// 分帧解码助手：从字节缓冲区尝试提取一帧完整的报文。
/// 成功提取则返回该报文并从 buffer 前端移除已消费数据；字节不足返回 std::nullopt。
std::optional<IpcMessage> tryExtractFrame(std::vector<uint8_t> &buffer);

}  // namespace babel::ipc

