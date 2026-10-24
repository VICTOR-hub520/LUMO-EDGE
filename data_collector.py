from __future__ import annotations

from typing import Any

import pandas as pd

from config import Config
from mt5_connector import MT5Connector


class DataCollector:
    def __init__(self, config: Config, connector: MT5Connector | None = None) -> None:
        self.config = config
        self.connector = connector or MT5Connector()

    def fetch_history(self, symbol: str | None = None, count: int = 500) -> pd.DataFrame:
        symbol = symbol or self.config.symbol
        candles = self.connector.get_candles(symbol, self.config.timeframe, count)
        if not candles:
            return self._build_sample_data(symbol, count)

        df = pd.DataFrame(candles)
        needed = ["time", "open", "high", "low", "close", "tick_volume"]
        for col in needed:
            if col not in df.columns:
                return self._build_sample_data(symbol, count)
        df["time"] = pd.to_datetime(df["time"], unit="s", errors="coerce")
        df = df.sort_values("time").reset_index(drop=True)
        return df

    def _build_sample_data(self, symbol: str, count: int) -> pd.DataFrame:
        rng = pd.date_range(end=pd.Timestamp.utcnow(), periods=count, freq="5min")
        price = 2330.0
        values = []
        for idx in range(count):
            drift = 0.3 * (idx % 15)
            open_price = price + (idx % 7) * 0.2
            close_price = open_price + (idx % 5 - 2) * 0.75 + drift
            high = max(open_price, close_price) + 1.2
            low = min(open_price, close_price) - 1.2
            values.append(
                {
                    "time": rng[idx],
                    "open": round(open_price, 2),
                    "high": round(high, 2),
                    "low": round(low, 2),
                    "close": round(close_price, 2),
                    "tick_volume": 1000,
                }
            )
            price = close_price
        return pd.DataFrame(values)

    def get_market_snapshot(self, symbol: str | None = None) -> dict[str, Any]:
        symbol = symbol or self.config.symbol
        info = self.connector.get_symbol_info(symbol)
        return {
            "symbol": symbol,
            "symbol_info": info,
            "available": info is not None,
        }
