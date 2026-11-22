#property strict
#property version   "1.00"
#property description "Read-only MT5 market and account data relay for Lumo Edge"

input string InpEndpoint = "http://127.0.0.1:8080/api/mt5/snapshot";
input string InpSymbol = "XAUUSD";
input string InpSymbolAliases = "XAUUSD,XAUUSDm,GOLD";
input ENUM_TIMEFRAMES InpTimeframe = PERIOD_M5;
input int InpHistoryCount = 500;
input int InpIntervalSeconds = 5;
input string InpBridgeToken = "";

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

bool SendSnapshot()
{
   string symbol = ResolveSymbol();
   if(symbol == "")
   {
      PrintFormat("Lumo Edge bridge: no unique broker symbol matches %s; "
                  "configure InpSymbol or InpSymbolAliases explicitly",
                  InpSymbol);
      return false;
   }
   if(!SymbolSelect(symbol, true))
   {
      PrintFormat("Lumo Edge bridge: could not select broker symbol %s (error %d)",
                  symbol, GetLastError());
      return false;
   }

   string timeframe = EnumToString(InpTimeframe);
   if(StringFind(timeframe, "PERIOD_") == 0)
      timeframe = StringSubstr(timeframe, 7);

   MqlTick tick;
   if(!SymbolInfoTick(symbol, tick))
   {
      PrintFormat("Lumo Edge bridge: quote unavailable for %s (error %d)",
                  symbol, GetLastError());
      return false;
   }

   double point = SymbolInfoDouble(symbol, SYMBOL_POINT);
   long digits = SymbolInfoInteger(symbol, SYMBOL_DIGITS);
   if(point <= 0.0 || tick.bid <= 0.0 || tick.ask < tick.bid)
   {
      PrintFormat("Lumo Edge bridge: invalid quote metadata for %s", symbol);
      return false;
   }

   MqlRates rates[];
   ArraySetAsSeries(rates, true);
   int copied = CopyRates(symbol, InpTimeframe, 1, InpHistoryCount, rates);
   if(copied < 250)
   {
      if(copied <= 0)
         PrintFormat("Lumo Edge bridge: need at least 250 closed candles for %s; "
                     "received %d (error %d)",
                     symbol, copied, GetLastError());
      else
         PrintFormat("Lumo Edge bridge: need at least 250 closed candles for %s; "
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
      PrintFormat("Lumo Edge bridge: daily deal history unavailable (error %d)",
                  GetLastError());
      return false;
   }
   for(int i = 0; i < HistoryDealsTotal(); ++i)
   {
      ulong deal = HistoryDealGetTicket(i);
      if(deal == 0)
      {
         PrintFormat("Lumo Edge bridge: could not read daily deal %d (error %d)",
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
         PrintFormat("Lumo Edge bridge: could not read open position %d (error %d)",
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
   string payload = "{\"symbol\":\"" + JsonEscape(symbol) +
      "\",\"timeframe\":\"" + JsonEscape(timeframe) +
      "\",\"account\":{\"server\":\"" +
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
      Print("Lumo Edge bridge: failed to encode snapshot");
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
      PrintFormat("Lumo Edge bridge: dashboard returned HTTP %d (error %d)",
                  response_code, GetLastError());
      if(ArraySize(response_data) > 0)
         Print("Lumo Edge bridge: ", CharArrayToString(response_data, 0, -1, CP_UTF8));
      return false;
   }
   return true;
}

int OnInit()
{
   if(InpHistoryCount < 250 || InpHistoryCount > 2000 || InpIntervalSeconds < 1)
   {
      Print("Lumo Edge bridge: history count must be 250-2000 and update interval positive");
      return INIT_PARAMETERS_INCORRECT;
   }
   EventSetTimer(InpIntervalSeconds);
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
