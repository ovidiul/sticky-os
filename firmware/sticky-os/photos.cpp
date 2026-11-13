#include "photos.h"

#include <dirent.h>
#include <vector>

#include "driver/sdspi_host.h"
#include "esp32s3/rom/tjpgd.h"
#include "esp_heap_caps.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#include "pins.h"
#include "settings.h"

static std::vector<String> files;  // full paths
static bool mounted;

bool photosCardIn() { return digitalRead(PIN_SD_DETECT) == LOW; }
int photosCount() { return files.size(); }

static void list(const char *dir) {
  DIR *d = opendir(dir);
  if (!d) return;
  while (dirent *e = readdir(d)) {
    String name = e->d_name;
    String lower = name;
    lower.toLowerCase();
    if (name.startsWith(".") || !(lower.endsWith(".jpg") || lower.endsWith(".jpeg"))) continue;
    if (files.size() < 500) files.push_back(String(dir) + "/" + name);
  }
  closedir(d);
  std::sort(files.begin(), files.end(), [](const String &a, const String &b) { return a < b; });
}

bool photosMount() {
  pinMode(PIN_SD_DETECT, INPUT_PULLUP);
  if (!mounted) {
    if (!photosCardIn()) {
      Serial.println("photos: no card in the slot");
      return false;
    }
    // The screen already set up SPI2; the card is one more device on it.
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;
    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs = (gpio_num_t)PIN_SD_CS;
    slot.host_id = SPI2_HOST;
    esp_vfs_fat_sdmmc_mount_config_t mount = {};
    mount.max_files = 4;
    sdmmc_card_t *card = nullptr;
    esp_err_t err = esp_vfs_fat_sdspi_mount("/sd", &host, &slot, &mount, &card);
    if (err != ESP_OK) {
      Serial.printf("photos: card mount failed: %s\n", esp_err_to_name(err));
      return false;
    }
    mounted = true;
  }
  files.clear();
  list(PHOTO_DIR);
  if (files.empty()) list("/sd");
  Serial.printf("photos: %d JPEG files\n", (int)files.size());
  return !files.empty();
}

// ---------------------------------------------------------------- decoding

namespace {
struct Job {
  FILE *file;
  uint8_t *grey;  // decoded picture, one byte a pixel
  int w, h;
};

UINT readJpeg(JDEC *jd, BYTE *buf, UINT len) {
  Job *job = (Job *)jd->device;
  if (buf) return fread(buf, 1, len, job->file);
  return fseek(job->file, len, SEEK_CUR) == 0 ? len : 0;
}

UINT storeBlock(JDEC *jd, void *bitmap, JRECT *r) {
  Job *job = (Job *)jd->device;
  const uint8_t *rgb = (const uint8_t *)bitmap;
  for (int y = r->top; y <= r->bottom; y++)
    for (int x = r->left; x <= r->right; x++, rgb += 3)
      if (x < job->w && y < job->h)
        job->grey[y * job->w + x] = (rgb[0] * 77 + rgb[1] * 150 + rgb[2] * 29) >> 8;
  return 1;
}
}  // namespace

bool photoRender(int index, uint8_t *out, int w, int h, char *name, size_t nameSize,
                 const char **why) {
  static const char *none = "";
  if (!why) why = &none;
  if (index < 0 || index >= (int)files.size()) return *why = "no such photo", false;
  const String &path = files[index];
  strlcpy(name, path.c_str() + path.lastIndexOf('/') + 1, nameSize);

  Job job = {};
  job.file = fopen(path.c_str(), "rb");
  if (!job.file) return *why = "can't open the file", false;

  static uint8_t work[4096];  // the decoder's own workspace
  JDEC jd;
  JRESULT res = jd_prepare(&jd, readJpeg, work, sizeof work, &job);
  if (res != JDR_OK) {
    fclose(job.file);
    return *why = res == JDR_FMT3 ? "progressive JPEG: save it as baseline" : "not a JPEG this board can read", false;
  }

  // The decoder can shrink by 2, 4 or 8 as it goes: take the most that still
  // leaves enough pixels to fill the area.
  int scale = 0;
  while (scale < 3 && (int)(jd.width >> (scale + 1)) >= w && (int)(jd.height >> (scale + 1)) >= h) scale++;
  job.w = jd.width >> scale;
  job.h = jd.height >> scale;
  if ((size_t)job.w * job.h > 6u * 1024 * 1024) {
    fclose(job.file);
    return *why = "picture too large", false;
  }
  job.grey = (uint8_t *)heap_caps_malloc((size_t)job.w * job.h, MALLOC_CAP_SPIRAM);
  if (!job.grey) {
    fclose(job.file);
    return *why = "out of memory", false;
  }
  res = jd_decomp(&jd, storeBlock, scale);
  fclose(job.file);
  if (res != JDR_OK) {
    heap_caps_free(job.grey);
    return *why = "the JPEG data is damaged", false;
  }

  // Source pixels per output pixel, in 16.16 fixed point. Landscape fills
  // (the larger zoom, cropped); portrait fits (the smaller zoom, white bars).
  uint32_t stepX = ((uint32_t)job.w << 16) / w, stepY = ((uint32_t)job.h << 16) / h;
  bool portrait = job.h > job.w;
  uint32_t step = portrait ? max(stepX, stepY) : min(stepX, stepY);
  int shownW = min<int64_t>(w, ((int64_t)job.w << 16) / step), shownH = min<int64_t>(h, ((int64_t)job.h << 16) / step);
  int offX = (w - shownW) / 2, offY = (h - shownH) / 2;                    // bars, when fitting
  int cropX = (job.w - (int)(((int64_t)shownW * step) >> 16)) / 2;          // crop, when filling
  int cropY = (job.h - (int)(((int64_t)shownH * step) >> 16)) / 2;

  // Floyd-Steinberg to four levels, a row at a time.
  int16_t *err = (int16_t *)calloc((w + 2) * 2, sizeof(int16_t));
  if (!err) {
    heap_caps_free(job.grey);
    return *why = "out of memory", false;
  }
  int16_t *cur = err + 1, *next = err + w + 3;
  memset(out, 0xFF, (size_t)w * h / 4);
  for (int y = 0; y < h; y++) {
    memset(next - 1, 0, (w + 2) * sizeof(int16_t));
    bool rowIn = y >= offY && y < offY + shownH;
    int sy = min(job.h - 1, cropY + (int)(((int64_t)(y - offY) * step) >> 16));
    for (int x = 0; x < w; x++) {
      if (!rowIn || x < offX || x >= offX + shownW) continue;  // bar: stays white
      int sx = min(job.w - 1, cropX + (int)(((int64_t)(x - offX) * step) >> 16));
      int want = constrain(job.grey[sy * job.w + sx] + cur[x] / 16, 0, 255);
      int level = (want + 42) / 85;  // 0..3
      int miss = want - level * 85;
      cur[x + 1] += miss * 7;
      next[x - 1] += miss * 3;
      next[x] += miss * 5;
      next[x + 1] += miss;
      uint8_t &b = out[y * (w / 4) + x / 4];
      int shift = (3 - (x & 3)) * 2;
      b = (b & ~(3 << shift)) | (level << shift);
    }
    std::swap(cur, next);
  }
  free(err);
  heap_caps_free(job.grey);
  return true;
}
