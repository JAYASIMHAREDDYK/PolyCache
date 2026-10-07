#include "src/metrics/metrics.hpp"
#include <algorithm>

namespace polycache::metrics {

MetricsRegistry::MetricsRegistry() {
    latency_samples_.resize(LATENCY_WINDOW, 0);
    last_ops_sec_check_ = std::chrono::steady_clock::now();
}

void MetricsRegistry::record_connection_opened() {
    total_connections_++;
    connected_clients_++;
}

void MetricsRegistry::record_connection_closed() {
    if (connected_clients_ > 0) {
        connected_clients_--;
    }
}

void MetricsRegistry::record_command(uint64_t latency_micros) {
    total_commands_++;

    latency_samples_[latency_index_] = static_cast<uint32_t>(std::min<uint64_t>(latency_micros, UINT32_MAX));
    latency_index_++;
    if (latency_index_ >= LATENCY_WINDOW) {
        latency_index_ = 0;
        latency_wrapped_ = true;
    }
}

void MetricsRegistry::tick_second() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_ops_sec_check_).count();
    if (elapsed_ms >= 1000) {
        uint64_t diff = total_commands_ - last_total_commands_;
        ops_per_sec_ = (diff * 1000) / elapsed_ms;
        last_total_commands_ = total_commands_;
        last_ops_sec_check_ = now;
    }
}

MetricsRegistry::LatencyPercentiles MetricsRegistry::get_latency_percentiles() const {
    size_t count = latency_wrapped_ ? LATENCY_WINDOW : latency_index_;
    if (count == 0) {
        return {0.0, 0.0, 0.0};
    }

    std::vector<uint32_t> sorted_samples;
    sorted_samples.reserve(count);
    if (latency_wrapped_) {
        sorted_samples.assign(latency_samples_.begin(), latency_samples_.end());
    } else {
        sorted_samples.assign(latency_samples_.begin(), latency_samples_.begin() + latency_index_);
    }

    std::sort(sorted_samples.begin(), sorted_samples.end());

    auto get_pct = [&](double pct) -> double {
        size_t idx = static_cast<size_t>(pct * (count - 1));
        return static_cast<double>(sorted_samples[idx]);
    };

    return {get_pct(0.50), get_pct(0.95), get_pct(0.99)};
}

} // namespace polycache::metrics
