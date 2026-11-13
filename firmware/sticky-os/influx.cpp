#include "influx.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "config.h"
#include "i18n.h"
#include "net.h"
#include <memory>

// One CSV row into fields (quoted fields may hold commas and "" quotes).
static int splitCsv(const String &line, String *f, int max) {
  int n = 0, i = 0, len = line.length();
  while (n < max) {
    String field;
    if (i < len && line[i] == '"') {
      for (i++; i < len; i++) {
        if (line[i] != '"') field += line[i];
        else if (i + 1 < len && line[i + 1] == '"') field += '"', i++;
        else { i++; break; }
      }
    }
    while (i < len && line[i] != ',') field += line[i++];
    f[n++] = field;
    if (i >= len) break;
    i++;  // the comma
  }
  return n;
}

bool fetchInflux(CalList &out, String &error) {
  // The writer fills all slots every 10 min; an empty title is an empty
  // slot. Nothing newer than 1 h means it stopped running.
  String flux =
      "from(bucket: \"" + cfg::str("influx.bucket") + "\") |> range(start: -1h)"
      " |> filter(fn: (r) => r._measurement == \"calendar\") |> last()"
      " |> pivot(rowKey: [\"slot\"], columnKey: [\"_field\"], valueColumn: \"_value\")"
      " |> group() |> filter(fn: (r) => r.title != \"\") |> sort(columns: [\"start\"])";
  static const char *const COLS[] = {"title", "where", "start", "end", "all_day"};
  HTTPClient http;
  http.setTimeout(10000);
  http.setConnectTimeout(NET_CONNECT_MS);
  String url = cfg::str("influx.url");
  if (!influxConfigured()) return error = T(S_N_INFLUX_NONE), false;
  while (url.endsWith("/")) url.remove(url.length() - 1);
  url += "/api/v2/query?org=" + cfg::str("influx.org");
  WiFiClientSecure tls;  // for an https:// URL: your own CA (mqtt.ca) when it's set, else the public ones
  bool ok;
  NetTls *use = nullptr;
  if (url.startsWith("https://")) {
    netTrust(tls, true);  // and the 8 s handshake
    use = new NetTls;
    tls.setTimeout(10000);
    ok = http.begin(tls, url);
  } else {
    ok = http.begin(url);
  }
  std::unique_ptr<NetTls> held(use);
  if (!ok) return error = T(S_N_INFLUX_BAD_ADDRESS), false;
  http.addHeader("Authorization", "Token " + cfg::str("influx.token"));
  http.addHeader("Content-Type", "application/vnd.flux");
  http.addHeader("Accept", "application/csv");
  int code = http.POST(flux);
  if (code != 200) {
    Serial.printf("calendar: InfluxDB answered %d (%s)\n", code, code < 0 ? HTTPClient::errorToString(code).c_str() : "");
    error = netFailText("InfluxDB", code, "token");
    http.end();
    return false;
  }
  String body;
  bool read = netReadBody(http, body, 256 * 1024);
  http.end();
  if (!read) return error = T(S_N_INFLUX_CUT), false;

  // Annotated CSV: "#..." rows, a header row naming the columns, data rows.
  CalList events;
  const int MAXF = 16;
  String f[MAXF];
  int idx[5];
  bool header = false;
  int from = 0;
  while (from < (int)body.length()) {
    int nl = body.indexOf('\n', from);
    if (nl < 0) nl = body.length();
    String line = body.substring(from, nl);
    from = nl + 1;
    line.trim();
    if (line.isEmpty() || line[0] == '#') continue;
    int nf = splitCsv(line, f, MAXF);
    if (!header || f[1] == "result") {  // a header row (one per table)
      for (int c = 0; c < 5; c++) {
        idx[c] = -1;
        for (int i = 0; i < nf; i++)
          if (f[i] == COLS[c]) idx[c] = i;
        if (idx[c] < 0) return error = T(S_N_INFLUX_NO_COLUMNS), false;
      }
      header = true;
      continue;
    }
    int need = 0;
    for (int c = 0; c < 5; c++) need = max(need, idx[c] + 1);
    if (nf < need) continue;  // a short row: skip it rather than read another row's fields
    CalEvent e;
    calSetText(e.title, sizeof e.title, f[idx[0]]);
    calSetText(e.where, sizeof e.where, f[idx[1]]);
    e.start = strtoul(f[idx[2]].c_str(), nullptr, 10);
    e.end = strtoul(f[idx[3]].c_str(), nullptr, 10);
    e.allDay = f[idx[4]] == "true";
    if (events.size() < (size_t)CAL_MAX_EVENTS) events.push_back(e);
  }
  out.swap(events);  // no header: no rows -- no events, or not set up yet
  error = "";
  return true;
}
