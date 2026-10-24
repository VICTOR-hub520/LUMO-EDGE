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

1. On Windows, install Python 3.11 or newer and create a virtual environment.
2. Install dependencies: `pip install -r requirements.txt`
3. Install and open the MetaTrader 5 desktop terminal, then sign in to a **demo account**.
4. Copy `.env.example` to `.env`; leave credentials blank to use the terminal's signed-in account.
   If using explicit login settings, set `MT5_LOGIN`, `MT5_PASSWORD`, and `MT5_SERVER` together.
5. Set `SYMBOL` to the broker's gold symbol if it differs from `XAUUSD`.
6. Run `python app.py` for a read-only connection and market-data check.
7. Launch the local dashboard with `streamlit run dashboard.py`.

The connector reads account details, quotes, and completed candles only. This setup does
not place, modify, or close trades.

## Streamlit Community Cloud

Deploy the repository's `main` branch and select `dashboard.py` as the app's main file.
Streamlit Community Cloud cannot connect to an MT5 terminal running on your personal
Windows computer. The hosted app therefore shows an offline status; run the dashboard
locally on the same Windows computer as MT5 to read broker data.

## Safety notes

- Live trading remains disabled by default.
- The hosted dashboard does not submit or manage orders.
- The current project is a development scaffold and is not ready for live trading.
- Use demo mode before any live trading.
