#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <chrono>

namespace polycache::metrics {

class MetricsRegistry {
public:
    MetricsRegistry();

    void record_connection_opened();
    void record_connection_closed();
    void record_command(uint64_t latency_micros);

    // Call once per second or during INFO to update ops/sec
    void tick_second();

    uint64_t total_connections() const { return total_connections_; }
    uint64_t connected_clients() const { return connected_clients_; }
    uint64_t total_commands() const { return total_commands_; }
    uint64_t ops_per_sec() const { return ops_per_sec_; }

    // Returns percentiles in microseconds: p50, p95, p99
    struct LatencyPercentiles {
        double p50{0.0};
        double p95{0.0};
        double p99{0.0};
    };
    LatencyPercentiles get_latency_percentiles() const;

private:
    uint64_t total_connections_{0};
    uint64_t connected_clients_{0};
    uint64_t total_commands_{0};
    uint64_t ops_per_sec_{0};

    uint64_t last_total_commands_{0};
    std::chrono::steady_clock::time_point last_ops_sec_check_;

    // Ring buffer of recent latencies in microseconds (up to 10,000 samples)
    static constexpr size_t LATENCY_WINDOW = 10000;
    std::vector<uint32_t> latency_samples_;
    size_t latency_index_{0};
    bool latency_wrapped_{false};
};

} // namespace polycache::metrics
