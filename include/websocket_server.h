#pragma once

#include <cstdint>
#include <functional>
#include <string>

// 本机单客户端 WebSocket 服务，不包含业务消息解析
class WebSocketServer {
 public:
  using MessageCallback = std::function<void(const std::string &)>;

  WebSocketServer() = default;

  // 关闭通信资源
  ~WebSocketServer();

  // 禁止复制
  WebSocketServer(const WebSocketServer &) = delete;

  // 禁止赋值
  WebSocketServer &operator=(const WebSocketServer &) = delete;

  // 启动监听
  bool startListening(std::uint16_t port);

  // 接受客户端
  bool acceptClient();

  // 接收消息
  void receiveMessages(const MessageCallback &callback);

  // 发送文本消息
  bool sendText(const std::string &payload);

  // 关闭客户端
  void closeClient();

  // 获取监听 socket
  int listenerFd() const { return listener_fd_; }

  // 获取客户端 socket
  int clientFd() const { return client_fd_; }

  // 判断连接是否就绪
  bool connected() const { return handshake_done_; }

 private:
  // 完成握手
  bool completeHandshake();

  // 处理消息帧
  bool processFrames(const MessageCallback &callback);

  // 发送消息帧
  bool sendFrame(std::uint8_t opcode, const std::string &payload);

  int listener_fd_ = -1;         // 监听 socket
  int client_fd_ = -1;           // 客户端 socket
  bool handshake_done_ = false;  // 握手已完成
  std::string buffer_;           // 接收缓冲区
};
