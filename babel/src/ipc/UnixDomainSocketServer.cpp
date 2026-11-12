#include "babel/src/ipc/UnixDomainSocketServer.h"

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <cstdlib>
#include <filesystem>
#include <iostream>

#include "babel/src/fastpath/FastCorrector.h"
#include "babel/src/ipc/IpcMessage.h"
#include "babel/src/slowpath/ModelPaths.h"

namespace babel::ipc {

UnixDomainSocketServer::UnixDomainSocketServer(std::shared_ptr<babel::fastpath::FastCorrector> fastCorrector,
                                               QObject *parent)
    : QObject(parent), m_fastCorrector(std::move(fastCorrector)), m_server(new QLocalServer(this)) {
  connect(m_server, &QLocalServer::newConnection, this, &UnixDomainSocketServer::handleNewConnection);
}

UnixDomainSocketServer::~UnixDomainSocketServer() {
  stop();
}

bool UnixDomainSocketServer::start(const std::string &socketPath) {
  stop();

  std::string targetPath = socketPath;
  if (targetPath.empty()) {
    const char *home = std::getenv("HOME");
#if defined(_WIN32)
    if (!home) {
      home = std::getenv("USERPROFILE");
    }
#endif
    if (home) {
      targetPath = (std::filesystem::path(home) / ".local" / "run" / "babel.sock").string();
    } else {
      targetPath = "/tmp/babel.sock";
    }
  }
  m_socketPath = targetPath;

  // 确保父目录存在
  std::error_code ec;
  std::filesystem::create_directories(std::filesystem::path(m_socketPath).parent_path(), ec);

  // 清理遗留套接字文件
  std::filesystem::remove(m_socketPath, ec);

  m_server->setSocketOptions(QLocalServer::UserAccessOption);
  const bool ok = m_server->listen(QString::fromStdString(m_socketPath));
  if (ok) {
    std::cout << "[UnixDomainSocketServer] Listening on " << m_socketPath << std::endl;
  } else {
    std::cerr << "[UnixDomainSocketServer] Failed to listen on " << m_socketPath << ": "
              << m_server->errorString().toStdString() << std::endl;
  }
  return ok;
}

void UnixDomainSocketServer::stop() {
  if (m_server && m_server->isListening()) {
    m_server->close();
    std::error_code ec;
    std::filesystem::remove(m_socketPath, ec);
  }
}

std::string UnixDomainSocketServer::socketPath() const {
  return m_socketPath;
}

void UnixDomainSocketServer::handleNewConnection() {
  while (m_server->hasPendingConnections()) {
    QLocalSocket *socket = m_server->nextPendingConnection();
    std::cout << "[UnixDomainSocketServer] Client connected" << std::endl;
    connect(socket, &QLocalSocket::readyRead, this, &UnixDomainSocketServer::handleClientReadyRead);
    connect(socket, &QLocalSocket::disconnected, this, &UnixDomainSocketServer::handleClientDisconnected);
  }
}

void UnixDomainSocketServer::handleClientReadyRead() {
  auto *socket = qobject_cast<QLocalSocket *>(sender());
  if (!socket) return;

  const QByteArray incoming = socket->readAll();
  m_receiveBuffer.insert(m_receiveBuffer.end(), incoming.begin(), incoming.end());

  while (auto msgOpt = tryExtractFrame(m_receiveBuffer)) {
    processMessage(socket, *msgOpt);
  }
}

void UnixDomainSocketServer::handleClientDisconnected() {
  auto *socket = qobject_cast<QLocalSocket *>(sender());
  std::cout << "[UnixDomainSocketServer] Client disconnected" << std::endl;
  if (socket) {
    socket->deleteLater();
  }
}

void UnixDomainSocketServer::processMessage(QLocalSocket *socket, const IpcMessage &msg) {
  if (!socket || socket->state() != QLocalSocket::ConnectedState) {
    return;
  }

  if (msg.type == MessageType::Hello) {
    HelloResponsePayload helloPayload;
    helloPayload.version = "0.1.0";
    helloPayload.daemonStatus = "READY";
    const auto modelPath = babel::slowpath::ModelPaths::resolveModelFilePath();
    std::error_code ec;
    helloPayload.modelAvailable = std::filesystem::exists(modelPath, ec) && !ec;
    helloPayload.modelPath = modelPath.string();

    const IpcMessage resp = IpcMessage::makeHelloResponse(msg.seq, helloPayload);
    const auto frame = resp.serialize();
    socket->write(reinterpret_cast<const char *>(frame.data()), static_cast<qint64>(frame.size()));
    socket->flush();
    return;
  }

  if (msg.type == MessageType::Reset) {
    m_currentComposingText.clear();
    return;
  }

  if (msg.type == MessageType::KeyDown) {
    KeyDownResponsePayload keyResp;
    keyResp.consumed = false;
    keyResp.action = "NONE";

    const QByteArray bytes(msg.rawPayloadJson.data(), static_cast<qsizetype>(msg.rawPayloadJson.size()));
    const QJsonDocument doc = QJsonDocument::fromJson(bytes);
    if (doc.isObject()) {
      const QJsonObject obj = doc.object();
      const int keyCode = obj.value(QStringLiteral("keyCode")).toInt(0);
      const QString characters = obj.value(QStringLiteral("characters")).toString();

      // macOS 键码对应（US ANSI 键盘布局）：
      // 36: Return / Enter
      // 51: Backspace / Delete
      // 49: Space
      // 53: Escape
      if (keyCode == 53) {  // Escape: 取消输入
        m_currentComposingText.clear();
        keyResp.consumed = true;
        keyResp.action = "UPDATE_PREEDIT";
        keyResp.markedText = "";
      } else if (keyCode == 36) {  // Enter: 确认上屏原始拼音/当前内容
        if (!m_currentComposingText.empty()) {
          keyResp.consumed = true;
          keyResp.action = "COMMIT_TEXT";
          keyResp.commitText = m_currentComposingText;
          m_currentComposingText.clear();
        }
      } else if (keyCode == 51) {  // Backspace
        if (!m_currentComposingText.empty()) {
          m_currentComposingText.pop_back();
          keyResp.consumed = true;
          keyResp.action = "UPDATE_PREEDIT";
          keyResp.markedText = m_currentComposingText;
        }
      } else if (keyCode == 49) {  // Space: 上屏候选词/当前词
        if (!m_currentComposingText.empty()) {
          keyResp.consumed = true;
          keyResp.action = "COMMIT_TEXT";
          // 若有快流分词与候选，优先取首个候选词或当前文本
          if (m_fastCorrector) {
            const auto lattice = m_fastCorrector->buildLattice(QString::fromStdString(m_currentComposingText));
            const QString displayText = lattice.toDisplayText();
            keyResp.commitText = displayText.isEmpty() ? m_currentComposingText : displayText.toStdString();
          } else {
            keyResp.commitText = m_currentComposingText;
          }
          m_currentComposingText.clear();
        }
      } else if (!characters.isEmpty() && characters.at(0).isLetterOrNumber()) {
        m_currentComposingText += characters.toStdString();
        keyResp.consumed = true;
        keyResp.action = "UPDATE_PREEDIT";
        keyResp.markedText = m_currentComposingText;
      }
    }

    const IpcMessage resp = IpcMessage::makeKeyDownResponse(msg.seq, keyResp);
    const auto frame = resp.serialize();
    socket->write(reinterpret_cast<const char *>(frame.data()), static_cast<qint64>(frame.size()));
    socket->flush();
  }
}

}  // namespace babel::ipc

