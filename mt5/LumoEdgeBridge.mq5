#property strict
#property version   "1.00"
#property description "Risk-gated MT5 market and account bridge for NEXUS"

#include <Trade/Trade.mqh>

input string InpEndpoint = "http://127.0.0.1:8080/api/mt5/snapshot";
input string InpTradeEventEndpoint = "http://127.0.0.1:8080/api/mt5/trade-event";
input string InpSymbol = "XAUUSD";
input string InpSymbolAliases = "XAUUSD,XAUUSDm,GOLD";
input ENUM_TIMEFRAMES InpTimeframe = PERIOD_M5;
input int InpHistoryCount = 500;
input int InpIntervalSeconds = 5;
input string InpBridgeToken = "";
input bool InpEnableOrderExecution = false;
input bool InpAllowLiveTrading = false;
input ulong InpMagicNumber = 260012;
input int InpDeviationPoints = 20;

CTrade g_trade;

string JsonEscape(const string value)
{
   string escaped = "";
   for(int i = 0; i < StringLen(value); ++i)
   {
      ushort ch = StringGetCharacter(value, i);
      if(ch == '\\')
         escaped += "\\\\";
      else if(ch == '"')
         escaped += "\\\"";
      else if(ch == '\n')
         escaped += "\\n";
      else if(ch == '\r')
         escaped += "\\r";
      else if(ch == '\t')
         escaped += "\\t";
      else if(ch < 32)
         escaped += StringFormat("\\u%04x", ch);
      else
         escaped += ShortToString(ch);
   }
   return escaped;
}

string Uppercase(const string value)
{
   string result = value;
   StringToUpper(result);
   return result;
}

string ResolveSymbol()
{
   string aliases[];
   int alias_count = StringSplit(
      InpSymbolAliases, StringGetCharacter(",", 0), aliases);
   if(alias_count < 0)
      alias_count = 0;
   string candidates[];
   ArrayResize(candidates, alias_count + 1);
   candidates[0] = InpSymbol;
   for(int i = 0; i < alias_count; ++i)
      candidates[i + 1] = aliases[i];

   int symbol_count = SymbolsTotal(false);
   for(int candidate = 0; candidate < ArraySize(candidates); ++candidate)
   {
      for(int index = 0; index < symbol_count; ++index)
      {
         string name = SymbolName(index, false);
         if(candidates[candidate] != "" &&
            Uppercase(name) == Uppercase(candidates[candidate]))
            return name;
      }
   }

   string prefix_matches[];
   ArrayResize(prefix_matches, 0);
   for(int candidate = 0; candidate < ArraySize(candidates); ++candidate)
   {
      string prefix = Uppercase(candidates[candidate]);
      if(prefix == "")
         continue;
      for(int index = 0; index < symbol_count; ++index)
      {
         string name = SymbolName(index, false);
         if(StringSubstr(Uppercase(name), 0, StringLen(prefix)) == prefix)
         {
            bool already_counted = false;
            for(int match = 0; match < ArraySize(prefix_matches); ++match)
            {
               if(Uppercase(prefix_matches[match]) == Uppercase(name))
               {
                  already_counted = true;
                  break;
               }
            }
            if(!already_counted)
            {
               int match_count = ArraySize(prefix_matches);
               ArrayResize(prefix_matches, match_count + 1);
               prefix_matches[match_count] = name;
               if(ArraySize(prefix_matches) > 1)
                  return "";
            }
         }
      }
   }
   return ArraySize(prefix_matches) == 1 ? prefix_matches[0] : "";
}

string JsonStringValue(const string json, const string key)
{
   const string needle = "\"" + key + "\":\"";
   int start = StringFind(json, needle);
   if(start < 0)
      return "";
   start += StringLen(needle);
   int finish = StringFind(json, "\"", start);
   if(finish < 0)
      return "";
   return StringSubstr(json, start, finish - start);
}

double JsonNumberValue(const string json, const string key, bool &found)
{
   const string needle = "\"" + key + "\":";
   int start = StringFind(json, needle);
   found = start >= 0;
   if(!found)
      return 0.0;
   start += StringLen(needle);
   int finish = start;
   while(finish < StringLen(json))
   {
      ushort ch = StringGetCharacter(json, finish);
      if(ch == ',' || ch == '}' || ch == ']' || ch == ' ' ||
         ch == '\r' || ch == '\n' || ch == '\t')
         break;
      ++finish;
   }
   if(finish == start)
   {
      found = false;
      return 0.0;
   }
   return StringToDouble(StringSubstr(json, start, finish - start));
}

int CountDailyEntries()
{
   const datetime now = TimeCurrent();
   const datetime day_start = StringToTime(TimeToString(now, TIME_DATE));
   if(!HistorySelect(day_start, now))
   {
      PrintFormat("NEXUS bridge: could not select daily trade history (error %d)",
                  GetLastError());
      return -1;
   }

   ulong positions[];
   int count = 0;
   for(int index = 0; index < HistoryDealsTotal(); ++index)
   {
      const ulong deal = HistoryDealGetTicket(index);
      if(deal == 0 ||
         (ulong)HistoryDealGetInteger(deal, DEAL_MAGIC) != InpMagicNumber)
         continue;
      const ENUM_DEAL_ENTRY entry =
         (ENUM_DEAL_ENTRY)HistoryDealGetInteger(deal, DEAL_ENTRY);
      if(entry != DEAL_ENTRY_IN && entry != DEAL_ENTRY_INOUT)
         continue;
      const ulong position_id =
         (ulong)HistoryDealGetInteger(deal, DEAL_POSITION_ID);
      bool already_counted = false;
      for(int existing = 0; existing < ArraySize(positions); ++existing)
      {
         if(positions[existing] == position_id)
         {
            already_counted = true;
            break;
         }
      }
      if(already_counted)
         continue;
      const int new_size = ArrayResize(positions, ArraySize(positions) + 1);
      if(new_size < 0)
      {
         Print("NEXUS bridge: could not allocate daily trade counter");
         return -1;
      }
      positions[new_size - 1] = position_id;
      ++count;
   }
   return count;
}

double CurrentRiskAtStops()
{
   double risk = 0.0;
   for(int index = 0; index < PositionsTotal(); ++index)
   {
      const ulong ticket = PositionGetTicket(index);
      if(ticket == 0)
         return -1.0;
      const string symbol = PositionGetString(POSITION_SYMBOL);
      const double volume = PositionGetDouble(POSITION_VOLUME);
      const double entry = PositionGetDouble(POSITION_PRICE_OPEN);
      const double stop_loss = PositionGetDouble(POSITION_SL);
      if(stop_loss <= 0.0)
         return -1.0;
      const ENUM_POSITION_TYPE position_type =
         (ENUM_POSITION_TYPE)PositionGetInteger(POSITION_TYPE);
      const ENUM_ORDER_TYPE order_type =
         position_type == POSITION_TYPE_BUY ? ORDER_TYPE_BUY : ORDER_TYPE_SELL;
      double profit_at_stop = 0.0;
      if(!OrderCalcProfit(order_type, symbol, volume, entry, stop_loss,
                          profit_at_stop))
         return -1.0;
      if(profit_at_stop < 0.0)
         risk += MathAbs(profit_at_stop);
   }
   return risk;
}

void CloseManagedPositions()
{
   for(int index = PositionsTotal() - 1; index >= 0; --index)
   {
      const ulong ticket = PositionGetTicket(index);
      if(ticket == 0 ||
         (ulong)PositionGetInteger(POSITION_MAGIC) != InpMagicNumber)
         continue;
      if(!g_trade.PositionClose(ticket))
      {
         PrintFormat("NEXUS bridge: close failed for position %I64u (%u: %s)",
                     ticket, g_trade.ResultRetcode(),
                     g_trade.ResultRetcodeDescription());
      }
   }
}

int VolumeDigits(const double step)
{
   for(int digits = 0; digits <= 8; ++digits)
   {
      if(MathAbs(NormalizeDouble(step, digits) - step) < 1e-10)
         return digits;
   }
   return 8;
}

void OpenManagedPosition(const string symbol, const string direction,
                         const double stop_loss, const double take_profit,
                         const double risk_per_trade,
                         const double max_open_risk_percent)
{
   const ENUM_ACCOUNT_TRADE_MODE mode =
      (ENUM_ACCOUNT_TRADE_MODE)AccountInfoInteger(ACCOUNT_TRADE_MODE);
   if(!InpEnableOrderExecution ||
      (mode == ACCOUNT_TRADE_MODE_REAL && !InpAllowLiveTrading) ||
      (mode != ACCOUNT_TRADE_MODE_DEMO && mode != ACCOUNT_TRADE_MODE_REAL))
   {
      Print("NEXUS bridge: order execution is disabled for this account");
      return;
   }

   const bool is_buy = direction == "BUY";
   if(!is_buy && direction != "SELL")
      return;
   MqlTick tick;
   if(!SymbolInfoTick(symbol, tick))
   {
      PrintFormat("NEXUS bridge: order quote unavailable for %s (error %d)",
                  symbol, GetLastError());
      return;
   }

   const double entry = is_buy ? tick.ask : tick.bid;
   const double point = SymbolInfoDouble(symbol, SYMBOL_POINT);
   const long stops_level = SymbolInfoInteger(symbol, SYMBOL_TRADE_STOPS_LEVEL);
   const double minimum_distance = (double)stops_level * point;
   if(entry <= 0.0 || point <= 0.0 ||
      (is_buy && (stop_loss >= entry || take_profit <= entry)) ||
      (!is_buy && (stop_loss <= entry || take_profit >= entry)) ||
      MathAbs(entry - stop_loss) < minimum_distance ||
      MathAbs(take_profit - entry) < minimum_distance)
   {
      Print("NEXUS bridge: order prices are stale or violate broker stop distance");
      return;
   }

   const double equity = AccountInfoDouble(ACCOUNT_EQUITY);
   const double current_risk = CurrentRiskAtStops();
   if(equity <= 0.0 || current_risk < 0.0)
   {
      Print("NEXUS bridge: existing account risk could not be verified");
      return;
   }
   const double remaining_risk =
      equity * max_open_risk_percent - current_risk;
   const double risk_budget =
      MathMin(equity * risk_per_trade, remaining_risk);
   if(risk_budget <= 0.0)
   {
      Print("NEXUS bridge: total open-risk limit leaves no room for another trade");
      return;
   }

   const ENUM_ORDER_TYPE order_type =
      is_buy ? ORDER_TYPE_BUY : ORDER_TYPE_SELL;
   double one_lot_loss = 0.0;
   if(!OrderCalcProfit(order_type, symbol, 1.0, entry, stop_loss,
                       one_lot_loss) ||
      one_lot_loss >= 0.0)
   {
      Print("NEXUS bridge: broker could not calculate risk for the proposed stop");
      return;
   }

   const double volume_min = SymbolInfoDouble(symbol, SYMBOL_VOLUME_MIN);
   const double volume_max = SymbolInfoDouble(symbol, SYMBOL_VOLUME_MAX);
   const double volume_step = SymbolInfoDouble(symbol, SYMBOL_VOLUME_STEP);
   if(volume_min <= 0.0 || volume_max < volume_min || volume_step <= 0.0)
   {
      Print("NEXUS bridge: broker volume constraints are invalid");
      return;
   }
   double volume =
      MathMin(volume_max, MathFloor(risk_budget / MathAbs(one_lot_loss) /
                                    volume_step) * volume_step);
   volume = NormalizeDouble(volume, VolumeDigits(volume_step));
   if(volume < volume_min)
   {
      Print("NEXUS bridge: minimum broker volume would exceed the risk budget");
      return;
   }
   double actual_loss = 0.0;
   if(!OrderCalcProfit(order_type, symbol, volume, entry, stop_loss,
                       actual_loss) ||
      actual_loss >= 0.0 ||
      current_risk + MathAbs(actual_loss) >
          equity * max_open_risk_percent + 0.01)
   {
      Print("NEXUS bridge: calculated order exceeds the total open-risk limit");
      return;
   }

   g_trade.SetExpertMagicNumber(InpMagicNumber);
   g_trade.SetDeviationInPoints(InpDeviationPoints);
   g_trade.SetTypeFillingBySymbol(symbol);
   const bool sent = is_buy
      ? g_trade.Buy(volume, symbol, 0.0, stop_loss, take_profit, "NEXUS auto")
      : g_trade.Sell(volume, symbol, 0.0, stop_loss, take_profit, "NEXUS auto");
   if(!sent || (g_trade.ResultRetcode() != TRADE_RETCODE_DONE &&
                g_trade.ResultRetcode() != TRADE_RETCODE_DONE_PARTIAL &&
                g_trade.ResultRetcode() != TRADE_RETCODE_PLACED))
   {
      PrintFormat("NEXUS bridge: order rejected (%u: %s)",
                  g_trade.ResultRetcode(),
                  g_trade.ResultRetcodeDescription());
      return;
   }
   PrintFormat("NEXUS bridge: %s %s %.2f lots submitted with broker SL/TP",
               direction, symbol, volume);
}

void ExecuteDecision(const string response, const string symbol)
{
   if(!InpEnableOrderExecution)
      return;
   const string action = JsonStringValue(response, "action");
   if(action == "CLOSE_ALL")
   {
      CloseManagedPositions();
      return;
   }
   if(action != "OPEN_BUY" && action != "OPEN_SELL")
      return;

   bool has_stop = false;
   bool has_target = false;
   bool has_risk = false;
   bool has_open_risk = false;
   const double stop_loss = JsonNumberValue(response, "stop_loss", has_stop);
   const double take_profit = JsonNumberValue(response, "take_profit", has_target);
   const double risk_per_trade =
      JsonNumberValue(response, "risk_per_trade", has_risk);
   const double max_open_risk =
      JsonNumberValue(response, "max_open_risk_percent", has_open_risk);
   if(!has_stop || !has_target || !has_risk || !has_open_risk)
   {
      Print("NEXUS bridge: server trade instruction was incomplete");
      return;
   }
   OpenManagedPosition(symbol, action == "OPEN_BUY" ? "BUY" : "SELL",
                       stop_loss, take_profit, risk_per_trade,
                       max_open_risk);
}

void SendTradeEvent(const ulong deal)
{
   if(deal == 0 || !HistoryDealSelect(deal))
      return;
   if((ulong)HistoryDealGetInteger(deal, DEAL_MAGIC) != InpMagicNumber)
      return;

   const ENUM_DEAL_TYPE deal_type =
      (ENUM_DEAL_TYPE)HistoryDealGetInteger(deal, DEAL_TYPE);
   if(deal_type != DEAL_TYPE_BUY && deal_type != DEAL_TYPE_SELL)
      return;
   const ENUM_DEAL_ENTRY entry =
      (ENUM_DEAL_ENTRY)HistoryDealGetInteger(deal, DEAL_ENTRY);
   const string status =
      entry == DEAL_ENTRY_IN ? "OPENED" :
      entry == DEAL_ENTRY_OUT || entry == DEAL_ENTRY_OUT_BY ? "CLOSED" :
      entry == DEAL_ENTRY_INOUT ? "REVERSED" : "FILL";
   const string direction = deal_type == DEAL_TYPE_BUY ? "BUY" : "SELL";
   const ulong order = (ulong)HistoryDealGetInteger(deal, DEAL_ORDER);
   double stop_loss = 0.0;
   double take_profit = 0.0;
   if(order > 0 && HistoryOrderSelect(order))
   {
      stop_loss = HistoryOrderGetDouble(order, ORDER_SL);
      take_profit = HistoryOrderGetDouble(order, ORDER_TP);
   }
   const string symbol = HistoryDealGetString(deal, DEAL_SYMBOL);
   const string server = AccountInfoString(ACCOUNT_SERVER);
   const long login = AccountInfoInteger(ACCOUNT_LOGIN);
   const long position_id = HistoryDealGetInteger(deal, DEAL_POSITION_ID);
   const long deal_time = HistoryDealGetInteger(deal, DEAL_TIME);
   const double price = HistoryDealGetDouble(deal, DEAL_PRICE);
   const double volume = HistoryDealGetDouble(deal, DEAL_VOLUME);
   const double pnl = HistoryDealGetDouble(deal, DEAL_PROFIT) +
      HistoryDealGetDouble(deal, DEAL_COMMISSION) +
      HistoryDealGetDouble(deal, DEAL_SWAP) +
      HistoryDealGetDouble(deal, DEAL_FEE);
   string payload =
      "{\"ticket\":" + IntegerToString((long)deal) +
      ",\"position_id\":" + IntegerToString(position_id) +
      ",\"order_id\":" + IntegerToString((long)order) +
      ",\"login\":" + IntegerToString(login) +
      ",\"server\":\"" + JsonEscape(server) +
      "\",\"symbol\":\"" + JsonEscape(symbol) +
      "\",\"direction\":\"" + direction +
      "\",\"status\":\"" + status +
      "\",\"price\":" + DoubleToString(price, 10) +
      ",\"stop_loss\":" + DoubleToString(stop_loss, 10) +
      ",\"take_profit\":" + DoubleToString(take_profit, 10) +
      ",\"volume\":" + DoubleToString(volume, 8) +
      ",\"pnl\":" + DoubleToString(pnl, 8) +
      ",\"event_time\":" + IntegerToString(deal_time) +
      ",\"magic\":" + IntegerToString((long)InpMagicNumber) + "}";

   char request_data[];
   int data_size = StringToCharArray(payload, request_data, 0, WHOLE_ARRAY, CP_UTF8);
   if(data_size <= 1)
   {
      Print("NEXUS bridge: failed to encode trade event");
      return;
   }
   ArrayResize(request_data, data_size - 1);
   char response_data[];
   string response_headers;
   const int response_code = WebRequest(
      "POST", InpTradeEventEndpoint,
      "Content-Type: application/json\r\nAuthorization: Bearer " +
         InpBridgeToken + "\r\n",
      5000, request_data, response_data, response_headers);
   if(response_code < 200 || response_code >= 300)
      PrintFormat("NEXUS bridge: trade event logging returned HTTP %d (error %d)",
                  response_code, GetLastError());
}

bool SendSnapshot()
{
   string symbol = ResolveSymbol();
   if(symbol == "")
   {
      PrintFormat("NEXUS bridge: no unique broker symbol matches %s; "
                  "configure InpSymbol or InpSymbolAliases explicitly",
                  InpSymbol);
      return false;
   }
   if(!SymbolSelect(symbol, true))
   {
      PrintFormat("NEXUS bridge: could not select broker symbol %s (error %d)",
                  symbol, GetLastError());
      return false;
   }

   string timeframe = EnumToString(InpTimeframe);
   if(StringFind(timeframe, "PERIOD_") == 0)
      timeframe = StringSubstr(timeframe, 7);

   MqlTick tick;
   if(!SymbolInfoTick(symbol, tick))
   {
      PrintFormat("NEXUS bridge: quote unavailable for %s (error %d)",
                  symbol, GetLastError());
      return false;
   }

   double point = SymbolInfoDouble(symbol, SYMBOL_POINT);
   long digits = SymbolInfoInteger(symbol, SYMBOL_DIGITS);
   if(point <= 0.0 || tick.bid <= 0.0 || tick.ask < tick.bid)
   {
      PrintFormat("NEXUS bridge: invalid quote metadata for %s", symbol);
      return false;
   }

   MqlRates rates[];
   ArraySetAsSeries(rates, true);
   int copied = CopyRates(symbol, InpTimeframe, 1, InpHistoryCount, rates);
   if(copied < 250)
   {
      if(copied <= 0)
         PrintFormat("NEXUS bridge: need at least 250 closed candles for %s; "
                     "received %d (error %d)",
                     symbol, copied, GetLastError());
      else
         PrintFormat("NEXUS bridge: need at least 250 closed candles for %s; "
                     "received %d",
                     symbol, copied);
      return false;
   }

   const double balance = AccountInfoDouble(ACCOUNT_BALANCE);
   double today_deals_pnl = 0.0;
   const datetime now = TimeCurrent();
   const datetime day_start = StringToTime(TimeToString(now, TIME_DATE));
   if(!HistorySelect(day_start, now))
   {
      PrintFormat("NEXUS bridge: daily deal history unavailable (error %d)",
                  GetLastError());
      return false;
   }
   for(int i = 0; i < HistoryDealsTotal(); ++i)
   {
      ulong deal = HistoryDealGetTicket(i);
      if(deal == 0)
      {
         PrintFormat("NEXUS bridge: could not read daily deal %d (error %d)",
                     i, GetLastError());
         return false;
      }
      const ENUM_DEAL_TYPE deal_type =
         (ENUM_DEAL_TYPE)HistoryDealGetInteger(deal, DEAL_TYPE);
      if(deal_type != DEAL_TYPE_BUY &&
         deal_type != DEAL_TYPE_SELL &&
         deal_type != DEAL_TYPE_COMMISSION &&
         deal_type != DEAL_TYPE_COMMISSION_DAILY &&
         deal_type != DEAL_TYPE_COMMISSION_MONTHLY &&
         deal_type != DEAL_TYPE_COMMISSION_AGENT_DAILY &&
         deal_type != DEAL_TYPE_COMMISSION_AGENT_MONTHLY)
         continue;
      today_deals_pnl += HistoryDealGetDouble(deal, DEAL_PROFIT);
      today_deals_pnl += HistoryDealGetDouble(deal, DEAL_COMMISSION);
      today_deals_pnl += HistoryDealGetDouble(deal, DEAL_SWAP);
      today_deals_pnl += HistoryDealGetDouble(deal, DEAL_FEE);
   }
   const double day_start_balance = balance - today_deals_pnl;

   int open_positions = 0;
   int positions_without_stop = 0;
   int positions_risk_unavailable = 0;
   double floating_pnl = 0.0;
   double open_volume = 0.0;
   double risk_at_stops = 0.0;
   for(int i = 0; i < PositionsTotal(); ++i)
   {
      ulong ticket = PositionGetTicket(i);
      if(ticket == 0)
      {
         PrintFormat("NEXUS bridge: could not read open position %d (error %d)",
                     i, GetLastError());
         return false;
      }
      ++open_positions;
      const string position_symbol = PositionGetString(POSITION_SYMBOL);
      const double volume = PositionGetDouble(POSITION_VOLUME);
      const double open_price = PositionGetDouble(POSITION_PRICE_OPEN);
      const double stop_loss = PositionGetDouble(POSITION_SL);
      const double profit = PositionGetDouble(POSITION_PROFIT);
      const double swap = PositionGetDouble(POSITION_SWAP);
      floating_pnl += profit + swap;
      open_volume += volume;
      if(stop_loss <= 0.0)
      {
         ++positions_without_stop;
         continue;
      }
      const ENUM_POSITION_TYPE position_type =
         (ENUM_POSITION_TYPE)PositionGetInteger(POSITION_TYPE);
      const ENUM_ORDER_TYPE order_type =
         position_type == POSITION_TYPE_BUY ? ORDER_TYPE_BUY : ORDER_TYPE_SELL;
      double profit_at_stop = 0.0;
      if(!OrderCalcProfit(order_type, position_symbol, volume, open_price,
                          stop_loss, profit_at_stop))
      {
         ++positions_risk_unavailable;
      }
      else if(profit_at_stop < 0.0)
      {
         risk_at_stops += MathAbs(profit_at_stop);
      }
   }

   const double margin = AccountInfoDouble(ACCOUNT_MARGIN);
   const double equity = AccountInfoDouble(ACCOUNT_EQUITY);
   const double free_margin = AccountInfoDouble(ACCOUNT_MARGIN_FREE);
   const int daily_trade_count = CountDailyEntries();
   const ENUM_ACCOUNT_TRADE_MODE account_mode =
      (ENUM_ACCOUNT_TRADE_MODE)AccountInfoInteger(ACCOUNT_TRADE_MODE);
   const bool execution_enabled =
      InpEnableOrderExecution && daily_trade_count >= 0 &&
      (account_mode == ACCOUNT_TRADE_MODE_DEMO ||
       (account_mode == ACCOUNT_TRADE_MODE_REAL && InpAllowLiveTrading));
   string payload = "{\"symbol\":\"" + JsonEscape(symbol) +
      "\",\"timeframe\":\"" + JsonEscape(timeframe) +
      "\",\"execution_enabled\":" +
      (execution_enabled ? "true" : "false") +
      ",\"account\":{\"server\":\"" +
      JsonEscape(AccountInfoString(ACCOUNT_SERVER)) +
      "\",\"currency\":\"" + JsonEscape(AccountInfoString(ACCOUNT_CURRENCY)) +
      "\",\"login\":" + IntegerToString((long)AccountInfoInteger(ACCOUNT_LOGIN)) +
      ",\"balance\":" + DoubleToString(balance, 2) +
      ",\"equity\":" + DoubleToString(equity, 2) +
      ",\"margin\":" + DoubleToString(margin, 2) +
      ",\"free_margin\":" + DoubleToString(free_margin, 2) +
      ",\"day_start_balance\":" + DoubleToString(day_start_balance, 2) +
      ",\"day_pnl\":" + DoubleToString(today_deals_pnl + floating_pnl, 2) +
      ",\"floating_pnl\":" + DoubleToString(floating_pnl, 2) +
      ",\"open_volume\":" + DoubleToString(open_volume, 2) +
      ",\"risk_at_stops\":" + DoubleToString(risk_at_stops, 2) +
      ",\"open_positions\":" + IntegerToString(open_positions) +
      ",\"positions_without_stop\":" + IntegerToString(positions_without_stop) +
      ",\"positions_risk_unavailable\":" + IntegerToString(positions_risk_unavailable) +
      ",\"daily_trade_count\":" +
      IntegerToString(daily_trade_count < 0 ? 0 : daily_trade_count) +
      ",\"trade_mode\":" + IntegerToString((int)AccountInfoInteger(ACCOUNT_TRADE_MODE)) +
      ",\"leverage\":" + IntegerToString((int)AccountInfoInteger(ACCOUNT_LEVERAGE)) +
      "},\"quote\":{\"bid\":" + DoubleToString(tick.bid, (int)digits) +
      ",\"ask\":" + DoubleToString(tick.ask, (int)digits) +
      ",\"point\":" + DoubleToString(point, 10) +
      ",\"digits\":" + IntegerToString((int)digits) +
      ",\"time\":" + IntegerToString((long)(tick.time_msc > 0 ? tick.time_msc / 1000 : tick.time)) +
      "},\"candles\":[";

   for(int i = copied - 1; i >= 0; --i)
   {
      if(i != copied - 1)
         payload += ",";
      payload += "{\"time\":" + IntegerToString((long)rates[i].time) +
         ",\"open\":" + DoubleToString(rates[i].open, (int)digits) +
         ",\"high\":" + DoubleToString(rates[i].high, (int)digits) +
         ",\"low\":" + DoubleToString(rates[i].low, (int)digits) +
         ",\"close\":" + DoubleToString(rates[i].close, (int)digits) +
         ",\"tick_volume\":" + IntegerToString((long)rates[i].tick_volume) + "}";
   }
   payload += "]}";

   char request_data[];
   int data_size = StringToCharArray(payload, request_data, 0, WHOLE_ARRAY, CP_UTF8);
   if(data_size <= 1)
   {
      Print("NEXUS bridge: failed to encode snapshot");
      return false;
   }
   ArrayResize(request_data, data_size - 1);

   char response_data[];
   string response_headers;
   ResetLastError();
   int response_code = WebRequest(
      "POST", InpEndpoint,
      "Content-Type: application/json\r\nAuthorization: Bearer " +
         InpBridgeToken + "\r\n",
      5000,
      request_data, response_data, response_headers);
   if(response_code < 200 || response_code >= 300)
   {
      PrintFormat("NEXUS bridge: dashboard returned HTTP %d (error %d)",
                  response_code, GetLastError());
      if(ArraySize(response_data) > 0)
         Print("NEXUS bridge: ", CharArrayToString(response_data, 0, -1, CP_UTF8));
      return false;
   }
   ExecuteDecision(CharArrayToString(response_data, 0, -1, CP_UTF8), symbol);
   return true;
}

int OnInit()
{
   if(InpHistoryCount < 250 || InpHistoryCount > 2000 || InpIntervalSeconds < 1)
   {
      Print("NEXUS bridge: history count must be 250-2000 and update interval positive");
      return INIT_PARAMETERS_INCORRECT;
   }
   EventSetTimer(InpIntervalSeconds);
   g_trade.SetExpertMagicNumber(InpMagicNumber);
   g_trade.SetDeviationInPoints(InpDeviationPoints);
   SendSnapshot();
   return INIT_SUCCEEDED;
}

void OnDeinit(const int reason)
{
   EventKillTimer();
}

void OnTimer()
{
   SendSnapshot();
}

void OnTradeTransaction(const MqlTradeTransaction& transaction,
                        const MqlTradeRequest& request,
                        const MqlTradeResult& result)
{
   if(transaction.type == TRADE_TRANSACTION_DEAL_ADD)
      SendTradeEvent(transaction.deal);
}
