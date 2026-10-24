from __future__ import annotations

import streamlit as st

from config import Config
from data_collector import DataCollector
from indicators import market_features
from mt5_connector import MT5ConnectionError, MT5Connector
from strategy import Strategy


def apply_styles() -> None:
    st.markdown(
        """
        <style>
        @import url('https://fonts.googleapis.com/css2?family=DM+Sans:wght@400;500;600;700&family=Space+Grotesk:wght@500;600;700&display=swap');

        :root {
            --lumo-bg: #0b1017;
            --lumo-panel: #121a24;
            --lumo-panel-raised: #17222e;
            --lumo-border: rgba(155, 179, 197, 0.14);
            --lumo-text: #edf4f7;
            --lumo-muted: #8fa2b1;
            --lumo-accent: #e9b949;
            --lumo-cyan: #59d4c4;
            --lumo-blue: #76a9ff;
            --lumo-violet: #bd91ff;
        }

        .stApp {
            color: var(--lumo-text);
            background:
                radial-gradient(ellipse at 8% 4%, rgba(89, 212, 196, 0.15), transparent 31%),
                radial-gradient(ellipse at 96% 8%, rgba(233, 185, 73, 0.14), transparent 28%),
                radial-gradient(ellipse at 70% 70%, rgba(118, 169, 255, 0.06), transparent 42%),
                var(--lumo-bg);
            font-family: 'DM Sans', sans-serif;
        }

        [data-testid="stHeader"] {
            background: rgba(11, 16, 23, 0.82);
        }

        [data-testid="stMainBlockContainer"] {
            max-width: 1180px;
            padding-top: 3rem;
            padding-bottom: 4rem;
        }

        h1, h2, h3 {
            color: var(--lumo-text);
            font-family: 'Space Grotesk', sans-serif;
            letter-spacing: -0.035em;
        }

        h1 {
            margin-bottom: 0.15rem;
            background: linear-gradient(100deg, #fff4d4 0%, #e9b949 43%, #59d4c4 100%);
            background-clip: text;
            -webkit-background-clip: text;
            -webkit-text-fill-color: transparent;
            font-size: clamp(2.6rem, 7vw, 4.2rem) !important;
            font-weight: 700 !important;
        }

        h2 {
            position: relative;
            margin-top: 2rem !important;
            padding-bottom: 0.65rem;
            border-bottom: 1px solid var(--lumo-border);
            color: #dce9ee;
            font-size: 1.45rem !important;
        }

        h2::after {
            position: absolute;
            bottom: -1px;
            left: 0;
            width: 54px;
            height: 2px;
            background: linear-gradient(90deg, var(--lumo-accent), var(--lumo-cyan));
            content: "";
        }

        h3 {
            color: var(--lumo-muted);
            font-family: 'DM Sans', sans-serif;
            font-size: 1rem !important;
            font-weight: 500 !important;
        }

        [data-testid="stCaptionContainer"] {
            color: var(--lumo-muted);
            font-size: 0.92rem;
        }

        [data-testid="stMetric"] {
            min-height: 116px;
            padding: 1.15rem 1.25rem;
            border: 1px solid var(--lumo-border);
            border-radius: 16px;
            background:
                linear-gradient(145deg, rgba(23, 34, 46, 0.96), rgba(16, 24, 33, 0.96));
            box-shadow: 0 12px 30px rgba(0, 0, 0, 0.14);
            transition: transform 160ms ease, border-color 160ms ease;
        }

        [data-testid="stMetric"]:hover {
            transform: translateY(-2px);
            border-color: rgba(233, 185, 73, 0.48);
        }

        [data-testid="stHorizontalBlock"] > [data-testid="column"]:nth-child(3n + 1) [data-testid="stMetric"] {
            border-top: 2px solid rgba(233, 185, 73, 0.75);
        }

        [data-testid="stHorizontalBlock"] > [data-testid="column"]:nth-child(3n + 2) [data-testid="stMetric"] {
            border-top: 2px solid rgba(89, 212, 196, 0.75);
        }

        [data-testid="stHorizontalBlock"] > [data-testid="column"]:nth-child(3n) [data-testid="stMetric"] {
            border-top: 2px solid rgba(118, 169, 255, 0.75);
        }

        [data-testid="stMetricLabel"] {
            color: var(--lumo-muted);
            font-size: 0.85rem;
        }

        [data-testid="stMetricValue"] {
            color: var(--lumo-text);
            font-family: 'Space Grotesk', sans-serif;
            font-size: 1.4rem;
        }

        [data-testid="stAlert"] {
            border: 1px solid rgba(233, 185, 73, 0.32);
            border-left: 4px solid var(--lumo-accent);
            border-radius: 14px;
            background: rgba(233, 185, 73, 0.08);
        }

        [data-testid="stAlert"] p {
            color: #f2d58d;
        }

        [data-testid="stAlert"]:has([data-testid="stNotificationContentInfo"]) {
            border-color: rgba(89, 212, 196, 0.26);
            border-left-color: var(--lumo-cyan);
            background: rgba(89, 212, 196, 0.07);
        }

        [data-testid="stAlert"]:has([data-testid="stNotificationContentInfo"]) p {
            color: #a8e8df;
        }

        [data-testid="stMarkdownContainer"] ol {
            color: #c5d1d9;
            line-height: 1.9;
        }

        [data-testid="stMarkdownContainer"] strong {
            color: var(--lumo-accent);
        }

        [data-testid="stMarkdownContainer"] a {
            color: var(--lumo-cyan);
        }

        hr {
            border-color: var(--lumo-border);
            margin: 2rem 0;
        }

        @media (max-width: 640px) {
            [data-testid="stMainBlockContainer"] {
                padding: 1.5rem 1rem 3rem;
            }

            [data-testid="stMetric"] {
                min-height: 96px;
                padding: 0.9rem;
            }
        }
        </style>
        """,
        unsafe_allow_html=True,
    )


def render_dashboard() -> None:
    st.set_page_config(page_title="LUMO-EDGE | Gold Trading", page_icon="🟡", layout="wide")
    apply_styles()

    config = Config.from_env()
    config.validate()

    st.title("LUMO-EDGE")
    st.subheader("AI-assisted gold trading dashboard")
    st.caption("Owned by GONTSE")

    if st.button("Refresh MT5 connection"):
        st.rerun()

    connector = MT5Connector()
    connected = connector.connect()
    account: dict = {}
    market: dict = {}
    signal: dict = {}
    latest_features = None
    symbol = config.symbol

    if connected:
        try:
            account = connector.get_account_info()
            symbol = connector.resolve_symbol(config.symbol, config.symbol_aliases)
            collector = DataCollector(config, connector)
            market = collector.get_market_snapshot(symbol)
            candles = collector.fetch_history(symbol, count=500)
            signal = Strategy(config).evaluate(candles)
            if not candles.empty:
                latest_features = market_features(candles).iloc[-1]
        except MT5ConnectionError as exc:
            st.error(f"Connected to MT5, but market data is unavailable: {exc}")
        finally:
            connector.disconnect()
    else:
        st.warning(
            "MT5 is not connected. Install and open MetaTrader 5 on this same Windows "
            "computer, sign in to your broker account, then refresh. Streamlit Community "
            "Cloud cannot access a terminal running on your personal computer."
        )
        if connector.last_error:
            st.caption(connector.last_error)

    account_mode = "Read-only"
    if account:
        trade_mode = account.get("trade_mode")
        if trade_mode == 0:
            account_mode = "DEMO · read-only"
        elif trade_mode == 2:
            account_mode = "LIVE · read-only"

    connection_col, mode_col, symbol_col = st.columns(3)
    connection_col.metric("MT5 connection", "Connected" if connected else "Offline")
    mode_col.metric("Account / access", account_mode)
    symbol_col.metric("Broker symbol · timeframe", f"{symbol} · {config.timeframe}")

    st.divider()
    st.header("Market scanner")
    if market:
        market_col, spread_col, signal_col = st.columns(3)
        market_col.metric("Bid / ask", f"{market['bid']:.{market['digits']}f} / {market['ask']:.{market['digits']}f}")
        spread_col.metric(
            "Spread",
            f"{market['spread_points']:.1f} points"
            if market["spread_points"] is not None
            else "Unavailable",
        )
        signal_col.metric("Rule-based signal", signal.get("direction", "Unavailable"))

        if latest_features is not None:
            ema_col, rsi_col, atr_col = st.columns(3)
            ema_col.metric("EMA 50", f"{latest_features['ema_fast']:.{market['digits']}f}")
            ema_col.metric("EMA 200", f"{latest_features['ema_slow']:.{market['digits']}f}")
            atr_col.metric("RSI · ATR", f"{latest_features['rsi']:.1f} · {latest_features['atr']:.{market['digits']}f}")
        if signal:
            st.info(signal.get("reason", "No signal explanation available."))
    else:
        st.info("Live quote, indicators, and signal will appear after MT5 connection succeeds.")

    st.header("Account and risk")
    account_col, equity_col, trades_col = st.columns(3)
    currency = account.get("currency", "")
    account_col.metric(
        "Account balance",
        f"{account['balance']:,.2f} {currency}" if "balance" in account else "Unavailable",
    )
    equity_col.metric(
        "Account equity",
        f"{account['equity']:,.2f} {currency}" if "equity" in account else "Unavailable",
    )
    trades_col.metric("Today's entries", "Not tracked")
    st.caption(
        "The dashboard connects to MT5 for account and market data only. It does not "
        "submit, modify, or close orders. Automated execution remains disabled."
    )

    st.header("Getting started")
    st.markdown(
        """
        1. Install and run the project on a Windows computer with MetaTrader 5 installed.
        2. Configure broker access locally using the environment variables in `.env.example`.
        3. Validate the scanner and risk controls on a demo account before considering live use.

        Streamlit Community Cloud cannot connect to an MT5 terminal running on your
        personal computer. Run this dashboard locally on Windows to read broker data.
        Missing connection or quote data is shown as unavailable, never simulated.
        """
    )


render_dashboard()
