#pragma once
#include <asio.hpp>
#include <string>
#include <functional>

class SocketServer {
public:
    SocketServer(asio::io_context& io_context, const std::string& path);
    void start_accept(std::function<bool(asio::local::stream_protocol::socket)> on_connect);
    void stop();

private:
    asio::local::stream_protocol::acceptor acceptor_;
};
