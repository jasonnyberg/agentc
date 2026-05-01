#include "socket_server.h"

#include <system_error>

SocketServer::SocketServer(asio::io_context& io_context, const std::string& path)
    : acceptor_(io_context, asio::local::stream_protocol::endpoint(path)) {}

void SocketServer::start_accept(std::function<bool(asio::local::stream_protocol::socket)> on_connect) {
    while (acceptor_.is_open()) {
        asio::local::stream_protocol::socket socket(acceptor_.get_executor());
        std::error_code ec;
        acceptor_.accept(socket, ec);
        if (ec) {
            if (!acceptor_.is_open()) {
                break;
            }
            continue;
        }
        if (!on_connect(std::move(socket))) {
            stop();
            break;
        }
    }
}

void SocketServer::stop() {
    std::error_code ec;
    acceptor_.close(ec);
}
