# Vendored drivers

Copied unchanged, MIT (see LICENSE):

- the e-paper panel component (SSD1677 / SSD2677 / UC8179), from
  https://github.com/limengdu/reterminal_sticky_handwriting_pad, commit c01e2d6;
- the GT911 touch driver and app_debug_log.h, from Seeed's
  https://github.com/Seeed-Projects/sticky-esp-idf-dashboard, commit 1a28b2a.
  Its reset holds INT low 60 ms before letting go, which the controller needs
  before it reports touches; the older copy skipped that and never saw one. They use ESP-IDF
APIs only (spi_master, i2c_master), which arduino-esp32 3.x ships.
