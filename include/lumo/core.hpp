#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lumo {

struct Config {
    std::string bot_name = "Forex Bot";
    std::string owner = "GONTSE";
    std::string symbol = "XAUUSD";
    std::string timeframe = "M5";
    std::size_t history_count = 500;
    int ema_fast = 50;
    int ema_slow = 200;
    int rsi_period = 14;
    int atr_period = 14;
    int port = 8080;
    double risk_per_trade = 0.01;
    double max_risk_per_trade = 0.03;
    double daily_loss_limit = 0.03;
    double requested_lot_size = 0.10;
    double min_lot = 0.01;
    double max_lot = 20.0;
    double lot_step = 0.01;
    int max_daily_trades = 5;
    double spread_limit_points = 40.0;
    double stop_loss_atr_multiplier = 1.8;
    double backtest_reward_risk = 2.0;
    std::string bridge_token;
    std::string database_path = "data/forex_bot.db";
    bool live_trading_enabled = false;
    bool emergency_stop = false;
    bool trading_lock = false;

    static Config from_environment();
    void validate() const;
};

struct Candle {
    std::int64_t time = 0;
    double open = 0.0;
    double high = 0.0;
    double low = 0.0;
    double close = 0.0;
    std::uint64_t tick_volume = 0;
};

struct Signal {
    std::string direction = "NO_TRADE";
    double confidence = 0.0;
    std::string reason = "Insufficient market data";
};

struct Indicators {
    double ema_fast = 0.0;
    double ema_slow = 0.0;
    double rsi = 50.0;
    double atr = 0.0;
};

struct Account {
    std::string server;
    std::string currency;
    double balance = 0.0;
    double equity = 0.0;
    double margin = 0.0;
    double free_margin = 0.0;
    double day_start_balance = 0.0;
    double day_pnl = 0.0;
    double floating_pnl = 0.0;
    double open_volume = 0.0;
    double risk_at_stops = 0.0;
    int trade_mode = -1;
    int open_positions = 0;
    int positions_without_stop = 0;
    int positions_risk_unavailable = 0;
    std::int64_t login = 0;
    int leverage = 0;
};

struct Quote {
    double bid = 0.0;
    double ask = 0.0;
    double point = 0.0;
    int digits = 0;
    std::int64_t time = 0;
};

struct Snapshot {
    std::string symbol;
    std::string timeframe;
    Account account;
    Quote quote;
    std::vector<Candle> candles;
    Signal signal;
    Indicators indicators;
    std::int64_t received_at = 0;
};

struct BacktestResult {
    int total_trades = 0;
    int wins = 0;
    int losses = 0;
    double net_profit = 0.0;
    double max_drawdown = 0.0;
    double win_rate = 0.0;
    double profit_factor = 0.0;
    bool profit_factor_unbounded = false;
};

struct RiskSummary {
    double risk_per_trade = 0.0;
    double daily_loss_limit = 0.0;
    int daily_trade_limit = 0;
    bool trade_lock = false;
    int daily_trade_count = 0;
    double daily_loss = 0.0;
    double day_loss_percent = 0.0;
    double open_risk_percent = 0.0;
    double margin_level_percent = 0.0;
    int open_positions = 0;
    int positions_without_stop = 0;
    int positions_risk_unavailable = 0;
    std::vector<std::string> warnings;
};

struct ScanResult {
    Signal signal;
    Indicators indicators;
};

struct MarketObservation {
    std::int64_t observed_at = 0;
    std::string classification = "Uncertain";
    std::string trend = "Uncertain";
    std::string volatility = "Unknown";
    std::string candle_pattern = "Unclassified";
    std::string market_session = "Unknown";
    std::string volume_condition = "Unavailable";
    std::string report;
    double support = 0.0;
    double resistance = 0.0;
    double sma_fast = 0.0;
    double sma_slow = 0.0;
    double macd = 0.0;
    double macd_signal = 0.0;
    double atr = 0.0;
    double volatility_ratio = 0.0;
    double relative_volume = 0.0;
    double spread_points = 0.0;
    std::size_t candle_count = 0;
    bool critical_data_available = false;
};

struct BrokerPosition {
    std::int64_t ticket = 0;
    double profit = 0.0;
};

struct PositionCommand {
    std::int64_t ticket = 0;
    std::string action;
    std::string reason;
};

class RiskManager {
public:
    explicit RiskManager(const Config& config);

    double calculate_position_size(double account_equity, double entry_price,
                                   double stop_loss, double tick_value = 1.0,
                                   double contract_size = 100.0) const;
    bool can_open_trade(double account_equity, int daily_trade_count,
                        double current_daily_loss) const;
    bool validate_trade(double lot_size, double stop_loss,
                        double account_equity) const;
    void record_daily_state(double start_equity, double daily_loss,
                            int trade_count);
    bool enforce_daily_trade_limit() const;
    void apply_loss_lock(double account_equity);
    void update_daily_loss(double pnl);
    RiskSummary summary() const;

private:
    const Config& config_;
    int daily_trade_count_ = 0;
    double daily_loss_ = 0.0;
    double start_of_day_equity_ = 0.0;
    bool trade_lock_ = false;
};

RiskSummary assess_risk(const Config& config, const Account& account,
                        double spread_points, int daily_trade_count = 0);
std::vector<double> ema(const std::vector<double>& values, int span);
std::vector<double> rsi(const std::vector<double>& values, int period);
std::vector<double> atr(const std::vector<Candle>& candles, int period);
int timeframe_duration_seconds(std::string_view timeframe);
std::string timeframe_from_seconds(int seconds);
Indicators calculate_indicators(const std::vector<Candle>& candles,
                                const Config& config);
Signal evaluate_strategy(const std::vector<Candle>& candles,
                         const Config& config);
ScanResult scan_market(const std::vector<Candle>& candles,
                       const Config& config);
MarketObservation observe_market(const std::vector<Candle>& candles,
                                 const Quote& quote, const Signal& signal,
                                 const Config& config,
                                 std::int64_t observed_at);
BacktestResult run_backtest(const std::vector<Candle>& candles,
                            const Config& config);
std::vector<PositionCommand> manage_positions(
    const std::vector<BrokerPosition>& positions,
    std::optional<double> stop_loss, std::optional<double> take_profit);
bool account_matches(const Account& account, std::int64_t login,
                     std::string_view server);
void validate_snapshot(const Snapshot& snapshot, const Config& config);
std::int64_t unix_time_now();

}  // namespace lumo
