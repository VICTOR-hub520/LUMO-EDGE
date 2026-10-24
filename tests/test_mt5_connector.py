from collections import namedtuple
from types import SimpleNamespace

import pytest
import numpy as np

from data_collector import DataCollector
from mt5_connector import MT5ConnectionError, MT5Connector
from config import Config


def make_mt5_mock():
    account_info = namedtuple(
        "AccountInfo", "login server currency balance equity trade_mode"
    )(12345, "Demo-Server", "USD", 1000.0, 1000.0, 0)
    constants = {
        "TIMEFRAME_M1": 1,
        "TIMEFRAME_M2": 2,
        "TIMEFRAME_M3": 3,
        "TIMEFRAME_M4": 4,
        "TIMEFRAME_M5": 5,
        "TIMEFRAME_M6": 6,
        "TIMEFRAME_M10": 10,
        "TIMEFRAME_M12": 12,
        "TIMEFRAME_M15": 15,
        "TIMEFRAME_M20": 20,
        "TIMEFRAME_M30": 30,
        "TIMEFRAME_H1": 60,
        "TIMEFRAME_H2": 120,
        "TIMEFRAME_H3": 180,
        "TIMEFRAME_H4": 240,
        "TIMEFRAME_H6": 360,
        "TIMEFRAME_H8": 480,
        "TIMEFRAME_H12": 720,
        "TIMEFRAME_D1": 1440,
        "TIMEFRAME_W1": 10080,
        "TIMEFRAME_MN1": 43200,
    }
    requests = []

    def copy_rates_from_pos(symbol, timeframe, shift, count):
        requests.append(("candles", (symbol, timeframe, shift, count)))
        return np.array(
            [(1, 2.0, 3.0, 1.0, 2.0, 4, 22, 0)],
            dtype=[
                ("time", "i8"),
                ("open", "f8"),
                ("high", "f8"),
                ("low", "f8"),
                ("close", "f8"),
                ("tick_volume", "u8"),
                ("spread", "i4"),
                ("real_volume", "u8"),
            ],
        )

    mock = SimpleNamespace(
        **constants,
        initialize=lambda **kwargs: requests.append(("initialize", kwargs)) or True,
        shutdown=lambda: requests.append(("shutdown", {})),
        account_info=lambda: account_info,
        symbols_get=lambda: [SimpleNamespace(name="XAUUSD.a")],
        symbol_select=lambda symbol, enabled: symbol == "XAUUSD.a" and enabled,
        copy_rates_from_pos=copy_rates_from_pos,
        last_error=lambda: (0, "OK"),
    )
    return mock, requests


def test_connect_resolve_symbol_and_get_closed_candles(monkeypatch):
    mock, requests = make_mt5_mock()
    monkeypatch.setitem(__import__("sys").modules, "MetaTrader5", mock)
    monkeypatch.delenv("MT5_PATH", raising=False)
    monkeypatch.delenv("MT5_LOGIN", raising=False)
    monkeypatch.delenv("MT5_PASSWORD", raising=False)
    monkeypatch.delenv("MT5_SERVER", raising=False)

    connector = MT5Connector()
    assert connector.connect()
    assert connector.get_account_info()["trade_mode"] == 0
    assert connector.resolve_symbol("XAUUSD", ("XAUUSDm",)) == "XAUUSD.a"
    assert connector.get_candles("XAUUSD.a", "M5", 10)[0]["close"] == 2
    assert ("candles", ("XAUUSD.a", 5, 1, 10)) in requests
    connector.disconnect()
    assert requests[-1][0] == "shutdown"


def test_data_collector_does_not_fabricate_missing_market_data():
    class EmptyConnector:
        def get_candles(self, symbol, timeframe, count):
            return []

    with pytest.raises(MT5ConnectionError, match="No verified market data"):
        DataCollector(Config(), EmptyConnector()).fetch_history(count=5)


def test_invalid_timeframe_is_rejected(monkeypatch):
    mock, _ = make_mt5_mock()
    monkeypatch.setitem(__import__("sys").modules, "MetaTrader5", mock)
    connector = MT5Connector()
    assert connector.connect()

    with pytest.raises(ValueError, match="Unsupported MT5 timeframe"):
        connector.get_candles("XAUUSD.a", "INVALID")
