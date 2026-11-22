#include "lumo/core.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace lumo {
namespace {

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::optional<std::string> read_environment(const char* name) {
#ifdef _WIN32
    char* value = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&value, &size, name) != 0 || value == nullptr) {
        return std::nullopt;
    }
    std::string result(value);
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    return value == nullptr ? std::optional<std::string>{}
                            : std::optional<std::string>{value};
#endif
}

void set_environment_if_missing(const std::string& key,
                               const std::string& value) {
    if (read_environment(key.c_str()).has_value()) {
        return;
    }
#ifdef _WIN32
    if (_putenv_s(key.c_str(), value.c_str()) != 0) {
        throw std::runtime_error("Could not load " + key + " from .env");
    }
#else
    if (setenv(key.c_str(), value.c_str(), 0) != 0) {
        throw std::runtime_error("Could not load " + key + " from .env");
    }
#endif
}

void load_dotenv() {
    const std::filesystem::path path(".env");
    if (!std::filesystem::exists(path)) {
        return;
    }
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Could not read .env");
    }
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        const auto normalized = trim(line);
        if (normalized.empty() || normalized.front() == '#') {
            continue;
        }
        const auto separator = normalized.find('=');
        if (separator == std::string::npos) {
            throw std::invalid_argument(".env line " +
                                        std::to_string(line_number) +
                                        " must use KEY=VALUE format");
        }
        const auto key = trim(normalized.substr(0, separator));
        auto value = trim(normalized.substr(separator + 1));
        if (key.empty() ||
            !std::all_of(key.begin(), key.end(), [](unsigned char ch) {
                return std::isalnum(ch) || ch == '_';
            })) {
            throw std::invalid_argument(".env line " +
                                        std::to_string(line_number) +
                                        " has an invalid variable name");
        }
        if (value.size() >= 2 &&
            ((value.front() == '"' && value.back() == '"') ||
             (value.front() == '\'' && value.back() == '\''))) {
            value = value.substr(1, value.size() - 2);
        }
        set_environment_if_missing(key, value);
    }
    if (input.bad()) {
        throw std::runtime_error("Failed while reading .env");
    }
}

std::string env_string(const char* name, const std::string& fallback) {
    const auto value = read_environment(name);
    return !value || value->empty() ? fallback : *value;
}

double env_double(const char* name, double fallback) {
    const auto value = read_environment(name);
    if (!value || value->empty()) {
        return fallback;
    }
    std::size_t parsed = 0;
    const double result = std::stod(*value, &parsed);
    if (parsed != value->size() || !std::isfinite(result)) {
        throw std::invalid_argument(std::string(name) + " must be a finite number");
    }
    return result;
}

int env_int(const char* name, int fallback) {
    const auto value = read_environment(name);
    if (!value || value->empty()) {
        return fallback;
    }
    std::size_t parsed = 0;
    const long result = std::stol(*value, &parsed);
    if (parsed != value->size() || result < std::numeric_limits<int>::min() ||
        result > std::numeric_limits<int>::max()) {
        throw std::invalid_argument(std::string(name) + " must be an integer");
    }
    return static_cast<int>(result);
}

bool env_bool(const char* name, bool fallback) {
    const auto value = env_string(name, "");
    if (value.empty()) {
        return fallback;
    }
    std::string normalized;
    normalized.reserve(value.size());
    for (const char ch : value) {
        normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    if (normalized == "1" || normalized == "true" || normalized == "yes" ||
        normalized == "on") {
        return true;
    }
    if (normalized == "0" || normalized == "false" || normalized == "no" ||
        normalized == "off") {
        return false;
    }
    throw std::invalid_argument(std::string(name) + " must be a boolean value");
}

bool is_finite_positive(double value) {
    return std::isfinite(value) && value > 0.0;
}

int timeframe_seconds(const std::string& timeframe) {
    static const std::unordered_map<std::string, int> periods{
        {"M1", 60},       {"M2", 120},     {"M3", 180},     {"M4", 240},
        {"M5", 300},      {"M6", 360},     {"M10", 600},    {"M12", 720},
        {"M15", 900},     {"M20", 1200},   {"M30", 1800},   {"H1", 3600},
        {"H2", 7200},     {"H3", 10800},   {"H4", 14400},   {"H6", 21600},
        {"H8", 28800},    {"H12", 43200},  {"D1", 86400},   {"W1", 604800},
        {"MN1", 2419200},
    };
    const auto found = periods.find(timeframe);
    if (found == periods.end()) {
        throw std::invalid_argument("Unsupported MT5 timeframe: " + timeframe);
    }
    return found->second;
}

}  // namespace

bool account_matches(const Account& account, std::int64_t login,
                     std::string_view server) {
    if (account.login != login || account.server.size() != server.size()) {
        return false;
    }
    return std::equal(account.server.begin(), account.server.end(), server.begin(),
                      [](unsigned char left, unsigned char right) {
                          return std::tolower(left) == std::tolower(right);
                      });
}

int timeframe_duration_seconds(std::string_view timeframe) {
    return timeframe_seconds(std::string(timeframe));
}

Config Config::from_environment() {
    load_dotenv();
    Config config;
    config.bot_name = env_string("BOT_NAME", config.bot_name);
    config.owner = env_string("OWNER", config.owner);
    config.symbol = env_string("SYMBOL", config.symbol);
    config.timeframe = env_string("TIMEFRAME", config.timeframe);
    std::transform(config.timeframe.begin(), config.timeframe.end(),
                   config.timeframe.begin(), [](unsigned char ch) {
                       return static_cast<char>(std::toupper(ch));
                   });
    const int history_count =
        env_int("HISTORY_COUNT", static_cast<int>(config.history_count));
    if (history_count < 1) {
        throw std::invalid_argument("HISTORY_COUNT must be positive");
    }
    config.history_count = static_cast<std::size_t>(history_count);
    config.port = env_int("LUMO_PORT", config.port);
    config.risk_per_trade = env_double("DEFAULT_RISK_PER_TRADE", config.risk_per_trade);
    config.max_risk_per_trade = env_double("MAX_RISK_PER_TRADE", config.max_risk_per_trade);
    config.daily_loss_limit = env_double("DAILY_LOSS_LIMIT", config.daily_loss_limit);
    config.requested_lot_size = env_double("REQUESTED_LOT_SIZE", config.requested_lot_size);
    config.max_daily_trades = env_int("MAX_DAILY_TRADES", config.max_daily_trades);
    config.spread_limit_points = env_double("SPREAD_LIMIT_POINTS", config.spread_limit_points);
    config.stop_loss_atr_multiplier =
        env_double("STOP_LOSS_ATR_MULTIPLIER", config.stop_loss_atr_multiplier);
    config.backtest_reward_risk =
        env_double("BACKTEST_REWARD_RISK", config.backtest_reward_risk);
    config.bridge_token = env_string("BRIDGE_TOKEN", config.bridge_token);
    config.database_path = env_string("DATABASE_PATH", config.database_path);
    config.live_trading_enabled = env_bool("LIVE_TRADING_ENABLED", false);
    config.emergency_stop = env_bool("EMERGENCY_STOP", false);
    config.trading_lock = env_bool("TRADING_LOCK", false);

    config.validate();
    return config;
}

void Config::validate() const {
    if (port < 1 || port > 65535) {
        throw std::invalid_argument("LUMO_PORT must be between 1 and 65535");
    }
    if (history_count < 250 || history_count > 2000) {
        throw std::invalid_argument("HISTORY_COUNT must be between 250 and 2000");
    }
    if (ema_fast <= 0 || ema_slow <= ema_fast || rsi_period <= 0 || atr_period <= 0) {
        throw std::invalid_argument("Indicator periods must be positive and EMA slow must exceed EMA fast");
    }
    if (max_daily_trades <= 0) {
        throw std::invalid_argument("MAX_DAILY_TRADES must be positive");
    }
    if (!is_finite_positive(risk_per_trade) ||
        !is_finite_positive(max_risk_per_trade) ||
        risk_per_trade > max_risk_per_trade || max_risk_per_trade > 1.0) {
        throw std::invalid_argument("Risk per trade must be positive, within its maximum, and at most 1");
    }
    if (!is_finite_positive(daily_loss_limit) || daily_loss_limit > 1.0) {
        throw std::invalid_argument("DAILY_LOSS_LIMIT must be between 0 and 1");
    }
    if (!is_finite_positive(spread_limit_points) ||
        !is_finite_positive(stop_loss_atr_multiplier) ||
        !is_finite_positive(backtest_reward_risk)) {
        throw std::invalid_argument("Spread, stop-loss, and reward/risk limits must be positive");
    }
    if (!is_finite_positive(requested_lot_size) || !is_finite_positive(min_lot) ||
        !is_finite_positive(max_lot) || !is_finite_positive(lot_step) ||
        min_lot > max_lot) {
        throw std::invalid_argument("Lot size configuration is invalid");
    }
    if (symbol.empty() || timeframe.empty()) {
        throw std::invalid_argument("SYMBOL and TIMEFRAME must not be empty");
    }
    static_cast<void>(timeframe_seconds(timeframe));
    if (!bridge_token.empty() && bridge_token.size() < 32) {
        throw std::invalid_argument("BRIDGE_TOKEN must contain at least 32 characters when set");
    }
}

std::string timeframe_from_seconds(int seconds) {
    static const std::unordered_map<int, std::string> periods{
        {60, "M1"},       {120, "M2"},      {180, "M3"},      {240, "M4"},
        {300, "M5"},      {360, "M6"},      {600, "M10"},     {720, "M12"},
        {900, "M15"},     {1200, "M20"},    {1800, "M30"},    {3600, "H1"},
        {7200, "H2"},     {10800, "H3"},    {14400, "H4"},    {21600, "H6"},
        {28800, "H8"},    {43200, "H12"},   {86400, "D1"},    {604800, "W1"},
        {2419200, "MN1"},
    };
    const auto found = periods.find(seconds);
    if (found == periods.end()) {
        throw std::invalid_argument("Unsupported timeframe duration");
    }
    return found->second;
}

RiskManager::RiskManager(const Config& config) : config_(config) {}

double RiskManager::calculate_position_size(double account_equity, double entry_price,
                                            double stop_loss, double tick_value,
                                            double contract_size) const {
    if (!is_finite_positive(account_equity) || !is_finite_positive(entry_price) ||
        !is_finite_positive(stop_loss) || !is_finite_positive(tick_value) ||
        !is_finite_positive(contract_size) || !is_finite_positive(config_.lot_step)) {
        return 0.0;
    }
    const double stop_distance = std::abs(entry_price - stop_loss);
    if (!is_finite_positive(stop_distance)) {
        return 0.0;
    }
    const double dollars_per_lot = contract_size * tick_value * std::max(stop_distance, 1.0);
    const double risk_limited_volume =
        account_equity * config_.risk_per_trade / dollars_per_lot;
    if (risk_limited_volume < config_.min_lot) {
        return 0.0;
    }
    const double capped_volume = std::min(
        {risk_limited_volume, config_.max_lot, config_.requested_lot_size});
    const double volume =
        std::floor(capped_volume / config_.lot_step) * config_.lot_step;
    return volume >= config_.min_lot ? volume : 0.0;
}

bool RiskManager::can_open_trade(double account_equity, int daily_trade_count,
                                 double current_daily_loss) const {
    if (!config_.live_trading_enabled || config_.emergency_stop ||
        config_.trading_lock || trade_lock_ ||
        !is_finite_positive(account_equity) ||
        daily_trade_count >= config_.max_daily_trades) {
        return false;
    }
    if (start_of_day_equity_ > 0.0 &&
        current_daily_loss <= -start_of_day_equity_ * config_.daily_loss_limit) {
        return false;
    }
    return true;
}

bool RiskManager::validate_trade(double lot_size, double stop_loss,
                                double account_equity) const {
    return is_finite_positive(lot_size) && is_finite_positive(stop_loss) &&
           is_finite_positive(account_equity) && lot_size <= config_.max_lot &&
           enforce_daily_trade_limit();
}

void RiskManager::record_daily_state(double start_equity, double daily_loss,
                                     int trade_count) {
    start_of_day_equity_ = start_equity;
    daily_loss_ = daily_loss;
    daily_trade_count_ = trade_count;
}

bool RiskManager::enforce_daily_trade_limit() const {
    return daily_trade_count_ < config_.max_daily_trades;
}

void RiskManager::apply_loss_lock(double account_equity) {
    if (start_of_day_equity_ != 0.0 && account_equity <= 0.0) {
        trade_lock_ = true;
    }
}

void RiskManager::update_daily_loss(double pnl) {
    daily_loss_ += pnl;
    if (start_of_day_equity_ > 0.0 &&
        daily_loss_ <= -start_of_day_equity_ * config_.daily_loss_limit) {
        trade_lock_ = true;
    }
}

RiskSummary RiskManager::summary() const {
    return {config_.risk_per_trade,
            config_.daily_loss_limit,
            config_.max_daily_trades,
            trade_lock_,
            daily_trade_count_,
            daily_loss_};
}

RiskSummary assess_risk(const Config& config, const Account& account,
                        double spread_points, int daily_trade_count) {
    RiskSummary result;
    result.risk_per_trade = config.risk_per_trade;
    result.daily_loss_limit = config.daily_loss_limit;
    result.daily_trade_limit = config.max_daily_trades;
    result.daily_trade_count = daily_trade_count;
    result.daily_loss = account.day_pnl;
    result.open_positions = account.open_positions;
    result.positions_without_stop = account.positions_without_stop;
    result.positions_risk_unavailable = account.positions_risk_unavailable;
    if (account.day_start_balance > 0.0) {
        result.day_loss_percent =
            std::max(0.0, -account.day_pnl / account.day_start_balance * 100.0);
    }
    if (account.equity <= 0.0) {
        result.warnings.emplace_back("Account equity is non-positive");
    }
    if (account.equity > 0.0) {
        result.open_risk_percent =
            std::max(0.0, account.risk_at_stops / account.equity * 100.0);
    }
    if (account.margin > 0.0) {
        result.margin_level_percent = account.equity / account.margin * 100.0;
    }
    result.trade_lock =
        account.day_start_balance > 0.0 &&
        account.day_pnl <= -account.day_start_balance * config.daily_loss_limit;
    if (result.trade_lock) {
        result.warnings.emplace_back("Daily loss limit reached");
    }
    if (spread_points > config.spread_limit_points) {
        result.warnings.emplace_back("Current spread exceeds the configured limit");
    }
    if (account.positions_without_stop > 0) {
        result.warnings.emplace_back("One or more open positions have no stop loss");
    }
    if (account.positions_risk_unavailable > 0) {
        result.warnings.emplace_back("Risk could not be calculated for one or more positions");
    }
    if (account.margin > 0.0 && account.free_margin <= 0.0) {
        result.warnings.emplace_back("Free margin is depleted");
    }
    if (daily_trade_count >= config.max_daily_trades) {
        result.warnings.emplace_back("Daily trade limit reached");
    }
    return result;
}

std::vector<double> ema(const std::vector<double>& values, int span) {
    if (span <= 0) {
        throw std::invalid_argument("EMA span must be positive");
    }
    std::vector<double> result;
    result.reserve(values.size());
    if (values.empty()) {
        return result;
    }
    const double alpha = 2.0 / (static_cast<double>(span) + 1.0);
    result.push_back(values.front());
    for (std::size_t i = 1; i < values.size(); ++i) {
        result.push_back(alpha * values[i] + (1.0 - alpha) * result.back());
    }
    return result;
}

std::vector<double> rsi(const std::vector<double>& values, int period) {
    if (period <= 0) {
        throw std::invalid_argument("RSI period must be positive");
    }
    std::vector<double> result(values.size(), 50.0);
    if (values.size() < 2) {
        return result;
    }
    const double alpha = 1.0 / static_cast<double>(period);
    double average_gain = std::max(values[1] - values[0], 0.0);
    double average_loss = std::max(values[0] - values[1], 0.0);
    result[1] = average_loss == 0.0
                    ? 50.0
                    : 100.0 - 100.0 / (1.0 + average_gain / average_loss);
    for (std::size_t i = 2; i < values.size(); ++i) {
        const double change = values[i] - values[i - 1];
        average_gain = alpha * std::max(change, 0.0) + (1.0 - alpha) * average_gain;
        average_loss = alpha * std::max(-change, 0.0) + (1.0 - alpha) * average_loss;
        result[i] = average_loss == 0.0
                        ? 50.0
                        : 100.0 - 100.0 / (1.0 + average_gain / average_loss);
    }
    return result;
}

std::vector<double> atr(const std::vector<Candle>& candles, int period) {
    if (period <= 0) {
        throw std::invalid_argument("ATR period must be positive");
    }
    std::vector<double> result(candles.size(), 0.0);
    if (candles.size() < 2) {
        if (!candles.empty()) {
            result[0] = candles[0].high - candles[0].low;
        }
        return result;
    }
    const double alpha = 1.0 / static_cast<double>(period);
    double average = candles[0].high - candles[0].low;
    result[0] = average;
    for (std::size_t i = 1; i < candles.size(); ++i) {
        const double true_range = std::max(
            {candles[i].high - candles[i].low,
             std::abs(candles[i].high - candles[i - 1].close),
             std::abs(candles[i].low - candles[i - 1].close)});
        average = alpha * true_range + (1.0 - alpha) * average;
        result[i] = average;
    }
    return result;
}

Indicators calculate_indicators(const std::vector<Candle>& candles,
                                const Config& config) {
    if (candles.empty()) {
        return {};
    }
    std::vector<double> closes;
    closes.reserve(candles.size());
    for (const auto& candle : candles) {
        closes.push_back(candle.close);
    }
    const auto fast = ema(closes, config.ema_fast);
    const auto slow = ema(closes, config.ema_slow);
    const auto rsi_values = rsi(closes, config.rsi_period);
    const auto atr_values = atr(candles, config.atr_period);
    const auto last = candles.size() - 1;
    return {fast[last], slow[last], rsi_values[last], atr_values[last]};
}

Signal evaluate_strategy(const std::vector<Candle>& candles,
                         const Config& config) {
    if (candles.size() < 250) {
        return {"NO_TRADE", 0.0, "Insufficient market data"};
    }

    std::vector<double> closes;
    closes.reserve(candles.size());
    for (const auto& candle : candles) {
        closes.push_back(candle.close);
    }
    const auto fast = ema(closes, config.ema_fast);
    const auto slow = ema(closes, config.ema_slow);
    const auto rsi_values = rsi(closes, config.rsi_period);
    const auto atr_values = atr(candles, config.atr_period);
    const auto last = candles.size() - 1;
    const auto previous = last - 1;
    const bool bullish =
        (fast[previous] < slow[previous] && fast[last] > slow[last]) ||
        (candles[last].close > candles[last].open && rsi_values[last] > 52.0);
    const bool bearish =
        (fast[previous] > slow[previous] && fast[last] < slow[last]) ||
        (candles[last].close < candles[last].open && rsi_values[last] < 48.0);
    if (bullish && rsi_values[last] > 50.0 && atr_values[last] > 0.0) {
        return {"BUY",
                std::clamp(0.5 + (fast[last] - slow[last]) /
                                     std::max(std::abs(slow[last]), 1.0) * 100.0,
                           0.0, 0.95),
                "EMA trend and RSI confirm bullish momentum"};
    }
    if (bearish && rsi_values[last] < 50.0 && atr_values[last] > 0.0) {
        return {"SELL",
                std::clamp(0.5 + (slow[last] - fast[last]) /
                                     std::max(std::abs(slow[last]), 1.0) * 100.0,
                           0.0, 0.95),
                "EMA trend and RSI confirm bearish momentum"};
    }
    return {"NO_TRADE", 0.0,
            "Market conditions do not satisfy the configured entry criteria"};
}

ScanResult scan_market(const std::vector<Candle>& candles,
                       const Config& config) {
    if (candles.empty()) {
        return {{"NO_TRADE", 0.0, "No market data available"}, {}};
    }
    const auto indicators = calculate_indicators(candles, config);
    std::string direction = "NO_TRADE";
    if (indicators.ema_fast > indicators.ema_slow && indicators.rsi > 52.0) {
        direction = "BUY";
    } else if (indicators.ema_fast < indicators.ema_slow &&
               indicators.rsi < 48.0) {
        direction = "SELL";
    }
    const std::string model_direction =
        indicators.ema_fast > indicators.ema_slow
            ? "BUY"
            : indicators.ema_fast < indicators.ema_slow ? "SELL" : "NO_TRADE";
    if (direction != "NO_TRADE" && model_direction == direction) {
        return {{direction, 0.62,
                 direction == "BUY"
                     ? "EMA trend and RSI confirm bullish momentum; signal filter supports the setup"
                     : "EMA trend and RSI confirm bearish momentum; signal filter supports the setup"},
                indicators};
    }
    return {{"NO_TRADE", 0.0,
             direction == "NO_TRADE"
                 ? "Trend is not sufficiently confirmed for a new trade"
                 : "Signal filter does not confirm the trade"},
            indicators};
}

MarketObservation observe_market(const std::vector<Candle>& candles,
                                 const Quote& quote, const Signal& signal,
                                 const Config& config,
                                 std::int64_t observed_at) {
    MarketObservation observation;
    observation.observed_at = observed_at;
    observation.candle_count = candles.size();
    observation.spread_points =
        quote.point > 0.0 ? (quote.ask - quote.bid) / quote.point : 0.0;
    if (observed_at > 0) {
        const auto utc_hour = static_cast<int>((observed_at / 3600) % 24);
        if (utc_hour < 7) {
            observation.market_session = "Asia (UTC)";
        } else if (utc_hour < 13) {
            observation.market_session = "London (UTC)";
        } else if (utc_hour < 21) {
            observation.market_session = "New York (UTC)";
        } else {
            observation.market_session = "Rollover / quiet hours (UTC)";
        }
    }

    if (candles.size() < 250 || quote.time <= 0 ||
        observed_at - quote.time < 0 || observed_at - quote.time > 60 ||
        quote.point <= 0.0 || quote.ask < quote.bid) {
        observation.report =
            "Critical market data is missing, insufficient, or stale. "
            "Observation is incomplete; trading must remain blocked.";
        return observation;
    }
    observation.critical_data_available = true;

    std::vector<double> closes;
    closes.reserve(candles.size());
    for (const auto& candle : candles) {
        closes.push_back(candle.close);
    }
    const auto indicators = calculate_indicators(candles, config);
    observation.atr = indicators.atr;
    const auto sma = [&closes](std::size_t period) {
        const auto begin = closes.end() - static_cast<std::ptrdiff_t>(period);
        double sum = 0.0;
        for (auto value = begin; value != closes.end(); ++value) {
            sum += *value;
        }
        return sum / static_cast<double>(period);
    };
    observation.sma_fast = sma(20);
    observation.sma_slow = sma(50);

    std::vector<double> macd_values;
    macd_values.reserve(closes.size());
    const auto ema_fast = ema(closes, 12);
    const auto ema_slow = ema(closes, 26);
    for (std::size_t i = 0; i < closes.size(); ++i) {
        macd_values.push_back(ema_fast[i] - ema_slow[i]);
    }
    const auto macd_signal_values = ema(macd_values, 9);
    observation.macd = macd_values.back();
    observation.macd_signal = macd_signal_values.back();

    const auto atr_values = atr(candles, config.atr_period);
    const auto volatility_count =
        std::min<std::size_t>(50, atr_values.size() - 1);
    const auto volatility_begin =
        atr_values.end() - static_cast<std::ptrdiff_t>(volatility_count + 1);
    const auto volatility_end = atr_values.end() - 1;
    double average_atr = 0.0;
    for (auto value = volatility_begin; value != volatility_end; ++value) {
        average_atr += *value;
    }
    average_atr = volatility_count > 0
                      ? average_atr / static_cast<double>(volatility_count)
                      : 0.0;
    observation.volatility_ratio =
        average_atr > 0.0 ? observation.atr / average_atr : 0.0;
    observation.volatility = observation.volatility_ratio >= 1.8
                                 ? "Elevated"
                                 : observation.volatility_ratio <= 0.65
                                       ? "Quiet"
                                       : "Normal";

    const std::size_t level_count = std::min<std::size_t>(20, candles.size());
    observation.support = candles.back().low;
    observation.resistance = candles.back().high;
    for (auto candle = candles.end() - static_cast<std::ptrdiff_t>(level_count);
         candle != candles.end(); ++candle) {
        observation.support = std::min(observation.support, candle->low);
        observation.resistance = std::max(observation.resistance, candle->high);
    }

    const auto& last = candles.back();
    const auto& previous = candles[candles.size() - 2];
    const double body = std::abs(last.close - last.open);
    const double range = last.high - last.low;
    const double upper_wick = last.high - std::max(last.open, last.close);
    const double lower_wick = std::min(last.open, last.close) - last.low;
    if (range > 0.0 && body <= range * 0.1) {
        observation.candle_pattern = "Doji";
    } else if (previous.close < previous.open && last.close > last.open &&
               last.open <= previous.close && last.close >= previous.open) {
        observation.candle_pattern = "Bullish engulfing";
    } else if (previous.close > previous.open && last.close < last.open &&
               last.open >= previous.close && last.close <= previous.open) {
        observation.candle_pattern = "Bearish engulfing";
    } else if (range > 0.0 && lower_wick >= body * 2.0 &&
               upper_wick <= body) {
        observation.candle_pattern = "Hammer-like candle";
    } else if (range > 0.0 && upper_wick >= body * 2.0 &&
               lower_wick <= body) {
        observation.candle_pattern = "Shooting-star-like candle";
    } else if (last.close > last.open) {
        observation.candle_pattern = "Bullish candle";
    } else {
        observation.candle_pattern = "Bearish candle";
    }

    const std::size_t volume_count =
        std::min<std::size_t>(20, candles.size() - 1);
    const auto volume_begin =
        candles.end() - static_cast<std::ptrdiff_t>(volume_count + 1);
    const auto volume_end = candles.end() - 1;
    double average_volume = 0.0;
    for (auto candle = volume_begin; candle != volume_end; ++candle) {
        average_volume += static_cast<double>(candle->tick_volume);
    }
    average_volume /= static_cast<double>(volume_count);
    observation.relative_volume =
        average_volume > 0.0
            ? static_cast<double>(last.tick_volume) / average_volume
            : 0.0;
    observation.volume_condition =
        average_volume <= 0.0 ? "Unavailable (no tick-volume data)"
        : observation.relative_volume >= 1.5 ? "Above recent average"
        : observation.relative_volume <= 0.6 ? "Below recent average"
                                             : "Near recent average";

    const bool bullish = indicators.ema_fast > indicators.ema_slow &&
                         observation.sma_fast > observation.sma_slow;
    const bool bearish = indicators.ema_fast < indicators.ema_slow &&
                         observation.sma_fast < observation.sma_slow;
    const bool sideways = observation.atr > 0.0 &&
                          std::abs(indicators.ema_fast - indicators.ema_slow) <
                              observation.atr * 0.2 &&
                          indicators.rsi >= 45.0 && indicators.rsi <= 55.0;
    observation.trend = bullish ? "Bullish" : bearish ? "Bearish"
                                           : sideways ? "Sideways"
                                                      : "Mixed / uncertain";
    if (observation.volatility == "Elevated") {
        observation.classification = "Highly volatile";
    } else if (sideways) {
        observation.classification = "Sideways";
    } else if (bullish && indicators.rsi >= 58.0 &&
               observation.macd > observation.macd_signal) {
        observation.classification = "Strong bullish";
    } else if (bullish) {
        observation.classification = "Weak bullish";
    } else if (bearish && indicators.rsi <= 42.0 &&
               observation.macd < observation.macd_signal) {
        observation.classification = "Strong bearish";
    } else if (bearish) {
        observation.classification = "Weak bearish";
    } else {
        observation.classification = "Uncertain";
    }

    observation.report =
        observation.classification + " conditions; " + observation.trend +
        " trend, " + observation.volatility + " volatility (ATR " +
        std::to_string(observation.atr) + ", " +
        std::to_string(observation.volatility_ratio) +
        "x recent baseline). Support/resistance over the latest " +
        std::to_string(level_count) + " candles: " +
        std::to_string(observation.support) + " / " +
        std::to_string(observation.resistance) + ". " +
        observation.candle_pattern + "; tick volume " +
        observation.volume_condition + "; spread " +
        std::to_string(observation.spread_points) + " points; session " +
        observation.market_session + ". Signal: " + signal.direction +
        " (" + signal.reason + "). News/event feed unavailable; no news "
        "assessment was made. Read-only mode: no order is eligible for execution.";
    return observation;
}

BacktestResult run_backtest(const std::vector<Candle>& candles,
                            const Config& config) {
    BacktestResult result;
    double cumulative_profit = 0.0;
    double peak = 0.0;
    double gross_profit = 0.0;
    double gross_loss = 0.0;

    for (std::size_t signal_index = 250;
         signal_index + 1 < candles.size(); ++signal_index) {
        const std::vector<Candle> history(candles.begin(),
                                          candles.begin() + signal_index + 1);
        const auto signal = evaluate_strategy(history, config);
        if (signal.direction == "NO_TRADE") {
            continue;
        }
        const auto history_atr = atr(history, config.atr_period).back();
        const double risk_distance =
            history_atr * config.stop_loss_atr_multiplier;
        if (!is_finite_positive(risk_distance)) {
            continue;
        }
        const bool is_buy = signal.direction == "BUY";
        const double entry = candles[signal_index + 1].open;
        const double stop = is_buy ? entry - risk_distance : entry + risk_distance;
        const double target = is_buy
                                  ? entry + risk_distance * config.backtest_reward_risk
                                  : entry - risk_distance * config.backtest_reward_risk;
        double pnl = 0.0;
        std::size_t exit_index = signal_index + 1;
        bool exited = false;
        for (std::size_t bar = signal_index + 1; bar < candles.size(); ++bar) {
            const auto& candle = candles[bar];
            const bool stop_touched =
                is_buy ? candle.low <= stop : candle.high >= stop;
            const bool target_touched =
                is_buy ? candle.high >= target : candle.low <= target;
            if (stop_touched) {
                const double exit_price =
                    is_buy ? std::min(stop, candle.open)
                           : std::max(stop, candle.open);
                pnl = (is_buy ? exit_price - entry : entry - exit_price) * 100.0;
                exit_index = bar;
                exited = true;
                break;
            }
            if (target_touched) {
                pnl = risk_distance * config.backtest_reward_risk * 100.0;
                exit_index = bar;
                exited = true;
                break;
            }
        }
        if (!exited) {
            exit_index = candles.size() - 1;
            const double final_close = candles[exit_index].close;
            pnl = (is_buy ? final_close - entry : entry - final_close) * 100.0;
        }
        ++result.total_trades;
        cumulative_profit += pnl;
        peak = std::max(peak, cumulative_profit);
        result.max_drawdown = std::max(result.max_drawdown, peak - cumulative_profit);
        if (pnl > 0.0) {
            ++result.wins;
            gross_profit += pnl;
        } else {
            ++result.losses;
            gross_loss += std::abs(pnl);
        }
        signal_index = exit_index - 1;
    }
    result.net_profit = std::round(cumulative_profit * 100.0) / 100.0;
    result.max_drawdown = std::round(result.max_drawdown * 100.0) / 100.0;
    result.win_rate = result.total_trades == 0
                          ? 0.0
                          : static_cast<double>(result.wins) / result.total_trades;
    result.profit_factor_unbounded = gross_loss == 0.0 && gross_profit > 0.0;
    result.profit_factor =
        gross_loss > 0.0 ? gross_profit / gross_loss : 0.0;
    return result;
}

std::vector<PositionCommand> manage_positions(
    const std::vector<BrokerPosition>& positions,
    std::optional<double> stop_loss, std::optional<double> take_profit) {
    std::vector<PositionCommand> commands;
    for (const auto& position : positions) {
        if (stop_loss.has_value() && position.profit < 0.0) {
            commands.push_back(
                {position.ticket, "close", "Protected stop triggered"});
        } else if (take_profit.has_value() &&
                   position.profit >= *take_profit) {
            commands.push_back({position.ticket, "close", "Take-profit reached"});
        }
    }
    return commands;
}

void validate_snapshot(const Snapshot& snapshot, const Config& config) {
    if (snapshot.symbol.empty() || snapshot.symbol.size() > 64 ||
        snapshot.timeframe.empty() || snapshot.timeframe.size() > 8) {
        throw std::invalid_argument("Snapshot symbol and timeframe are required");
    }
    if (snapshot.candles.empty() || snapshot.candles.size() > config.history_count) {
        throw std::invalid_argument("Snapshot must contain between 1 and HISTORY_COUNT candles");
    }
    if (!is_finite_positive(snapshot.quote.bid) ||
        !std::isfinite(snapshot.quote.ask) ||
        snapshot.quote.bid > 1.0e12 || snapshot.quote.ask > 1.0e12 ||
        snapshot.quote.ask < snapshot.quote.bid ||
        !is_finite_positive(snapshot.quote.point) ||
        snapshot.quote.point < 1.0e-10 ||
        snapshot.quote.digits < 0 || snapshot.quote.digits > 10) {
        throw std::invalid_argument("Snapshot quote is invalid");
    }
    const auto now = unix_time_now();
    if (snapshot.quote.time <= 0 || snapshot.quote.time > now + 60 ||
        now - snapshot.quote.time > 60) {
        throw std::invalid_argument("Snapshot quote is stale or has an invalid timestamp");
    }
    if (!std::isfinite(snapshot.account.balance) ||
        !std::isfinite(snapshot.account.equity) ||
        !std::isfinite(snapshot.account.margin) ||
        !std::isfinite(snapshot.account.free_margin) ||
        !std::isfinite(snapshot.account.day_start_balance) ||
        !std::isfinite(snapshot.account.day_pnl) ||
        !std::isfinite(snapshot.account.floating_pnl) ||
        !std::isfinite(snapshot.account.open_volume) ||
        !std::isfinite(snapshot.account.risk_at_stops) ||
        snapshot.account.margin < 0.0 ||
        snapshot.account.open_volume > 1.0e9 ||
        snapshot.account.risk_at_stops > 1.0e15 ||
        snapshot.account.currency.empty() ||
        snapshot.account.open_positions < 0 ||
        snapshot.account.positions_without_stop < 0 ||
        snapshot.account.positions_without_stop > snapshot.account.open_positions ||
        snapshot.account.positions_risk_unavailable < 0 ||
        snapshot.account.positions_risk_unavailable >
            snapshot.account.open_positions -
                snapshot.account.positions_without_stop ||
        snapshot.account.open_volume < 0.0 ||
        snapshot.account.risk_at_stops < 0.0 ||
        snapshot.account.day_start_balance < 0.0 ||
        std::abs(snapshot.account.balance) > 1.0e15 ||
        std::abs(snapshot.account.equity) > 1.0e15 ||
        std::abs(snapshot.account.day_pnl) > 1.0e15 ||
        snapshot.account.margin > 1.0e15 ||
        std::abs(snapshot.account.free_margin) > 1.0e15 ||
        std::abs(snapshot.account.floating_pnl) > 1.0e15 ||
        snapshot.account.risk_at_stops > 1.0e15 ||
        snapshot.account.trade_mode < 0 || snapshot.account.trade_mode > 2 ||
        snapshot.account.login <= 0 ||
        snapshot.account.leverage < 1 ||
        snapshot.account.currency.size() > 8 ||
        snapshot.account.server.empty() ||
        snapshot.account.server.size() > 128) {
        throw std::invalid_argument("Snapshot account data is invalid");
    }
    const auto interval = timeframe_seconds(snapshot.timeframe);
    std::int64_t previous_time = 0;
    for (const auto& candle : snapshot.candles) {
        if (candle.time <= previous_time || candle.time <= 0 ||
            (previous_time > 0 && candle.time - previous_time < interval) ||
            !is_finite_positive(candle.open) || !is_finite_positive(candle.high) ||
            !is_finite_positive(candle.low) || !is_finite_positive(candle.close) ||
            candle.high < std::max(candle.open, candle.close) ||
            candle.low > std::min(candle.open, candle.close) ||
            candle.high < candle.low) {
            throw std::invalid_argument("Snapshot contains invalid or unordered candles");
        }
        previous_time = candle.time;
    }
    if (snapshot.candles.back().time > now + 60) {
        throw std::invalid_argument("Snapshot contains candle timestamps in the future");
    }
}

std::int64_t unix_time_now() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

}  // namespace lumo
