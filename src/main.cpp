// mini screeny – Waveshare ESP32-S3-LCD-1.69
// Pages (PWR): Claude buddy, stock, Claude quota (VibePulse), football team; which ones and in what
// order comes from config.toml on the Mac ("cfg" line), stored in NVS.  BOOT: three quick presses = settings page on the Mac; long press on the buddy page = recalibrate.
// No network on the board: tools/companion.py on the Mac fetches the data and sends it over USB serial.
#include "board.h"
#include <nvs_flash.h>
#include "imu.h"
#include "frame.h"
#include "buddy.h"
#include "sound.h"
#include "stock.h"
#include "pulse.h"
#include "football.h"
#include <Preferences.h>
#include <ArduinoJson.h>
#include "link.h"
#include "theme.h"

Arduino_DataBus *bus = new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_CLK, LCD_DIN, GFX_NOT_DEFINED);
Arduino_GFX *gfx = new Arduino_ST7789(bus, LCD_RST, 0, true, PANEL_W, PANEL_H, 0, 20, 0, 20);
// The panel stays at rotation 0; only the canvas is rotated (MOUNT_ROT), so flush() copies it 1:1.
Arduino_Canvas *canvas = new Arduino_Canvas(PANEL_W, PANEL_H, gfx);

void backlight(uint8_t level) { ledcWrite(LEDC_BL, level); }

enum Page { PAGE_BUDDY, PAGE_STOCK, PAGE_PULSE, PAGE_FOOTBALL, PAGE_COUNT };   // numbers = config.PAGES in tools/config.py
static int page = PAGE_BUDDY;

// ---------------- settings from config.toml (via the companion's "cfg" line), kept in NVS ----------------
static uint8_t order[PAGE_COUNT] = {PAGE_BUDDY, PAGE_STOCK, PAGE_PULSE, PAGE_FOOTBALL};
static uint8_t nOrder = PAGE_COUNT;
static char ownerName[24] = "";

static void settingsLoad() {
  Preferences p; p.begin("buddy", true);
  p.getString("name", ownerName, sizeof ownerName);
  uint8_t o[PAGE_COUNT]; size_t n = p.getBytesLength("pages");
  if (n >= 1 && n <= PAGE_COUNT && p.getBytes("pages", o, n) == n) {
    bool ok = true;
    for (size_t i = 0; i < n; i++) ok &= o[i] < PAGE_COUNT;
    if (ok) { memcpy(order, o, n); nOrder = n; }
  }
  p.end();
  buddySetName(ownerName);
}

static int orderIndex(int pg) {
  for (int i = 0; i < nOrder; i++) if (order[i] == pg) return i;
  return -1;
}
static bool canvasOk = false;

// ---------------- buttons ----------------
struct Button {
  int pin, idle;
  bool down = false, longFired = false;
  uint32_t t0 = 0;
  // returns 1 = short press (on release), 2 = long press (while held)
  int poll() {
    bool d = digitalRead(pin) != idle;
    uint32_t now = millis();
    if (d && !down) { down = true; longFired = false; t0 = now; }
    else if (d && down && !longFired && now - t0 > 1200) { longFired = true; return 2; }
    else if (!d && down) { down = false; if (!longFired && now - t0 > 25) return 1; }
    return 0;
  }
};
static Button pwrBtn, bootBtn;
static uint8_t bootClicks = 0; static uint32_t bootLast = 0;   // BOOT short presses being counted

// ---------------- calibration UI ----------------
static int calStep = -1;          // -1 = not calibrating, 0 = flat, 1 = upright, 2 = done screen
static float calU1[3];
static uint32_t calMsgUntil = 0, calDoneAt = 0;
static const char *calMsg = nullptr;
static bool calCapturing = false;
static uint32_t calCapStart = 0;
static float calSum[3]; static int calN = 0;
static float calLastUp[3] = {0, 0, 1};

static void calStart() { calStep = 0; calMsg = nullptr; }

// A press starts a short averaging window. It never rejects forever: after a timeout it uses what it has.
static void calConfirm() {
  if (calCapturing) return;
  calCapturing = true; calCapStart = millis();
  calSum[0] = calSum[1] = calSum[2] = 0; calN = 0;
  sfx::click();
}

static void calSample(const ImuSnap &s) {
  memcpy(calLastUp, s.up, sizeof calLastUp);
  if (!calCapturing) return;
  uint32_t el = millis() - calCapStart;
  if (el < 150) return;                       // let the button-press jolt settle
  float am = sqrtf(s.acc[0] * s.acc[0] + s.acc[1] * s.acc[1] + s.acc[2] * s.acc[2]);
  if (fabsf(am - 1.0f) < 0.15f) { for (int i = 0; i < 3; i++) calSum[i] += s.acc[i]; calN++; }
}

static void calFinish();

static void calTick() {
  if (!calCapturing) return;
  uint32_t el = millis() - calCapStart;
  if ((el > 750 && calN >= 8) || el > 2500) calFinish();
}

static void calFinish() {
  calCapturing = false;
  ImuSnap s;
  if (calN > 0) for (int i = 0; i < 3; i++) s.up[i] = calSum[i] / calN;
  else memcpy(s.up, calLastUp, sizeof s.up);
  Serial.printf("cal step %d: n=%d up=(%.2f %.2f %.2f)\n", calStep, calN, s.up[0], s.up[1], s.up[2]);
  if (calStep == 0) {
    memcpy(calU1, s.up, sizeof calU1);
    calStep = 1; calMsg = nullptr;
    sfx::click();
  } else if (calStep == 1) {
    if (frameFromCalibration(calU1, s.up)) {
      frameSave();
      calStep = 2; calDoneAt = millis();
      sfx::tada();
      Serial.printf("cal X=(%.2f %.2f %.2f) Y=(%.2f %.2f %.2f) Z=(%.2f %.2f %.2f)\n",
                    screenFrame.X[0], screenFrame.X[1], screenFrame.X[2], screenFrame.Y[0], screenFrame.Y[1],
                    screenFrame.Y[2], screenFrame.Z[0], screenFrame.Z[1], screenFrame.Z[2]);
    } else {
      calMsg = "Inte upprätt nog - igen!"; calMsgUntil = millis() + 2000;
    }
  }
}

static void centerOn(Arduino_GFX *g, const char *s, int y) {
  int16_t x1, y1; uint16_t w, h;
  g->getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  g->setCursor(SCREEN_W / 2 - w / 2 - x1, y);
  g->print(s);
}

static void calDraw(Arduino_GFX *g) {
  const uint16_t C_BG = theme::BG, C_TEXT = theme::TEXT, C_MUTED = theme::MUTED, C_ORANGE = theme::ACCENT, C_INK = theme::INK;
  g->fillScreen(C_BG);
  uint32_t ms = millis();
  if (calStep == 2) {
    g->setFont(u8g2_font_helvB14_tf); g->setTextColor(C_TEXT);
    const int cx = SCREEN_W / 2;
    centerOn(g, "Tack!", 98);
    g->setFont(u8g2_font_helvR10_tf); g->setTextColor(C_MUTED);
    centerOn(g, "Nu vet jag vad som är upp.", 126);
    g->fillRoundRect(cx - 24, 146, 48, 34, 9, C_ORANGE);
    g->fillRect(cx - 12, 156, 6, 10, C_INK); g->fillRect(cx + 6, 156, 6, 10, C_INK);
    return;
  }
  g->setFont(u8g2_font_helvR08_tf); g->setTextColor(C_MUTED);
  const int cx = SCREEN_W / 2;
  centerOn(g, calStep == 0 ? "KALIBRERING  1 / 2" : "KALIBRERING  2 / 2", 34);
  g->setFont(u8g2_font_helvB12_tf); g->setTextColor(C_TEXT);
  if (calStep == 0) { centerOn(g, "Lägg mig platt", 62); centerOn(g, "på bordet", 82); }
  else { centerOn(g, "Håll mig upprätt", 62); centerOn(g, "som jag sitter monterad", 82); }

  // illustration
  if (calStep == 0) {
    g->fillRoundRect(cx - 80, 160, 160, 6, 3, C_MUTED);            // table
    g->fillRoundRect(cx - 50, 144, 100, 14, 5, C_ORANGE);          // board lying flat
  } else {
    float bob = sinf(ms / 300.0f) * 3;
    g->fillRoundRect(cx - 32, 112 + bob, 64, 50, 10, C_ORANGE);    // board standing in its mount
    g->fillRect(cx - 12, 126 + bob, 6, 12, C_INK); g->fillRect(cx + 6, 126 + bob, 6, 12, C_INK);
    g->fillRoundRect(cx - 50, 164, 100, 4, 2, C_MUTED);            // floor
  }

  g->setFont(u8g2_font_helvR10_tf); g->setTextColor(C_MUTED);
  centerOn(g, "...och tryck BOOT", 196);
  g->setFont(u8g2_font_helvR08_tf);
  centerOn(g, "(eller PWR)", 213);
  if (calCapturing || (calMsg && ms < calMsgUntil)) {
    g->fillRoundRect(cx - 100, 92, 200, 26, 12, calCapturing ? g->color565(140, 220, 160) : g->color565(255, 214, 120));
    g->setFont(u8g2_font_helvB10_tf); g->setTextColor(C_INK);
    centerOn(g, calCapturing ? "Håll still..." : calMsg, 110);
  }
}

// ---------------- toast: a short message on top of any page ----------------
static char toastMsg[40] = "";
static uint32_t toastUntil = 0;
static bool toastShown = false;

static void drawToast(Arduino_GFX *g) {
  if (!toastMsg[0] || millis() >= toastUntil) return;
  g->setFont(u8g2_font_helvB10_tf);
  int16_t x1, y1; uint16_t w, h;
  g->getTextBounds(toastMsg, 0, 0, &x1, &y1, &w, &h);
  int bw = w + 32, bh = 34, bx = SCREEN_W / 2 - bw / 2, by = SCREEN_H - bh - 18;
  g->fillRoundRect(bx, by, bw, bh, 12, theme::TEXT);
  g->setTextColor(theme::INK);
  g->setCursor(SCREEN_W / 2 - w / 2 - x1, by + 22);
  g->print(toastMsg);
}

static void drawStockPage() {
  if (!canvasOk) return;
  drawStock(canvas);
  drawToast(canvas);
  canvas->flush();
}

static void drawFootball() {
  if (!canvasOk) return;
  footballDraw(canvas);
  drawToast(canvas);
  canvas->flush();
}

static void drawPulse() {
  if (!canvasOk) return;
  pulseDraw(canvas);
  drawToast(canvas);
  canvas->flush();
}

static void showPage() {
  backlight(255);
  if (page == PAGE_STOCK) drawStockPage();
  else if (page == PAGE_PULSE) drawPulse();
  else if (page == PAGE_FOOTBALL) drawFootball();
}

// ---------------- serial test interface ----------------
// Commands (one per line):
//   sim 1|0                         take over the buddy with injected sensors (deterministic 30 fps steps)
//   sens gx gy gz wz wmag jerk jx jy ff still
//   step N                          advance N frames
//   dump                            raw RGB565 panel framebuffer: "FBDUMP 240 280 <MOUNT_ROT>\n" + 134400 bytes
//   page N | dbg | stat | calreset | calguess
//   vp {json}                       VibePulse quota line from tools/companion.py
//   stk {json}                      Kjell stock quote (Avanza fields) from tools/companion.py
//   fb {json}                       football team table/next/last from tools/companion.py
//   cfg {json}                      {"name": owner, "pages": [page numbers in PWR order]} from config.toml
//   nvswipe                         erase the NVS partition, keep calibration + mute (one-off cleanup)
//   ping                            keep-alive from the companion (no reply)
// Board -> companion: "evt settings" (BOOT pressed three times) = open the settings page on the Mac
//   hello                           "ok hello mini-screeny ..." (the board also prints "mini-screeny boot" at start)
static bool simMode = false;
static Sensors simS = {0, 1, 0, 0, 0, 0, 0, 0, 0, 0};
static char line[768]; static uint16_t lineLen = 0;

static bool canvasPage() { return page == PAGE_BUDDY; }

static void renderBuddyFrame() {
  if (calStep >= 0) calDraw(canvas);
  else { buddyDraw(canvas); drawToast(canvas); }
  canvas->flush();
}

static void showPage();
static void toast(const char *msg, uint32_t ms) {
  strlcpy(toastMsg, msg, sizeof toastMsg);
  toastUntil = millis() + ms;
  toastShown = true;
  if (page != PAGE_BUDDY) showPage();   // data pages redraw only on demand
}

// BOOT pressed three times quickly: ask the companion on the Mac to open the settings page.
static void openSettingsOnMac() {
  if (linkAlive()) {
    Serial.println("evt settings");
    toast("Öppnar på datorn ...", 2500);
  } else {
    toast("Ingen kontakt med datorn", 2500);
  }
  sfx::click();
}

// "cfg {json}": owner name + page order. Written to NVS only when something changed (sent every minute).
static void applyConfig(const char *json) {
  JsonDocument d;
  if (deserializeJson(d, json)) { Serial.println("err cfg json"); return; }
  bool changed = false;
  const char *name = d["name"] | "";
  if (strcmp(name, ownerName)) {
    strlcpy(ownerName, name, sizeof ownerName);
    changed = true;
    if (buddySetName(ownerName) && page == PAGE_BUDDY) {
      char g[40]; snprintf(g, sizeof g, ownerName[0] ? "Hej %s!" : "Hej!", ownerName);
      buddySay(g, 2.5f, 2);
    }
  }
  JsonArray pa = d["pages"];
  uint8_t o[PAGE_COUNT]; uint8_t n = 0;
  for (JsonVariant v : pa) {
    int pg = v | -1;
    bool dup = false;
    for (int i = 0; i < n; i++) dup |= o[i] == pg;
    if (pg >= 0 && pg < PAGE_COUNT && !dup && n < PAGE_COUNT) o[n++] = pg;
  }
  if (n && (n != nOrder || memcmp(o, order, n))) {
    memcpy(order, o, n); nOrder = n; changed = true;
    if (orderIndex(page) < 0) { page = order[0]; showPage(); }
  }
  if (changed) {
    Preferences p; p.begin("buddy", false);
    p.putString("name", ownerName); p.putBytes("pages", order, nOrder);
    p.end();
  }
  Serial.printf("ok cfg name='%s' pages=%d%s\n", ownerName, nOrder, changed ? " (saved)" : "");
}

static void handleCommand(char *c) {
  if (!strncmp(c, "vp ", 3)) {
    if (pulseFeed(c + 3)) Serial.printf("ok %s\n", pulseDebug()); else Serial.println("err vp json");
  }
  else if (!strncmp(c, "fb ", 3)) {
    if (footballFeed(c + 3)) Serial.printf("ok %s\n", footballDebug()); else Serial.println("err fb json");
  }
  else if (!strncmp(c, "cfg ", 4)) applyConfig(c + 4);
  else if (!strncmp(c, "stk ", 4)) {
    if (stockFeed(c + 4)) Serial.printf("ok %s\n", stockDebug()); else Serial.println("err stk json");
  }
  else if (!strcmp(c, "nvswipe")) {
    // Physically erase the whole NVS partition (old firmware left the Wi-Fi hotspot credentials there;
    // an NVS erase only marks entries deleted), then write back the only things this firmware keeps.
    bool hadCal = screenFrame.valid, m = soundMuted();
    esp_err_t e1 = nvs_flash_deinit(), e2 = nvs_flash_erase(), e3 = nvs_flash_init();
    if (hadCal) frameSave();
    soundSetMuted(m);
    Serial.printf("ok nvswipe deinit=%d erase=%d init=%d cal=%d mute=%d\n", e1, e2, e3, hadCal, m);
  }
  else if (!strncmp(c, "bootclicks ", 11)) { bootClicks = atoi(c + 11); bootLast = millis(); Serial.println("ok bootclicks"); }   // test: N BOOT presses
  else if (!strcmp(c, "ping")) {}       // companion keep-alive: only marks contact (linkSeen), no reply
  else if (!strcmp(c, "hello")) Serial.printf("ok hello mini-screeny pages=buddy,stock,pulse,football up=%lus\n", millis() / 1000);
  else if (!strcmp(c, "vpdbg")) Serial.println(pulseDebug());
  else if (!strncmp(c, "sim ", 4)) { simMode = atoi(c + 4); Serial.printf("ok sim=%d\n", simMode); }
  else if (!strncmp(c, "sens ", 5)) {
    Sensors &s = simS;
    int n = sscanf(c + 5, "%f %f %f %f %f %f %f %f %f %f", &s.gx, &s.gy, &s.gz, &s.wz, &s.wmag, &s.jerk, &s.jx, &s.jy, &s.freefallMs, &s.stillSec);
    Serial.printf("ok sens n=%d\n", n);
  } else if (!strncmp(c, "step ", 5)) {
    int n = atoi(c + 5);
    for (int i = 0; i < n; i++) {
      buddyUpdate(simS, 1.0f / 30);
      if (simS.stillSec > 0) simS.stillSec += 1.0f / 30;
    }
    renderBuddyFrame();
    static uint32_t nStep = 0;
    Serial.printf("ok step #%u %s\n", ++nStep, buddyDebug());
  } else if (!strcmp(c, "dump")) {
    if (!canvasOk) { Serial.println("err no canvas"); return; }
    Serial.printf("FBDUMP %d %d %d\n", PANEL_W, PANEL_H, MOUNT_ROT);
    Serial.write((uint8_t *)canvas->getFramebuffer(), PANEL_W * PANEL_H * 2);
    Serial.flush();
    Serial.println("\nEND");
  } else if (!strncmp(c, "page ", 5)) { page = atoi(c + 5) % PAGE_COUNT; showPage(); Serial.println("ok"); }
  else if (!strcmp(c, "dbg")) Serial.println(buddyDebug());
  else if (!strncmp(c, "calfrom ", 8)) {
    float u1[3], u2[3];
    sscanf(c + 8, "%f %f %f %f %f %f", &u1[0], &u1[1], &u1[2], &u2[0], &u2[1], &u2[2]);
    bool ok = frameFromCalibration(u1, u2);
    ImuSnap sn = imuTake();
    Sensors se = sensorsFromImu(sn, 0);
    Serial.printf("ok calfrom=%d X=(%.2f %.2f %.2f) Y=(%.2f %.2f %.2f) Z=(%.2f %.2f %.2f) now: gx=%.2f gy=%.2f gz=%.2f\n", ok,
                  screenFrame.X[0], screenFrame.X[1], screenFrame.X[2], screenFrame.Y[0], screenFrame.Y[1], screenFrame.Y[2],
                  screenFrame.Z[0], screenFrame.Z[1], screenFrame.Z[2], se.gx, se.gy, se.gz);
  }
  else if (!strcmp(c, "calpress")) { if (calStep == 0 || calStep == 1) { calConfirm(); Serial.printf("ok calpress step=%d\n", calStep); } else Serial.println("err not calibrating"); }
  else if (!strcmp(c, "calstep")) Serial.printf("ok calstep=%d capturing=%d\n", calStep, calCapturing);
  else if (!strcmp(c, "calsave")) { frameSave(); Serial.println("ok saved"); }
  else if (!strcmp(c, "reboot")) { Serial.println("ok reboot"); Serial.flush(); delay(100); ESP.restart(); }
  else if (!strcmp(c, "reset")) { buddyInit(); Serial.println("ok reset"); }
  else if (!strcmp(c, "stat")) {
    Serial.printf("heap=%u maxblk=%u psram=%u canvas=%d cal=%d muted=%d\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap(),
                  ESP.getFreePsram(), canvasOk, screenFrame.valid, soundMuted());
  } else if (!strcmp(c, "calreset")) { frameClear(); calStart(); Serial.println("ok"); }
  else if (!strcmp(c, "calguess")) {
    // identity mapping, only for testing without a human
    const float X[3] = {1, 0, 0}, Y[3] = {0, 1, 0}, Z[3] = {0, 0, 1};
    frameSetDirect(X, Y, Z); calStep = -1; Serial.println("ok (not saved)");
  } else if (!strcmp(c, "imu")) {
    ImuSnap s = imuTake();
    Sensors se = sensorsFromImu(s, 0);
    Serial.printf("up=(%.2f %.2f %.2f) gyro=(%.1f %.1f %.1f) still=%.1f screen: gx=%.2f gy=%.2f gz=%.2f X=(%.2f %.2f %.2f)\n", s.up[0], s.up[1], s.up[2],
                  s.gyro[0], s.gyro[1], s.gyro[2], s.stillSec, se.gx, se.gy, se.gz, screenFrame.X[0], screenFrame.X[1], screenFrame.X[2]);
  } else Serial.printf("err unknown '%s'\n", c);
}

static void pollSerial() {
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\n' || ch == '\r') { if (lineLen) { line[lineLen] = 0; linkSeen(); handleCommand(line); Serial.flush(); lineLen = 0; } }
    else if (lineLen < sizeof line - 1) line[lineLen++] = ch;
  }
}

// ---------------- setup / loop ----------------
void setup() {
  Serial.setTxBufferSize(4096);
  Serial.setRxBufferSize(1024);   // "vp {json}" lines are ~300 bytes; the HWCDC default of 256 drops the tail
  Serial.begin(115200);

  pinMode(PWR_PIN, INPUT_PULLUP);
  pinMode(BOOT_PIN, INPUT_PULLUP);
  delay(30);
  pwrBtn.pin = PWR_PIN; pwrBtn.idle = digitalRead(PWR_PIN);
  bootBtn.pin = BOOT_PIN; bootBtn.idle = HIGH;

  ledcSetup(LEDC_BL, 5000, 8);
  ledcAttachPin(LCD_BL, LEDC_BL);
  backlight(255);
  if (!gfx->begin()) Serial.println("gfx->begin() failed");
  gfx->setUTF8Print(true);
  gfx->fillScreen(BLACK);
  canvasOk = canvas->begin(GFX_SKIP_OUTPUT_BEGIN);
  canvas->setRotation(MOUNT_ROT);
  canvas->setUTF8Print(true);
  canvas->setTextWrap(false);   // a long name is clipped at the edge instead of wrapping onto a new line
  Serial.printf("canvas=%d heap=%u maxblk=%u\n", canvasOk, ESP.getFreeHeap(), ESP.getMaxAllocHeap());

  soundInit();
  imuBegin();
  if (!frameLoad()) calStart();
  settingsLoad();
  page = order[0];
  buddyInit();
  Serial.println("mini-screeny boot");   // tools/companion.py resends its cached data when it sees this
  showPage();
}

void loop() {
  soundUpdate();
  pollSerial();

  int p = pwrBtn.poll(), bt = bootBtn.poll();
  if (calStep == 0 || calStep == 1) {
    if (p == 1 || bt == 1) calConfirm();
  } else {
    if (p == 1) { int i = orderIndex(page); page = order[(i + 1) % nOrder]; sfx::click(); showPage(); }
    // BOOT short presses are counted: three quickly = open the settings page on the Mac.
    if (bt == 1) { bootClicks++; bootLast = millis(); }
    if (bootClicks && millis() - bootLast > 420) {
      if (bootClicks >= 3) openSettingsOnMac();
      bootClicks = 0;
    }
    if (page == PAGE_BUDDY && bt == 2) { calStart(); sfx::click(); }
  }
  if (calStep == 2 && millis() - calDoneAt > 1800) { calStep = -1; buddySay("Nu kör vi!", 2, 3); }

  if (toastShown && millis() >= toastUntil) { toastShown = false; if (page != PAGE_BUDDY) showPage(); }
  if (page == PAGE_STOCK && stockConsumeDirty()) drawStockPage();
  if (page == PAGE_PULSE && pulseWantsDraw()) drawPulse();
  if (page == PAGE_FOOTBALL && footballConsumeDirty()) drawFootball();

  if (canvasPage() && canvasOk && !simMode) {
    static uint32_t last = millis();
    uint32_t now = millis();
    float dt = (now - last) / 1000.0f;
    last = now;
    ImuSnap snap = imuTake();
    calSample(snap);
    calTick();
    if (calStep < 0) {
      Sensors s = sensorsFromImu(snap, dt);
      buddyUpdate(s, dt);
      backlight(buddyAsleep() ? 40 : 255);
    }
    renderBuddyFrame();

    static uint32_t frames = 0, nextLog = 0;
    frames++;
    if (now > nextLog) {
      Serial.printf("fps=%.1f heap=%u %s\n", frames / 5.0f, ESP.getFreeHeap(), buddyDebug());
      frames = 0; nextLog = now + 5000;
    }
  } else {
    delay(5);
  }
}
