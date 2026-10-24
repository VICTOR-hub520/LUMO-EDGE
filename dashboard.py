from __future__ import annotations

import streamlit as st


def render_dashboard() -> None:
    st.set_page_config(page_title="LUMO-EDGE | Gold Trading", page_icon="🟡", layout="wide")

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
