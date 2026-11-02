// mini screeny – Waveshare ESP32-S3-LCD-1.69
// Pages (PWR): Claude buddy -> Kjell stock -> photo.  BOOT on buddy page: short = mute, long = recalibrate.
#include "board.h"
#include "imu.h"
#include "frame.h"
#include "buddy.h"
#include "sound.h"
#include "stock.h"
#include "shots.h"

#if __has_include("photo.h")
#include "photo.h"
#endif

Arduino_DataBus *bus = new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_CLK, LCD_DIN, GFX_NOT_DEFINED);
Arduino_GFX *gfx = new Arduino_ST7789(bus, LCD_RST, 0, true, SCREEN_W, SCREEN_H, 0, 20, 0, 20);
Arduino_Canvas *canvas = new Arduino_Canvas(SCREEN_W, SCREEN_H, gfx);

void backlight(uint8_t level) { ledcWrite(LEDC_BL, level); }

enum Page { PAGE_BUDDY, PAGE_SHOTS, PAGE_STOCK, PAGE_PHOTO, PAGE_COUNT };
static int page = PAGE_BUDDY;
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
  const uint16_t C_BG = g->color565(16, 13, 22), C_TEXT = g->color565(245, 240, 235), C_MUTED = g->color565(150, 140, 160);
  const uint16_t C_ORANGE = g->color565(217, 119, 87), C_INK = g->color565(32, 20, 18);
  g->fillScreen(C_BG);
  uint32_t ms = millis();
  if (calStep == 2) {
    g->setFont(u8g2_font_helvB14_tf); g->setTextColor(C_TEXT);
    centerOn(g, "Tack!", 120);
    g->setFont(u8g2_font_helvR10_tf); g->setTextColor(C_MUTED);
    centerOn(g, "Nu vet jag vad som är upp.", 148);
    g->fillRoundRect(96, 170, 48, 34, 9, C_ORANGE);
    g->fillRect(108, 180, 6, 10, C_INK); g->fillRect(126, 180, 6, 10, C_INK);
    return;
  }
  g->setFont(u8g2_font_helvR08_tf); g->setTextColor(C_MUTED);
  centerOn(g, calStep == 0 ? "KALIBRERING  1 / 2" : "KALIBRERING  2 / 2", 46);
  g->setFont(u8g2_font_helvB12_tf); g->setTextColor(C_TEXT);
  if (calStep == 0) { centerOn(g, "Lägg mig platt", 76); centerOn(g, "på bordet", 96); }
  else { centerOn(g, "Håll mig upprätt", 76); centerOn(g, "framför dig", 96); }

  // illustration
  if (calStep == 0) {
    g->fillRoundRect(40, 168, 160, 6, 3, C_MUTED);                 // table
    g->fillRoundRect(70, 152, 100, 14, 5, C_ORANGE);               // board lying flat
  } else {
    float bob = sinf(ms / 300.0f) * 3;
    g->fillRoundRect(95, 122 + bob, 50, 64, 10, C_ORANGE);         // board standing
    g->fillRect(108, 140 + bob, 6, 12, C_INK); g->fillRect(126, 140 + bob, 6, 12, C_INK);
  }

  g->setFont(u8g2_font_helvR10_tf); g->setTextColor(C_MUTED);
  centerOn(g, "...och tryck BOOT", 222);
  g->setFont(u8g2_font_helvR08_tf);
  centerOn(g, "(eller PWR)", 240);
  if (calCapturing) {
    g->fillRoundRect(24, 104, 192, 26, 12, g->color565(140, 220, 160));
    g->setFont(u8g2_font_helvB10_tf); g->setTextColor(C_INK);
    centerOn(g, "Håll still...", 122);
  } else if (calMsg && ms < calMsgUntil) {
    g->fillRoundRect(24, 104, 192, 26, 12, g->color565(255, 214, 120));
    g->setFont(u8g2_font_helvB10_tf); g->setTextColor(C_INK);
    centerOn(g, calMsg, 122);
  }
}

// ---------------- photo page ----------------
static void drawPhoto() {
#ifdef HAS_PHOTO
  gfx->draw16bitRGBBitmap(0, 0, (uint16_t *)PHOTO, SCREEN_W, SCREEN_H);
#else
  gfx->fillScreen(gfx->color565(8, 10, 16));
  gfx->setFont(u8g2_font_helvB14_tf); gfx->setTextColor(WHITE);
  gfx->setCursor(40, 130); gfx->print("Foto saknas");
#endif
}

static void showPage() {
  backlight(255);
  if (page == PAGE_STOCK) drawStock();
  else if (page == PAGE_PHOTO) drawPhoto();
}

// ---------------- serial test interface ----------------
// Commands (one per line):
//   sim 1|0                         take over the buddy with injected sensors (deterministic 30 fps steps)
//   sens gx gy gz wz wmag jerk jx jy ff still
//   step N                          advance N frames
//   dump                            raw RGB565 framebuffer: "FBDUMP 240 280\n" + 134400 bytes
//   page N | dbg | stat | calreset | calguess
static bool simMode = false;
static Sensors simS = {0, 1, 0, 0, 0, 0, 0, 0, 0, 0};
static char line[160]; static uint8_t lineLen = 0;

static bool canvasPage() { return page == PAGE_BUDDY || page == PAGE_SHOTS; }

static void renderBuddyFrame() {
  if (calStep >= 0) calDraw(canvas);
  else if (page == PAGE_SHOTS) shotsDraw(canvas);
  else buddyDraw(canvas);
  canvas->flush();
}

static void handleCommand(char *c) {
  if (!strncmp(c, "sim ", 4)) { simMode = atoi(c + 4); Serial.printf("ok sim=%d\n", simMode); }
  else if (!strncmp(c, "sens ", 5)) {
    Sensors &s = simS;
    int n = sscanf(c + 5, "%f %f %f %f %f %f %f %f %f %f", &s.gx, &s.gy, &s.gz, &s.wz, &s.wmag, &s.jerk, &s.jx, &s.jy, &s.freefallMs, &s.stillSec);
    Serial.printf("ok sens n=%d\n", n);
  } else if (!strncmp(c, "step ", 5)) {
    int n = atoi(c + 5);
    for (int i = 0; i < n; i++) {
      if (page == PAGE_SHOTS) shotsUpdate(simS, 1.0f / 30); else buddyUpdate(simS, 1.0f / 30);
      if (simS.stillSec > 0) simS.stillSec += 1.0f / 30;
    }
    renderBuddyFrame();
    static uint32_t nStep = 0;
    Serial.printf("ok step #%u %s\n", ++nStep, page == PAGE_SHOTS ? shotsDebug() : buddyDebug());
  } else if (!strcmp(c, "dump")) {
    if (!canvasOk) { Serial.println("err no canvas"); return; }
    Serial.printf("FBDUMP %d %d\n", SCREEN_W, SCREEN_H);
    Serial.write((uint8_t *)canvas->getFramebuffer(), SCREEN_W * SCREEN_H * 2);
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
  else if (!strcmp(c, "next")) { shotsNext(); Serial.println("ok next"); }
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
    if (ch == '\n' || ch == '\r') { if (lineLen) { line[lineLen] = 0; handleCommand(line); Serial.flush(); lineLen = 0; } }
    else if (lineLen < sizeof line - 1) line[lineLen++] = ch;
  }
}

// ---------------- setup / loop ----------------
void setup() {
  Serial.setTxBufferSize(4096);
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
  canvas->setUTF8Print(true);
  Serial.printf("canvas=%d heap=%u maxblk=%u\n", canvasOk, ESP.getFreeHeap(), ESP.getMaxAllocHeap());

  soundInit();
  imuBegin();
  if (!frameLoad()) calStart();
  buddyInit();
  shotsInit();
  stockBegin();
  showPage();
}

void loop() {
  soundUpdate();
  pollSerial();

  int p = pwrBtn.poll(), bt = bootBtn.poll();
  if (calStep == 0 || calStep == 1) {
    if (p == 1 || bt == 1) calConfirm();
  } else {
    if (p == 1) { page = (page + 1) % PAGE_COUNT; sfx::click(); showPage(); }
    if (page == PAGE_BUDDY && bt == 1) buddyBootShort();
    if (page == PAGE_SHOTS && bt == 1) shotsNext();
    if (page == PAGE_BUDDY && bt == 2) { calStart(); sfx::click(); }
  }
  if (calStep == 2 && millis() - calDoneAt > 1800) { calStep = -1; buddySay("Nu kör vi!", 2, 3); }

  if (page == PAGE_STOCK && stockConsumeDirty()) drawStock();

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
      if (page == PAGE_SHOTS) { shotsUpdate(s, dt); backlight(255); }
      else { buddyUpdate(s, dt); backlight(buddyAsleep() ? 40 : 255); }
    }
    renderBuddyFrame();

    static uint32_t frames = 0, nextLog = 0;
    frames++;
    if (now > nextLog) {
      Serial.printf("fps=%.1f heap=%u %s\n", frames / 5.0f, ESP.getFreeHeap(), page == PAGE_SHOTS ? shotsDebug() : buddyDebug());
      frames = 0; nextLog = now + 5000;
    }
  } else {
    delay(5);
  }
}
