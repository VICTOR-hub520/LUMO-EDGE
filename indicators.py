from __future__ import annotations

import numpy as np
import pandas as pd


def ema(series: pd.Series, span: int) -> pd.Series:
    return series.ewm(span=span, adjust=False).mean()


def rsi(series: pd.Series, period: int = 14) -> pd.Series:
    delta = series.diff()
    gain = delta.clip(lower=0)
    loss = -delta.clip(upper=0)
    avg_gain = gain.ewm(alpha=1 / period, adjust=False).mean()
    avg_loss = loss.ewm(alpha=1 / period, adjust=False).mean()
    rs = avg_gain / avg_loss.replace(0, np.nan)
    rsi_values = 100 - (100 / (1 + rs))
    return rsi_values.fillna(50)


def atr(df: pd.DataFrame, period: int = 14) -> pd.Series:
    high_low = df["high"] - df["low"]
    high_close = (df["high"] - df["close"].shift(1)).abs()
    low_close = (df["low"] - df["close"].shift(1)).abs()
    tr = pd.concat([high_low, high_close, low_close], axis=1).max(axis=1)
    return tr.ewm(alpha=1 / period, adjust=False).mean()


def market_features(df: pd.DataFrame) -> pd.DataFrame:
    data = df.copy()
    data["ema_fast"] = ema(data["close"], 50)
    data["ema_slow"] = ema(data["close"], 200)
    data["rsi"] = rsi(data["close"], 14)
    data["atr"] = atr(data, 14)
    data["ret_1"] = data["close"].pct_change(1)
    data["ret_5"] = data["close"].pct_change(5)
    body = (data["close"] - data["open"]).abs()
    data["body_size"] = body
    data["wick_size"] = (data["high"] - data["low"]).abs()
    data["volatility"] = data["high"] - data["low"]
    return data
