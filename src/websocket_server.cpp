#include "websocket_server.h"

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>

#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;

constexpr std::size_t kMaxMessageSize = 64 * 1024;

// 单个浏览器连接，由异步回调共同持有
class WebSocketSession
    : public std::enable_shared_from_this<WebSocketSession> {
public:
  using MessageHandler = std::function<void(std::string)>;

  // 保存连接和消息回调
  WebSocketSession(tcp::socket socket, MessageHandler message_handler)
      : stream_(std::move(socket)),
        message_handler_(std::move(message_handler)) {}

  // 完成握手并开始读消息
  void start() {
    // 限制单条消息大小
    stream_.read_message_max(kMaxMessageSize);

    // 设置服务端超时
    stream_.set_option(
        websocket::stream_base::timeout::suggested(beast::role_type::server));

    // 等待握手完成
    const auto self = shared_from_this();
    stream_.async_accept([self](beast::error_code error) {
      if (error) {
        self->close();
        return;
      }

      // 握手成功后启动读取
      self->connected_ = true;
      self->readNext();
    });
  }

  // 消息入队等待发送
  bool sendMessage(std::string message) {
    if (!connected_)
      return false;

    // 队列空闲时立即启动写入
    write_queue_.push_back(std::move(message));
    if (write_queue_.size() == 1)
      writeNext();
    return true;
  }

  // 关闭底层 TCP 连接
  void close() {
    connected_ = false;
    beast::error_code error;

    // 忽略关闭错误
    beast::get_lowest_layer(stream_).shutdown(tcp::socket::shutdown_both,
                                              error);
    beast::get_lowest_layer(stream_).close(error);
  }

  // 判断连接是否仍可发送
  bool connected() const { return connected_; }

private:
  // 异步读取下一条完整消息
  void readNext() {
    // 回调期间保持会话存活
    const auto self = shared_from_this();
    stream_.async_read(
        read_buffer_, [self](beast::error_code error, std::size_t) {
          if (error) {
            self->close();
            return;
          }

          // 转交完整文本消息
          self->message_handler_(
              beast::buffers_to_string(self->read_buffer_.data()));

          // 清空缓冲并继续读取
          self->read_buffer_.consume(self->read_buffer_.size());
          self->readNext();
        });
  }

  // 串行发送队首文本消息
  void writeNext() {
    // 本服务只发送文本帧
    stream_.text(true);

    // 回调期间保持队列有效
    const auto self = shared_from_this();
    stream_.async_write(
        asio::buffer(write_queue_.front()),
        [self](beast::error_code error, std::size_t) {
          if (error) {
            // 写失败后丢弃队列
            self->close();
            self->write_queue_.clear();
            return;
          }

          // 继续发送剩余消息
          self->write_queue_.pop_front();
          if (!self->write_queue_.empty())
            self->writeNext();
        });
  }

  websocket::stream<tcp::socket> stream_;  // WebSocket 流
  beast::flat_buffer read_buffer_;         // 读取缓冲
  std::deque<std::string> write_queue_;    // 待发送文本队列
  MessageHandler message_handler_;         // 上层消息处理
  bool connected_ = false;                 // 握手后的连接状态
};

// 创建监听器并绑定事件循环
WebSocketServer::WebSocketServer() : acceptor_(io_context_) {}

// 析构时由成员对象释放通信资源
WebSocketServer::~WebSocketServer() = default;

// 启动本机 WebSocket 监听
bool WebSocketServer::startListening(std::uint16_t port) {
  beast::error_code error;

  // 仅监听本机
  const tcp::endpoint endpoint(asio::ip::make_address("127.0.0.1"), port);

  // 创建监听 socket
  acceptor_.open(endpoint.protocol(), error);
  if (error)
    return false;

  // 允许复用端口
  acceptor_.set_option(asio::socket_base::reuse_address(true), error);
  if (error)
    return false;

  // 绑定本机端口并开始监听
  acceptor_.bind(endpoint, error);
  if (error)
    return false;
  acceptor_.listen(1, error);
  if (error)
    return false;

  // 等待首个连接
  acceptNext();
  return true;
}

// 推进当前已就绪的异步事件
void WebSocketServer::poll() {
  // 只处理已就绪事件
  io_context_.restart();
  io_context_.poll();
}

// 取出已缓存的页面消息
std::optional<std::string> WebSocketServer::takeMessage() {
  if (received_messages_.empty())
    return std::nullopt;

  std::string message = std::move(received_messages_.front());
  received_messages_.pop();
  return message;
}

// 向当前浏览器连接发送消息
bool WebSocketServer::sendMessage(const std::string &message) {
  return session_ && session_->sendMessage(message);
}

// 主动关闭当前浏览器连接
void WebSocketServer::closeClient() {
  if (session_)
    session_->close();

  // 清空连接状态
  session_.reset();
  received_messages_ = {};
}

// 判断当前浏览器连接是否可用
bool WebSocketServer::connected() const {
  return session_ && session_->connected();
}

// 接受新连接并替换当前浏览器会话
void WebSocketServer::acceptNext() {
  acceptor_.async_accept([this](beast::error_code error, tcp::socket socket) {
    if (!error) {
      // 新连接替换旧连接
      closeClient();

      // 页面消息先入队
      session_ = std::make_shared<WebSocketSession>(
          std::move(socket), [this](std::string message) {
            received_messages_.push(std::move(message));
          });
      session_->start();
    }

    // 继续等待连接
    acceptNext();
  });
}
