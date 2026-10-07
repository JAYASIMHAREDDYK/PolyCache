#pragma once

#include "src/storage/store.hpp"
#include "src/persistence/aof.hpp"
#include "src/metrics/metrics.hpp"
#include "src/protocol/resp.hpp"
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>

namespace polycache::command {

struct CommandContext {
    storage::Store& store;
    persistence::AofManager& aof;
    metrics::MetricsRegistry& metrics;
};

struct ExecutionResult {
    std::string response;
    bool is_write{false};
    bool success{true};
};

using CommandHandler = std::function<ExecutionResult(const std::vector<std::string>& args, CommandContext& ctx)>;

class CommandDispatcher {
public:
    CommandDispatcher();

    ExecutionResult execute(const std::vector<std::string>& args, CommandContext& ctx);

private:
    void register_command(std::string name, CommandHandler handler);

    std::unordered_map<std::string, CommandHandler> handlers_;
};

} // namespace polycache::command
