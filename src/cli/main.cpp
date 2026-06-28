// Mimir CLI client — a minimal REPL that connects to a Mimir server
// and speaks RESP over TCP.
#include "../protocol/serializer.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <netdb.h>
#include <netinet/in.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

static int connect_to(const char* host, uint16_t port) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    if (::inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
        // Try resolving hostname
        struct hostent* he = ::gethostbyname(host);
        if (!he) { ::close(fd); return -1; }
        std::memcpy(&addr.sin_addr, he->h_addr, he->h_length);
    }

    if (::connect(fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

static bool send_all(int fd, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (n <= 0) return false;
        sent += n;
    }
    return true;
}

static std::string read_response(int fd) {
    std::string result;
    char buf[4096];
    // Read until we have a complete RESP response (simple heuristic: ends with \r\n)
    while (true) {
        ssize_t n = ::recv(fd, buf, sizeof(buf) - 1, 0);
        if (n <= 0) break;
        buf[n] = '\0';
        result += buf;
        if (result.size() >= 2 &&
            result[result.size()-2] == '\r' &&
            result[result.size()-1] == '\n') break;
    }
    return result;
}

static std::string format_response(const std::string& resp) {
    if (resp.empty()) return "(empty)";
    char type = resp[0];
    std::string body = resp.substr(1);
    // Strip trailing \r\n
    while (!body.empty() && (body.back() == '\n' || body.back() == '\r'))
        body.pop_back();

    switch (type) {
        case '+': return body;
        case '-': return "(error) " + body;
        case ':': return "(integer) " + body;
        case '$':
            if (body == "-1") return "(nil)";
            // bulk: skip the length line
            {
                auto nl = body.find("\r\n");
                if (nl != std::string::npos) body = body.substr(nl + 2);
                while (!body.empty() && (body.back()=='\r'||body.back()=='\n'))
                    body.pop_back();
            }
            return "\"" + body + "\"";
        default:
            return resp;
    }
}

int main(int argc, char* argv[]) {
    const char* host = "127.0.0.1";
    uint16_t    port = 6379;

    if (argc >= 2) host = argv[1];
    if (argc >= 3) port = static_cast<uint16_t>(std::stoi(argv[2]));

    int fd = connect_to(host, port);
    if (fd < 0) {
        std::cerr << "Could not connect to " << host << ":" << port << "\n";
        return 1;
    }

    std::cout << "Connected to Mimir at " << host << ":" << port << "\n";
    std::cout << "Type commands (e.g. SET foo bar, GET foo). CTRL+C to quit.\n\n";

    std::string line;
    while (true) {
        std::cout << host << ":" << port << "> ";
        std::cout.flush();
        if (!std::getline(std::cin, line)) break;
        if (line.empty()) continue;
        if (line == "quit" || line == "exit") break;

        // Send as inline command + newline
        if (!send_all(fd, line + "\r\n")) {
            std::cerr << "Send error\n";
            break;
        }

        std::string resp = read_response(fd);
        std::cout << format_response(resp) << "\n";
    }

    ::close(fd);
    return 0;
}
