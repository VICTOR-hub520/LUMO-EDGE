from __future__ import annotations

from typing import Any

from config import Config


class TradeExecutor:
    def __init__(self, config: Config) -> None:
        self.config = config
        self.connected = False

    def connect(self) -> bool:
        try:
            import MetaTrader5 as mt5
            self.connected = bool(mt5.initialize())
            return self.connected
        except Exception:
            self.connected = False
            return False

    def place_order(self, symbol: str, direction: str, lot_size: float, stop_loss: float | None = None, take_profit: float | None = None) -> dict[str, Any]:
        if not self.connected:
            return {"success": False, "reason": "MT5 is not connected"}
        try:
            import MetaTrader5 as mt5
            request = {
                "action": mt5.TRADE_ACTION_DEAL,
                "symbol": symbol,
                "volume": lot_size,
                "type": mt5.ORDER_TYPE_BUY if direction == "BUY" else mt5.ORDER_TYPE_SELL,
                "sl": stop_loss,
                "tp": take_profit,
                "deviation": 10,
                "magic": 202501,
            }
            result = mt5.order_send(request)
            return {"success": bool(result and result.retcode == mt5.TRADE_RETCODE_DONE), "result": result}
        except Exception as exc:
            return {"success": False, "reason": str(exc)}

    def close_position(self, ticket: int) -> dict[str, Any]:
        if not self.connected:
            return {"success": False, "reason": "MT5 is not connected"}
        try:
            import MetaTrader5 as mt5
            result = mt5.Close(ticket)
            return {"success": bool(result), "result": result}
        except Exception as exc:
            return {"success": False, "reason": str(exc)}
