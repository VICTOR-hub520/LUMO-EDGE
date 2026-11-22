#include "lumo/database.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

void remove_database_files(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::remove(path, error);
    std::filesystem::remove(path.string() + "-wal", error);
    std::filesystem::remove(path.string() + "-shm", error);
}

void test_database_persists_signal_history_and_state() {
    const auto filename =
        "lumo-edge-test-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
        ".db";
    const auto path = std::filesystem::temp_directory_path() / filename;
    remove_database_files(path);
    {
        lumo::Database database(path.string());
        database.save_signal("XAUUSD", "BUY", 0.75, "Test signal");
        database.save_signal("XAUUSD", "SELL", 0.62, "Second signal");
        const auto signals = database.recent_signals(1);
        assert(signals.size() == 1);
        assert(signals.front().symbol == "XAUUSD");
        assert(signals.front().direction == "SELL");
        assert(signals.front().confidence == 0.62);
        database.set_state("risk_lock", "true");
        assert(database.get_state("risk_lock") == "true");
        assert(!database.get_state("missing").has_value());
        database.save_observation(1'700'000'000, 123456, "Broker-Demo",
                                  "XAUUSD", 1'700'000'000, 2300.0, "Bullish",
                                  "{\"classification\":\"Weak bullish\"}");
        auto observations =
            database.recent_observations(10, 123456, "broker-demo");
        assert(observations.size() == 1);
        assert(observations.front().symbol == "XAUUSD");
        assert(observations.front().predicted_direction == "Bullish");
        assert(observations.front().actual_direction.empty());
        assert(database.recent_observations(10, 654321, "Broker-Demo").empty());
        database.save_observation(1'700'000'010, 654321, "Broker-Demo",
                                  "XAUUSD", 1'700'000'010, 2400.0, "Bearish",
                                  "{\"classification\":\"Weak bearish\"}");
        database.evaluate_latest_observation(
            "XAUUSD", 1'700'000'300, 1'700'000'000, 2301.0,
            1'700'000'300, 123456, "BROKER-DEMO");
        observations =
            database.recent_observations(10, 123456, "Broker-Demo");
        assert(observations.front().actual_direction == "BULLISH");
        assert(observations.front().payload_json ==
               "{\"classification\":\"Weak bullish\"}");
        assert(database.recent_observations(10, 654321, "Broker-Demo")
                   .front().actual_direction.empty());
    }
    remove_database_files(path);
}

}  // namespace

int main() {
    test_database_persists_signal_history_and_state();
    std::cout << "All C++ database tests passed\n";
}
