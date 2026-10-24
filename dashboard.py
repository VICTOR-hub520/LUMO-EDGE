from __future__ import annotations

import streamlit as st


def render_dashboard() -> None:
    st.set_page_config(page_title="LUMO EDGE — AI GOLD TRADING", layout="wide")
    st.title("LUMO EDGE — AI GOLD TRADING")
    st.caption("Owned by GONTSE")

    col1, col2, col3 = st.columns(3)
    with col1:
        st.metric("MT5 connection", "ONLINE")
    with col2:
        st.metric("Mode", "DEMO")
    with col3:
        st.metric("XAUUSD", "M5")

    st.subheader("Account overview")
    st.write("Balance: $10,000 | Equity: $10,200 | Margin available: $7,500")

    st.subheader("Market scanner")
    st.write("Market trend: Bullish | Signal: BUY | Confidence: 0.74")

    st.subheader("Trade controls")
    if st.button("Start scanning"):
        st.success("Scanning started")
    if st.button("Pause new entries"):
        st.warning("New entries paused")
    if st.button("Emergency stop"):
        st.error("Emergency stop activated")
