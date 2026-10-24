from __future__ import annotations

import pandas as pd

from config import Config
from indicators import atr, ema, rsi


class Strategy:
    def __init__(self, config: Config) -> None:
        self.config = config

    def evaluate(self, df: pd.DataFrame) -> dict:
        if df.empty or len(df) < 250:
            return {"direction": "NO_TRADE", "confidence": 0.0, "reason": "Insufficient market data"}

        recent = df.iloc[-1]
        prev = df.iloc[-2]

        fast = ema(df["close"], self.config.ema_fast).iloc[-1]
        slow = ema(df["close"], self.config.ema_slow).iloc[-1]
        prev_fast = ema(df["close"], self.config.ema_fast).iloc[-2]
        prev_slow = ema(df["close"], self.config.ema_slow).iloc[-2]
        rsi_value = rsi(df["close"], self.config.rsi_period).iloc[-1]
        atr_value = atr(df, self.config.atr_period).iloc[-1]

        bullish = (prev_fast < prev_slow and fast > slow) or (recent["close"] > recent["open"] and rsi_value > 52)
        bearish = (prev_fast > prev_slow and fast < slow) or (recent["close"] < recent["open"] and rsi_value < 48)

        if bullish and rsi_value > 50 and atr_value > 0:
            return {
                "direction": "BUY",
                "confidence": min(0.95, 0.5 + (fast - slow) / max(abs(slow), 1.0) * 100),
                "reason": "EMA 50 crossed above EMA 200 with bullish momentum and RSI confirmation",
            }

        if bearish and rsi_value < 50 and atr_value > 0:
            return {
                "direction": "SELL",
                "confidence": min(0.95, 0.5 + (slow - fast) / max(abs(slow), 1.0) * 100),
                "reason": "EMA 50 crossed below EMA 200 with bearish momentum and RSI confirmation",
            }

        return {"direction": "NO_TRADE", "confidence": 0.0, "reason": "Market conditions do not satisfy the configured entry criteria"}
