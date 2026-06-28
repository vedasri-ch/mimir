#include "connection.hpp"
#include "../logging/logger.hpp"

#include <cerrno>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>

namespace mimir {

Connection::Connection(int fd) : fd_(fd), closed_(false) {}

Connection::~Connection() { close(); }

void Connection::close() {
    if (!closed_) {
        ::close(fd_);
        closed_ = true;
    }
}

void Connection::feed(const char* data, std::size_t len,
                      const std::function<void(const std::string&)>& on_line) {
    read_buf_.append(data, len);

    std::size_t pos;
    while ((pos = read_buf_.find('\n')) != std::string::npos) {
        std::string line = read_buf_.substr(0, pos);
        read_buf_.erase(0, pos + 1);
        // Strip trailing \r if present
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (!line.empty())
            on_line(line);
    }

    // Prevent unbounded buffer growth from a misbehaving client
    if (read_buf_.size() > 64 * 1024) {
        LOG_WARN("Client fd=%d: read buffer overflow, closing", fd_);
        close();
    }
}

bool Connection::write(const std::string& data) {
    write_buf_ += data;
    return flush();
}

bool Connection::flush() {
    while (!write_buf_.empty()) {
        ssize_t n = ::send(fd_, write_buf_.data(), write_buf_.size(), MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return true; // retry later
            LOG_WARN("Connection::flush send error fd=%d: %s", fd_, std::strerror(errno));
            close();
            return false;
        }
        write_buf_.erase(0, static_cast<std::size_t>(n));
    }
    return true;
}

} // namespace mimir
