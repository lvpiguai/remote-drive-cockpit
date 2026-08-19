#pragma once

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <queue>
#include <string>

class WebSocketSession;

// 本机单客户端 WebSocket 服务，不包含业务消息解析
class WebSocketServer {
 public:
  WebSocketServer();

  // 关闭通信资源
  ~WebSocketServer();

  // 禁止复制
  WebSocketServer(const WebSocketServer &) = delete;

  // 禁止赋值
  WebSocketServer &operator=(const WebSocketServer &) = delete;

  // 启动监听
  bool startListening(std::uint16_t port);

  // 推进异步网络事件
  void poll();

  // 取出一条页面消息
  std::optional<std::string> takeMessage();

  // 发送一条消息
  bool sendMessage(const std::string &message);

  // 关闭客户端
  void closeClient();

  // 判断连接是否就绪
  bool connected() const;

 private:
  // 持续等待下一个浏览器连接
  void acceptNext();

  boost::asio::io_context io_context_;                  // 异步事件循环
  boost::asio::ip::tcp::acceptor acceptor_;             // TCP 监听器
  std::shared_ptr<WebSocketSession> session_;           // 当前浏览器连接
  std::queue<std::string> received_messages_;           // 已接收待处理消息
};
