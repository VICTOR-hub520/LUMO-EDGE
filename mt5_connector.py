from __future__ import annotations

import os
from typing import Any


class MT5ConnectionError(RuntimeError):
    """Raised when MT5 market or account data cannot be verified."""


class MT5Connector:
    def __init__(self) -> None:
        self.mt5: Any = None
        self.connected = False
        self.account_info: dict[str, Any] | None = None
        self.last_error: str | None = None

    def connect(self) -> bool:
        try:
            import MetaTrader5 as mt5
        except ImportError:
            self.connected = False
            self.account_info = None
            self.last_error = (
                "MetaTrader5 package is unavailable. Install it on Windows with "
                "'python -m pip install MetaTrader5'."
            )
            return False

        self.mt5 = mt5
        path = os.getenv("MT5_PATH", "").strip()
        login_value = os.getenv("MT5_LOGIN", "").strip()
        password = os.getenv("MT5_PASSWORD", "").strip()
        server = os.getenv("MT5_SERVER", "").strip()

        options: dict[str, Any] = {}
        if path:
            options["path"] = path
        if login_value:
            try:
                options["login"] = int(login_value)
            except ValueError:
                self.last_error = "MT5_LOGIN must be a numeric account login."
                return False
            if not password or not server:
                self.last_error = (
                    "MT5_PASSWORD and MT5_SERVER are required when MT5_LOGIN is set."
                )
                return False
            options["password"] = password
            options["server"] = server

        if not mt5.initialize(**options):
            self.connected = False
            self.account_info = None
            self.last_error = f"MT5 initialize failed: {mt5.last_error()}"
            return False

        info = mt5.account_info()
        if info is None:
            error = mt5.last_error()
            mt5.shutdown()
            self.connected = False
            self.account_info = None
            self.last_error = f"MT5 connected but account information is unavailable: {error}"
            return False

        self.connected = True
        self.account_info = info._asdict()
        self.last_error = None
        return True

    def disconnect(self) -> None:
        if self.mt5 is not None and self.connected:
            self.mt5.shutdown()
        self.connected = False
        self.account_info = None

    def _require_connection(self) -> Any:
        if self.mt5 is None or not self.connected:
            raise MT5ConnectionError(self.last_error or "MetaTrader 5 is not connected.")
        return self.mt5

    def get_account_info(self) -> dict[str, Any]:
        mt5 = self._require_connection()
        info = mt5.account_info()
        if info is None:
            self.connected = False
            self.account_info = None
            self.last_error = f"Could not read MT5 account information: {mt5.last_error()}"
            raise MT5ConnectionError(self.last_error)
        self.account_info = info._asdict()
        return self.account_info

    def resolve_symbol(
        self, symbol: str, aliases: tuple[str, ...] | None = None
    ) -> str:
        mt5 = self._require_connection()
        symbols = mt5.symbols_get()
        if symbols is None:
            self.last_error = f"Could not list broker symbols: {mt5.last_error()}"
            raise MT5ConnectionError(self.last_error)

        available = {item.name.upper(): item.name for item in symbols}
        candidates = tuple(dict.fromkeys((symbol, *(aliases or ()))))
        selected = next(
            (available[name.upper()] for name in candidates if name.upper() in available),
            None,
        )

        if selected is None:
            base_names = tuple(name.upper() for name in candidates)
            selected = next(
                (
                    name
                    for upper_name, name in available.items()
                    if any(upper_name.startswith(base) for base in base_names)
                ),
                None,
            )

        if selected is None:
            self.last_error = (
                f"Gold symbol '{symbol}' was not found. Configure SYMBOL to match "
                "a symbol available in the broker's MT5 Market Watch."
            )
            raise MT5ConnectionError(self.last_error)

        if not mt5.symbol_select(selected, True):
            self.last_error = (
                f"Could not select broker symbol '{selected}': {mt5.last_error()}"
            )
            raise MT5ConnectionError(self.last_error)
        return selected

    def get_symbol_info(self, symbol: str) -> Any:
        mt5 = self._require_connection()
        info = mt5.symbol_info(symbol)
        if info is None:
            self.last_error = f"Symbol '{symbol}' is unavailable: {mt5.last_error()}"
            raise MT5ConnectionError(self.last_error)
        return info

    def get_tick(self, symbol: str) -> dict[str, Any]:
        mt5 = self._require_connection()
        tick = mt5.symbol_info_tick(symbol)
        if tick is None:
            self.last_error = f"Could not read '{symbol}' quote: {mt5.last_error()}"
            raise MT5ConnectionError(self.last_error)
        return tick._asdict()

    def get_candles(
        self, symbol: str, timeframe: str = "M5", count: int = 200
    ) -> list[dict[str, Any]]:
        mt5 = self._require_connection()
        if count <= 0:
            raise ValueError("count must be a positive integer")

        timeframe_map = {
            "M1": mt5.TIMEFRAME_M1,
            "M2": mt5.TIMEFRAME_M2,
            "M3": mt5.TIMEFRAME_M3,
            "M4": mt5.TIMEFRAME_M4,
            "M5": mt5.TIMEFRAME_M5,
            "M6": mt5.TIMEFRAME_M6,
            "M10": mt5.TIMEFRAME_M10,
            "M12": mt5.TIMEFRAME_M12,
            "M15": mt5.TIMEFRAME_M15,
            "M20": mt5.TIMEFRAME_M20,
            "M30": mt5.TIMEFRAME_M30,
            "H1": mt5.TIMEFRAME_H1,
            "H2": mt5.TIMEFRAME_H2,
            "H3": mt5.TIMEFRAME_H3,
            "H4": mt5.TIMEFRAME_H4,
            "H6": mt5.TIMEFRAME_H6,
            "H8": mt5.TIMEFRAME_H8,
            "H12": mt5.TIMEFRAME_H12,
            "D1": mt5.TIMEFRAME_D1,
            "W1": mt5.TIMEFRAME_W1,
            "MN1": mt5.TIMEFRAME_MN1,
        }
        normalized_timeframe = timeframe.upper()
        if normalized_timeframe not in timeframe_map:
            raise ValueError(f"Unsupported MT5 timeframe: {timeframe}")

        candles = mt5.copy_rates_from_pos(
            symbol, timeframe_map[normalized_timeframe], 1, count
        )
        if candles is None:
            self.last_error = f"Could not retrieve candles: {mt5.last_error()}"
            raise MT5ConnectionError(self.last_error)
        if len(candles) == 0:
            self.last_error = (
                f"No historical candles returned for {symbol} {normalized_timeframe}."
            )
            raise MT5ConnectionError(self.last_error)
        field_names = getattr(getattr(candles, "dtype", None), "names", None)
        if field_names:
            return [
                {
                    name: value.item() if hasattr(value, "item") else value
                    for name in field_names
                    for value in (candle[name],)
                }
                for candle in candles
            ]
        return [dict(candle) for candle in candles]
