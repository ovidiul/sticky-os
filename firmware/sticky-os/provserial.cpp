#include "provserial.h"

static ProvSerialHooks hooks;
static const char *PREFIX = "@cfg ";

// One write call per line: HardwareSerial holds the UART's lock for the
// whole of it, so no other task's log line lands inside. The leading
// newline ends any log line that was half printed (a print() before its
// println()), so the message always starts a line of its own.
void provSerialSend(const String &json) {
  String line;
  line.reserve(json.length() + 8);
  line = "\n";
  line += PREFIX;
  line += json;
  line += "\n";
  Serial.write((const uint8_t *)line.c_str(), line.length());
  Serial.flush();
}

void provSerialBegin(const ProvSerialHooks &h) { hooks = h; }

void provSerialBanner() {
  provSerialSend(String("{\"ready\":true,\"max\":") + (unsigned)(PROV_SERIAL_LINE - strlen(PREFIX)) +
                 ",\"reset\":" + (int)esp_reset_reason() + "," + provInfoFields(false) + "}");
}

void provSerialLine(const String &line) {
  if (!line.startsWith(PREFIX)) return;
  if (line.length() >= PROV_SERIAL_LINE) {  // cut short by the sketch's line limit
    provSerialSend("{\"id\":null,\"ok\":false,\"error\":\"framing: too large\"}");
    return;
  }
  ProvAfter after;
  String resp = provHandle(line.substring(strlen(PREFIX)), after);
  provSerialSend(resp);
  if (after == PROV_AFTER_COMMITTED || after == PROV_AFTER_RESET) {
    delay(300);  // the line gets out first
    if (hooks.after) hooks.after(after);
    else ESP.restart();
  }
}
