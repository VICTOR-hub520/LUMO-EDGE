from __future__ import annotations

from typing import Any

import pandas as pd

from config import Config
from mt5_connector import MT5ConnectionError, MT5Connector


class DataCollector:
    def __init__(self, config: Config, connector: MT5Connector | None = None) -> None:
        self.config = config
        self.connector = connector or MT5Connector()

    def fetch_history(self, symbol: str | None = None, count: int = 500) -> pd.DataFrame:
        symbol = symbol or self.config.symbol
        candles = self.connector.get_candles(symbol, self.config.timeframe, count)
        if not candles:
            raise MT5ConnectionError(
                f"No verified market data received for {symbol} {self.config.timeframe}."
            )

        df = pd.DataFrame(candles)
        needed = ["time", "open", "high", "low", "close", "tick_volume"]
        for col in needed:
            if col not in df.columns:
                raise MT5ConnectionError(f"MT5 candle data is missing required column '{col}'.")
        df["time"] = pd.to_datetime(df["time"], unit="s", errors="coerce")
        df = df.dropna(subset=["time", "open", "high", "low", "close"])
        df = df.sort_values("time").reset_index(drop=True)
        if df.empty:
            raise MT5ConnectionError(f"MT5 returned invalid candles for {symbol}.")
        return df

    def get_market_snapshot(self, symbol: str | None = None) -> dict[str, Any]:
        symbol = symbol or self.config.symbol
        info = self.connector.get_symbol_info(symbol)
        tick = self.connector.get_tick(symbol)
        point = float(info.point)
        if point <= 0:
            raise MT5ConnectionError(f"Broker returned an invalid price point for {symbol}.")
        bid = float(tick["bid"])
        ask = float(tick["ask"])
        if bid <= 0 or ask < bid:
            raise MT5ConnectionError(f"Broker returned an invalid quote for {symbol}.")
        return {
            "symbol": symbol,
            "bid": bid,
            "ask": ask,
            "spread_points": (ask - bid) / point,
            "digits": int(info.digits),
            "point": point,
            "trade_allowed": bool(info.trade_mode),
        }
