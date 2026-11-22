#include "lumo/core.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {

std::vector<lumo::Candle> make_candles(std::size_t count) {
    std::vector<lumo::Candle> candles;
    candles.reserve(count);
    double price = 2300.0;
    for (std::size_t i = 0; i < count; ++i) {
        const double open = price;
        const double close = price + (i % 8 < 4 ? 1.5 : -1.1);
        candles.push_back({
            1'700'000'000 + static_cast<std::int64_t>(i * 300),
            open,
            std::max(open, close) + 1.5,
            std::min(open, close) - 1.5,
            close,
            100,
        });
        price = close;
    }
    return candles;
}

lumo::Snapshot make_execution_snapshot() {
    lumo::Snapshot snapshot;
    snapshot.symbol = "XAUUSD";
    snapshot.timeframe = "M5";
    snapshot.account.server = "Broker-Demo";
    snapshot.account.currency = "USD";
    snapshot.account.balance = 10'000.0;
    snapshot.account.equity = 10'000.0;
    snapshot.account.day_start_balance = 10'000.0;
    snapshot.account.login = 123456;
    snapshot.account.trade_mode = 0;
    snapshot.account.leverage = 100;
    snapshot.account.daily_trade_count = 0;
    snapshot.quote = {2300.0, 2300.1, 0.01, 2, lumo::unix_time_now()};
    snapshot.candles = make_candles(260);
    snapshot.received_at = lumo::unix_time_now();
    return snapshot;
}

void test_ema() {
    const auto values = lumo::ema({1.0, 2.0, 3.0}, 3);
    assert(values.size() == 3);
    assert(std::abs(values[0] - 1.0) < 1e-9);
    assert(std::abs(values[1] - 1.5) < 1e-9);
    assert(std::abs(values[2] - 2.25) < 1e-9);
}

void test_atr_uses_the_first_candle_range() {
    const std::vector<lumo::Candle> candles{
        {1, 10.0, 12.0, 8.0, 10.0, 1},
        {2, 10.0, 14.0, 9.0, 13.0, 1},
    };
    const auto values = lumo::atr(candles, 1);
    assert(values.size() == 2);
    assert(std::abs(values[0] - 4.0) < 1e-9);
    assert(std::abs(values[1] - 5.0) < 1e-9);
}

void test_strategy_requires_sufficient_data() {
    const lumo::Config config;
    const auto signal = lumo::evaluate_strategy(make_candles(249), config);
    assert(signal.direction == "NO_TRADE");
    assert(signal.reason == "Insufficient market data");
}

void test_strategy_returns_supported_direction() {
    const lumo::Config config;
    const auto signal = lumo::evaluate_strategy(make_candles(260), config);
    assert(signal.direction == "BUY" || signal.direction == "SELL" ||
           signal.direction == "NO_TRADE");
    assert(std::isfinite(signal.confidence));
    assert(signal.confidence >= 0.0 && signal.confidence <= 0.95);
}

void test_risk_controls_keep_execution_disabled() {
    const lumo::Config config;
    const lumo::RiskManager risk(config);
    assert(!risk.can_open_trade(10'000.0, 0, 0.0));
    const double size = risk.calculate_position_size(10'000.0, 2300.0, 2290.0);
    (void)size;
    assert(size > 0.0);
    assert(size <= config.requested_lot_size);
    assert(!risk.validate_trade(0.0, 2290.0, 10'000.0));

    lumo::Config strict_config = config;
    strict_config.min_lot = 0.1;
    strict_config.risk_per_trade = 0.001;
    const lumo::RiskManager strict_risk(strict_config);
    assert(strict_risk.calculate_position_size(100.0, 2300.0, 2290.0) == 0.0);
}

void test_execution_decisions_enforce_mode_and_risk_limits() {
    lumo::Config config;
    auto snapshot = make_execution_snapshot();
    snapshot.execution_enabled = true;
    lumo::MarketObservation observation;
    observation.critical_data_available = true;
    observation.trend = "Bullish";
    observation.atr = 2.0;
    observation.spread_points = 10.0;

    auto decision =
        lumo::decide_execution(config, snapshot, observation, true, true);
    assert(decision.action == "DISABLED");

    config.auto_trading_enabled = true;
    config.bridge_token = std::string(32, 't');
    decision =
        lumo::decide_execution(config, snapshot, observation, true, true);
    assert(decision.action == "OPEN_BUY");
    assert(decision.direction == "BUY");
    assert(decision.stop_loss < snapshot.quote.ask);
    assert(decision.take_profit > snapshot.quote.ask);

    snapshot.account.trade_mode = 2;
    decision =
        lumo::decide_execution(config, snapshot, observation, true, true);
    assert(decision.action == "LIVE_DISABLED");

    config.live_trading_enabled = true;
    snapshot.account.risk_at_stops = 250.0;
    decision =
        lumo::decide_execution(config, snapshot, observation, true, true);
    assert(decision.action == "BLOCKED");
    assert(decision.reason.find("open-risk") != std::string::npos);

    snapshot.account.risk_at_stops = 0.0;
    snapshot.account.daily_trade_count = config.max_daily_trades;
    decision =
        lumo::decide_execution(config, snapshot, observation, true, true);
    assert(decision.action == "LOCKED");
}

void test_execution_closes_managed_positions_at_daily_limit() {
    lumo::Config config;
    config.auto_trading_enabled = true;
    config.bridge_token = std::string(32, 't');
    auto snapshot = make_execution_snapshot();
    snapshot.execution_enabled = true;
    snapshot.account.open_positions = 1;
    snapshot.account.day_pnl = -1000.0;

    lumo::MarketObservation observation;
    const auto decision =
        lumo::decide_execution(config, snapshot, observation, false, true);
    assert(decision.action == "CLOSE_ALL");
    assert(decision.reason == "Daily drawdown limit reached");
}

void test_risk_assessment_reports_stopless_positions_and_daily_limit() {
    lumo::Config config;
    lumo::Account account;
    account.currency = "USD";
    account.balance = 1000.0;
    account.equity = 900.0;
    account.day_start_balance = 1000.0;
    account.day_pnl = -20.0;
    account.risk_at_stops = 20.0;
    account.open_positions = 2;
    account.positions_without_stop = 1;
    account.positions_risk_unavailable = 0;
    const auto risk = lumo::assess_risk(config, account, 50.0);
    assert(risk.day_loss_percent == 2.0);
    assert(risk.open_risk_percent > 2.2 && risk.open_risk_percent < 2.3);
    assert(!risk.trade_lock);
    assert(risk.warnings.size() == 2);

    account.day_pnl = -101.0;
    const auto locked_risk = lumo::assess_risk(config, account, 10.0);
    assert(locked_risk.trade_lock);
}

void test_account_matching_requires_login_and_server() {
    lumo::Account account;
    account.login = 123456;
    account.server = "Broker-Demo";
    assert(lumo::account_matches(account, 123456, "broker-demo"));
    assert(!lumo::account_matches(account, 123457, "Broker-Demo"));
    assert(!lumo::account_matches(account, 123456, "Broker-Live"));
}

void test_market_observation_reports_real_market_features() {
    const auto candles = make_candles(260);
    lumo::Quote quote{candles.back().close, candles.back().close + 0.1,
                      0.01, 2, lumo::unix_time_now()};
    const auto observation = lumo::observe_market(
        candles, quote, lumo::evaluate_strategy(candles, lumo::Config{}),
        lumo::Config{}, lumo::unix_time_now());
    assert(observation.critical_data_available);
    assert(observation.candle_count == candles.size());
    assert(observation.support > 0.0);
    assert(observation.resistance >= observation.support);
    assert(observation.sma_fast > 0.0);
    assert(std::isfinite(observation.macd));
    assert(observation.volatility == "Elevated" ||
           observation.volatility == "Normal" ||
           observation.volatility == "Quiet");
    assert(observation.report.find("News/event feed unavailable") !=
           std::string::npos);

    quote.time -= 61;
    const auto stale = lumo::observe_market(
        candles, quote, {}, lumo::Config{}, lumo::unix_time_now());
    assert(!stale.critical_data_available);
    assert(stale.report.find("trading must remain blocked") !=
           std::string::npos);
}

void test_backtest_uses_candles_and_reports_closed_trades() {
    const auto result = lumo::run_backtest(make_candles(320), lumo::Config{});
    assert(result.total_trades > 0);
    assert(result.wins + result.losses == result.total_trades);
    assert(std::isfinite(result.net_profit));
    assert(std::isfinite(result.max_drawdown));
    assert(result.max_drawdown >= 0.0);
    assert(std::isfinite(result.profit_factor));
}

void test_snapshot_rejects_unordered_candles() {
    lumo::Snapshot snapshot;
    snapshot.symbol = "XAUUSD";
    snapshot.timeframe = "M5";
    snapshot.account = {"Demo", "USD", 1000.0, 1000.0, 0};
    snapshot.account.login = 123456;
    snapshot.account.server = "Broker-Demo";
    snapshot.account.trade_mode = 0;
    snapshot.account.leverage = 100;
    snapshot.quote = {2300.0, 2300.1, 0.01, 2, lumo::unix_time_now()};
    snapshot.candles = {
        {1'700'000'300, 2300.0, 2301.0, 2299.0, 2300.5, 100},
        {1'700'000'000, 2300.5, 2301.5, 2299.5, 2301.0, 100},
    };
    bool rejected = false;
    try {
        lumo::validate_snapshot(snapshot, lumo::Config{});
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    assert(rejected);
}

void test_snapshot_accepts_verified_ordered_data() {
    lumo::Snapshot snapshot;
    snapshot.symbol = "XAUUSD.a";
    snapshot.timeframe = "M5";
    snapshot.account = {"Demo", "USD", 1000.0, 1000.0};
    snapshot.account.server = "Broker-Demo";
    snapshot.account.trade_mode = 0;
    snapshot.account.login = 123456;
    snapshot.account.leverage = 100;
    snapshot.quote = {2300.0, 2300.1, 0.01, 2, lumo::unix_time_now()};
    snapshot.candles = make_candles(250);
    lumo::validate_snapshot(snapshot, lumo::Config{});

    snapshot.account.leverage = 0;
    bool rejected = false;
    try {
        lumo::validate_snapshot(snapshot, lumo::Config{});
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    assert(rejected);
}

void test_snapshot_rejects_unsupported_timeframe() {
    lumo::Snapshot snapshot;
    snapshot.symbol = "XAUUSD";
    snapshot.timeframe = "INVALID";
    snapshot.account.currency = "USD";
    snapshot.account.trade_mode = 0;
    snapshot.quote = {2300.0, 2300.1, 0.01, 2, lumo::unix_time_now()};
    snapshot.candles = make_candles(1);
    bool rejected = false;
    try {
        lumo::validate_snapshot(snapshot, lumo::Config{});
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    assert(rejected);
}

void test_snapshot_rejects_overflowing_position_counts() {
    lumo::Snapshot snapshot;
    snapshot.symbol = "XAUUSD";
    snapshot.timeframe = "M5";
    snapshot.account.currency = "USD";
    snapshot.account.trade_mode = 0;
    snapshot.account.open_positions = std::numeric_limits<int>::max();
    snapshot.account.positions_without_stop =
        std::numeric_limits<int>::max();
    snapshot.account.positions_risk_unavailable =
        std::numeric_limits<int>::max();
    snapshot.quote = {2300.0, 2300.1, 0.01, 2, lumo::unix_time_now()};
    snapshot.candles = make_candles(1);

    bool rejected = false;
    try {
        lumo::validate_snapshot(snapshot, lumo::Config{});
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    assert(rejected);
}

void test_config_rejects_history_too_short_for_strategy() {
    lumo::Config config;
    config.history_count = 249;
    bool rejected = false;
    try {
        config.validate();
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    assert(rejected);
}

}  // namespace

int main() {
    test_ema();
    test_atr_uses_the_first_candle_range();
    test_strategy_requires_sufficient_data();
    test_strategy_returns_supported_direction();
    test_risk_controls_keep_execution_disabled();
    test_execution_decisions_enforce_mode_and_risk_limits();
    test_execution_closes_managed_positions_at_daily_limit();
    test_risk_assessment_reports_stopless_positions_and_daily_limit();
    test_account_matching_requires_login_and_server();
    test_market_observation_reports_real_market_features();
    test_backtest_uses_candles_and_reports_closed_trades();
    test_snapshot_rejects_unordered_candles();
    test_snapshot_accepts_verified_ordered_data();
    test_snapshot_rejects_unsupported_timeframe();
    test_snapshot_rejects_overflowing_position_counts();
    test_config_rejects_history_too_short_for_strategy();
    std::cout << "All C++ core tests passed\n";
}
