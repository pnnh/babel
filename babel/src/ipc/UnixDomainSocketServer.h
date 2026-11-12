#pragma once

#include <QByteArray>
#include <QObject>
#include <memory>
#include <string>
#include <vector>

class QLocalServer;
class QLocalSocket;

namespace babel::fastpath {
class FastCorrector;
}

namespace babel::ipc {

class IpcMessage;

/// Unix Domain Socket 守护进程服务端
/// 负责在 $HOME/.local/run/babel.sock 监听来自 Swift IMKit 前端的击键请求
class UnixDomainSocketServer : public QObject {
  Q_OBJECT

 public:
  explicit UnixDomainSocketServer(std::shared_ptr<babel::fastpath::FastCorrector> fastCorrector,
                                  QObject *parent = nullptr);
  ~UnixDomainSocketServer() override;

  /// 启动监听。socketPath 为空时使用约定路径 $HOME/.local/run/babel.sock
  bool start(const std::string &socketPath = "");

  /// 停止监听并释放套接字
  void stop();

  /// 当前监听的物理文件路径
  std::string socketPath() const;

 private slots:
  void handleNewConnection();
  void handleClientReadyRead();
  void handleClientDisconnected();

 private:
  void processMessage(QLocalSocket *socket, const IpcMessage &msg);

  std::shared_ptr<babel::fastpath::FastCorrector> m_fastCorrector;
  QLocalServer *m_server = nullptr;
  std::string m_socketPath;
  std::vector<uint8_t> m_receiveBuffer;
  std::string m_currentComposingText;
};

}  // namespace babel::ipc

