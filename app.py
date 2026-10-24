from __future__ import annotations

from config import Config
from data_collector import DataCollector
from logger_config import configure_logging
from mt5_connector import MT5Connector
from strategy import Strategy


def main() -> None:
    config = Config.from_env()
    config.validate()
    logger = configure_logging(log_dir="logs", log_file="lumo_edge.log")
    logger.info("Starting LUMO EDGE")

    connector = MT5Connector()
    connector.connect()
    collector = DataCollector(config, connector)
    df = collector.fetch_history()

    strategy = Strategy(config)
    signal = strategy.evaluate(df)
    logger.info("Latest signal: %s - %s", signal["direction"], signal["reason"])
    print(f"Latest signal: {signal['direction']} | confidence={signal['confidence']:.2f} | {signal['reason']}")


if __name__ == "__main__":
    main()
