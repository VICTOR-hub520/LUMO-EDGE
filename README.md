# NEXUS

NEXUS is a C++20 gold-market dashboard for MetaTrader 5 with an optional guarded
execution path that is disabled by default. The local C++ server serves a browser
dashboard, calculates market indicators and rule-based signals, and stores history
in SQLite. An MQL5 Expert Advisor relays terminal data and, when explicitly enabled,
can receive guarded order instructions.

The application binds to `127.0.0.1` only. Order execution requires both server and EA
execution flags; live accounts additionally require a separate server flag. Keep these
disabled until the implementation has been compiled and validated on a demo account.
Missing or stale terminal data is shown as offline; market data is never simulated.

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
the local C++ server automatically. Its logs and SQLite database are stored
in the current Windows user's NEXUS application-data folder.

For development, build the backend with CMake and run `npm start` from `desktop/`.
The vcpkg manifest installs `cpp-httplib`, `nlohmann-json`, and `sqlite3` during
configuration.

At first launch, the app asks for a local profile name. This is only a display label;
the MT5 login number and broker server are verified against the terminal snapshot when
you connect. MT5 passwords stay in the signed-in terminal and are never requested or
stored by NEXUS. The profile label is not user authentication: this local app is
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
4. In NEXUS, enter the login number and broker server shown by MT5, then select
   **Connect**. The app verifies both values against fresh terminal data before showing
   account details. Balance, equity, used/free margin, currency, leverage, account mode,
   and market data are displayed only for the matching account.
5. Select **Disconnect** to stop displaying that account in the dashboard. This does
   not sign out of MT5 or stop the Expert Advisor from sending local snapshots; remove
   or disable the bridge in MT5 to stop that relay.
6. Keep NEXUS open; the desktop app runs the local server while it is open.

The bridge reads data from the terminal's existing signed-in account. Automated
execution is disabled in both the server and bridge by default. Do not enable execution
on a live account, expose the local server through a tunnel or public network, or treat
the heuristic signals/backtest as evidence of profitability.

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

The bridge does not supply an economic-news feed. It reports deal events for the
configured EA magic number; each row is an individual MT5 deal event, not an aggregated
trade lifecycle or strategy-performance calculation. Account-level position counts and
aggregate floating P&L remain visible. Missing or stale quotes and insufficient candles
are marked incomplete; no market data is simulated.

## Configuration

The server reads optional environment variables; copy `.env.example` as a reference and
set the values in your process environment before launching:

- `BOT_NAME`, `OWNER`, `SYMBOL`, `TIMEFRAME`
- `HISTORY_COUNT` (250–2000), `LUMO_PORT` (default `8080`)
- `DEFAULT_RISK_PER_TRADE`, `MAX_RISK_PER_TRADE`, `DAILY_LOSS_LIMIT`
- `REQUESTED_LOT_SIZE`, `MAX_DAILY_TRADES`, `SPREAD_LIMIT_POINTS`
- `STOP_LOSS_ATR_MULTIPLIER`, `BACKTEST_REWARD_RISK`
- `AUTO_TRADING_ENABLED` (default `false`), `LIVE_TRADING_ENABLED` (default `false`),
  `MAX_OPEN_RISK_PERCENT`, `EMERGENCY_STOP`, `TRADING_LOCK`
- `DATABASE_PATH` (default `data/forex_bot.db`), optional `BRIDGE_TOKEN`

Automated execution requires `AUTO_TRADING_ENABLED=true`, the EA's
`InpEnableOrderExecution=true`, and a private bridge token. Live accounts additionally
require `LIVE_TRADING_ENABLED=true` and `InpAllowLiveTrading=true`. These controls must
remain off until the complete build and demo-account validation are successful.
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

## Installable PWA

The web dashboard includes a PWA manifest and service worker for installation on
supported mobile and desktop browsers. Open NEXUS through the local C++ server:

```text
http://127.0.0.1:8080
```

Mobile and remote-device installation requires the server to be reachable through
HTTPS. A phone cannot use this app from `127.0.0.1` because that address refers to
the phone itself. Production mobile access therefore needs a hosted HTTPS backend,
or a secure reverse proxy in front of the Windows NEXUS server.

## Android app shell

The native Android project is in `mobile/android`. It can be opened in Android
Studio and built as an APK without MT5 credentials or a VPS. Firebase
Authentication provides email/password sign-in and keeps the session across app
restarts without storing the password. To enable it, create a Firebase project,
register Android package `com.nexus.mobile`, enable Email/Password sign-in, and
put its `google-services.json` in `mobile/android/app/`. On first launch the app
shows sign-in; after sign-in it shows a setup screen until an HTTPS NEXUS server
is configured. The app rejects non-HTTPS server URLs and restricts dashboard
navigation to that server's origin.

Firebase sign-in currently gates the app UI only. Do not expose the NEXUS
backend publicly until server-side Firebase token validation and per-user access
controls are implemented. The current backend APIs do not enforce user sign-in.
MetaQuotes-Demo is an MT5 demo server, not a retail API-key provider; without a
broker-documented API, private account data still requires the MT5 desktop bridge.

To build an APK, install Android Studio with Android SDK 35, open `mobile/android`,
let Gradle sync, then choose **Build → Build APK(s)**. The generated APK is under
`mobile/android/app/build/outputs/apk/`. On Windows, the same debug build can be
created from `mobile/android` with `.\gradlew.bat assembleDebug`. Without
`google-services.json`, the APK builds but Firebase sign-in is disabled.

## Additional tools

- `lumo-edge --check-config` validates settings without starting the web server.
- `lumo-edge --version` prints the built version; `lumo-edge --help` lists commands.
- `GET /healthz` reports local server health and execution/authentication mode.
- `GET /api/observations?limit=50` returns up to 100 saved, account-scoped market
  observations and next-candle direction evaluations.
- `GET /api/trades?limit=50` returns account-scoped MT5 deal events for the connected
  terminal account.
- `POST /api/mt5/trade-event` accepts authenticated bridge events only for the currently
  connected account; duplicate deal tickets are ignored.
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
