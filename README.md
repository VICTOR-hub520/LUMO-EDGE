# Lumo Edge

Lumo Edge is a C++20, read-only gold-market dashboard for MetaTrader 5. The local C++
server serves a browser dashboard, calculates the market indicators and rule-based
signal, and stores signals in SQLite. An MQL5 Expert Advisor relays closed candles,
the current quote, and account summary from the signed-in MT5 terminal.

The application binds to `127.0.0.1` only. It does not expose internet-facing endpoints
or submit, modify, or close orders. Missing or stale terminal data is shown as offline;
market data is never simulated.

## Requirements

- Windows 10 or newer
- Visual Studio 2022 with the **Desktop development with C++** workload
- CMake 3.25 or newer
- [vcpkg](https://learn.microsoft.com/vcpkg/get_started/get-started) for C++ dependencies
- MetaTrader 5 desktop terminal, signed in to a demo account for testing

## Build and run the Windows desktop app

Install Node.js LTS, Visual Studio 2022 with the **Desktop development with C++**
workload, CMake 3.25+, and vcpkg. Set `VCPKG_ROOT` to the vcpkg directory, then run:

```powershell
cd desktop
npm install
npm run dist:win
```

The Windows installer is written to `desktop/dist/`. The desktop app starts and stops
the local read-only C++ server automatically. Its logs and SQLite database are stored
in the current Windows user's Lumo Edge application-data folder.

For development, build the backend with CMake and run `npm start` from `desktop/`.
The vcpkg manifest installs `cpp-httplib`, `nlohmann-json`, and `sqlite3` during
configuration.

At first launch, the app asks for a local profile name. This is only a display label;
the MT5 login number and broker server are verified against the terminal snapshot when
you connect. MT5 passwords stay in the signed-in terminal and are never requested or
stored by Lumo Edge. The profile label is not user authentication: this local app is
for one Windows user/runtime and is not a shared hosted multi-user service.

## Connect MetaTrader 5

1. Open `mt5/LumoEdgeBridge.mq5` in MetaEditor and compile it.
2. In MT5, open **Tools → Options → Expert Advisors** and allow
   `http://127.0.0.1:8080` in the WebRequest URL list.
3. Attach **LumoEdgeBridge** to a chart. Set its symbol and timeframe inputs to the
   instrument you want to monitor; its symbol aliases default to `XAUUSD,XAUUSDm,GOLD`.
   Prefix aliases are used only when they identify one broker symbol uniquely; set
   `InpSymbol` or `InpSymbolAliases` explicitly if the bridge reports an ambiguous match.
   The bridge requires 250–2000 closed candles, and historical backtesting requires at
   least 252. Daily P&L includes trading deals and commission entries, excluding deposits,
   withdrawals, and other balance operations. If you change `HISTORY_COUNT`, update the
   bridge input too. If you change `LUMO_PORT`, update the bridge's `InpEndpoint` and
   the terminal's allowed WebRequest URL.
4. In Lumo Edge, enter the login number and broker server shown by MT5, then select
   **Connect**. The app verifies both values against fresh terminal data before showing
   account details. Balance, equity, used/free margin, currency, leverage, account mode,
   and market data are displayed only for the matching account.
5. Select **Disconnect** to stop displaying that account in the dashboard. This does
   not sign out of MT5 or stop the Expert Advisor from sending local snapshots; remove
   or disable the bridge in MT5 to stop that relay.
6. Keep Lumo Edge open; the desktop app runs the local server while it is open.

The bridge reads data from the terminal's existing signed-in account; it has no order
placement code. The dashboard is read-only and does not submit, modify, or close orders.
Do not expose the local server through a tunnel or public network.

## Market observability

The local observability engine evaluates verified terminal candles and quotes before
publishing a market report. It calculates EMA/RSI/ATR (used by the existing strategy),
SMA 20/50, MACD, recent 20-candle high/low levels, basic single-candle/engulfing
patterns, relative MT5 tick volume, spread, and a UTC session label. ATR compared with
its recent 50-reading average labels volatility as quiet, normal, or elevated. The
dashboard records one observation per newly closed candle for the selected login and
broker server, then compares its bullish/bearish trend label with the next closed
candle when candle timestamps are contiguous; gaps are marked skipped rather than
misreported as next-candle results. The labels and sample-based strategy-review note are deterministic heuristics,
not a trained AI model or forecast; they never change live settings. This directional
check is not trade performance or evidence of profitability.

The bridge does not supply an economic-news feed or individual position/deal history.
News, per-trade monitoring, actual entry/exit analysis, and post-trade recommendations
are therefore not inferred. Account-level position counts and aggregate floating P&L
remain visible. The current app is read-only: risk/market checks and signals are
observational, approval and execution remain disabled, and no market observation can
change the strategy or risk settings. Missing or stale quotes and insufficient candles
are marked incomplete; no market data is simulated.

## Configuration

The server reads optional environment variables; copy `.env.example` as a reference and
set the values in your process environment before launching:

- `BOT_NAME`, `OWNER`, `SYMBOL`, `TIMEFRAME`
- `HISTORY_COUNT` (250–2000), `LUMO_PORT` (default `8080`)
- `DEFAULT_RISK_PER_TRADE`, `MAX_RISK_PER_TRADE`, `DAILY_LOSS_LIMIT`
- `REQUESTED_LOT_SIZE`, `MAX_DAILY_TRADES`, `SPREAD_LIMIT_POINTS`
- `STOP_LOSS_ATR_MULTIPLIER`, `BACKTEST_REWARD_RISK`
- `LIVE_TRADING_ENABLED`, `EMERGENCY_STOP`, `TRADING_LOCK`
- `DATABASE_PATH` (default `data/forex_bot.db`), optional `BRIDGE_TOKEN`

Risk settings are retained for analysis and compatibility; enabling a setting does not
enable order execution. The live dashboard continues to be read-only.
Set the bridge's symbol and timeframe inputs to match `SYMBOL` and `TIMEFRAME`.

For local bridge authentication, set `BRIDGE_TOKEN` in `.env` to a private random
value of at least 32 characters and enter the same value in the bridge's
`InpBridgeToken` input. One way to generate a value in PowerShell is:

```powershell
-join (1..4 | ForEach-Object { [guid]::NewGuid().ToString("N") })
```

The `.env` file is loaded automatically from the project root and is ignored by Git.
The WebRequest allowlist in MT5 must include the server's local origin. If authentication
is enabled but the bridge token does not match, the bridge receives HTTP 401 and the
dashboard reports stale/offline data.

The server validates `.env` at startup; process environment variables override values
from the file. The server warns on the dashboard if bridge authentication is disabled.

## Additional tools

- `lumo-edge --check-config` validates settings without starting the web server.
- `lumo-edge --version` prints the built version; `lumo-edge --help` lists commands.
- `GET /healthz` reports local server health and read-only/authentication mode.
- `GET /api/observations?limit=50` returns up to 100 saved, account-scoped market
  observations and next-candle direction evaluations.
- `GET /api/history?limit=25` returns up to 100 persisted signal records.
- `GET /api/backtest` runs a historical candle simulation only when fresh market data is
  available. It uses a fixed 1-lot, 100-unit contract assumption and omits trading costs;
  it is not an account-currency projection or performance guarantee.
- SQLite uses WAL mode, a bounded busy wait, and an index for recent signal history.

## Scope and limitations

- The scanner uses EMA, RSI, and ATR calculations on completed bars. Signals are
  informational and are not financial advice.
- Account and market data are available only while the local MT5 bridge is running.
- Snapshots with stale quotes, malformed OHLC candles, unsupported timeframes, or
  inconsistent account/position fields are rejected; the dashboard marks expired data
  offline instead of continuing to present it as live.
- Backtesting and risk sizing are development utilities, not evidence of live
  profitability or an execution-ready strategy. Backtests use a simplified 1-lot,
  100-unit contract model and conservative stop-first handling when both exit levels
  fall within one candle; costs and currency conversion are excluded.
- The previous Python ML scaffold did not ship a trained model artifact; the active
  dashboard used deterministic rules. The C++ scanner keeps that deterministic behavior.
