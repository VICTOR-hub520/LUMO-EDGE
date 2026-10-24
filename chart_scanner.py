from __future__ import annotations

from typing import Any

import pandas as pd

from ai_model import MarketModel
from config import Config
from indicators import market_features


class ChartScanner:
    def __init__(self, config: Config, model: MarketModel | None = None) -> None:
        self.config = config
        self.model = model or MarketModel()

    def scan(self, df: pd.DataFrame) -> dict[str, Any]:
        if df.empty:
            return {"direction": "NO_TRADE", "confidence": 0.0, "reasons": ["No market data available"], "analysis": {}}

        features = market_features(df)
        latest = features.iloc[-1]
        prev = features.iloc[-2] if len(features) > 1 else latest
        reasons: list[str] = []

        ema_fast = latest["ema_fast"]
        ema_slow = latest["ema_slow"]
        rsi_value = latest["rsi"]
        atr_value = latest["atr"]

        direction = "NO_TRADE"
        if ema_fast > ema_slow and rsi_value > 52:
            direction = "BUY"
            reasons.append("EMA 50 is above EMA 200 and RSI confirms bullish momentum")
        elif ema_fast < ema_slow and rsi_value < 48:
            direction = "SELL"
            reasons.append("EMA 50 is below EMA 200 and RSI confirms bearish momentum")
        else:
            reasons.append("Trend is not sufficiently confirmed for a new trade")

        if pd.isna(atr_value):
            atr_value = 0.0
        if pd.isna(rsi_value):
            rsi_value = 50.0

        model_direction, model_confidence = self.model.predict(features)
        if model_direction == "BUY" and direction == "BUY":
            confidence = min(0.99, max(model_confidence, 0.55))
            reasons.append("AI model supports the bullish setup")
        elif model_direction == "SELL" and direction == "SELL":
            confidence = min(0.99, max(model_confidence, 0.55))
            reasons.append("AI model supports the bearish setup")
        else:
            confidence = 0.0
            reasons.append("AI model does not confirm the signal")
            direction = "NO_TRADE"

        return {
            "direction": direction,
            "confidence": round(float(confidence), 2),
            "reasons": reasons,
            "analysis": {
                "ema_fast": float(ema_fast),
                "ema_slow": float(ema_slow),
                "rsi": float(rsi_value),
                "atr": float(atr_value),
                "close": float(latest["close"]),
            },
        }
