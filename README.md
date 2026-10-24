# LUMO-EDGE

LUMO EDGE is an AI-assisted gold trading bot for MetaTrader 5. It is designed for demo testing first, with safety controls and a modular architecture that keeps the trading logic, risk policy, AI filter, and execution layer separate.

## Features

- MT5 connection abstraction
- Market data collection for XAUUSD on M5
- EMA 50 / EMA 200 trend analysis
- RSI and ATR filtering
- AI confidence filtering
- Risk manager with per-trade and daily loss controls
- SQLite storage for signals and trade events
- Streamlit dashboard
- Backtesting scaffold

## Quick start

1. Create a Python virtual environment.
2. Install dependencies: `pip install -r requirements.txt`
3. Copy `.env.example` to `.env` and set MT5 credentials if needed.
4. Run: `python app.py`
5. Launch the dashboard: `streamlit run dashboard.py`

## Safety notes

- Live trading remains disabled by default.
- The bot never bypasses the risk manager.
- All orders must be validated before execution.
- Use demo mode before any live trading.
