from __future__ import annotations

from typing import Any

import pandas as pd

from indicators import ema, rsi, atr


class MarketModel:
    def __init__(self) -> None:
        self.model: Any = None
        self.trained = False

    def prepare_features(self, df: pd.DataFrame) -> pd.DataFrame:
        data = df.copy()
        data["ema_fast"] = ema(data["close"], 50)
        data["ema_slow"] = ema(data["close"], 200)
        data["rsi"] = rsi(data["close"], 14)
        data["atr"] = atr(data, 14)
        data["ret"] = data["close"].pct_change().fillna(0)
        data["target"] = data["close"].shift(-5).pct_change(5).fillna(0)
        data["target_bin"] = (data["target"] > 0).astype(int)
        return data.dropna(subset=["ema_fast", "ema_slow", "rsi", "atr", "ret", "target_bin"])

    def fit(self, df: pd.DataFrame) -> None:
        try:
            from sklearn.ensemble import RandomForestClassifier

            features = self.prepare_features(df)
            X = features[["ema_fast", "ema_slow", "rsi", "atr", "ret"]]
            y = features["target_bin"]
            self.model = RandomForestClassifier(n_estimators=100, random_state=42)
            self.model.fit(X, y)
            self.trained = True
        except ImportError:
            self.model = None
            self.trained = False

    def predict(self, df: pd.DataFrame) -> tuple[str, float]:
        if df.empty:
            return "NO_TRADE", 0.0
        if self.model is None:
            recent = df.iloc[-1]
            ema_fast = ema(df["close"], 50).iloc[-1]
            ema_slow = ema(df["close"], 200).iloc[-1]
            if ema_fast > ema_slow:
                return "BUY", 0.62
            if ema_fast < ema_slow:
                return "SELL", 0.62
            return "NO_TRADE", 0.0

        features = self.prepare_features(df).tail(1)
        if features.empty:
            return "NO_TRADE", 0.0
        X = features[["ema_fast", "ema_slow", "rsi", "atr", "ret"]]
        probs = self.model.predict_proba(X)[0]
        prob_up = float(probs[1]) if len(probs) > 1 else 0.5
        if prob_up >= 0.55:
            return "BUY", prob_up
        if prob_up <= 0.45:
            return "SELL", 1.0 - prob_up
        return "NO_TRADE", 0.0
