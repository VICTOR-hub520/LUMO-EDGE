from __future__ import annotations

import streamlit as st


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
        }

        .stApp {
            color: var(--lumo-text);
            background:
                radial-gradient(ellipse at 10% 0%, rgba(38, 92, 100, 0.18), transparent 35%),
                radial-gradient(ellipse at 100% 5%, rgba(180, 130, 43, 0.10), transparent 28%),
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
            font-size: clamp(2.6rem, 7vw, 4.2rem) !important;
            font-weight: 700 !important;
        }

        h2 {
            margin-top: 2rem !important;
            font-size: 1.45rem !important;
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
            background: linear-gradient(145deg, rgba(23, 34, 46, 0.96), rgba(16, 24, 33, 0.96));
            box-shadow: 0 12px 30px rgba(0, 0, 0, 0.14);
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
            border-radius: 14px;
            background: rgba(233, 185, 73, 0.08);
        }

        [data-testid="stAlert"] p {
            color: #f2d58d;
        }

        [data-testid="stAlert"]:has([data-testid="stNotificationContentInfo"]) {
            border-color: rgba(89, 212, 196, 0.26);
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

    st.title("LUMO-EDGE")
    st.subheader("AI-assisted gold trading dashboard")
    st.caption("Owned by GONTSE")

    st.warning(
        "Dashboard preview only. This hosted app is not connected to MetaTrader 5 "
        "and does not receive live market or account data."
    )

    connection_col, mode_col, symbol_col = st.columns(3)
    connection_col.metric("MT5 connection", "Not connected")
    mode_col.metric("Execution mode", "Demo — trading disabled")
    symbol_col.metric("Configured market", "XAUUSD · M5")

    st.divider()
    st.header("Market scanner")
    st.info("Signal, indicators, and price will appear here when a verified market-data source is connected.")

    st.header("Account and risk")
    account_col, equity_col, trades_col = st.columns(3)
    account_col.metric("Account balance", "Unavailable")
    equity_col.metric("Account equity", "Unavailable")
    trades_col.metric("Today's entries", "Unavailable")
    st.caption(
        "No trades can be opened or managed from this dashboard. "
        "Live execution is not configured."
    )

    st.header("Getting started")
    st.markdown(
        """
        1. Install and run the project on a Windows computer with MetaTrader 5 installed.
        2. Configure broker access locally using the environment variables in `.env.example`.
        3. Validate the scanner and risk controls on a demo account before considering live use.

        Streamlit Community Cloud cannot connect to an MT5 terminal running on your
        personal computer. This page intentionally shows unavailable data instead of
        inventing prices, account figures, or signals.
        """
    )


render_dashboard()
