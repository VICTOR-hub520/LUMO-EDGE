import pandas as pd

from config import Config
from strategy import Strategy


def build_df():
    rng = pd.date_range(end='2024-01-01', periods=260, freq='5min')
    data = []
    price = 2300.0
    for i in range(260):
        open_price = price
        close_price = price + 1.5 if i % 8 < 4 else price - 1.1
        high = max(open_price, close_price) + 1.5
        low = min(open_price, close_price) - 1.5
        data.append({
            'time': rng[i],
            'open': open_price,
            'high': high,
            'low': low,
            'close': close_price,
            'tick_volume': 100,
        })
        price = close_price
    return pd.DataFrame(data)


def test_strategy_produces_allowed_signal():
    config = Config()
    strategy = Strategy(config)
    df = build_df()
    result = strategy.evaluate(df)
    assert result['direction'] in {'BUY', 'SELL', 'NO_TRADE'}
    assert isinstance(result['confidence'], float)
