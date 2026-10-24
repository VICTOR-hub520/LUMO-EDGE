from __future__ import annotations

from dataclasses import dataclass

import pandas as pd

from config import Config
from strategy import Strategy


@dataclass
class BacktestResult:
    total_trades: int = 0
    wins: int = 0
    losses: int = 0
    net_profit: float = 0.0
    max_drawdown: float = 0.0
    win_rate: float = 0.0
    profit_factor: float = 0.0


class Backtester:
    def __init__(self, config: Config) -> None:
        self.config = config
        self.strategy = Strategy(config)

    def run(self, df: pd.DataFrame) -> BacktestResult:
        trade_count = 0
        wins = 0
        losses = 0
        net_profit = 0.0
        equity_curve: list[float] = []

        for idx in range(250, len(df)):
            slice_df = df.iloc[: idx + 1]
            signal = self.strategy.evaluate(slice_df)
            if signal["direction"] == "NO_TRADE":
                continue
            trade_count += 1
            entry = float(slice_df["close"].iloc[-1])
            stop = entry * 0.995 if signal["direction"] == "BUY" else entry * 1.005
            exit_price = entry * 1.01 if signal["direction"] == "BUY" else entry * 0.99
            pnl = (exit_price - entry) * 100 if signal["direction"] == "BUY" else (entry - exit_price) * 100
            net_profit += pnl
            if pnl >= 0:
                wins += 1
            else:
                losses += 1
            equity_curve.append(net_profit)

        max_drawdown = 0.0
        running_min = 0.0
        for value in equity_curve:
            running_min = min(running_min, value)
            max_drawdown = max(max_drawdown, abs(running_min - value))

        win_rate = (wins / trade_count) if trade_count else 0.0
        profit_factor = (sum(max(0.0, x) for x in [0]) or 1) / max(1.0, abs(sum(min(0.0, x) for x in [0])))
        return BacktestResult(
            total_trades=trade_count,
            wins=wins,
            losses=losses,
            net_profit=round(net_profit, 2),
            max_drawdown=round(max_drawdown, 2),
            win_rate=round(win_rate, 3),
            profit_factor=round(profit_factor, 3),
        )
