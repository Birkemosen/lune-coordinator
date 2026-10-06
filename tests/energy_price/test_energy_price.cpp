// Host tests for energy_price.h (all-in electricity price pushed to Odin 2.0).
#include "energy_price.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

using namespace lune_touch_price;

static bool near(double a, double b, double eps = 1e-4) { return std::fabs(a - b) < eps; }

static std::string spot_body_for_day(int ymd, int hours, double base_dkk_mwh, bool repeat_hour2) {
  // 15-minute records with local TimeDK strings, like DayAheadPrices.
  std::string s = "{\"total\":96,\"dataset\":\"DayAheadPrices\",\"records\":[";
  bool first = true;
  char date[16];
  format_ymd(ymd, date, sizeof(date));
  auto add = [&](int h, int q, double dkk) {
    char rec[160];
    std::snprintf(rec, sizeof(rec),
                  "%s{\"TimeDK\":\"%sT%02d:%02d:00\",\"DayAheadPriceDKK\":%.6f,\"DayAheadPriceEUR\":%.6f}",
                  first ? "" : ",", date, h, q * 15, dkk, dkk / 7.4625);
    s += rec;
    first = false;
  };
  for (int h = 0; h < 24; h++) {
    if (hours == 23 && h == 2) continue;  // spring: 02:00 does not exist
    for (int q = 0; q < 4; q++) add(h, q, base_dkk_mwh + h * 10 + q);
    if (repeat_hour2 && h == 2)
      for (int q = 0; q < 4; q++) add(h, q, base_dkk_mwh + 1000 + q);  // the second 02:xx
  }
  s += "]}";
  return s;
}

int main() {
  // --- dates -----------------------------------------------------------------
  assert(add_days(20261006, 1) == 20261007);
  assert(add_days(20261231, 1) == 20270101);
  assert(add_days(20280228, 1) == 20280229);
  assert(add_days(20260301, -1) == 20260228);
  int ymd = 0, hour = 0, minute = 0;
  assert(parse_iso_local("2026-10-06T17:45:00", &ymd, &hour, &minute));
  assert(ymd == 20261006 && hour == 17 && minute == 45);
  assert(!parse_iso_local("garbage", &ymd, &hour, &minute));
  assert(hour_key(20261006, 7) == 2026100607LL);

  // --- schedule parse / expand ----------------------------------------------
  Schedule s{};
  assert(parse_schedule("[{\"h\":0,\"v\":0.077},{\"h\":6,\"v\":0.231},{\"h\":17,\"v\":0.692},{\"h\":21,\"v\":0.231}]",
                        &s));
  assert(s.count == 4);
  float g[HOURS];
  assert(expand_schedule(s, g));
  assert(near(g[0], 0.077) && near(g[5], 0.077) && near(g[6], 0.231) && near(g[16], 0.231));
  assert(near(g[17], 0.692) && near(g[20], 0.692) && near(g[21], 0.231) && near(g[23], 0.231));
  // Default schedule equals Nettarif C.
  float d[HOURS];
  assert(expand_schedule(default_grid_schedule(), d));
  for (size_t h = 0; h < HOURS; h++) assert(near(d[h], g[h]));
  // Round trip.
  char buf[512];
  assert(format_schedule(s, buf, sizeof(buf)) > 0);
  Schedule back{};
  assert(parse_schedule(buf, &back) && back.count == 4 && near(back.blocks[2].v, 0.692));
  // Unsorted input, no h=0: the last block wraps past midnight.
  Schedule wrap{};
  assert(parse_schedule(" [ {\"h\": 21, \"v\": 0.1}, {\"h\":6,\"v\":\"0.3\"} ] ", &wrap));
  assert(wrap.count == 2 && wrap.blocks[0].h == 6);
  float w[HOURS];
  assert(expand_schedule(wrap, w));
  assert(near(w[0], 0.1) && near(w[5], 0.1) && near(w[6], 0.3) && near(w[20], 0.3) && near(w[23], 0.1));
  // Single block = flat.
  Schedule flat{};
  assert(parse_schedule("[{\"h\":0,\"v\":0.2}]", &flat));
  assert(expand_schedule(flat, w) && near(w[0], 0.2) && near(w[23], 0.2));
  // Rejections.
  Schedule bad{};
  assert(!parse_schedule("", &bad));
  assert(!parse_schedule("[]", &bad));
  assert(!parse_schedule("[{\"h\":24,\"v\":0.1}]", &bad));
  assert(!parse_schedule("[{\"h\":3,\"v\":0.1},{\"h\":3,\"v\":0.2}]", &bad));
  assert(!parse_schedule("[{\"h\":3}]", &bad));
  assert(!parse_schedule("[{\"h\":3,\"v\":99}]", &bad));
  assert(!parse_schedule("[{\"h\":1.5,\"v\":0.1}]", &bad));

  // --- spot: quarter → hour --------------------------------------------------
  static SpotSample samples[MAX_SPOT_SAMPLES];
  {
    const std::string body = spot_body_for_day(20261006, 24, 1000.0, false);
    const size_t n = parse_spot_records(body.c_str(), body.size(), samples, MAX_SPOT_SAMPLES);
    assert(n == 96);
    const SpotDay day = aggregate_spot_day(samples, n, 20261006);
    assert(day.complete && day.hours_with_data == 24);
    // Quarters base+h*10+{0,1,2,3} → mean base+h*10+1.5 DKK/MWh.
    assert(near(day.dkk_kwh[0], 1.0015) && near(day.dkk_kwh[17], 1.1715));
    assert(near(spot_fx(samples, n), 7.4625, 1e-3));
    // Tomorrow is absent from the body.
    assert(!aggregate_spot_day(samples, n, 20261007).complete);
  }
  {
    // Spring DST (23 h): no 02:00 — copied from 01:00, day still complete.
    const std::string body = spot_body_for_day(20260329, 23, 500.0, false);
    const size_t n = parse_spot_records(body.c_str(), body.size(), samples, MAX_SPOT_SAMPLES);
    assert(n == 92);
    const SpotDay day = aggregate_spot_day(samples, n, 20260329);
    assert(day.complete && day.hours_with_data == 23);
    assert(near(day.dkk_kwh[2], day.dkk_kwh[1]));
  }
  {
    // Autumn DST (25 h): 02:xx twice — all 8 quarters average into hour 2.
    const std::string body = spot_body_for_day(20261025, 24, 500.0, true);
    const size_t n = parse_spot_records(body.c_str(), body.size(), samples, MAX_SPOT_SAMPLES);
    assert(n == 100);
    const SpotDay day = aggregate_spot_day(samples, n, 20261025);
    assert(day.complete);
    // (520+521+522+523 + 1500+1501+1502+1503)/8 = 1011.5 DKK/MWh
    assert(near(day.dkk_kwh[2], 1.0115));
  }
  {
    // Half a day is not enough; nulls are skipped; cap is respected.
    const char *partial =
        "{\"records\":[{\"TimeDK\":\"2026-10-06T00:00:00\",\"DayAheadPriceDKK\":100,\"DayAheadPriceEUR\":13.4},"
        "{\"TimeDK\":\"2026-10-06T01:00:00\",\"DayAheadPriceDKK\":null,\"DayAheadPriceEUR\":null}]}";
    const size_t n = parse_spot_records(partial, std::strlen(partial), samples, MAX_SPOT_SAMPLES);
    assert(n == 1);
    assert(!aggregate_spot_day(samples, n, 20261006).complete);
    assert(parse_spot_records(partial, std::strlen(partial), samples, 0) == 0);
    assert(parse_spot_records("{\"error\":\"x\"}", 13, samples, MAX_SPOT_SAMPLES) == 0);
  }

  // --- DataHub record selection ----------------------------------------------
  {
    // Shape of a real DatahubPricelist answer (sort=ValidFrom desc), trimmed.
    const char *body =
        "{\"total\":3,\"records\":["
        "{\"ChargeTypeCode\":\"TNT1009\",\"ValidFrom\":\"2026-10-01T00:00:00\",\"ValidTo\":\"2027-01-01T00:00:00\","
        "\"Price1\":0.0769,\"Price2\":0.0769,\"Price3\":0.0769,\"Price4\":0.0769,\"Price5\":0.0769,\"Price6\":0.0769,"
        "\"Price7\":0.2307,\"Price8\":0.2307,\"Price9\":0.2307,\"Price10\":0.2307,\"Price11\":0.2307,\"Price12\":0.2307,"
        "\"Price13\":0.2307,\"Price14\":0.2307,\"Price15\":0.2307,\"Price16\":0.2307,\"Price17\":0.2307,"
        "\"Price18\":0.6922,\"Price19\":0.6922,\"Price20\":0.6922,\"Price21\":0.6922,"
        "\"Price22\":0.2307,\"Price23\":0.2307,\"Price24\":0.2307},"
        "{\"ChargeTypeCode\":\"TNT1009\",\"ValidFrom\":\"2026-04-01T00:00:00\",\"ValidTo\":\"2026-10-01T00:00:00\","
        "\"Price1\":0.0638,\"Price2\":null,\"Price24\":null},"
        "{\"ChargeTypeCode\":\"TNT1009\",\"ValidFrom\":\"2026-01-01T00:00:00\",\"ValidTo\":null,"
        "\"Price1\":0.5,\"Price2\":null}"
        "]}";
    TariffSeries ts{};
    assert(parse_tariff_records(body, std::strlen(body), "TNT1009", &ts) == 3);
    float t[HOURS];
    // 2026-10-06 → the October record (hourly shape).
    assert(tariff_for_day(ts, 20261006, t));
    assert(near(t[0], 0.0769) && near(t[6], 0.2307) && near(t[17], 0.6922) && near(t[20], 0.6922) &&
           near(t[21], 0.2307));
    // 2026-09-30 → April record beats the open-ended January one (later ValidFrom); flat.
    assert(tariff_for_day(ts, 20260930, t));
    assert(near(t[0], 0.0638) && near(t[23], 0.0638));
    // 2026-02-01 → only the open-ended record is valid.
    assert(tariff_for_day(ts, 20260201, t) && near(t[12], 0.5));
    // 2025-12-31 → nothing valid.
    assert(!tariff_for_day(ts, 20251231, t));
    // Code filter drops other charge codes.
    TariffSeries none{};
    assert(parse_tariff_records(body, std::strlen(body), "40000", &none) == 0);
    assert(!tariff_for_day(none, 20261006, t));
  }
  {
    // Same ValidFrom twice (re-issued list): the first (most recent in the API order) wins.
    const char *body =
        "{\"records\":[{\"ChargeTypeCode\":\"40000\",\"ValidFrom\":\"2026-01-01T00:00:00\",\"ValidTo\":null,"
        "\"Price1\":0.043},{\"ChargeTypeCode\":\"40000\",\"ValidFrom\":\"2026-01-01T00:00:00\","
        "\"ValidTo\":\"2027-01-01T00:00:00\",\"Price1\":0.099},"
        "{\"ChargeTypeCode\":\"41000\",\"ValidFrom\":\"2026-01-01T00:00:00\",\"ValidTo\":\"2027-01-01T00:00:00\","
        "\"Price1\":0.072}]}";
    TariffSeries a{}, b{};
    assert(parse_tariff_records(body, std::strlen(body), "40000", &a) == 2);
    assert(parse_tariff_records(body, std::strlen(body), "41000", &b) == 1);
    float ta[HOURS], tb[HOURS];
    assert(tariff_for_day(a, 20261006, ta) && tariff_for_day(b, 20261006, tb));
    assert(near(ta[5], 0.043) && near(ta[5] + tb[5], 0.115));
  }

  // --- all-in formula --------------------------------------------------------
  Config cfg{};
  // spot 1.07 + tariff 0.077 + energinet 0.115 + elafgift 0.008 + markup 0 = 1.270 DKK, × 1.25 VAT
  assert(near(all_in_dkk(1.07f, 0.077f, 0.115f, cfg), 1.5875));
  cfg.markup_dkk = 0.05f;
  cfg.vat_pct = 0.0f;
  assert(near(all_in_dkk(1.07f, 0.077f, 0.115f, cfg), 1.32));
  cfg = Config{};
  // Negative spot is allowed (the all-in can go below the taxes).
  assert(near(all_in_dkk(-0.2f, 0.0f, 0.0f, cfg), (-0.2 + 0.008) * 1.25));

  // € conversion, 4 decimals.
  assert(near(to_eur(1.5875f, 7.4625f), 0.2127));
  assert(std::isnan(to_eur(1.0f, 0.0f)));

  // --- day + Odin array -----------------------------------------------------
  float spot[HOURS], grid[HOURS], en[HOURS];
  for (size_t h = 0; h < HOURS; h++) { spot[h] = 1.07f; en[h] = 0.115f; }
  expand_schedule(default_grid_schedule(), grid);
  const DayPrices today = build_day(20261006, spot, grid, en, cfg);
  assert(today.valid && near(today.total[0], 1.5875) && near(today.total[18], (1.07 + 0.692 + 0.115 + 0.008) * 1.25));
  DayPrices tomorrow{};
  float out[48];
  assert(build_odin_array(today, tomorrow, 7.4625f, out, 48) == 24);
  tomorrow = build_day(20261007, spot, grid, en, cfg);
  assert(build_odin_array(today, tomorrow, 7.4625f, out, 48) == 48);
  assert(near(out[0], 0.2127) && near(out[24], 0.2127));
  assert(build_odin_array(DayPrices{}, tomorrow, 7.4625f, out, 48) == 0);
  // A missing component invalidates the day.
  spot[3] = NAN;
  assert(!build_day(20261006, spot, grid, en, cfg).valid);
  char payload[600];
  assert(format_odin_payload(out, 48, payload, sizeof(payload)) > 0);
  assert(std::strncmp(payload, "{\"prices\":[0.2127,", 18) == 0);
  assert(payload[std::strlen(payload) - 1] == '}');
  assert(format_odin_payload(out, 48, payload, 40) == 0);

  // --- scheduler -------------------------------------------------------------
  SchedulerState st{};
  // Disabled / no clock.
  assert(push_due(false, true, true, 20261006, 600, st) == Due::NONE);
  assert(push_due(true, false, false, 20261006, 600, st) == Due::NONE);
  assert(push_due(true, true, false, 20261006, 600, st) == Due::REQUEST);
  // Boot: push at once.
  assert(push_due(true, false, true, 20261006, 600, st) == Due::INITIAL);
  st.attempt_ymd = 20261006; st.attempt_min = 600; st.attempt_ok = true;
  st.pushed_ymd = 20261006; st.pushed_tomorrow = false;
  assert(push_due(true, false, true, 20261006, 700, st) == Due::NONE);
  // 14:15: day-ahead, then every 30 min until tomorrow is in.
  assert(push_due(true, false, true, 20261006, 855, st) == Due::DAY_AHEAD);
  st.attempt_min = 855;
  assert(push_due(true, false, true, 20261006, 870, st) == Due::NONE);
  assert(push_due(true, false, true, 20261006, 885, st) == Due::DAY_AHEAD);
  st.attempt_min = 885; st.pushed_tomorrow = true;
  assert(push_due(true, false, true, 20261006, 1000, st) == Due::NONE);
  // No tomorrow by 20:00 → give up for today.
  st.pushed_tomorrow = false; st.attempt_min = 1190;
  assert(push_due(true, false, true, 20261006, 1230, st) == Due::NONE);
  // Midnight: nothing before 00:05, then a new-day push.
  assert(push_due(true, false, true, 20261007, 2, st) == Due::NONE);
  assert(push_due(true, false, true, 20261007, 5, st) == Due::NEW_DAY);
  // Failed new-day push retries after 30 min.
  st.attempt_ymd = 20261007; st.attempt_min = 5; st.attempt_ok = false;
  assert(push_due(true, false, true, 20261007, 20, st) == Due::NONE);
  assert(push_due(true, false, true, 20261007, 35, st) == Due::RETRY);
  // A boot shortly after midnight pushes immediately, too.
  SchedulerState fresh{};
  assert(push_due(true, false, true, 20261007, 1, fresh) == Due::INITIAL);
  assert(minutes_since_attempt(st, 20261008, 10) == 10 + 1440 - 5);

  // --- validation helpers ----------------------------------------------------
  assert(valid_gln("5790000610976") && !valid_gln("57900006109") && !valid_gln("57900006109x6"));
  assert(valid_charge_code("TNT1009") && valid_charge_code("40000") && !valid_charge_code("a\"b") &&
         !valid_charge_code(""));
  Area area{};
  assert(parse_area("DK2", &area) && area == Area::DK2 && !parse_area("SE3", &area));

  std::printf("energy_price: all tests passed\n");
  return 0;
}
