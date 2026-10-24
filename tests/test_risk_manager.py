from config import Config
from risk_manager import RiskManager


def test_position_size_is_valid_and_safe():
    config = Config()
    risk_manager = RiskManager(config)
    size = risk_manager.calculate_position_size(account_equity=10000, entry_price=2300, stop_loss=2290)
    assert 0 < size <= config.requested_lot_size


def test_daily_trade_limit_flags_when_reached():
    config = Config()
    risk_manager = RiskManager(config)
    risk_manager.daily_trade_count = config.max_daily_trades
    assert risk_manager.enforce_daily_trade_limit() is False


def test_trade_validation_rejects_invalid_volume():
    config = Config()
    risk_manager = RiskManager(config)
    assert risk_manager.validate_trade(0, 10, 1000) is False
    assert risk_manager.validate_trade(0.1, 10, 1000) is True
