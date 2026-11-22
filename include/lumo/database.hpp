#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <cstdint>
#include <vector>

struct sqlite3;

namespace lumo {

struct SignalRecord {
    std::int64_t id = 0;
    std::string symbol;
    std::string direction;
    double confidence = 0.0;
    std::string reason;
    std::string created_at;
};

struct ObservationRecord {
    std::int64_t id = 0;
    std::int64_t observed_at = 0;
    std::int64_t reference_candle_time = 0;
    std::string symbol;
    double reference_close = 0.0;
    std::string predicted_direction;
    std::string actual_direction;
    std::string payload_json;
};

struct TradeEventRecord {
    std::int64_t deal_ticket = 0;
    std::int64_t position_id = 0;
    std::int64_t order_id = 0;
    std::int64_t account_login = 0;
    std::int64_t magic = 0;
    std::int64_t event_time = 0;
    std::string account_server;
    std::string symbol;
    std::string direction;
    std::string status;
    double price = 0.0;
    double stop_loss = 0.0;
    double take_profit = 0.0;
    double volume = 0.0;
    double pnl = 0.0;
};

class Database {
public:
    explicit Database(const std::string& path);
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    void save_signal(const std::string& symbol, const std::string& direction,
                     double confidence, const std::string& reason);
    std::vector<SignalRecord> recent_signals(int limit);
    void save_observation(std::int64_t observed_at, std::int64_t account_login,
                          const std::string& account_server,
                          const std::string& symbol,
                          std::int64_t reference_candle_time,
                          double reference_close,
                          const std::string& predicted_direction,
                          const std::string& payload_json);
    void evaluate_latest_observation(const std::string& symbol,
                                    std::int64_t next_candle_time,
                                    std::int64_t expected_previous_candle_time,
                                    double next_close,
                                    std::int64_t evaluated_at,
                                    std::int64_t account_login,
                                    const std::string& account_server);
    std::vector<ObservationRecord> recent_observations(
        int limit, std::int64_t account_login,
        const std::string& account_server);
    void save_trade(const std::string& symbol, const std::string& direction,
                    double entry_price, double stop_loss, double take_profit,
                    double volume, const std::string& status,
                    const std::string& metadata_json = "{}");
    void update_trade_status(std::int64_t trade_id, const std::string& status,
                            std::optional<double> pnl = std::nullopt,
                            std::optional<std::string> closed_at = std::nullopt);
    bool save_trade_event(const TradeEventRecord& event);
    std::vector<TradeEventRecord> recent_trade_events(
        int limit, std::int64_t account_login,
        const std::string& account_server);
    void set_state(const std::string& key, const std::string& value);
    std::optional<std::string> get_state(const std::string& key);

private:
    sqlite3* connection_ = nullptr;
    std::mutex mutex_;
};

}  // namespace lumo
