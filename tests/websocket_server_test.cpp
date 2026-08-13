#include "websocket_server.h"

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <unistd.h>

#include <atomic>
#include <cassert>
#include <chrono>
#include <string>
#include <thread>

int main() {
  namespace asio = boost::asio;
  namespace beast = boost::beast;
  namespace websocket = beast::websocket;
  using tcp = asio::ip::tcp;
  using namespace std::chrono_literals;

  const auto port = static_cast<std::uint16_t>(30000 + getpid() % 10000);
  WebSocketServer server;
  assert(server.startListening(port));

  std::atomic<bool> client_done{false};
  bool client_succeeded = false;
  std::thread client([&] {
    try {
      asio::io_context io_context;
      websocket::stream<beast::tcp_stream> stream(io_context);
      beast::get_lowest_layer(stream).expires_after(2s);
      beast::get_lowest_layer(stream).connect(
          {asio::ip::make_address("127.0.0.1"), port});
      stream.handshake("127.0.0.1", "/");
      stream.write(asio::buffer(std::string("select")));

      beast::flat_buffer response;
      stream.read(response);
      client_succeeded = beast::buffers_to_string(response.data()) == "accepted";

      beast::error_code error;
      beast::get_lowest_layer(stream).socket().close(error);
    } catch (...) {
      client_succeeded = false;
    }
    client_done = true;
  });

  bool received = false;
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (!client_done && std::chrono::steady_clock::now() < deadline) {
    server.poll();
    while (auto message = server.takeMessage()) {
      received = *message == "select";
      assert(server.sendText("accepted"));
    }
    std::this_thread::sleep_for(1ms);
  }

  client.join();
  assert(received);
  assert(client_succeeded);
}
