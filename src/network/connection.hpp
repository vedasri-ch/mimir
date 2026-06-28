#pragma once

#include <string>
#include <functional>

namespace mimir {

// Represents a live client connection.
// Owns the read buffer and accumulates data until a full command line is ready.
class Connection {
public:
    using ResponseFn = std::function<void(const std::string&)>;

    explicit Connection(int fd);
    ~Connection();

    int  fd() const { return fd_; }
    bool is_closed() const { return closed_; }
    void close();

    // Append raw data received from the socket.
    // Returns complete command lines via `on_line` callback.
    void feed(const char* data, std::size_t len,
              const std::function<void(const std::string&)>& on_line);

    // Write data to the socket. Handles EAGAIN via buffering.
    bool write(const std::string& data);

    // Flush pending write buffer. Returns false if connection should be closed.
    bool flush();

    const std::string& write_buf() const { return write_buf_; }
    bool has_pending_write() const { return !write_buf_.empty(); }

private:
    int         fd_;
    bool        closed_;
    std::string read_buf_;
    std::string write_buf_;
};

} // namespace mimir
