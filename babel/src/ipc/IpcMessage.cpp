#include "babel/src/ipc/IpcMessage.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

namespace babel::ipc {

std::string messageTypeToString(MessageType type) {
  switch (type) {
    case MessageType::Hello:
      return "HELLO";
    case MessageType::KeyDown:
      return "KEY_DOWN";
    case MessageType::Reset:
      return "RESET";
    case MessageType::Commit:
      return "COMMIT";
    case MessageType::Heartbeat:
      return "HEARTBEAT";
    default:
      return "UNKNOWN";
  }
}

MessageType stringToMessageType(const std::string &str) {
  if (str == "HELLO") return MessageType::Hello;
  if (str == "KEY_DOWN") return MessageType::KeyDown;
  if (str == "RESET") return MessageType::Reset;
  if (str == "COMMIT") return MessageType::Commit;
  if (str == "HEARTBEAT") return MessageType::Heartbeat;
  return MessageType::Unknown;
}

std::vector<uint8_t> IpcMessage::serialize() const {
  QJsonObject root;
  root[QStringLiteral("seq")] = static_cast<qint64>(seq);
  root[QStringLiteral("type")] = QString::fromStdString(messageTypeToString(type));

  QJsonDocument payloadDoc = QJsonDocument::fromJson(QByteArray::fromRawData(rawPayloadJson.data(), static_cast<qsizetype>(rawPayloadJson.size())));
  if (payloadDoc.isObject()) {
    root[QStringLiteral("payload")] = payloadDoc.object();
  } else {
    root[QStringLiteral("payload")] = QJsonObject();
  }

  const QByteArray jsonBytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
  const uint32_t length = static_cast<uint32_t>(jsonBytes.size());

  std::vector<uint8_t> result;
  result.reserve(4 + length);

  // 4 字节网络大端序长度
  result.push_back(static_cast<uint8_t>((length >> 24) & 0xFF));
  result.push_back(static_cast<uint8_t>((length >> 16) & 0xFF));
  result.push_back(static_cast<uint8_t>((length >> 8) & 0xFF));
  result.push_back(static_cast<uint8_t>(length & 0xFF));

  result.insert(result.end(), jsonBytes.begin(), jsonBytes.end());
  return result;
}

std::optional<IpcMessage> IpcMessage::fromJson(const std::string &jsonStr) {
  const QByteArray bytes(jsonStr.data(), static_cast<qsizetype>(jsonStr.size()));
  const QJsonDocument doc = QJsonDocument::fromJson(bytes);
  if (!doc.isObject()) {
    return std::nullopt;
  }

  const QJsonObject root = doc.object();
  IpcMessage msg;
  msg.seq = static_cast<uint64_t>(root.value(QStringLiteral("seq")).toInteger(0));
  msg.type = stringToMessageType(root.value(QStringLiteral("type")).toString().toStdString());

  if (root.contains(QStringLiteral("payload"))) {
    const QJsonValue payloadVal = root.value(QStringLiteral("payload"));
    if (payloadVal.isObject()) {
      msg.rawPayloadJson = QJsonDocument(payloadVal.toObject()).toJson(QJsonDocument::Compact).toStdString();
    } else {
      msg.rawPayloadJson = "{}";
    }
  }

  return msg;
}

IpcMessage IpcMessage::makeKeyDownResponse(uint64_t seq, const KeyDownResponsePayload &payload) {
  IpcMessage msg;
  msg.seq = seq;
  msg.type = MessageType::KeyDown;

  QJsonObject obj;
  obj[QStringLiteral("consumed")] = payload.consumed;
  obj[QStringLiteral("action")] = QString::fromStdString(payload.action);
  obj[QStringLiteral("markedText")] = QString::fromStdString(payload.markedText);
  obj[QStringLiteral("commitText")] = QString::fromStdString(payload.commitText);

  msg.rawPayloadJson = QJsonDocument(obj).toJson(QJsonDocument::Compact).toStdString();
  return msg;
}

IpcMessage IpcMessage::makeHelloResponse(uint64_t seq, const HelloResponsePayload &payload) {
  IpcMessage msg;
  msg.seq = seq;
  msg.type = MessageType::Hello;

  QJsonObject obj;
  obj[QStringLiteral("version")] = QString::fromStdString(payload.version);
  obj[QStringLiteral("daemonStatus")] = QString::fromStdString(payload.daemonStatus);
  obj[QStringLiteral("modelAvailable")] = payload.modelAvailable;
  obj[QStringLiteral("modelPath")] = QString::fromStdString(payload.modelPath);

  msg.rawPayloadJson = QJsonDocument(obj).toJson(QJsonDocument::Compact).toStdString();
  return msg;
}

std::optional<IpcMessage> tryExtractFrame(std::vector<uint8_t> &buffer) {
  if (buffer.size() < 4) {
    return std::nullopt;
  }

  const uint32_t length = (static_cast<uint32_t>(buffer[0]) << 24) |
                          (static_cast<uint32_t>(buffer[1]) << 16) |
                          (static_cast<uint32_t>(buffer[2]) << 8) |
                          static_cast<uint32_t>(buffer[3]);

  // 最大防攻击单包限制 1MB
  if (length > 1024 * 1024) {
    buffer.clear();
    return std::nullopt;
  }

  if (buffer.size() < 4 + length) {
    return std::nullopt;
  }

  const std::string jsonStr(reinterpret_cast<const char *>(buffer.data() + 4), length);
  buffer.erase(buffer.begin(), buffer.begin() + 4 + length);

  return IpcMessage::fromJson(jsonStr);
}

}  // namespace babel::ipc

