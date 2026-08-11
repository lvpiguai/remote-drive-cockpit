#include "websocket_server.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {

// WebSocket 协议参数
constexpr std::size_t kMaxMessageSize = 64 * 1024;
constexpr char kWebSocketGuid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

// 将 socket 切换为非阻塞模式
bool setNonBlocking(int fd) {
  const int flags = fcntl(fd, F_GETFL, 0);
  return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

// 循环发送直至完整写出缓冲区
bool sendAll(int fd, const std::string &data) {
  std::size_t offset = 0;
  while (offset < data.size()) {
    const ssize_t sent =
        send(fd, data.data() + offset, data.size() - offset, MSG_NOSIGNAL);
    if (sent > 0) {
      offset += static_cast<std::size_t>(sent);
      continue;
    }
    if (sent < 0 && errno == EINTR) {
      continue;
    }
    return false;
  }
  return true;
}

// 移除字符串首尾空白
std::string trim(std::string value) {
  const std::size_t begin = value.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    return {};
  }
  const std::size_t end = value.find_last_not_of(" \t\r\n");
  return value.substr(begin, end - begin + 1);
}

// 根据客户端密钥生成 WebSocket 握手摘要
std::string websocketAccept(const std::string &key) {
  const std::string source = key + kWebSocketGuid;
  unsigned char digest[SHA_DIGEST_LENGTH]{};
  SHA1(reinterpret_cast<const unsigned char *>(source.data()), source.size(),
       digest);

  unsigned char encoded[4 * ((SHA_DIGEST_LENGTH + 2) / 3) + 1]{};
  const int size = EVP_EncodeBlock(encoded, digest, SHA_DIGEST_LENGTH);
  return std::string(reinterpret_cast<char *>(encoded), size);
}

}  // namespace

// 关闭客户端和监听 socket
WebSocketServer::~WebSocketServer() {
  closeClient();
  if (listener_fd_ >= 0) {
    close(listener_fd_);
  }
}

// 在本机指定端口启动 WebSocket 监听
bool WebSocketServer::startListening(std::uint16_t port) {
  // 创建 TCP socket
  listener_fd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (listener_fd_ < 0) {
    perror("websocket socket");
    return false;
  }

  // 允许快速重启复用端口
  int reuse = 1;
  setsockopt(listener_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  // 配置本机监听地址
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  // 绑定端口并启动非阻塞监听
  if (bind(listener_fd_, reinterpret_cast<sockaddr *>(&address),
           sizeof(address)) < 0 ||
      listen(listener_fd_, 1) < 0 || !setNonBlocking(listener_fd_)) {
    perror("websocket listen");
    close(listener_fd_);
    listener_fd_ = -1;
    return false;
  }
  return true;
}

// 接受新客户端并替换旧连接
bool WebSocketServer::acceptClient() {
  // 准备客户端地址
  sockaddr_in address{};
  socklen_t address_size = sizeof(address);

  // 接受新连接
  const int next_fd = accept(
      listener_fd_, reinterpret_cast<sockaddr *>(&address), &address_size);
  if (next_fd < 0) {
    return false;
  }

  // 替换当前客户端
  closeClient();
  client_fd_ = next_fd;

  // 切换为非阻塞模式
  if (!setNonBlocking(client_fd_)) {
    closeClient();
    return false;
  }
  return true;
}

// 读取连接数据并向上层分发完整消息
void WebSocketServer::receiveMessages(const MessageCallback &callback) {
  // 忽略未建立的客户端连接
  if (client_fd_ < 0) {
    return;
  }

  // 读取接收数据
  char tmp_buffer[4096];
  while (true) {
    const ssize_t size =
        recv(client_fd_, tmp_buffer, sizeof(tmp_buffer), 0);
    if (size > 0) {
      buffer_.append(tmp_buffer, static_cast<std::size_t>(size));
      if (buffer_.size() > kMaxMessageSize) {
        closeClient();
        return;
      }
      continue;
    }
    if (size == 0) {
      closeClient();
      return;
    }
    if (errno == EINTR) {
      continue;
    }
    if (errno != EAGAIN && errno != EWOULDBLOCK) {
      closeClient();
      return;
    }
    break;
  }

  // 完成 WebSocket 握手
  if (!handshake_done_ && !completeHandshake()) {
    return;
  }

  // 处理完整消息帧
  if (handshake_done_) {
    processFrames(callback);
  }
}

// 向当前客户端发送文本消息
bool WebSocketServer::sendText(const std::string &payload) {
  return sendFrame(0x1, payload);
}

// 关闭当前客户端并清空解析状态
void WebSocketServer::closeClient() {
  if (client_fd_ >= 0) {
    close(client_fd_);
  }
  client_fd_ = -1;
  handshake_done_ = false;
  buffer_.clear();
}

// 解析 HTTP Upgrade 请求并完成握手
bool WebSocketServer::completeHandshake() {
  // 等待完整 HTTP 请求
  const std::size_t request_end = buffer_.find("\r\n\r\n");
  if (request_end == std::string::npos) {
    return false;
  }

  // 提取握手密钥
  const std::string request = buffer_.substr(0, request_end + 4);
  constexpr char key_header[] = "Sec-WebSocket-Key:";
  const std::size_t key_pos = request.find(key_header);
  if (request.rfind("GET ", 0) != 0 || key_pos == std::string::npos) {
    closeClient();
    return false;
  }

  const std::size_t value_begin = key_pos + std::strlen(key_header);
  const std::size_t value_end = request.find("\r\n", value_begin);
  if (value_end == std::string::npos) {
    closeClient();
    return false;
  }

  const std::string key =
      trim(request.substr(value_begin, value_end - value_begin));

  // 返回升级响应
  const std::string response =
      "HTTP/1.1 101 Switching Protocols\r\n"
      "Upgrade: websocket\r\n"
      "Connection: Upgrade\r\n"
      "Sec-WebSocket-Accept: " +
      websocketAccept(key) + "\r\n\r\n";
  if (!sendAll(client_fd_, response)) {
    closeClient();
    return false;
  }

  buffer_.erase(0, request_end + 4);
  handshake_done_ = true;
  return true;
}

// 从接收缓冲区解析并处理 WebSocket 帧
bool WebSocketServer::processFrames(const MessageCallback &callback) {
  while (buffer_.size() >= 2) {
    // 解析帧头
    const auto *bytes = reinterpret_cast<const unsigned char *>(buffer_.data());
    const bool final = (bytes[0] & 0x80u) != 0;
    const std::uint8_t opcode = bytes[0] & 0x0fu;
    const bool masked = (bytes[1] & 0x80u) != 0;
    std::uint64_t payload_size = bytes[1] & 0x7fu;
    std::size_t header_size = 2;

    // 解析扩展长度
    if (payload_size == 126) {
      if (buffer_.size() < 4) return true;
      payload_size = (static_cast<std::uint64_t>(bytes[2]) << 8) | bytes[3];
      header_size = 4;
    } else if (payload_size == 127) {
      if (buffer_.size() < 10) return true;
      payload_size = 0;
      for (int i = 2; i < 10; ++i) {
        payload_size = (payload_size << 8) | bytes[i];
      }
      header_size = 10;
    }

    // 校验帧格式
    if (!masked || !final || payload_size > kMaxMessageSize ||
        payload_size > std::numeric_limits<std::size_t>::max()) {
      closeClient();
      return false;
    }

    // 等待完整负载
    if (buffer_.size() < header_size + 4 + payload_size) {
      return true;
    }

    // 解码掩码负载
    const unsigned char *mask = bytes + header_size;
    const std::size_t payload_begin = header_size + 4;
    std::string payload(static_cast<std::size_t>(payload_size), '\0');
    for (std::size_t i = 0; i < payload.size(); ++i) {
      payload[i] = buffer_[payload_begin + i] ^ mask[i % 4];
    }
    buffer_.erase(0, payload_begin + payload.size());

    // 处理帧类型
    if (opcode == 0x1) {
      callback(payload);
    } else if (opcode == 0x8) {
      sendFrame(0x8, payload);
      closeClient();
      return false;
    } else if (opcode == 0x9) {
      if (!sendFrame(0xA, payload)) {
        closeClient();
        return false;
      }
    } else if (opcode != 0xA) {
      closeClient();
      return false;
    }
  }
  return true;
}

// 向客户端发送一个 WebSocket 帧
bool WebSocketServer::sendFrame(std::uint8_t opcode,
                                const std::string &payload) {
  if (payload.size() > kMaxMessageSize || client_fd_ < 0 || !handshake_done_) {
    return false;
  }

  const std::uint64_t payload_size = payload.size();
  std::string frame;
  frame.push_back(static_cast<char>(0x80u | opcode));
  if (payload_size <= 125) {
    frame.push_back(static_cast<char>(payload_size));
  } else if (payload_size <= 0xffff) {
    frame.push_back(static_cast<char>(126));
    frame.push_back(static_cast<char>((payload_size >> 8) & 0xff));
    frame.push_back(static_cast<char>(payload_size & 0xff));
  } else {
    frame.push_back(static_cast<char>(127));
    for (int shift = 56; shift >= 0; shift -= 8) {
      frame.push_back(static_cast<char>((payload_size >> shift) & 0xff));
    }
  }
  frame += payload;
  return sendAll(client_fd_, frame);
}
