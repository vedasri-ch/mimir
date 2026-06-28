#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include "../config/config.hpp"
#include "../storage/store.hpp"
#include "../wal/wal.hpp"
#include "../persistence/snapshot.hpp"
#include "../expiration/expiry_manager.hpp"
#include "../threadpool/thread_pool.hpp"
#include "../network/tcp_server.hpp"
#include "command_handler.hpp"

namespace mimir {

// Top-level server object. Wires all subsystems together.
class Server {
public:
    explicit Server(Config cfg);
    ~Server();

    void run();   // blocks until shutdown
    void stop();

private:
    void restore_state();
    void start_snapshot_timer();

    Config                       cfg_;
    std::unique_ptr<Store>       store_;
    std::unique_ptr<Wal>         wal_;
    std::unique_ptr<Snapshot>    snapshot_;
    std::unique_ptr<ExpiryManager> expiry_;
    std::unique_ptr<ThreadPool>  pool_;
    std::unique_ptr<CommandHandler> handler_;
    std::unique_ptr<TcpServer>   tcp_;

    // Background snapshot thread
    std::thread          snap_thread_;
    std::mutex           snap_mu_;
    std::condition_variable snap_cv_;
    std::atomic<bool>    snap_running_{false};
    std::atomic<bool>    stopped_{false};
};

} // namespace mimir
