from __future__ import annotations

from config import Config
from data_collector import DataCollector
from logger_config import configure_logging
from mt5_connector import MT5ConnectionError, MT5Connector
from strategy import Strategy


def main() -> None:
    config = Config.from_env()
    config.validate()
    logger = configure_logging(log_dir="logs", log_file="lumo_edge.log")
    logger.info("Starting LUMO EDGE")

    connector = MT5Connector()
    if not connector.connect():
        logger.error("MT5 connection failed: %s", connector.last_error)
        print(f"MT5 connection failed: {connector.last_error}")
        return

    try:
        symbol = connector.resolve_symbol(config.symbol, config.symbol_aliases)
        account = connector.get_account_info()
        collector = DataCollector(config, connector)
        market = collector.get_market_snapshot(symbol)
        candles = collector.fetch_history(symbol)

        signal = Strategy(config).evaluate(candles)
        logger.info(
            "Connected to MT5 (%s, %s); received %d closed candles for %s",
            account.get("server", "unknown server"),
            account.get("currency", "unknown currency"),
            len(candles),
            symbol,
        )
        logger.info(
            "Market bid=%s ask=%s spread=%s points",
            market["bid"],
            market["ask"],
            market["spread_points"],
        )
        logger.info("Latest analysis: %s - %s", signal["direction"], signal["reason"])
        print(
            f"MT5 connected | {symbol} {config.timeframe} | "
            f"bid={market['bid']} ask={market['ask']} "
            f"spread={market['spread_points']:.1f} points"
        )
        print(f"Analysis only: {signal['direction']} | {signal['reason']}")
        print("No orders were submitted.")
    except MT5ConnectionError as exc:
        logger.error("MT5 market data unavailable: %s", exc)
        print(f"MT5 market data unavailable: {exc}")
    finally:
        connector.disconnect()


if __name__ == "__main__":
    main()
