from __future__ import annotations

import os
from typing import Any


class MT5Connector:
    def __init__(self) -> None:
        self.mt5 = None
        self.connected = False
        self.account_info: dict[str, Any] | None = None

    def connect(self) -> bool:
        try:
            import MetaTrader5 as mt5
        except ImportError:
            self.connected = False
            self.account_info = None
            return False

        self.mt5 = mt5
        login_value = os.getenv("MT5_LOGIN", "0").strip()
        password_value = os.getenv("MT5_PASSWORD", "").strip()
        server_value = os.getenv("MT5_SERVER", "").strip()
        mt5_path = os.getenv("MT5_PATH", "").strip()

        try:
            login_id = int(login_value) if login_value else 0
        except ValueError:
            login_id = 0

        if not mt5.initialize(path=mt5_path, login=login_id, password=password_value, server=server_value):
            self.connected = False
            self.account_info = None
            return False

        self.connected = True
        info = mt5.account_info()
        self.account_info = info._asdict() if info else {}
        return True

    def disconnect(self) -> None:
        if self.mt5 is not None:
            try:
                self.mt5.shutdown()
            except Exception:
                pass
        self.connected = False
        self.account_info = None

    def get_symbol_info(self, symbol: str) -> Any:
        if self.mt5 is None or not self.connected:
            return None
        try:
            return self.mt5.symbol_info(symbol)
        except Exception:
            return None

    def resolve_symbol(self, symbol: str, aliases: tuple[str, ...] | None = None) -> str:
        candidates = tuple(aliases or ("XAUUSD", "XAUUSDm", "GOLD"))
        if symbol in candidates:
            return symbol
        return symbol

    def get_candles(self, symbol: str, timeframe: str = "M5", count: int = 200) -> list[dict[str, Any]]:
        if self.mt5 is None or not self.connected:
            return []

        try:
            tf_map = {
                "M1": self.mt5.TIMEFRAME_M1,
                "M5": self.mt5.TIMEFRAME_M5,
                "M15": self.mt5.TIMEFRAME_M15,
                "H1": self.mt5.TIMEFRAME_H1,
            }
            mt5_timeframe = tf_map.get(timeframe.upper(), self.mt5.TIMEFRAME_M5)
            candles = self.mt5.copy_rates_from_pos(symbol, mt5_timeframe, 0, count)
            if candles is None:
                return []
            return [dict(r) for r in candles]
        except Exception:
            return []
