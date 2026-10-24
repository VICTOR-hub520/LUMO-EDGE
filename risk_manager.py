from __future__ import annotations

import math
from typing import Any

from config import Config


class RiskManager:
    def __init__(self, config: Config) -> None:
        self.config = config
        self.daily_trade_count = 0
        self.daily_loss = 0.0
        self.start_of_day_equity = 0.0
        self.trade_lock = False

    def calculate_position_size(self, account_equity: float, entry_price: float, stop_loss: float, tick_value: float = 1.0, contract_size: float = 100.0) -> float:
        if account_equity <= 0 or entry_price <= 0 or stop_loss <= 0:
            return 0.0

        stop_distance = abs(entry_price - stop_loss)
        if stop_distance <= 0:
            return 0.0

        risk_amount = account_equity * self.config.risk_per_trade
        if risk_amount <= 0:
            return 0.0

        dollars_per_lot = contract_size * tick_value * max(stop_distance, 1.0)
        if dollars_per_lot <= 0:
            return 0.0

        volume = risk_amount / dollars_per_lot
        volume = min(max(volume, self.config.min_lot), self.config.max_lot)
        volume = round(volume / self.config.lot_step) * self.config.lot_step
        return max(0.0, min(volume, self.config.requested_lot_size))

    def can_open_trade(self, account_equity: float, current_daily_trade_count: int, current_daily_loss: float) -> bool:
        if not self.config.live_trading_enabled:
            return False
        if self.config.emergency_stop or self.config.trading_lock:
            return False
        if current_daily_trade_count >= self.config.max_daily_trades:
            return False
        if self.start_of_day_equity > 0:
            daily_loss_limit_value = self.start_of_day_equity * self.config.daily_loss_limit
            if current_daily_loss <= -daily_loss_limit_value:
                return False
        return account_equity > 0

    def record_daily_state(self, start_equity: float, daily_loss: float, trade_count: int) -> None:
        self.start_of_day_equity = start_equity
        self.daily_loss = daily_loss
        self.daily_trade_count = trade_count

    def enforce_daily_trade_limit(self) -> bool:
        return self.daily_trade_count < self.config.max_daily_trades

    def apply_loss_lock(self, account_equity: float) -> None:
        if self.start_of_day_equity and account_equity <= 0:
            self.trade_lock = True

    def update_daily_loss(self, pnl: float) -> None:
        self.daily_loss += pnl
        if self.start_of_day_equity > 0:
            loss_limit_value = self.start_of_day_equity * self.config.daily_loss_limit
            if self.daily_loss <= -loss_limit_value:
                self.trade_lock = True

    def validate_trade(self, lot_size: float, stop_loss: float, account_equity: float) -> bool:
        if lot_size <= 0:
            return False
        if stop_loss <= 0:
            return False
        if account_equity <= 0:
            return False
        if lot_size > self.config.max_lot:
            return False
        if not self.enforce_daily_trade_limit():
            return False
        return True

    def get_risk_summary(self) -> dict[str, Any]:
        return {
            "risk_per_trade": self.config.risk_per_trade,
            "daily_loss_limit": self.config.daily_loss_limit,
            "daily_trade_limit": self.config.max_daily_trades,
            "trade_lock": self.trade_lock,
            "daily_trade_count": self.daily_trade_count,
            "daily_loss": self.daily_loss,
        }
