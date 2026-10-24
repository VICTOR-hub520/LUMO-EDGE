from __future__ import annotations

import os
from dataclasses import dataclass
from pathlib import Path

try:
    from dotenv import load_dotenv
except ImportError:  # pragma: no cover - optional dependency in some environments
    def load_dotenv(*args, **kwargs):
        return False


def _coerce_bool(value: str | None, default: bool = False) -> bool:
    if value is None:
        return default
    return value.strip().lower() in {"1", "true", "yes", "on"}


@dataclass
class Config:
    bot_name: str = "LUMO EDGE"
    owner: str = "GONTSE"
    symbol: str = "XAUUSD"
    symbol_aliases: tuple[str, ...] = ("XAUUSD", "XAUUSDm", "GOLD")
    timeframe: str = "M5"
    platform: str = "MetaTrader 5"
    max_daily_trades: int = 5
    risk_per_trade: float = 0.01
    max_risk_per_trade: float = 0.03
    daily_loss_limit: float = 0.03
    requested_lot_size: float = 0.10
    min_lot: float = 0.01
    max_lot: float = 20.0
    lot_step: float = 0.01
    stop_loss_atr_multiplier: float = 1.8
    min_stop_distance_points: float = 25.0
    max_stop_distance_points: float = 500.0
    spread_limit_points: float = 40.0
    ema_fast: int = 50
    ema_slow: int = 200
    rsi_period: int = 14
    atr_period: int = 14
    default_mode: str = "demo"
    live_trading_enabled: bool = False
    model_enabled: bool = True
    scanner_enabled: bool = True
    emergency_stop: bool = False
    trading_lock: bool = False
    timezone: str = "UTC"
    data_dir: str = "data"
    logs_dir: str = "logs"
    db_path: str = "data/lumo_edge.db"

    @classmethod
    def from_env(cls) -> "Config":
        project_dir = Path(__file__).resolve().parent
        env_path = project_dir / ".env"
        if env_path.exists():
            load_dotenv(dotenv_path=env_path, override=False)

        cfg = cls(
            bot_name=os.getenv("BOT_NAME", "LUMO EDGE"),
            owner=os.getenv("OWNER", "GONTSE"),
            symbol=os.getenv("SYMBOL", "XAUUSD"),
            symbol_aliases=tuple(
                symbol.strip()
                for symbol in os.getenv("SYMBOL_ALIASES", "XAUUSD,XAUUSDm,GOLD").split(",")
                if symbol.strip()
            ),
            timeframe=os.getenv("TIMEFRAME", "M5"),
            max_daily_trades=int(os.getenv("MAX_DAILY_TRADES", "5")),
            risk_per_trade=float(os.getenv("DEFAULT_RISK_PER_TRADE", "0.01")),
            max_risk_per_trade=float(os.getenv("MAX_RISK_PER_TRADE", "0.03")),
            daily_loss_limit=float(os.getenv("DAILY_LOSS_LIMIT", "0.03")),
            requested_lot_size=float(os.getenv("REQUESTED_LOT_SIZE", "0.10")),
            default_mode=os.getenv("DEFAULT_MODE", "demo").lower(),
            live_trading_enabled=_coerce_bool(os.getenv("LIVE_TRADING_ENABLED"), False),
            emergency_stop=_coerce_bool(os.getenv("EMERGENCY_STOP"), False),
            trading_lock=_coerce_bool(os.getenv("TRADING_LOCK"), False),
        )
        return cfg

    def validate(self) -> None:
        if self.max_daily_trades <= 0:
            raise ValueError("max_daily_trades must be positive")
        if not 0 < self.risk_per_trade <= self.max_risk_per_trade:
            raise ValueError("risk_per_trade must be greater than 0 and not exceed max_risk_per_trade")
        if self.requested_lot_size <= 0:
            raise ValueError("requested_lot_size must be positive")
        if self.default_mode not in {"demo", "live"}:
            raise ValueError("default_mode must be 'demo' or 'live'")

        Path(self.data_dir).mkdir(exist_ok=True, parents=True)
        Path(self.logs_dir).mkdir(exist_ok=True, parents=True)


DEFAULT_CONFIG = Config()
