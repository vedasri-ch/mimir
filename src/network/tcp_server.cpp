#include "tcp_server.hpp"
#include "../logging/logger.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdexcept>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

namespace mimir {

static constexpr int MAX_EVENTS = 1024;
static constexpr int READ_BUF   = 4096;

static void set_nonblocking(int fd) {
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) throw std::runtime_error("fcntl F_GETFL failed");
    if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
        throw std::runtime_error("fcntl F_SETFL failed");
}

TcpServer::TcpServer(const std::string& host, uint16_t port, int backlog,
                     ThreadPool& pool, CommandCallback cb)
    : host_(host), port_(port), backlog_(backlog), pool_(pool), cb_(std::move(cb)) {
    setup_listen_socket();
    setup_epoll();
}

TcpServer::~TcpServer() {
    if (epoll_fd_ >= 0) ::close(epoll_fd_);
    if (listen_fd_ >= 0) ::close(listen_fd_);
    if (event_fd_  >= 0) ::close(event_fd_);
}

void TcpServer::setup_listen_socket() {
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) throw std::runtime_error("socket() failed");

    int opt = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
    ::setsockopt(listen_fd_, IPPROTO_TCP, TCP_NODELAY,  &opt, sizeof(opt));

    set_nonblocking(listen_fd_);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port_);
    ::inet_pton(AF_INET, host_.c_str(), &addr.sin_addr);

    if (::bind(listen_fd_, (sockaddr*)&addr, sizeof(addr)) < 0)
        throw std::runtime_error(std::string("bind() failed: ") + std::strerror(errno));
    if (::listen(listen_fd_, backlog_) < 0)
        throw std::runtime_error("listen() failed");

    LOG_INFO("Listening on %s:%u", host_.c_str(), port_);
}

void TcpServer::setup_epoll() {
    epoll_fd_ = ::epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ < 0) throw std::runtime_error("epoll_create1 failed");

    // Register listen socket
    epoll_event ev{};
    ev.events  = EPOLLIN | EPOLLET;
    ev.data.fd = listen_fd_;
    ::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, listen_fd_, &ev);

    // Shutdown eventfd
    event_fd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ev.events  = EPOLLIN;
    ev.data.fd = event_fd_;
    ::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, event_fd_, &ev);
}

void TcpServer::run() {
    running_ = true;
    epoll_event events[MAX_EVENTS];

    while (running_) {
        int n = ::epoll_wait(epoll_fd_, events, MAX_EVENTS, 100 /*ms timeout*/);
        if (n < 0) {
            if (errno == EINTR) continue;
            LOG_ERR("epoll_wait error: %s", std::strerror(errno));
            break;
        }

        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;

            if (fd == event_fd_) {
                // shutdown signal
                running_ = false;
                break;
            }

            if (fd == listen_fd_) {
                accept_client();
                continue;
            }

            if (events[i].events & (EPOLLERR | EPOLLHUP)) {
                remove_client(fd);
                continue;
            }

            if (events[i].events & EPOLLIN)  handle_read(fd);
            if (events[i].events & EPOLLOUT) handle_write(fd);
        }
    }
    LOG_INFO("Server event loop exited");
}

void TcpServer::stop() {
    uint64_t val = 1;
    ::write(event_fd_, &val, sizeof(val));
}

void TcpServer::accept_client() {
    while (true) {
        sockaddr_in addr{};
        socklen_t len = sizeof(addr);
        int cfd = ::accept4(listen_fd_, (sockaddr*)&addr, &len, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (cfd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            LOG_WARN("accept4 failed: %s", std::strerror(errno));
            break;
        }

        // TCP_NODELAY for low latency
        int opt = 1;
        ::setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

        clients_[cfd] = std::make_unique<Connection>(cfd);

        epoll_event ev{};
        ev.events  = EPOLLIN | EPOLLET | EPOLLRDHUP;
        ev.data.fd = cfd;
        ::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, cfd, &ev);

        char ip[INET_ADDRSTRLEN];
        ::inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
        LOG_DEBUG("New client fd=%d from %s:%u", cfd, ip, ntohs(addr.sin_port));
    }
}

void TcpServer::handle_read(int fd) {
    auto it = clients_.find(fd);
    if (it == clients_.end()) return;
    Connection* conn = it->second.get();

    char buf[READ_BUF];
    while (true) {
        ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n > 0) {
            conn->feed(buf, static_cast<std::size_t>(n),
                [this, conn](const std::string& line) {
                    // Command execution happens on the I/O thread intentionally:
                    // commands are fast (sub-microsecond for simple gets/sets)
                    // and dispatching to the pool adds synchronisation overhead.
                    // For future slow commands (SCAN, KEYS) move cb_ into pool_.enqueue.
                    auto response = cb_(line);
                    conn->write(response);
                    if (conn->has_pending_write()) arm_write(conn->fd());
                });
            if (conn->is_closed()) { remove_client(fd); return; }
        } else if (n == 0) {
            remove_client(fd);
            return;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            remove_client(fd);
            return;
        }
    }
}

void TcpServer::handle_write(int fd) {
    auto it = clients_.find(fd);
    if (it == clients_.end()) return;
    Connection* conn = it->second.get();

    if (!conn->flush()) { remove_client(fd); return; }

    if (!conn->has_pending_write()) {
        // Re-arm for read only
        epoll_event ev{};
        ev.events  = EPOLLIN | EPOLLET | EPOLLRDHUP;
        ev.data.fd = fd;
        ::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev);
    }
}

void TcpServer::arm_write(int fd) {
    epoll_event ev{};
    ev.events  = EPOLLIN | EPOLLOUT | EPOLLET | EPOLLRDHUP;
    ev.data.fd = fd;
    ::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev);
}

void TcpServer::remove_client(int fd) {
    ::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
    clients_.erase(fd);
    LOG_DEBUG("Client fd=%d disconnected", fd);
}

} // namespace mimir
