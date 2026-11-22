#include "lumo/database.hpp"

#include <sqlite3.h>

#include <filesystem>
#include <stdexcept>
#include <string>

namespace lumo {
namespace {

void check_sqlite(int result, sqlite3* connection, const std::string& operation) {
    if (result != SQLITE_OK && result != SQLITE_DONE && result != SQLITE_ROW) {
        throw std::runtime_error(operation + ": " + sqlite3_errmsg(connection));
    }
}

class Statement {
public:
    Statement(sqlite3* connection, const char* sql) : connection_(connection) {
        check_sqlite(sqlite3_prepare_v2(connection_, sql, -1, &statement_, nullptr),
                     connection_, "Preparing database statement failed");
    }
    ~Statement() { sqlite3_finalize(statement_); }

    sqlite3_stmt* get() const { return statement_; }

    void bind_text(int index, const std::string& value) {
        check_sqlite(sqlite3_bind_text(statement_, index, value.c_str(), -1,
                                       SQLITE_TRANSIENT),
                     connection_, "Binding database text failed");
    }

    void bind_double(int index, double value) {
        check_sqlite(sqlite3_bind_double(statement_, index, value), connection_,
                     "Binding database number failed");
    }

    void bind_int64(int index, std::int64_t value) {
        check_sqlite(sqlite3_bind_int64(statement_, index, value), connection_,
                     "Binding database integer failed");
    }

    void bind_null(int index) {
        check_sqlite(sqlite3_bind_null(statement_, index), connection_,
                     "Binding database null failed");
    }

    int step() {
        return sqlite3_step(statement_);
    }

private:
    sqlite3* connection_;
    sqlite3_stmt* statement_ = nullptr;
};

}  // namespace

Database::Database(const std::string& path) {
    const std::filesystem::path database_path(path);
    if (database_path.has_parent_path()) {
        std::filesystem::create_directories(database_path.parent_path());
    }
    const int result = sqlite3_open_v2(
        path.c_str(), &connection_,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
    if (result != SQLITE_OK) {
        const std::string message = connection_ == nullptr
                                        ? "Could not open database"
                                        : sqlite3_errmsg(connection_);
        if (connection_ != nullptr) {
            sqlite3_close(connection_);
            connection_ = nullptr;
        }
        throw std::runtime_error("Opening database failed: " + message);
    }
    sqlite3_busy_timeout(connection_, 5000);
    char* pragma_error = nullptr;
    const int pragma_result =
        sqlite3_exec(connection_, "PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL;",
                     nullptr, nullptr, &pragma_error);
    if (pragma_result != SQLITE_OK) {
        const std::string message =
            pragma_error == nullptr ? sqlite3_errmsg(connection_) : pragma_error;
        sqlite3_free(pragma_error);
        sqlite3_close(connection_);
        connection_ = nullptr;
        throw std::runtime_error("Configuring database failed: " + message);
    }
    const char* schema =
        "CREATE TABLE IF NOT EXISTS signals ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, symbol TEXT, direction TEXT, "
        "confidence REAL, reason TEXT, created_at TEXT DEFAULT CURRENT_TIMESTAMP);"
        "CREATE TABLE IF NOT EXISTS trades ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, symbol TEXT, direction TEXT, "
        "entry_price REAL, stop_loss REAL, take_profit REAL, volume REAL, status TEXT, "
        "opened_at TEXT DEFAULT CURRENT_TIMESTAMP, closed_at TEXT, pnl REAL DEFAULT 0.0, "
        "metadata TEXT);"
        "CREATE TABLE IF NOT EXISTS risk_state (key TEXT PRIMARY KEY, value TEXT);"
        "CREATE TABLE IF NOT EXISTS observations ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, observed_at INTEGER NOT NULL, "
        "account_login INTEGER NOT NULL, account_server TEXT NOT NULL, "
        "symbol TEXT NOT NULL, reference_candle_time INTEGER NOT NULL, "
        "reference_close REAL NOT NULL, "
        "predicted_direction TEXT NOT NULL, actual_direction TEXT NOT NULL DEFAULT '', "
        "evaluated_at INTEGER, payload TEXT NOT NULL);"
        "CREATE TABLE IF NOT EXISTS trade_events ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "account_login INTEGER NOT NULL, account_server TEXT NOT NULL COLLATE NOCASE, "
        "deal_ticket INTEGER NOT NULL, position_id INTEGER NOT NULL, "
        "order_id INTEGER NOT NULL, magic INTEGER NOT NULL, symbol TEXT NOT NULL, "
        "direction TEXT NOT NULL, status TEXT NOT NULL, price REAL NOT NULL, "
        "stop_loss REAL NOT NULL, take_profit REAL NOT NULL, volume REAL NOT NULL, "
        "pnl REAL NOT NULL, event_time INTEGER NOT NULL, "
        "UNIQUE(account_login, account_server, deal_ticket));"
        "CREATE INDEX IF NOT EXISTS signals_created_at_idx ON signals(created_at DESC);"
        "CREATE INDEX IF NOT EXISTS observations_account_time_idx "
        "ON observations(account_login, account_server, observed_at DESC);"
        "CREATE INDEX IF NOT EXISTS observations_pending_idx "
        "ON observations(account_login, account_server, symbol, actual_direction, "
        "reference_candle_time DESC);"
        "CREATE INDEX IF NOT EXISTS trade_events_account_time_idx "
        "ON trade_events(account_login, account_server, event_time DESC);";
    char* error = nullptr;
    const int schema_result = sqlite3_exec(connection_, schema, nullptr, nullptr, &error);
    if (schema_result != SQLITE_OK) {
        const std::string message = error == nullptr ? sqlite3_errmsg(connection_) : error;
        sqlite3_free(error);
        sqlite3_close(connection_);
        connection_ = nullptr;
        throw std::runtime_error("Initializing database failed: " + message);
    }
}

Database::~Database() {
    if (connection_ != nullptr) {
        sqlite3_close(connection_);
    }
}

void Database::save_signal(const std::string& symbol, const std::string& direction,
                           double confidence, const std::string& reason) {
    std::lock_guard lock(mutex_);
    Statement statement(connection_,
                        "INSERT INTO signals (symbol, direction, confidence, reason) "
                        "VALUES (?, ?, ?, ?)");
    statement.bind_text(1, symbol);
    statement.bind_text(2, direction);
    statement.bind_double(3, confidence);
    statement.bind_text(4, reason);
    check_sqlite(statement.step(), connection_, "Saving signal failed");
}

std::vector<SignalRecord> Database::recent_signals(int limit) {
    if (limit < 1 || limit > 100) {
        throw std::invalid_argument("Signal history limit must be between 1 and 100");
    }
    std::lock_guard lock(mutex_);
    Statement statement(
        connection_,
        "SELECT id, symbol, direction, confidence, reason, created_at "
        "FROM signals ORDER BY id DESC LIMIT ?");
    statement.bind_int64(1, limit);
    std::vector<SignalRecord> signals;
    int result = statement.step();
    while (result == SQLITE_ROW) {
        const auto text_column = [&statement](int column) {
            const auto* value = sqlite3_column_text(statement.get(), column);
            return value == nullptr
                       ? std::string{}
                       : std::string(reinterpret_cast<const char*>(value));
        };
        signals.push_back({
            sqlite3_column_int64(statement.get(), 0),
            text_column(1),
            text_column(2),
            sqlite3_column_double(statement.get(), 3),
            text_column(4),
            text_column(5),
        });
        result = statement.step();
    }
    check_sqlite(result, connection_, "Loading signal history failed");
    return signals;
}

void Database::save_observation(std::int64_t observed_at,
                                std::int64_t account_login,
                                const std::string& account_server,
                                const std::string& symbol,
                                std::int64_t reference_candle_time,
                                double reference_close,
                                const std::string& predicted_direction,
                                const std::string& payload_json) {
    std::lock_guard lock(mutex_);
    Statement statement(connection_,
                        "INSERT INTO observations (observed_at, symbol, "
                        "account_login, account_server, reference_candle_time, "
                        "reference_close, predicted_direction, payload) "
                        "VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
    statement.bind_int64(1, observed_at);
    statement.bind_text(2, symbol);
    statement.bind_int64(3, account_login);
    statement.bind_text(4, account_server);
    statement.bind_int64(5, reference_candle_time);
    statement.bind_double(6, reference_close);
    statement.bind_text(7, predicted_direction);
    statement.bind_text(8, payload_json);
    check_sqlite(statement.step(), connection_, "Saving market observation failed");
}

void Database::evaluate_latest_observation(const std::string& symbol,
                                           std::int64_t next_candle_time,
                                           std::int64_t expected_previous_candle_time,
                                           double next_close,
                                           std::int64_t evaluated_at,
                                           std::int64_t account_login,
                                           const std::string& account_server) {
    std::lock_guard lock(mutex_);
    Statement statement(
        connection_,
        "UPDATE observations SET actual_direction = CASE "
        "WHEN reference_candle_time != ? THEN 'GAP' "
        "WHEN ? > reference_close THEN 'BULLISH' "
        "WHEN ? < reference_close THEN 'BEARISH' ELSE 'UNCHANGED' END, "
        "evaluated_at = ? WHERE id = (SELECT id FROM observations "
        "WHERE symbol = ? AND account_login = ? AND account_server = ? COLLATE NOCASE "
        "AND actual_direction = '' AND reference_candle_time < ? "
        "ORDER BY observed_at DESC, id DESC LIMIT 1)");
    statement.bind_int64(1, expected_previous_candle_time);
    statement.bind_double(2, next_close);
    statement.bind_double(3, next_close);
    statement.bind_int64(4, evaluated_at);
    statement.bind_text(5, symbol);
    statement.bind_int64(6, account_login);
    statement.bind_text(7, account_server);
    statement.bind_int64(8, next_candle_time);
    check_sqlite(statement.step(), connection_,
                 "Evaluating prior market observation failed");
}

std::vector<ObservationRecord> Database::recent_observations(
    int limit, std::int64_t account_login, const std::string& account_server) {
    if (limit < 1 || limit > 100) {
        throw std::invalid_argument(
            "Market observation history limit must be between 1 and 100");
    }
    std::lock_guard lock(mutex_);
    Statement statement(
        connection_,
        "SELECT id, observed_at, reference_candle_time, symbol, reference_close, predicted_direction, "
        "actual_direction, payload FROM observations "
        "WHERE account_login = ? AND account_server = ? COLLATE NOCASE "
        "ORDER BY observed_at DESC, id DESC LIMIT ?");
    statement.bind_int64(1, account_login);
    statement.bind_text(2, account_server);
    statement.bind_int64(3, limit);
    std::vector<ObservationRecord> observations;
    int result = statement.step();
    while (result == SQLITE_ROW) {
        const auto text_column = [&statement](int column) {
            const auto* value = sqlite3_column_text(statement.get(), column);
            return value == nullptr
                       ? std::string{}
                       : std::string(reinterpret_cast<const char*>(value));
        };
        observations.push_back({
            sqlite3_column_int64(statement.get(), 0),
            sqlite3_column_int64(statement.get(), 1),
            sqlite3_column_int64(statement.get(), 2),
            text_column(3),
            sqlite3_column_double(statement.get(), 4),
            text_column(5),
            text_column(6),
            text_column(7),
        });
        result = statement.step();
    }
    check_sqlite(result, connection_,
                 "Loading market observation history failed");
    return observations;
}

void Database::save_trade(const std::string& symbol, const std::string& direction,
                          double entry_price, double stop_loss, double take_profit,
                          double volume, const std::string& status,
                          const std::string& metadata_json) {
    std::lock_guard lock(mutex_);
    Statement statement(
        connection_,
        "INSERT INTO trades (symbol, direction, entry_price, stop_loss, take_profit, "
        "volume, status, metadata) VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
    statement.bind_text(1, symbol);
    statement.bind_text(2, direction);
    statement.bind_double(3, entry_price);
    statement.bind_double(4, stop_loss);
    statement.bind_double(5, take_profit);
    statement.bind_double(6, volume);
    statement.bind_text(7, status);
    statement.bind_text(8, metadata_json);
    check_sqlite(statement.step(), connection_, "Saving trade failed");
}

void Database::update_trade_status(std::int64_t trade_id,
                                   const std::string& status,
                                   std::optional<double> pnl,
                                   std::optional<std::string> closed_at) {
    std::lock_guard lock(mutex_);
    if (!pnl.has_value() && !closed_at.has_value()) {
        Statement statement(connection_, "UPDATE trades SET status = ? WHERE id = ?");
        statement.bind_text(1, status);
        statement.bind_int64(2, trade_id);
        check_sqlite(statement.step(), connection_,
                     "Updating trade status failed");
        return;
    }
    Statement statement(
        connection_,
        "UPDATE trades SET status = ?, pnl = ?, closed_at = ? WHERE id = ?");
    statement.bind_text(1, status);
    if (pnl.has_value()) {
        statement.bind_double(2, *pnl);
    } else {
        statement.bind_null(2);
    }
    if (closed_at.has_value()) {
        statement.bind_text(3, *closed_at);
    } else {
        statement.bind_null(3);
    }
    statement.bind_int64(4, trade_id);
    check_sqlite(statement.step(), connection_,
                 "Updating trade status failed");
}

bool Database::save_trade_event(const TradeEventRecord& event) {
    std::lock_guard lock(mutex_);
    Statement statement(
        connection_,
        "INSERT OR IGNORE INTO trade_events "
        "(account_login, account_server, deal_ticket, position_id, order_id, "
        "magic, symbol, direction, status, price, stop_loss, take_profit, "
        "volume, pnl, event_time) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    statement.bind_int64(1, event.account_login);
    statement.bind_text(2, event.account_server);
    statement.bind_int64(3, event.deal_ticket);
    statement.bind_int64(4, event.position_id);
    statement.bind_int64(5, event.order_id);
    statement.bind_int64(6, event.magic);
    statement.bind_text(7, event.symbol);
    statement.bind_text(8, event.direction);
    statement.bind_text(9, event.status);
    statement.bind_double(10, event.price);
    statement.bind_double(11, event.stop_loss);
    statement.bind_double(12, event.take_profit);
    statement.bind_double(13, event.volume);
    statement.bind_double(14, event.pnl);
    statement.bind_int64(15, event.event_time);
    check_sqlite(statement.step(), connection_, "Saving MT5 trade event failed");
    return sqlite3_changes(connection_) > 0;
}

std::vector<TradeEventRecord> Database::recent_trade_events(
    int limit, std::int64_t account_login,
    const std::string& account_server) {
    std::lock_guard lock(mutex_);
    Statement statement(
        connection_,
        "SELECT deal_ticket, position_id, order_id, account_login, magic, "
        "event_time, account_server, symbol, direction, status, price, "
        "stop_loss, take_profit, volume, pnl FROM trade_events "
        "WHERE account_login = ? AND account_server = ? COLLATE NOCASE "
        "ORDER BY event_time DESC, id DESC LIMIT ?");
    statement.bind_int64(1, account_login);
    statement.bind_text(2, account_server);
    statement.bind_int64(3, limit);
    std::vector<TradeEventRecord> events;
    int result = statement.step();
    while (result == SQLITE_ROW) {
        const auto text_column = [&statement](int column) {
            const auto* value = sqlite3_column_text(statement.get(), column);
            return value == nullptr
                       ? std::string{}
                       : std::string(reinterpret_cast<const char*>(value));
        };
        events.push_back({
            sqlite3_column_int64(statement.get(), 0),
            sqlite3_column_int64(statement.get(), 1),
            sqlite3_column_int64(statement.get(), 2),
            sqlite3_column_int64(statement.get(), 3),
            sqlite3_column_int64(statement.get(), 4),
            sqlite3_column_int64(statement.get(), 5),
            text_column(6),
            text_column(7),
            text_column(8),
            text_column(9),
            sqlite3_column_double(statement.get(), 10),
            sqlite3_column_double(statement.get(), 11),
            sqlite3_column_double(statement.get(), 12),
            sqlite3_column_double(statement.get(), 13),
            sqlite3_column_double(statement.get(), 14),
        });
        result = statement.step();
    }
    check_sqlite(result, connection_, "Loading MT5 trade events failed");
    return events;
}

void Database::set_state(const std::string& key, const std::string& value) {
    std::lock_guard lock(mutex_);
    Statement statement(
        connection_,
        "INSERT INTO risk_state (key, value) VALUES (?, ?) "
        "ON CONFLICT(key) DO UPDATE SET value = excluded.value");
    statement.bind_text(1, key);
    statement.bind_text(2, value);
    check_sqlite(statement.step(), connection_, "Saving application state failed");
}

std::optional<std::string> Database::get_state(const std::string& key) {
    std::lock_guard lock(mutex_);
    Statement statement(connection_, "SELECT value FROM risk_state WHERE key = ?");
    statement.bind_text(1, key);
    const int result = statement.step();
    if (result == SQLITE_DONE) {
        return std::nullopt;
    }
    check_sqlite(result, connection_, "Reading application state failed");
    const auto* value = sqlite3_column_text(statement.get(), 0);
    return value == nullptr ? std::optional<std::string>{}
                            : std::optional<std::string>{reinterpret_cast<const char*>(value)};
}

}  // namespace lumo
