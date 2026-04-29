#pragma once
#define ASIO_STANDALONE
#include <asio.hpp>
#include <string>
#include <functional>

class SocketServer {
public:
    SocketServer(asio::io_context& io_context, const std::string& path);
    void start_accept(std::function<void(asio::local::stream_protocol::socket)> on_connect);

private:
    asio::local::stream_protocol::acceptor acceptor_;
};
