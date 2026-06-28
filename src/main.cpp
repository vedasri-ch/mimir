#include "config/config.hpp"
#include "logging/logger.hpp"
#include "server/server.hpp"

#include <csignal>
#include <cstdlib>
#include <iostream>

static mimir::Server* g_server = nullptr;

static void signal_handler(int sig) {
    LOG_INFO("Caught signal %d, shutting down...", sig);
    if (g_server) g_server->stop();
}

int main(int argc, char* argv[]) {
    std::string config_path = "config.yaml";
    if (argc >= 2) config_path = argv[1];

    mimir::Config cfg = mimir::Config::from_file(config_path);

    // Apply log level
    if (cfg.log_level == "DEBUG")
        mimir::Logger::instance().set_level(mimir::LogLevel::DEBUG);
    else if (cfg.log_level == "WARN")
        mimir::Logger::instance().set_level(mimir::LogLevel::WARN);
    else if (cfg.log_level == "ERROR")
        mimir::Logger::instance().set_level(mimir::LogLevel::ERR);

    LOG_INFO("Starting Mimir v1.0.0");

    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);
    std::signal(SIGPIPE, SIG_IGN); // ignore broken pipe

    try {
        mimir::Server server(cfg);
        g_server = &server;
        server.run();
    } catch (const std::exception& e) {
        LOG_ERR("Fatal error: %s", e.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
