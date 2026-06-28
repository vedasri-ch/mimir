// Mimir micro-benchmark
// Measures throughput and latency for SET/GET against the in-memory store
// and command handler (no network overhead).

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

#include "../src/storage/store.hpp"
#include "../src/server/command_handler.hpp"
#include "../src/protocol/parser.hpp"

using namespace mimir;
using namespace std::chrono;

static double ns_to_us(double ns)  { return ns / 1000.0; }

struct BenchResult {
    double   throughput_ops_sec;
    double   avg_latency_us;
    double   p99_latency_us;
    uint64_t ops;
};

template <typename Fn>
BenchResult run_bench(const char* name, int threads, uint64_t ops_per_thread, Fn fn) {
    std::vector<std::vector<double>> latencies(threads);
    for (auto& v : latencies) v.reserve(ops_per_thread);

    auto t0 = steady_clock::now();

    std::vector<std::thread> workers;
    workers.reserve(threads);
    for (int t = 0; t < threads; ++t) {
        workers.emplace_back([&, t] {
            for (uint64_t i = 0; i < ops_per_thread; ++i) {
                auto s = steady_clock::now();
                fn(t, i);
                auto e = steady_clock::now();
                latencies[t].push_back(
                    (double)duration_cast<nanoseconds>(e - s).count());
            }
        });
    }
    for (auto& w : workers) w.join();

    auto t1 = steady_clock::now();
    double elapsed_sec = duration_cast<microseconds>(t1 - t0).count() / 1e6;

    uint64_t total_ops = (uint64_t)threads * ops_per_thread;

    // Flatten latencies
    std::vector<double> all;
    all.reserve(total_ops);
    for (auto& v : latencies) all.insert(all.end(), v.begin(), v.end());
    std::sort(all.begin(), all.end());

    double avg = std::accumulate(all.begin(), all.end(), 0.0) / (double)all.size();
    double p99 = all[(std::size_t)(all.size() * 0.99)];

    BenchResult r;
    r.throughput_ops_sec = total_ops / elapsed_sec;
    r.avg_latency_us     = ns_to_us(avg);
    r.p99_latency_us     = ns_to_us(p99);
    r.ops                = total_ops;

    std::printf("%-30s  threads=%-3d  ops=%-8llu  tput=%10.0f ops/s  "
                "avg=%.2f µs  p99=%.2f µs\n",
                name, threads, (unsigned long long)total_ops,
                r.throughput_ops_sec, r.avg_latency_us, r.p99_latency_us);

    return r;
}

int main() {
    constexpr int     OPS        = 200'000;
    constexpr int     THREADS[]  = {1, 4, 8};

    Store          store(16);
    CommandHandler handler(store, nullptr);

    std::printf("\n=== Mimir In-Process Benchmark ===\n\n");
    std::printf("%-30s  %-10s  %-8s  %-22s  %-12s  %-12s\n",
                "Benchmark", "Threads", "Ops", "Throughput", "Avg Lat", "P99 Lat");
    std::printf("%s\n", std::string(110, '-').c_str());

    for (int t : THREADS) {
        // SET benchmark
        run_bench("SET (inline)", t, OPS / t, [&](int tid, uint64_t i) {
            std::string line = "SET key" + std::to_string(tid * 1000000 + i) +
                               " value" + std::to_string(i);
            handler.handle(Parser::parse(line));
        });

        // GET benchmark (keys pre-populated from SET above)
        run_bench("GET (inline)", t, OPS / t, [&](int tid, uint64_t i) {
            std::string line = "GET key" + std::to_string(tid * 1000000 + i);
            handler.handle(Parser::parse(line));
        });

        // Mixed 50/50
        run_bench("SET+GET 50/50 (inline)", t, OPS / t, [&](int tid, uint64_t i) {
            if (i % 2 == 0) {
                std::string line = "SET bench" + std::to_string(tid) + " v";
                handler.handle(Parser::parse(line));
            } else {
                std::string line = "GET bench" + std::to_string(tid);
                handler.handle(Parser::parse(line));
            }
        });

        std::printf("\n");
    }

    // INCR contention benchmark (all threads hit the same key)
    for (int t : THREADS) {
        run_bench("INCR (shared key, contention)", t, OPS / t, [&](int, uint64_t) {
            handler.handle(Parser::parse("INCR __bench_counter__"));
        });
    }

    std::printf("\nDone.\n");
    return 0;
}
