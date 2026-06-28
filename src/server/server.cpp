#include "server.hpp"
#include "../logging/logger.hpp"
#include "../protocol/parser.hpp"

#include <chrono>
#include <condition_variable>
#include <csignal>
#include <mutex>
#include <thread>

namespace mimir {

Server::Server(Config cfg) : cfg_(std::move(cfg)) {
    // Storage
    store_ = std::make_unique<Store>(cfg_.num_shards, cfg_.eviction, cfg_.max_memory);

    // WAL
    if (cfg_.wal_enabled) {
        try {
            wal_ = std::make_unique<Wal>(cfg_.wal_path,
                                         cfg_.fsync_always,
                                         cfg_.fsync_every_sec);
        } catch (const std::exception& e) {
            LOG_ERR("WAL init failed: %s — disabling persistence", e.what());
        }
    }

    // Snapshot
    snapshot_ = std::make_unique<Snapshot>(cfg_.rdb_path);

    // Restore persisted state
    restore_state();

    // Expiry sweeper
    expiry_ = std::make_unique<ExpiryManager>(
        *store_,
        std::chrono::milliseconds(cfg_.expiry_check_interval_ms));
    expiry_->start();

    // Thread pool
    pool_ = std::make_unique<ThreadPool>(cfg_.worker_threads);

    // Command handler
    handler_ = std::make_unique<CommandHandler>(*store_, wal_.get());

    // TCP server
    tcp_ = std::make_unique<TcpServer>(
        cfg_.host, cfg_.port, cfg_.backlog, *pool_,
        [this](const std::string& line) -> std::string {
            Command cmd = Parser::parse(line);
            return handler_->handle(cmd);
        });
}

Server::~Server() { stop(); }

void Server::restore_state() {
    // Try snapshot first (faster bulk load), then replay WAL on top.
    if (!snapshot_->load(*store_)) {
        LOG_INFO("No snapshot found or load failed — starting fresh");
    }

    if (wal_) {
        wal_->replay([this](const WalRecord& rec) {
            switch (rec.op) {
                case WalOp::SET: {
                    std::optional<std::chrono::seconds> ttl;
                    if (rec.ttl_sec > 0) ttl = std::chrono::seconds(rec.ttl_sec);
                    store_->set(rec.key, rec.value, ttl);
                    break;
                }
                case WalOp::DEL:
                    store_->del(rec.key);
                    break;
                case WalOp::EXPIRE:
                    store_->expire(rec.key, std::chrono::seconds(rec.ttl_sec));
                    break;
                case WalOp::FLUSH:
                    store_->flush_all();
                    break;
                case WalOp::INCR: {
                    // WAL stores the delta; replay applies the same increment.
                    int64_t delta = std::stoll(rec.value);
                    store_->incr(rec.key, delta);
                    break;
                }
            }
        });
    }
}

void Server::start_snapshot_timer() {
    if (cfg_.snapshot_interval_sec <= 0 || !snapshot_) return;
    snap_running_ = true;
    snap_thread_ = std::thread([this] {
        while (true) {
            std::unique_lock<std::mutex> lock(snap_mu_);
            snap_cv_.wait_for(lock,
                std::chrono::seconds(cfg_.snapshot_interval_sec),
                [this] { return !snap_running_.load(); });
            if (!snap_running_) break;
            lock.unlock();
            LOG_INFO("Taking periodic snapshot...");
            if (snapshot_->save(*store_) && wal_)
                wal_->truncate();
        }
    });
}

void Server::run() {
    start_snapshot_timer();
    tcp_->run(); // blocks
}

void Server::stop() {
    // Guard against double-stop: destructor + explicit signal handler both call stop().
    bool expected = false;
    if (!stopped_.compare_exchange_strong(expected, true)) return;

    if (tcp_)    tcp_->stop();
    if (expiry_) expiry_->stop();

    snap_running_ = false;
    snap_cv_.notify_all();
    if (snap_thread_.joinable()) snap_thread_.join();

    // Final snapshot on clean shutdown
    if (snapshot_) snapshot_->save(*store_);
    if (wal_)      wal_->sync();

    LOG_INFO("Server stopped cleanly");
}

} // namespace mimir
