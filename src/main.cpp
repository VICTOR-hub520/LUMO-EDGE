#include "lumo/core.hpp"
#include "lumo/database.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {

using json = nlohmann::json;
std::ofstream log_file;
void log_line(const std::string& message);

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

json observation_to_json(const lumo::MarketObservation& observation) {
    return {
        {"observed_at", observation.observed_at},
        {"classification", observation.classification},
        {"trend", observation.trend},
        {"volatility", observation.volatility},
        {"candle_pattern", observation.candle_pattern},
        {"market_session", observation.market_session},
        {"volume_condition", observation.volume_condition},
        {"report", observation.report},
        {"support", observation.support},
        {"resistance", observation.resistance},
        {"sma_fast", observation.sma_fast},
        {"sma_slow", observation.sma_slow},
        {"macd", observation.macd},
        {"macd_signal", observation.macd_signal},
        {"atr", observation.atr},
        {"volatility_ratio", observation.volatility_ratio},
        {"relative_volume", observation.relative_volume},
        {"spread_points", observation.spread_points},
        {"candle_count", observation.candle_count},
        {"critical_data_available", observation.critical_data_available},
    };
}

class AppState {
public:
    AppState(lumo::Database& database, const lumo::Config& config)
        : database_(database), config_(config) {}

    std::optional<std::string> connect(std::int64_t login,
                                       std::string server) {
        std::unique_lock lock(mutex_);
        connection_target_ = std::make_pair(login, std::move(server));
        if (snapshot_ && snapshot_is_current(*snapshot_) &&
            !lumo::account_matches(snapshot_->account, login,
                                   connection_target_->second)) {
            return "The MT5 terminal is connected to a different account. "
                   "Check the login number and broker server in the terminal.";
        }
        return std::nullopt;
    }

    void disconnect() {
        std::unique_lock lock(mutex_);
        connection_target_.reset();
    }

    void update(lumo::Snapshot snapshot) {
        std::unique_lock lock(mutex_);
        if (snapshot_ &&
            snapshot.candles.back().time < snapshot_->candles.back().time) {
            throw std::invalid_argument("Snapshot candles are older than the current data");
        }
        const bool new_candle =
            !snapshot_ ||
            snapshot_->candles.back().time != snapshot.candles.back().time;
        if (new_candle && connection_target_ &&
            lumo::account_matches(snapshot.account, connection_target_->first,
                                  connection_target_->second)) {
            database_.save_signal(snapshot.symbol, snapshot.signal.direction,
                                  snapshot.signal.confidence, snapshot.signal.reason);
            const auto observation = lumo::observe_market(
                snapshot.candles, snapshot.quote, snapshot.signal, config_,
                snapshot.received_at);
            if (observation.critical_data_available) {
                database_.evaluate_latest_observation(
                    snapshot.symbol, snapshot.candles.back().time,
                    snapshot.candles.back().time -
                        lumo::timeframe_duration_seconds(snapshot.timeframe),
                    snapshot.candles.back().close, snapshot.received_at,
                    connection_target_->first, connection_target_->second);
                database_.save_observation(snapshot.received_at,
                                           connection_target_->first,
                                           connection_target_->second,
                                           snapshot.symbol,
                                           snapshot.candles.back().time,
                                           snapshot.candles.back().close,
                                           observation.trend,
                                           observation_to_json(observation).dump());
            }
            log_line("Market observation: " + snapshot.symbol + " " +
                     observation.classification + " / " + observation.trend +
                     (observation.critical_data_available
                          ? ""
                          : " (critical data unavailable; execution blocked)"));
        }
        snapshot_ = std::move(snapshot);
    }

    json to_json(const lumo::Config& config) const {
        std::shared_lock lock(mutex_);
        json result = {
            {"bot_name", config.bot_name},
            {"owner", config.owner},
            {"configured_symbol", config.symbol},
            {"timeframe", config.timeframe},
            {"port", config.port},
            {"bridge_token_required", !config.bridge_token.empty()},
            {"connected", false},
            {"connection_status", connection_target_ ? "waiting" : "disconnected"},
            {"connection_message",
             connection_target_
                 ? "Waiting for fresh data from the MT5 bridge."
                 : "Connect the signed-in MT5 terminal by entering its login and broker server."},
            {"expected_account", connection_target_
                                    ? json{{"login", connection_target_->first},
                                           {"server", connection_target_->second}}
                                    : json(nullptr)},
            {"updated_at", nullptr},
            {"account", nullptr},
            {"market", nullptr},
            {"indicators", nullptr},
            {"signal", nullptr},
            {"observation", nullptr},
            {"alerts", json::array()},
            {"candles", json::array()},
        };
        if (!snapshot_) {
            return result;
        }

        const auto& snapshot = *snapshot_;
        const auto age_seconds = lumo::unix_time_now() - snapshot.received_at;
        const auto quote_age_seconds =
            lumo::unix_time_now() - snapshot.quote.time;
        const bool current = snapshot_is_current(snapshot);
        result["data_age_seconds"] = age_seconds;
        result["quote_age_seconds"] = quote_age_seconds;
        if (!connection_target_) {
            result["connection_status"] = "disconnected";
            result["connection_message"] =
                "Disconnected from the dashboard. Disable the bridge in MT5 to stop its local data relay.";
            return result;
        }
        if (!current) {
            result["connection_status"] = "stale";
            result["connection_message"] =
                "MT5 bridge data is stale. The dashboard will reconnect when fresh data resumes.";
            return result;
        }
        if (!lumo::account_matches(snapshot.account, connection_target_->first,
                                   connection_target_->second)) {
            result["connection_status"] = "account_mismatch";
            result["connection_message"] =
                "The MT5 terminal account does not match the requested login and broker server.";
            return result;
        }

        result["connected"] = true;
        result["connection_status"] = "connected";
        result["connection_message"] =
            "Connected to the matching MT5 account. The dashboard is read-only.";
        result["expected_account"] = {
            {"login", connection_target_->first},
            {"server", connection_target_->second},
        };
        result["timeframe"] = snapshot.timeframe;
        result["updated_at"] = snapshot.received_at;
        result["symbol"] = snapshot.symbol;
        result["account"] = {
            {"server", snapshot.account.server},
            {"currency", snapshot.account.currency},
            {"login", snapshot.account.login},
            {"balance", snapshot.account.balance},
            {"equity", snapshot.account.equity},
            {"margin", snapshot.account.margin},
            {"free_margin", snapshot.account.free_margin},
            {"day_start_balance", snapshot.account.day_start_balance},
            {"day_pnl", snapshot.account.day_pnl},
            {"floating_pnl", snapshot.account.floating_pnl},
            {"open_volume", snapshot.account.open_volume},
            {"risk_at_stops", snapshot.account.risk_at_stops},
            {"open_positions", snapshot.account.open_positions},
            {"positions_without_stop", snapshot.account.positions_without_stop},
            {"positions_risk_unavailable",
             snapshot.account.positions_risk_unavailable},
            {"trade_mode", snapshot.account.trade_mode},
            {"leverage", snapshot.account.leverage},
        };
        result["market"] = {
            {"bid", snapshot.quote.bid},
            {"ask", snapshot.quote.ask},
            {"spread_points", (snapshot.quote.ask - snapshot.quote.bid) /
                                  snapshot.quote.point},
            {"point", snapshot.quote.point},
            {"digits", snapshot.quote.digits},
        };
        result["indicators"] = {
            {"ema_fast", snapshot.indicators.ema_fast},
            {"ema_slow", snapshot.indicators.ema_slow},
            {"rsi", snapshot.indicators.rsi},
            {"atr", snapshot.indicators.atr},
        };
        result["signal"] = {
            {"direction", snapshot.signal.direction},
            {"confidence", snapshot.signal.confidence},
            {"reason", snapshot.signal.reason},
        };
        const auto observation = lumo::observe_market(
            snapshot.candles, snapshot.quote, snapshot.signal, config_,
            snapshot.received_at);
        result["observation"] = observation_to_json(observation);
        if (!observation.critical_data_available) {
            result["alerts"].push_back(
                "Market observation is incomplete; trading must remain blocked.");
        }
        if (observation.volatility == "Elevated") {
            result["alerts"].push_back(
                "Volatility is elevated relative to the recent ATR baseline.");
        }
        if (observation.spread_points > config_.spread_limit_points) {
            result["alerts"].push_back(
                "Current spread exceeds the configured observation threshold.");
        }
        if ((snapshot.signal.direction == "BUY" &&
             observation.trend == "Bearish") ||
            (snapshot.signal.direction == "SELL" &&
             observation.trend == "Bullish")) {
            result["alerts"].push_back(
                "Strategy signal conflicts with the broader observed trend.");
        }
        if (observation.atr > 0.0 &&
            (std::abs(snapshot.candles.back().close - observation.support) <=
                 observation.atr * 0.25 ||
             std::abs(snapshot.candles.back().close - observation.resistance) <=
                 observation.atr * 0.25)) {
            result["alerts"].push_back(
                "Price is near a recent 20-candle support or resistance level.");
        }
        if (observation.candle_pattern == "Bullish engulfing" ||
            observation.candle_pattern == "Bearish engulfing" ||
            observation.candle_pattern == "Doji" ||
            observation.candle_pattern == "Shooting-star-like candle" ||
            observation.candle_pattern == "Hammer-like candle") {
            result["alerts"].push_back(
                "A potential reversal or indecision candle was observed; review before acting.");
        }
        const auto risk =
            lumo::assess_risk(config, snapshot.account,
                              (snapshot.quote.ask - snapshot.quote.bid) /
                                  snapshot.quote.point);
        result["risk"] = {
            {"day_pnl", risk.daily_loss},
            {"day_loss_percent", risk.day_loss_percent},
            {"daily_loss_limit_percent", config.daily_loss_limit * 100.0},
            {"daily_loss_limit_reached", risk.trade_lock},
            {"open_risk_percent", risk.open_risk_percent},
            {"margin_level_percent", risk.margin_level_percent},
            {"open_positions", risk.open_positions},
            {"positions_without_stop", risk.positions_without_stop},
            {"positions_risk_unavailable",
             risk.positions_risk_unavailable},
            {"warnings", risk.warnings},
        };
        for (const auto& warning : risk.warnings) {
            result["alerts"].push_back(warning);
        }
        result["decision_protocol"] = {
            {"market_observation", observation.critical_data_available},
            {"market_analysis", observation.critical_data_available},
            {"trend_identification", observation.critical_data_available},
            {"volatility_analysis", observation.critical_data_available},
            {"signal_generation", observation.critical_data_available},
            {"risk_assessment", true},
            {"risk_checks_passed", risk.warnings.empty()},
            {"trade_approval", false},
            {"trade_execution", false},
            {"market_monitoring", observation.critical_data_available},
            {"trade_closure", false},
            {"post_trade_analysis", false},
            {"performance_report", true},
            {"execution_status", "Disabled: local dashboard is read-only"},
        };
        for (const auto& candle : snapshot.candles) {
            result["candles"].push_back({
                {"time", candle.time},
                {"open", candle.open},
                {"high", candle.high},
                {"low", candle.low},
                {"close", candle.close},
                {"tick_volume", candle.tick_volume},
            });
        }
        return result;
    }

    std::optional<std::vector<lumo::Candle>> candles_for_backtest() const {
        std::shared_lock lock(mutex_);
        if (!snapshot_ || !connection_target_ ||
            !snapshot_is_current(*snapshot_) ||
            !lumo::account_matches(snapshot_->account, connection_target_->first,
                                   connection_target_->second)) {
            return std::nullopt;
        }
        return snapshot_->candles;
    }

    std::optional<std::pair<std::int64_t, std::string>>
    observation_history_account() const {
        std::shared_lock lock(mutex_);
        if (!snapshot_ || !connection_target_ ||
            !snapshot_is_current(*snapshot_) ||
            !lumo::account_matches(snapshot_->account, connection_target_->first,
                                   connection_target_->second)) {
            return std::nullopt;
        }
        return connection_target_;
    }

private:
    static bool snapshot_is_current(const lumo::Snapshot& snapshot) {
        const auto age_seconds = lumo::unix_time_now() - snapshot.received_at;
        const auto quote_age_seconds = lumo::unix_time_now() - snapshot.quote.time;
        return age_seconds >= 0 && age_seconds <= 15 &&
               quote_age_seconds >= 0 && quote_age_seconds <= 60;
    }

    lumo::Database& database_;
    const lumo::Config& config_;
    mutable std::shared_mutex mutex_;
    std::optional<lumo::Snapshot> snapshot_;
    std::optional<std::pair<std::int64_t, std::string>> connection_target_;
};

void send_json(httplib::Response& response, int status, const json& body) {
    response.status = status;
    response.set_header("Cache-Control", "no-store");
    response.set_header("X-Content-Type-Options", "nosniff");
    response.set_content(body.dump(), "application/json; charset=utf-8");
}

bool secure_token_matches(std::string_view expected, std::string_view actual) {
    std::size_t difference = expected.size() ^ actual.size();
    const auto length = std::max(expected.size(), actual.size());
    for (std::size_t i = 0; i < length; ++i) {
        const auto expected_char = i < expected.size() ? expected[i] : '\0';
        const auto actual_char = i < actual.size() ? actual[i] : '\0';
        difference |= static_cast<std::size_t>(
            static_cast<unsigned char>(expected_char ^ actual_char));
    }
    return difference == 0;
}

bool bridge_request_authorized(const httplib::Request& request,
                               const lumo::Config& config) {
    if (config.bridge_token.empty()) {
        return true;
    }
    const std::string expected = "Bearer " + config.bridge_token;
    return secure_token_matches(expected, request.get_header_value("Authorization"));
}

double require_finite_number(const json& object, const char* key) {
    if (!object.contains(key) || !object.at(key).is_number()) {
        throw std::invalid_argument(std::string("Missing numeric field: ") + key);
    }
    const double value = object.at(key).get<double>();
    if (!std::isfinite(value)) {
        throw std::invalid_argument(std::string("Field must be finite: ") + key);
    }
    return value;
}

lumo::Snapshot parse_snapshot(const json& body, const lumo::Config& config) {
    if (!body.is_object()) {
        throw std::invalid_argument("Request body must be a JSON object");
    }
    lumo::Snapshot snapshot;
    snapshot.symbol = body.at("symbol").get<std::string>();
    snapshot.timeframe = body.at("timeframe").get<std::string>();

    const auto& account = body.at("account");
    const auto& quote = body.at("quote");
    if (!account.is_object() || !quote.is_object() ||
        !body.contains("candles") || !body.at("candles").is_array()) {
        throw std::invalid_argument("Snapshot account, quote, and candles have invalid shapes");
    }
    snapshot.account.server = account.at("server").get<std::string>();
    snapshot.account.currency = account.at("currency").get<std::string>();
    snapshot.account.login = account.at("login").get<std::int64_t>();
    snapshot.account.balance = require_finite_number(account, "balance");
    snapshot.account.equity = require_finite_number(account, "equity");
    snapshot.account.margin = require_finite_number(account, "margin");
    snapshot.account.free_margin = require_finite_number(account, "free_margin");
    snapshot.account.day_start_balance =
        require_finite_number(account, "day_start_balance");
    snapshot.account.day_pnl = require_finite_number(account, "day_pnl");
    snapshot.account.floating_pnl = require_finite_number(account, "floating_pnl");
    snapshot.account.open_volume = require_finite_number(account, "open_volume");
    snapshot.account.risk_at_stops =
        require_finite_number(account, "risk_at_stops");
    snapshot.account.open_positions = account.at("open_positions").get<int>();
    snapshot.account.positions_without_stop =
        account.at("positions_without_stop").get<int>();
    snapshot.account.positions_risk_unavailable =
        account.at("positions_risk_unavailable").get<int>();
    snapshot.account.trade_mode = account.at("trade_mode").get<int>();
    snapshot.account.leverage = account.at("leverage").get<int>();
    snapshot.quote.bid = require_finite_number(quote, "bid");
    snapshot.quote.ask = require_finite_number(quote, "ask");
    snapshot.quote.point = require_finite_number(quote, "point");
    snapshot.quote.digits = quote.at("digits").get<int>();
    snapshot.quote.time = quote.at("time").get<std::int64_t>();

    for (const auto& item : body.at("candles")) {
        if (!item.is_object()) {
            throw std::invalid_argument("Each candle must be a JSON object");
        }
        snapshot.candles.push_back({
            item.at("time").get<std::int64_t>(),
            require_finite_number(item, "open"),
            require_finite_number(item, "high"),
            require_finite_number(item, "low"),
            require_finite_number(item, "close"),
            item.at("tick_volume").get<std::uint64_t>(),
        });
    }
    lumo::validate_snapshot(snapshot, config);
    snapshot.received_at = lumo::unix_time_now();
    snapshot.indicators = lumo::calculate_indicators(snapshot.candles, config);
    snapshot.signal = lumo::evaluate_strategy(snapshot.candles, config);
    return snapshot;
}

void log_line(const std::string& message) {
    static std::mutex log_mutex;
    std::lock_guard lock(log_mutex);
    const auto line = "[" + std::to_string(lumo::unix_time_now()) + "] " + message;
    std::cout << line << std::endl;
    if (log_file.is_open()) {
        log_file << line << std::endl;
    }
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc > 2) {
            std::cerr << "Usage: lumo-edge [--help|--version|--check-config]"
                      << std::endl;
            return 2;
        }
        if (argc == 2 && std::string_view(argv[1]) == "--help") {
            std::cout << "Lumo Edge read-only MT5 dashboard\n"
                         "Usage: lumo-edge [--help|--version|--check-config]\n"
                         "Run from the project root. Optional settings are read "
                         "from .env and process environment.\n";
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "--version") {
#ifndef LUMO_EDGE_VERSION
#define LUMO_EDGE_VERSION "unknown"
#endif
            std::cout << "Lumo Edge " << LUMO_EDGE_VERSION << std::endl;
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) != "--check-config") {
            std::cerr << "Unknown option: " << argv[1] << std::endl;
            return 2;
        }
        const auto config = lumo::Config::from_environment();
        if (argc == 2) {
            std::cout << "Configuration is valid. Read-only server will listen on "
                      << "127.0.0.1:" << config.port << "." << std::endl;
            return 0;
        }
        const auto configured_log_path = read_environment("LUMO_LOG_PATH");
        const std::filesystem::path log_path =
            !configured_log_path || configured_log_path->empty()
                ? std::filesystem::path("logs/forex_bot.log")
                : std::filesystem::path(*configured_log_path);
        if (log_path.has_parent_path()) {
            std::filesystem::create_directories(log_path.parent_path());
        }
        log_file.open(log_path, std::ios::app);
        if (!log_file.is_open()) {
            throw std::runtime_error("Could not open " + log_path.string());
        }
        lumo::Database database(config.database_path);
        AppState state(database, config);
        httplib::Server server;
        server.set_payload_max_length(1024 * 1024);
        server.set_read_timeout(5, 0);
        server.set_write_timeout(5, 0);
        server.new_task_queue = [] { return new httplib::ThreadPool(4); };

        server.Get("/healthz", [&config](const httplib::Request&,
                                         httplib::Response& response) {
            send_json(response, 200,
                      {{"status", "ok"},
                       {"mode", "read-only"},
                       {"bridge_auth_required", !config.bridge_token.empty()}});
        });
        server.Get("/api/state", [&state, &config](const httplib::Request&,
                                                   httplib::Response& response) {
            send_json(response, 200, state.to_json(config));
        });
        server.Post("/api/mt5/connection",
                    [&state, &config](const httplib::Request& request,
                                      httplib::Response& response) {
                        if (request.get_header_value("Content-Type")
                                .find("application/json") == std::string::npos) {
                            send_json(response, 415,
                                      {{"error", "Content-Type must be application/json"}});
                            return;
                        }
                        try {
                            const auto body = json::parse(request.body);
                            if (!body.is_object() || !body.contains("login") ||
                                !body.at("login").is_number_integer() ||
                                !body.contains("server") ||
                                !body.at("server").is_string()) {
                                throw std::invalid_argument(
                                    "Enter an MT5 login number and broker server");
                            }
                            const auto login = body.at("login").get<std::int64_t>();
                            auto server = body.at("server").get<std::string>();
                            const auto first = server.find_first_not_of(" \t\r\n");
                            if (first == std::string::npos) {
                                throw std::invalid_argument(
                                    "Broker server name cannot be empty");
                            }
                            const auto last = server.find_last_not_of(" \t\r\n");
                            server = server.substr(first, last - first + 1);
                            if (login <= 0 || server.size() > 128) {
                                throw std::invalid_argument(
                                    "Login must be positive and broker server must be at most 128 characters");
                            }
                            const auto error = state.connect(login, std::move(server));
                            if (error) {
                                send_json(response, 409, {{"error", *error}});
                                return;
                            }
                            const auto current = state.to_json(config);
                            const bool connected =
                                current.at("connection_status") == "connected";
                            send_json(response, connected ? 200 : 202,
                                      {{"status", current.at("connection_status")},
                                       {"message", current.at("connection_message")}});
                        } catch (const json::exception& error) {
                            send_json(response, 400,
                                      {{"error", std::string("Invalid connection request: ") +
                                                     error.what()}});
                        } catch (const std::invalid_argument& error) {
                            send_json(response, 400, {{"error", error.what()}});
                        }
                    });
        server.Delete("/api/mt5/connection",
                      [&state](const httplib::Request&,
                               httplib::Response& response) {
                          state.disconnect();
                          send_json(response, 200,
                                    {{"status", "disconnected"},
                                     {"message", "Dashboard disconnected. Disable the bridge in MT5 to stop its local data relay."}});
                      });
        server.Get("/api/history", [&database](const httplib::Request& request,
                                               httplib::Response& response) {
            try {
                int limit = 25;
                if (request.has_param("limit")) {
                    const auto value = request.get_param_value("limit");
                    const auto parsed = std::from_chars(
                        value.data(), value.data() + value.size(), limit);
                    if (parsed.ec != std::errc{} ||
                        parsed.ptr != value.data() + value.size() ||
                        limit < 1 || limit > 100) {
                        send_json(response, 400,
                                  {{"error", "limit must be an integer from 1 to 100"}});
                        return;
                    }
                }
                json rows = json::array();
                for (const auto& signal : database.recent_signals(limit)) {
                    rows.push_back({
                        {"id", signal.id},
                        {"symbol", signal.symbol},
                        {"direction", signal.direction},
                        {"confidence", signal.confidence},
                        {"reason", signal.reason},
                        {"created_at", signal.created_at},
                    });
                }
                send_json(response, 200, {{"signals", std::move(rows)}});
            } catch (const std::exception& error) {
                log_line(std::string("Loading signal history failed: ") + error.what());
                send_json(response, 500, {{"error", "Could not load signal history"}});
            }
        });
        server.Get("/api/observations",
                   [&database, &state](const httplib::Request& request,
                                      httplib::Response& response) {
                       try {
                           const auto account = state.observation_history_account();
                           if (!account) {
                               send_json(response, 200,
                                         {{"observations", json::array()}});
                               return;
                           }
                           int limit = 50;
                           if (request.has_param("limit")) {
                               const auto value = request.get_param_value("limit");
                               const auto parsed = std::from_chars(
                                   value.data(), value.data() + value.size(), limit);
                               if (parsed.ec != std::errc{} ||
                                   parsed.ptr != value.data() + value.size() ||
                                   limit < 1 || limit > 100) {
                                   send_json(response, 400,
                                             {{"error", "limit must be an integer from 1 to 100"}});
                                   return;
                               }
                           }
                           json rows = json::array();
                           for (const auto& observation :
                                database.recent_observations(
                                    limit, account->first, account->second)) {
                               rows.push_back({
                                   {"id", observation.id},
                                   {"observed_at", observation.observed_at},
                                   {"symbol", observation.symbol},
                                   {"reference_close", observation.reference_close},
                                   {"predicted_direction",
                                    observation.predicted_direction},
                                   {"actual_direction",
                                    observation.actual_direction.empty()
                                        ? json(nullptr)
                                        : json(observation.actual_direction)},
                                   {"observation",
                                    json::parse(observation.payload_json)},
                               });
                           }
                           send_json(response, 200,
                                     {{"observations", std::move(rows)}});
                       } catch (const std::exception& error) {
                           log_line(std::string("Loading observations failed: ") +
                                    error.what());
                           send_json(response, 500,
                                     {{"error", "Could not load market observations"}});
                       }
                   });
        server.Get("/api/backtest", [&state, &config](const httplib::Request&,
                                                      httplib::Response& response) {
            try {
                const auto candles = state.candles_for_backtest();
                if (!candles || candles->size() < 252) {
                    send_json(response, 503,
                              {{"error", "At least 252 fresh, verified candles are required"}});
                    return;
                }
                const auto result = lumo::run_backtest(*candles, config);
                send_json(response, 200,
                          {{"total_trades", result.total_trades},
                           {"wins", result.wins},
                           {"losses", result.losses},
                           {"net_profit", result.net_profit},
                           {"max_drawdown", result.max_drawdown},
                           {"win_rate", result.win_rate},
                           {"profit_factor", result.profit_factor},
                           {"profit_factor_unbounded",
                            result.profit_factor_unbounded},
                           {"bars", candles->size()},
                           {"note", "Historical simulation only; assumes one lot and a 100-unit contract, excludes currency conversion, spread, slippage, commissions, and swap."}});
            } catch (const std::exception& error) {
                log_line(std::string("Backtest failed: ") + error.what());
                send_json(response, 500, {{"error", "Could not complete the backtest"}});
            }
        });
        server.Post("/api/mt5/snapshot",
                    [&state, &config](const httplib::Request& request,
                                      httplib::Response& response) {
                        if (!bridge_request_authorized(request, config)) {
                            send_json(response, 401,
                                      {{"error", "Invalid bridge authorization"}});
                            return;
                        }
                        if (request.get_header_value("Content-Type")
                                .find("application/json") == std::string::npos) {
                            send_json(response, 415,
                                      {{"error", "Content-Type must be application/json"}});
                            return;
                        }
                        try {
                            const auto body = json::parse(request.body);
                            auto snapshot = parse_snapshot(body, config);
                            const json accepted = {
                                {"accepted", true},
                                {"direction", snapshot.signal.direction},
                            };
                            state.update(std::move(snapshot));
                            send_json(response, 200, accepted);
                        } catch (const json::exception& error) {
                            send_json(response, 400,
                                      {{"error", std::string("Invalid snapshot JSON: ") +
                                                     error.what()}});
                        } catch (const std::invalid_argument& error) {
                            send_json(response, 400, {{"error", error.what()}});
                        } catch (const std::exception& error) {
                            log_line(std::string("Snapshot processing failed: ") + error.what());
                            send_json(response, 500, {{"error", "Snapshot processing failed"}});
                        }
                    });

        const auto configured_web_root = read_environment("LUMO_WEB_ROOT");
        const std::string web_root =
            !configured_web_root || configured_web_root->empty()
                ? "web"
                : *configured_web_root;
        if (!server.set_mount_point("/", web_root)) {
            throw std::runtime_error("Could not mount the web/ dashboard directory");
        }
        log_line("Starting read-only dashboard at http://127.0.0.1:" +
                 std::to_string(config.port));
        log_line("MT5 bridge endpoint: http://127.0.0.1:" +
                 std::to_string(config.port) + "/api/mt5/snapshot");
        if (!server.listen("127.0.0.1", config.port)) {
            throw std::runtime_error("Could not bind the local dashboard port");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Startup failed: " << error.what() << std::endl;
        return 1;
    }
}
