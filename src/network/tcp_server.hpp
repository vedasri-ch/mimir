#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include "connection.hpp"
#include "../threadpool/thread_pool.hpp"

namespace mimir {

// Event-driven TCP server using Linux epoll.
//
// Design rationale:
//   - A single I/O thread runs the epoll event loop (accept + read-ready).
//   - Once a full command line is parsed from a Connection's read buffer,
//     the actual command execution is dispatched to the ThreadPool so that
//     slow commands don't block the I/O loop.
//   - Writes are performed back on the I/O thread (EPOLLOUT) to avoid
//     concurrent socket writes from multiple worker threads.

class TcpServer {
public:
    using CommandCallback = std::function<std::string(const std::string& line)>;

    TcpServer(const std::string& host, uint16_t port, int backlog,
              ThreadPool& pool, CommandCallback cb);
    ~TcpServer();

    // Blocking: run the epoll event loop until stop() is called.
    void run();
    void stop();

private:
    void setup_listen_socket();
    void setup_epoll();
    void accept_client();
    void handle_read(int fd);
    void handle_write(int fd);
    void remove_client(int fd);
    void arm_write(int fd);

    std::string        host_;
    uint16_t           port_;
    int                backlog_;
    int                listen_fd_  = -1;
    int                epoll_fd_   = -1;
    int                event_fd_   = -1; // used to wake up epoll for shutdown
    std::atomic<bool>  running_{false};
    ThreadPool&        pool_;
    CommandCallback    cb_;

    std::unordered_map<int, std::unique_ptr<Connection>> clients_;
};

} // namespace mimir
