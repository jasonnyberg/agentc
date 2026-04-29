#include "socket_server.h"

SocketServer::SocketServer(asio::io_context& io_context, const std::string& path)
    : acceptor_(io_context, asio::local::stream_protocol::endpoint(path)) {}

void SocketServer::start_accept(std::function<void(asio::local::stream_protocol::socket)> on_connect) {
    while (true) {
        asio::local::stream_protocol::socket socket(acceptor_.get_executor());
        acceptor_.accept(socket);
        on_connect(std::move(socket));
    }
}
