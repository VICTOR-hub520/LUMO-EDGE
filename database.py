from __future__ import annotations

import json
import sqlite3
from pathlib import Path
from typing import Any


class Database:
    def __init__(self, db_path: str = "data/lumo_edge.db") -> None:
        self.db_path = Path(db_path)
        self.db_path.parent.mkdir(exist_ok=True, parents=True)
        self.conn = sqlite3.connect(self.db_path)
        self.conn.row_factory = sqlite3.Row
        self._init_db()

    def _init_db(self) -> None:
        sql = """
        CREATE TABLE IF NOT EXISTS signals (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            symbol TEXT,
            direction TEXT,
            confidence REAL,
            reason TEXT,
            created_at TEXT DEFAULT CURRENT_TIMESTAMP
        );

        CREATE TABLE IF NOT EXISTS trades (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            symbol TEXT,
            direction TEXT,
            entry_price REAL,
            stop_loss REAL,
            take_profit REAL,
            volume REAL,
            status TEXT,
            opened_at TEXT DEFAULT CURRENT_TIMESTAMP,
            closed_at TEXT,
            pnl REAL DEFAULT 0.0,
            metadata TEXT
        );

        CREATE TABLE IF NOT EXISTS risk_state (
            key TEXT PRIMARY KEY,
            value TEXT
        );
        """
        self.conn.executescript(sql)
        self.conn.commit()

    def save_signal(self, symbol: str, direction: str, confidence: float, reason: str) -> None:
        self.conn.execute(
            "INSERT INTO signals (symbol, direction, confidence, reason) VALUES (?, ?, ?, ?)",
            (symbol, direction, confidence, reason),
        )
        self.conn.commit()

    def save_trade(self, symbol: str, direction: str, entry_price: float, stop_loss: float, take_profit: float, volume: float, status: str, metadata: dict | None = None) -> None:
        self.conn.execute(
            "INSERT INTO trades (symbol, direction, entry_price, stop_loss, take_profit, volume, status, metadata) VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
            (symbol, direction, entry_price, stop_loss, take_profit, volume, status, json.dumps(metadata or {})),
        )
        self.conn.commit()

    def update_trade_status(self, trade_id: int, status: str, pnl: float | None = None, closed_at: str | None = None) -> None:
        if pnl is None and closed_at is None:
            self.conn.execute("UPDATE trades SET status = ? WHERE id = ?", (status, trade_id))
        else:
            self.conn.execute(
                "UPDATE trades SET status = ?, pnl = ?, closed_at = ? WHERE id = ?",
                (status, pnl, closed_at, trade_id),
            )
        self.conn.commit()

    def set_state(self, key: str, value: Any) -> None:
        self.conn.execute(
            "INSERT INTO risk_state (key, value) VALUES (?, ?) ON CONFLICT(key) DO UPDATE SET value = excluded.value",
            (key, str(value)),
        )
        self.conn.commit()

    def get_state(self, key: str, default: Any = None) -> Any:
        row = self.conn.execute("SELECT value FROM risk_state WHERE key = ?", (key,)).fetchone()
        if row is None:
            return default
        return row["value"]

    def close(self) -> None:
        self.conn.close()
