"use strict";

const byId = (id) => document.getElementById(id);
const unavailable = "Unavailable";
const profileLabelKey = "lumo-edge-profile-label";
let refreshing = false;
let lastState = null;
let lastHistoryLoad = 0;
let lastObservationLoad = 0;
let lastTradeEventLoad = 0;

if ("serviceWorker" in navigator) {
  window.addEventListener("load", () => {
    navigator.serviceWorker.register("/service-worker.js").catch(() => {});
  });
}

function setText(id, value) {
  byId(id).textContent = value;
}

function formatPrice(value, digits) {
  return Number(value).toFixed(Number.isInteger(digits) ? digits : 2);
}

function formatMoney(value, currency) {
  if (!Number.isFinite(Number(value))) return unavailable;
  return `${Number(value).toLocaleString(undefined, {
    minimumFractionDigits: 2,
    maximumFractionDigits: 2,
  })} ${currency || ""}`.trim();
}

function setMoney(id, value, currency) {
  const element = byId(id);
  element.textContent = formatMoney(value, currency);
  element.classList.toggle("negative", Number(value) < 0);
  element.classList.toggle("positive", Number(value) > 0);
}

function renderRiskWarnings(risk) {
  const container = byId("risk-warnings");
  while (container.firstChild) container.removeChild(container.firstChild);
  if (!risk) {
    container.className = "risk-warnings";
    return;
  }
  const warnings = Array.isArray(risk.warnings) ? risk.warnings : [];
  container.className = warnings.length ? "risk-warnings warning" : "risk-warnings ok";
  const heading = document.createElement("strong");
  heading.textContent = warnings.length ? "Risk checks need attention" : "No configured risk warnings";
  container.appendChild(heading);
  if (warnings.length) {
    const list = document.createElement("ul");
    for (const warning of warnings) {
      const item = document.createElement("li");
      item.textContent = warning;
      list.appendChild(item);
    }
    container.appendChild(list);
  }
}

function renderHistory(signals) {
  const body = byId("signal-history");
  while (body.firstChild) body.removeChild(body.firstChild);
  if (!signals.length) {
    const row = document.createElement("tr");
    const cell = document.createElement("td");
    cell.colSpan = 5;
    cell.textContent = "No saved signals yet.";
    row.appendChild(cell);
    body.appendChild(row);
    setText("history-status", "No history");
    return;
  }
  for (const signal of signals) {
    const row = document.createElement("tr");
    const timestamp = String(signal.created_at || "");
    const date = new Date(`${timestamp.replace(" ", "T")}Z`);
    const values = [
      Number.isNaN(date.getTime()) ? timestamp : date.toLocaleString(),
      signal.symbol,
      signal.direction,
      Number(signal.confidence).toFixed(2),
      signal.reason,
    ];
    for (const value of values) {
      const cell = document.createElement("td");
      cell.textContent = value == null ? "" : String(value);
      row.appendChild(cell);
    }
    body.appendChild(row);
  }
  setText("history-status", `Latest ${signals.length} signal records`);
}

function renderTradeEvents(events) {
  const body = byId("trade-history");
  while (body.firstChild) body.removeChild(body.firstChild);
  if (!events.length) {
    const row = document.createElement("tr");
    const cell = document.createElement("td");
    cell.colSpan = 9;
    cell.textContent = lastState?.connected
      ? "No persisted trade events for this connected account."
      : "Connect a verified MT5 account to view trade events.";
    row.appendChild(cell);
    body.appendChild(row);
    setText("trade-history-status", lastState?.connected
      ? "No trade events recorded"
      : "Waiting for a connected MT5 account");
    return;
  }
  for (const event of events) {
    const date = new Date(Number(event.event_time) * 1000);
    const digits = Number(lastState?.market?.digits);
    const values = [
      Number.isNaN(date.getTime()) ? String(event.event_time || "") : date.toLocaleString(),
      event.ticket,
      event.symbol,
      event.direction,
      event.status,
      formatPrice(event.price, digits),
      Number(event.volume).toLocaleString(undefined, {
        minimumFractionDigits: 0,
        maximumFractionDigits: 8,
      }),
      `${formatPrice(event.stop_loss, digits)} · ${formatPrice(event.take_profit, digits)}`,
      formatMoney(event.pnl, lastState?.account?.currency),
    ];
    const row = document.createElement("tr");
    for (const value of values) {
      const cell = document.createElement("td");
      cell.textContent = value == null ? "" : String(value);
      row.appendChild(cell);
    }
    const pnlCell = row.lastElementChild;
    pnlCell.classList.toggle("negative", Number(event.pnl) < 0);
    pnlCell.classList.toggle("positive", Number(event.pnl) > 0);
    body.appendChild(row);
  }
  setText("trade-history-status", `Latest ${events.length} MT5 deal events · individual deal P&L, not aggregate strategy performance`);
}

function drawChart(candles) {
  const svg = byId("price-chart");
  while (svg.firstChild) svg.removeChild(svg.firstChild);
  if (!Array.isArray(candles) || candles.length < 2) {
    setText("chart-symbol", "Waiting for candles");
    return;
  }

  const recent = candles.slice(-120);
  const values = recent.map((candle) => Number(candle.close));
  const minimum = Math.min(...values);
  const maximum = Math.max(...values);
  const range = maximum - minimum || 1;
  const points = values.map((value, index) => {
    const x = 12 + (index / (values.length - 1)) * 876;
    const y = 220 - ((value - minimum) / range) * 200;
    return `${x.toFixed(1)},${y.toFixed(1)}`;
  }).join(" ");
  const line = document.createElementNS("http://www.w3.org/2000/svg", "polyline");
  line.setAttribute("points", points);
  line.setAttribute("fill", "none");
  line.setAttribute("stroke", "#59d4c4");
  line.setAttribute("stroke-width", "2.5");
  line.setAttribute("stroke-linecap", "round");
  line.setAttribute("stroke-linejoin", "round");
  svg.appendChild(line);
  setText("chart-symbol", `${recent.length} closed candles`);
}

function drawObservabilityChart(candles, observation, digits) {
  const svg = byId("observation-chart");
  while (svg.firstChild) svg.removeChild(svg.firstChild);
  if (!Array.isArray(candles) || candles.length < 2 || !observation) return;

  const recent = candles.slice(-120);
  const closes = recent.map((candle) => Number(candle.close));
  const support = Number(observation.support);
  const resistance = Number(observation.resistance);
  const minimum = Math.min(...closes, support);
  const maximum = Math.max(...closes, resistance);
  const range = maximum - minimum || 1;
  const yFor = (value) => 220 - ((value - minimum) / range) * 200;
  const points = closes.map((value, index) => {
    const x = 12 + (index / (closes.length - 1)) * 876;
    return `${x.toFixed(1)},${yFor(value).toFixed(1)}`;
  }).join(" ");
  const line = document.createElementNS("http://www.w3.org/2000/svg", "polyline");
  line.setAttribute("points", points);
  line.setAttribute("fill", "none");
  line.setAttribute("stroke", "#59d4c4");
  line.setAttribute("stroke-width", "2.5");
  line.setAttribute("stroke-linecap", "round");
  line.setAttribute("stroke-linejoin", "round");
  svg.appendChild(line);

  for (const level of [
    { value: support, color: "#76a9ff", label: `Support ${formatPrice(support, digits)}` },
    { value: resistance, color: "#e9b949", label: `Resistance ${formatPrice(resistance, digits)}` },
  ]) {
    const y = yFor(level.value);
    const marker = document.createElementNS("http://www.w3.org/2000/svg", "line");
    marker.setAttribute("x1", "12");
    marker.setAttribute("x2", "888");
    marker.setAttribute("y1", String(y));
    marker.setAttribute("y2", String(y));
    marker.setAttribute("stroke", level.color);
    marker.setAttribute("stroke-width", "1.5");
    marker.setAttribute("stroke-dasharray", "6 5");
    svg.appendChild(marker);
    const label = document.createElementNS("http://www.w3.org/2000/svg", "text");
    label.setAttribute("x", "18");
    label.setAttribute("y", String(Math.max(14, y - 5)));
    label.setAttribute("fill", level.color);
    label.setAttribute("font-size", "12");
    label.textContent = level.label;
    svg.appendChild(label);
  }
}

function renderObservationHistory(rows) {
  const body = byId("observation-history");
  while (body.firstChild) body.removeChild(body.firstChild);
  const observations = Array.isArray(rows) ? rows : [];
  const completed = observations.filter((row) =>
    ["bullish", "bearish"].includes(String(row.predicted_direction || "").toLowerCase())
    && (row.actual_direction === "BULLISH" || row.actual_direction === "BEARISH"));
  const correct = completed.filter((row) => {
    const prediction = String(row.predicted_direction || "").toLowerCase();
    return (prediction === "bullish" && row.actual_direction === "BULLISH")
      || (prediction === "bearish" && row.actual_direction === "BEARISH");
  }).length;
  const symbol = observations[0]?.symbol || "";
  setText("observation-evaluations", String(completed.length));
  setText("observation-accuracy", completed.length
    ? `${((correct / completed.length) * 100).toFixed(1)}%`
    : unavailable);
  setText("observation-history-status", observations.length
    ? `Latest ${observations.length} saved observations`
    : "No saved observations yet");

  if (!observations.length) {
    const row = document.createElement("tr");
    const cell = document.createElement("td");
    cell.colSpan = 5;
    cell.textContent = "No persisted observations yet.";
    row.appendChild(cell);
    body.appendChild(row);
    return;
  }
  for (const entry of observations) {
    const observation = entry.observation || {};
    const date = new Date(Number(entry.observed_at) * 1000);
    const predicted = String(entry.predicted_direction || "Uncertain");
    const actual = entry.actual_direction === "GAP"
      ? "Skipped (candle gap)"
      : entry.actual_direction || "Pending next candle";
    let reviewed = "Pending";
    if (entry.actual_direction === "GAP") {
      reviewed = "Skipped (data gap)";
    } else if (entry.actual_direction) {
      reviewed = ["Bullish", "Bearish"].includes(predicted)
        ? predicted.toUpperCase() === entry.actual_direction ? "Matched" : "Did not match"
        : "Not directional";
    }
    const values = [
      Number.isNaN(date.getTime()) ? String(entry.observed_at || "") : date.toLocaleString(),
      observation.classification || "Uncertain",
      predicted,
      actual,
      reviewed,
    ];
    const row = document.createElement("tr");
    for (const value of values) {
      const cell = document.createElement("td");
      cell.textContent = String(value);
      row.appendChild(cell);
    }
    body.appendChild(row);
  }
  if (completed.length) {
    const accuracy = correct / completed.length;
    const recommendation = completed.length < 20
      ? "Collect at least 20 directional evaluations before considering a strategy review."
      : accuracy < 0.5
        ? "Review the directional filters against this sample before considering any approved strategy change."
        : "No strategy change is suggested by this directional sample alone.";
    setText(
      "observation-insight",
      `${symbol ? `${symbol}: ` : ""}directional observation matched the next closed candle ${correct} of ${completed.length} times (${(accuracy * 100).toFixed(1)}%). ${recommendation} This is not trade performance or profitability; no live strategy/risk settings are changed.`,
    );
  } else {
    setText("observation-insight", "Waiting for a later MT5 candle to evaluate recorded market directions. This review does not represent trade performance; no strategy settings are changed automatically.");
  }
}

function renderObservability(data) {
  const observation = data.connected ? data.observation : null;
  const market = data.connected ? data.market : null;
  const signal = data.connected ? data.signal : null;
  const account = data.connected ? data.account : null;
  const risk = data.connected ? data.risk : null;
  const available = Boolean(observation && observation.critical_data_available);
  const badge = byId("observation-badge");
  badge.textContent = observation ? observation.classification.toUpperCase() : "WAITING";
  badge.className = `badge ${observation && observation.classification.includes("bullish") ? "buy" : observation && observation.classification.includes("bearish") ? "sell" : "neutral"}`;
  setText("observation-trend", available ? observation.trend : unavailable);
  setText("observation-volatility", available
    ? `${observation.volatility} · ${Number(observation.volatility_ratio).toFixed(2)}×`
    : unavailable);
  setText("observation-signal", signal
    ? `${signal.direction} · ${Number(signal.confidence).toFixed(2)}`
    : unavailable);
  setText("observation-levels", available
    ? `${formatPrice(observation.support, market.digits)} · ${formatPrice(observation.resistance, market.digits)}`
    : unavailable);
  setText("observation-sma", available
    ? `${formatPrice(observation.sma_fast, market.digits)} · ${formatPrice(observation.sma_slow, market.digits)}`
    : unavailable);
  setText("observation-macd", available
    ? `${formatPrice(observation.macd, market.digits)} · ${formatPrice(observation.macd_signal, market.digits)}`
    : unavailable);
  setText("observation-pattern", available ? observation.candle_pattern : unavailable);
  setText("observation-volume", available
    ? `${observation.volume_condition} · ${Number(observation.relative_volume).toFixed(2)}×`
    : unavailable);
  setText("observation-cost-session", available
    ? `${Number(observation.spread_points).toFixed(1)} points · ${observation.market_session}`
    : unavailable);
  setText("observation-report", available
    ? observation.report
    : "Critical market data is unavailable or stale. Analysis is incomplete; trading must remain blocked.");
  const observedAt = observation && Number(observation.observed_at) > 0
    ? new Date(Number(observation.observed_at) * 1000).toLocaleString()
    : "No observation yet";
  setText("observation-updated", observedAt);
  drawObservabilityChart(data.connected ? data.candles : [], observation, market ? market.digits : 2);

  const protocol = data.decision_protocol;
  const protocolLabels = [
    ["market_observation", "Market observation"],
    ["market_analysis", "Market analysis"],
    ["trend_identification", "Trend identification"],
    ["volatility_analysis", "Volatility analysis"],
    ["signal_generation", "Signal generation"],
    ["risk_assessment", "Risk assessment"],
    ["risk_checks_passed", "Risk checks passed"],
    ["trade_approval", "Trade approval"],
    ["trade_execution", "Trade execution"],
    ["market_monitoring", "Continuous market monitoring"],
    ["trade_closure", "Trade closure"],
    ["post_trade_analysis", "Post-trade analysis"],
    ["performance_report", "Observation performance report"],
  ];
  const protocolList = byId("decision-protocol");
  while (protocolList.firstChild) protocolList.removeChild(protocolList.firstChild);
  for (const [key, label] of protocolLabels) {
    const item = document.createElement("li");
    const executionDisabled = (key === "trade_execution" || key === "trade_approval")
      && !data.execution_enabled;
    const failedRiskCheck = key === "risk_checks_passed" && protocol && protocol.risk_assessment && !protocol[key];
    item.textContent = `${label}: ${protocol && protocol[key] ? "complete" : executionDisabled ? "disabled (execution config off)" : failedRiskCheck ? "failed (review alerts)" : "waiting / unavailable"}`;
    if (protocol && protocol[key]) item.className = "complete";
    else if (failedRiskCheck) item.className = "blocked";
    else if (executionDisabled) item.className = "blocked";
    protocolList.appendChild(item);
  }
  const alerts = byId("system-alerts");
  while (alerts.firstChild) alerts.removeChild(alerts.firstChild);
  const alertValues = data.connected
    ? (Array.isArray(data.alerts) ? data.alerts : [])
    : [data.connection_message || "No fresh verified market data; trading is blocked."];
  if (!alertValues.length) alertValues.push("No configured risk or market-condition alerts.");
  for (const message of alertValues) {
    const item = document.createElement("li");
    item.textContent = message;
    alerts.appendChild(item);
  }
  setText("observed-positions", account && risk
    ? `${risk.open_positions} account-level open position(s) · aggregate P&L ${formatMoney(account.floating_pnl, account.currency)}. Individual tickets/stops are not sent by this bridge.`
    : unavailable);
}

function render(data) {
  lastState = data;
  setText("owner", `· Profile ${getProfileLabel()}`);
  setText("instrument", `${data.symbol || data.configured_symbol || "Unknown"} · ${data.timeframe || "—"}`);
  const connectionStatus = data.connection_status || (data.connected ? "connected" : "offline");
  setText("connection", ({
    connected: "Connected",
    waiting: "Waiting for MT5",
    stale: "Data stale",
    account_mismatch: "Account mismatch",
    disconnected: "Disconnected",
  })[connectionStatus] || "Offline");
  const accountType = data.account
    ? (data.account.trade_mode === 0 ? "DEMO"
      : data.account.trade_mode === 1 ? "CONTEST"
        : data.account.trade_mode === 2 ? "LIVE" : "Unknown")
    : "Not connected";
  setText("account-mode", data.account && Number(data.account.login) > 0
    ? `MT5 #${data.account.login} · ${accountType} · ${data.execution_enabled ? "Guarded auto" : "Read-only"}`
    : accountType);

  const expectedAccount = data.expected_account;
  const loginInput = byId("mt5-login");
  const serverInput = byId("mt5-server");
  if (expectedAccount && !loginInput.value && document.activeElement !== loginInput) {
    loginInput.value = String(expectedAccount.login);
  }
  if (expectedAccount && !serverInput.value && document.activeElement !== serverInput) {
    serverInput.value = expectedAccount.server;
  }
  byId("disconnect-mt5").disabled = !expectedAccount;
  const feedback = byId("connection-feedback");
  feedback.textContent = data.connection_message || "";
  feedback.className = `connection-feedback ${connectionStatus === "connected" ? "ok" : connectionStatus === "account_mismatch" ? "error" : ""}`;

  const notice = byId("notice");
  if (data.connected) {
    notice.className = "notice ok";
    notice.textContent = data.execution_enabled
      ? "Connected to the matching MT5 account. Guarded automated execution is enabled; verify all order activity in MT5."
      : "Connected to the matching MT5 account. Automated order execution is disabled.";
    setText("last-updated", `Updated ${new Date(data.updated_at * 1000).toLocaleTimeString()} · quote ${data.quote_age_seconds}s old`);
  } else if (connectionStatus === "account_mismatch") {
    notice.className = "notice error";
    notice.textContent = data.connection_message;
    setText("last-updated", "MT5 account verification failed");
  } else if (connectionStatus === "disconnected") {
    notice.className = "notice";
    notice.textContent = "No MT5 account is connected to the dashboard. Sign in to the MT5 terminal, attach the bridge, then enter its login number and broker server above.";
    setText("last-updated", "Disconnected from MT5 account");
  } else {
    notice.className = "notice";
    const bridgeOrigin = `http://127.0.0.1:${data.port || 8080}`;
    notice.textContent = `${data.connection_message || "MT5 bridge is offline or stale."} Start the bridge in your terminal and allow ${bridgeOrigin} in MT5 WebRequest settings. No market data is simulated.`;
    setText("last-updated", "Waiting for MT5 bridge");
  }

  const market = data.connected ? data.market : null;
  const indicators = data.connected ? data.indicators : null;
  const account = data.connected ? data.account : null;
  const signal = data.connected ? data.signal : null;
  const risk = data.connected ? data.risk : null;
  renderObservability(data);
  setText("bid-ask", market ? `${formatPrice(market.bid, market.digits)} / ${formatPrice(market.ask, market.digits)}` : unavailable);
  setText("spread", market ? `${Number(market.spread_points).toFixed(1)} points` : unavailable);
  setText("ema-fast", indicators ? formatPrice(indicators.ema_fast, market.digits) : unavailable);
  setText("ema-slow", indicators ? formatPrice(indicators.ema_slow, market.digits) : unavailable);
  setText("rsi-atr", indicators ? `${Number(indicators.rsi).toFixed(1)} · ${formatPrice(indicators.atr, market.digits)}` : unavailable);
  setText("signal", signal ? signal.direction : unavailable);
  setText("signal-reason", signal
    ? `${signal.reason} · Confidence ${Number(signal.confidence).toFixed(2)}`
    : "No verified market data received.");
  setText("balance", account ? formatMoney(account.balance, account.currency) : unavailable);
  setText("equity", account ? formatMoney(account.equity, account.currency) : unavailable);
  if (account && risk) {
    setMoney("day-pnl", account.day_pnl, account.currency);
    setMoney("floating-pnl", account.floating_pnl, account.currency);
    setText("margin", `${formatMoney(account.margin, account.currency)} · ${formatMoney(account.free_margin, account.currency)}`);
    setText("positions", `${account.open_positions} · ${Number(account.open_volume).toFixed(2)} lots`);
    setText("open-risk", `${formatMoney(account.risk_at_stops, account.currency)} · ${Number(risk.open_risk_percent).toFixed(2)}% of equity`);
    setText("daily-loss", `${Number(risk.day_loss_percent).toFixed(2)}% · ${Number(risk.daily_loss_limit_percent).toFixed(2)}%`);
    setText("leverage", `1:${Number(account.leverage).toLocaleString()}`);
  } else {
    for (const id of ["day-pnl", "floating-pnl", "margin", "positions", "open-risk", "daily-loss", "leverage"]) {
      setText(id, unavailable);
    }
    byId("day-pnl").classList.remove("negative", "positive");
    byId("floating-pnl").classList.remove("negative", "positive");
  }
  renderRiskWarnings(risk);
  const securityNotice = byId("bridge-security");
  if (data.bridge_token_required) {
    securityNotice.className = "notice ok";
    securityNotice.textContent = "Bridge token authentication is enabled. Keep the token private and do not expose this local server to the internet.";
  } else {
    securityNotice.className = "notice";
    securityNotice.textContent = "Bridge token authentication is disabled. The server is loopback-only; configure a private 32+ character token in .env and the MT5 bridge for protection from other local processes.";
  }

  const badge = byId("signal-badge");
  badge.textContent = signal ? signal.direction : "NO DATA";
  badge.className = `badge ${signal && signal.direction === "BUY" ? "buy" : signal && signal.direction === "SELL" ? "sell" : "neutral"}`;
  drawChart(data.connected ? data.candles : []);
}

async function refresh() {
  if (refreshing) return;
  refreshing = true;
  try {
    const response = await fetch("/api/state", { cache: "no-store" });
    if (!response.ok) throw new Error(`Dashboard API returned HTTP ${response.status}`);
    render(await response.json());
  } catch (error) {
    render({
      connected: false,
      account: null,
      market: null,
      indicators: null,
      signal: null,
      candles: [],
    });
    const notice = byId("notice");
    notice.className = "notice error";
    notice.textContent = `Could not load dashboard data: ${error.message}`;
    setText("connection", "Unavailable");
  } finally {
    refreshing = false;
  }
}

async function loadHistory() {
  try {
    const response = await fetch("/api/history?limit=25", { cache: "no-store" });
    if (!response.ok) throw new Error(`History API returned HTTP ${response.status}`);
    const data = await response.json();
    renderHistory(Array.isArray(data.signals) ? data.signals : []);
    lastHistoryLoad = Date.now();
  } catch (error) {
    setText("history-status", `History unavailable: ${error.message}`);
  }
}

async function loadObservations() {
  try {
    const response = await fetch("/api/observations?limit=50", { cache: "no-store" });
    if (!response.ok) throw new Error(`Observations API returned HTTP ${response.status}`);
    const data = await response.json();
    renderObservationHistory(Array.isArray(data.observations) ? data.observations : []);
    lastObservationLoad = Date.now();
  } catch (error) {
    setText("observation-history-status", `Observation history unavailable: ${error.message}`);
  }
}

async function loadTradeEvents() {
  try {
    const response = await fetch("/api/trades?limit=50", { cache: "no-store" });
    if (!response.ok) throw new Error(`Trade history API returned HTTP ${response.status}`);
    const data = await response.json();
    renderTradeEvents(Array.isArray(data.trades) ? data.trades : []);
    lastTradeEventLoad = Date.now();
  } catch (error) {
    setText("trade-history-status", `Trade history unavailable: ${error.message}`);
  }
}

async function runBacktest() {
  const result = byId("backtest-result");
  if (!lastState || !lastState.connected) {
    result.textContent = "Backtest requires a current, verified MT5 snapshot.";
    return;
  }
  const button = byId("run-backtest");
  button.disabled = true;
  result.textContent = "Running simulation on the latest verified candles…";
  try {
    const response = await fetch("/api/backtest", { cache: "no-store" });
    const data = await response.json();
    if (!response.ok) throw new Error(data.error || `Backtest API returned HTTP ${response.status}`);
    const factor = data.profit_factor_unbounded
      ? "No losing trades in this sample"
      : Number(data.profit_factor).toFixed(2);
    result.textContent = `${data.total_trades} trades · ${data.wins} wins · ${data.losses} losses · Net P&L ${Number(data.net_profit).toFixed(2)} quote units · Max drawdown ${Number(data.max_drawdown).toFixed(2)} quote units · Win rate ${(Number(data.win_rate) * 100).toFixed(1)}% · Profit factor ${factor} · ${data.bars} candles. ${data.note}`;
  } catch (error) {
    result.textContent = `Backtest failed: ${error.message}`;
  } finally {
    button.disabled = false;
  }
}

async function connectAccount(event) {
  event.preventDefault();
  const login = Number(byId("mt5-login").value.trim());
  const server = byId("mt5-server").value.trim();
  const feedback = byId("connection-feedback");
  if (!Number.isSafeInteger(login) || login <= 0 || !server) {
    feedback.className = "connection-feedback error";
    feedback.textContent = "Enter a valid MT5 login number and broker server.";
    return;
  }

  const button = byId("connect-mt5");
  button.disabled = true;
  feedback.className = "connection-feedback";
  feedback.textContent = "Verifying the account reported by the MT5 terminal…";
  try {
    const response = await fetch("/api/mt5/connection", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ login, server }),
      cache: "no-store",
    });
    const data = await response.json();
    if (!response.ok) throw new Error(data.error || `Connection API returned HTTP ${response.status}`);
    feedback.textContent = data.message;
    await refresh();
    await loadObservations();
    await loadTradeEvents();
  } catch (error) {
    feedback.className = "connection-feedback error";
    feedback.textContent = `Could not connect to MT5: ${error.message}`;
    await refresh();
  } finally {
    button.disabled = false;
  }
}

async function disconnectAccount() {
  const button = byId("disconnect-mt5");
  button.disabled = true;
  try {
    const response = await fetch("/api/mt5/connection", {
      method: "DELETE",
      cache: "no-store",
    });
    const data = await response.json();
    if (!response.ok) throw new Error(data.error || `Connection API returned HTTP ${response.status}`);
    byId("mt5-login").value = "";
    byId("mt5-server").value = "";
    await refresh();
    await loadObservations();
    await loadTradeEvents();
  } catch (error) {
    const feedback = byId("connection-feedback");
    feedback.className = "connection-feedback error";
    feedback.textContent = `Could not disconnect from MT5: ${error.message}`;
  }
}

function getProfileLabel() {
  return localStorage.getItem(profileLabelKey) || "Not set";
}

function showProfileDialog() {
  const dialog = byId("profile-dialog");
  const savedLabel = localStorage.getItem(profileLabelKey);
  if (savedLabel) {
    setText("owner", `· Profile ${savedLabel}`);
    return;
  }
  dialog.showModal();
  byId("profile-label").focus();
}

byId("profile-form").addEventListener("submit", (event) => {
  event.preventDefault();
  const label = byId("profile-label").value.trim();
  if (!label) {
    setText("profile-error", "Enter a profile name to continue.");
    return;
  }
  localStorage.setItem(profileLabelKey, label);
  setText("profile-error", "");
  setText("owner", `· Profile ${label}`);
  byId("profile-dialog").close();
});

byId("refresh").addEventListener("click", async () => {
  await refresh();
  await loadTradeEvents();
});
byId("run-backtest").addEventListener("click", runBacktest);
byId("mt5-connection-form").addEventListener("submit", connectAccount);
byId("disconnect-mt5").addEventListener("click", disconnectAccount);
showProfileDialog();
refresh();
loadHistory();
loadObservations();
loadTradeEvents();
window.setInterval(() => {
  refresh();
  if (Date.now() - lastHistoryLoad >= 30000) loadHistory();
  if (Date.now() - lastObservationLoad >= 30000) loadObservations();
  if (Date.now() - lastTradeEventLoad >= 30000) loadTradeEvents();
}, 5000);
