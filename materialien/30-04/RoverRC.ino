// ============================================================
// ESP32-CAM Rover – Fernsteuerung über Serial + Kamera-Offset
// Steuerung: w=vor, s=zurück, a=links, d=rechts, SPACE=stop
// Zeigt alle 2s den Linien-Offset der Kamera
// ============================================================
#include "esp_camera.h"
#include <WiFi.h>

// ----- AI Thinker ESP32-CAM Kamera-Pins -----
#define PWDN_GPIO_NUM    32
#define RESET_GPIO_NUM   -1
#define XCLK_GPIO_NUM     0
#define SIOD_GPIO_NUM    26
#define SIOC_GPIO_NUM    27
#define Y9_GPIO_NUM      35
#define Y8_GPIO_NUM      34
#define Y7_GPIO_NUM      39
#define Y6_GPIO_NUM      36
#define Y5_GPIO_NUM      21
#define Y4_GPIO_NUM      19
#define Y3_GPIO_NUM      18
#define Y2_GPIO_NUM       5
#define VSYNC_GPIO_NUM   25
#define HREF_GPIO_NUM    23
#define PCLK_GPIO_NUM    22

// ----- Motor-Pins (L298N) -----
// IO2  → IN1 (Motor A)
// IO14 → IN2 (Motor A)
// IO15 → IN3 (Motor B)
// IO13 → IN4 (Motor B)
#define PIN_IN1   2
#define PIN_IN2  14
#define PIN_IN3  15
#define PIN_IN4  13

// ----- Flash-LED -----
#define FLASH_LED  4

// ----- Bild -----
#define IMG_W      160
#define IMG_H      120
#define SCAN_END   (IMG_H / 3)   // Kamera kopfüber: oberes Drittel = Boden vor Rover
#define CENTER_X   (IMG_W / 2)

// ============================================================
//  Motor-Steuerung – Jeder Pin einzeln steuerbar
// ============================================================
void allStop() {
  digitalWrite(PIN_IN1, LOW);
  digitalWrite(PIN_IN2, LOW);
  digitalWrite(PIN_IN3, LOW);
  digitalWrite(PIN_IN4, LOW);
}

void goForward() {
  // Motor A: IN1=L, IN2=H  |  Motor B: IN3=L, IN4=H
  digitalWrite(PIN_IN1, LOW);
  digitalWrite(PIN_IN3, LOW);
  digitalWrite(PIN_IN2, HIGH);
  digitalWrite(PIN_IN4, HIGH);
}

void goBackward() {
  // Motor A: IN1=H, IN2=L  |  Motor B: IN3=H, IN4=L
  digitalWrite(PIN_IN2, LOW);
  digitalWrite(PIN_IN4, LOW);
  digitalWrite(PIN_IN1, HIGH);
  digitalWrite(PIN_IN3, HIGH);
}

void turnLeft() {
  // Motor A rückwärts, Motor B vorwärts → dreht links
  digitalWrite(PIN_IN1, LOW);
  digitalWrite(PIN_IN4, LOW);
  digitalWrite(PIN_IN2, HIGH);
  digitalWrite(PIN_IN3, HIGH);
}

void turnRight() {
  // Motor A vorwärts, Motor B rückwärts → dreht rechts
  digitalWrite(PIN_IN2, LOW);
  digitalWrite(PIN_IN3, LOW);
  digitalWrite(PIN_IN1, HIGH);
  digitalWrite(PIN_IN4, HIGH);
}

// ============================================================
//  Kamera
// ============================================================
bool initCamera() {
  camera_config_t cfg;
  cfg.ledc_channel = LEDC_CHANNEL_4;
  cfg.ledc_timer   = LEDC_TIMER_1;
  cfg.pin_d0       = Y2_GPIO_NUM;
  cfg.pin_d1       = Y3_GPIO_NUM;
  cfg.pin_d2       = Y4_GPIO_NUM;
  cfg.pin_d3       = Y5_GPIO_NUM;
  cfg.pin_d4       = Y6_GPIO_NUM;
  cfg.pin_d5       = Y7_GPIO_NUM;
  cfg.pin_d6       = Y8_GPIO_NUM;
  cfg.pin_d7       = Y9_GPIO_NUM;
  cfg.pin_xclk     = XCLK_GPIO_NUM;
  cfg.pin_pclk     = PCLK_GPIO_NUM;
  cfg.pin_vsync    = VSYNC_GPIO_NUM;
  cfg.pin_href     = HREF_GPIO_NUM;
  cfg.pin_sccb_sda = SIOD_GPIO_NUM;
  cfg.pin_sccb_scl = SIOC_GPIO_NUM;
  cfg.pin_pwdn     = PWDN_GPIO_NUM;
  cfg.pin_reset    = RESET_GPIO_NUM;
  cfg.xclk_freq_hz = 20000000;
  cfg.pixel_format = PIXFORMAT_GRAYSCALE;
  cfg.frame_size   = FRAMESIZE_QQVGA;
  cfg.fb_count     = 1;
  cfg.grab_mode    = CAMERA_GRAB_LATEST;
  cfg.jpeg_quality = 12;
  cfg.fb_location  = CAMERA_FB_IN_DRAM;

  esp_err_t err = esp_camera_init(&cfg);
  if (err != ESP_OK) {
    Serial.printf("Kamera-Init fehlgeschlagen: 0x%x\n", err);
    return false;
  }
  return true;
}

// ============================================================
//  Linien-Offset messen
// ============================================================
void printLineOffset() {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) { Serial.println("[CAM] kein Bild"); return; }

  // Kamera ist kopfüber → oberes Drittel scannen, X spiegeln
  uint32_t totalVal = 0, totalPx = 0;
  for (int y = 0; y < SCAN_END; y++) {
    const uint8_t *row = fb->buf + y * IMG_W;
    for (int x = 0; x < IMG_W; x++) {
      totalVal += row[x];
      totalPx++;
    }
  }
  uint8_t avg = (uint8_t)(totalVal / totalPx);
  uint8_t thr = avg + 30;
  if (thr < avg) thr = 255;

  uint32_t sumX = 0, cnt = 0;
  for (int y = 0; y < SCAN_END; y++) {
    const uint8_t *row = fb->buf + y * IMG_W;
    for (int x = 0; x < IMG_W; x++) {
      if (row[x] > thr) { sumX += (IMG_W - 1 - x); cnt++; }  // X spiegeln!
    }
  }
  esp_camera_fb_return(fb);

  if (cnt < 10) {
    Serial.printf("[CAM] avg=%u thr=%u brt=%u  --> KEINE LINIE\n", avg, thr, cnt);
  } else {
    int centroid = (int)(sumX / cnt);
    int offset = centroid - CENTER_X;
    Serial.printf("[CAM] avg=%u thr=%u brt=%u c=%d  --> OFFSET=%d", avg, thr, cnt, centroid, offset);
    if (offset < -20) Serial.println(" (Linie LINKS)");
    else if (offset > 20) Serial.println(" (Linie RECHTS)");
    else Serial.println(" (Linie MITTE)");
  }
}

// ============================================================
//  Setup
// ============================================================
const char *currentCmd = "STOP";

void setup() {
  // SOFORT alle Motor-Pins LOW!
  pinMode(PIN_IN1, OUTPUT); digitalWrite(PIN_IN1, LOW);
  pinMode(PIN_IN2, OUTPUT); digitalWrite(PIN_IN2, LOW);
  pinMode(PIN_IN3, OUTPUT); digitalWrite(PIN_IN3, LOW);
  pinMode(PIN_IN4, OUTPUT); digitalWrite(PIN_IN4, LOW);

  // Flash-LED aus
  pinMode(FLASH_LED, OUTPUT);
  digitalWrite(FLASH_LED, LOW);

  Serial.begin(115200);
  delay(500);

  Serial.println("\n========================================");
  Serial.println("  ESP32-CAM Rover – Fernsteuerung");
  Serial.println("========================================");
  Serial.println("Befehle (Serial Monitor eingeben):");
  Serial.println("  w = Vorwaerts");
  Serial.println("  s = Rueckwaerts");
  Serial.println("  a = Drehen Links");
  Serial.println("  d = Drehen Rechts");
  Serial.println("  SPACE oder x = STOP");
  Serial.println("  1 = nur IN1 (IO2)  HIGH");
  Serial.println("  2 = nur IN2 (IO14) HIGH");
  Serial.println("  3 = nur IN3 (IO15) HIGH");
  Serial.println("  4 = nur IN4 (IO13) HIGH");
  Serial.println("  0 = alle LOW");
  Serial.println("----------------------------------------");
  Serial.println("Kamera-Offset wird alle 2s angezeigt.");
  Serial.println("========================================\n");

  WiFi.mode(WIFI_OFF);
  btStop();

  if (initCamera()) {
    Serial.println("[OK] Kamera initialisiert");
  } else {
    Serial.println("[!!] Kamera FEHLER – Offset nicht verfuegbar");
  }

  // Pins nochmal LOW nach Kamera-Init
  allStop();
  digitalWrite(FLASH_LED, LOW);

  Serial.println("[OK] Motoren gestoppt. Warte auf Befehle...\n");
}

// ============================================================
//  Loop
// ============================================================
void loop() {
  // Serial-Befehle verarbeiten
  while (Serial.available()) {
    char c = Serial.read();
    switch (c) {
      case 'w': case 'W':
        goForward();
        currentCmd = "VORWAERTS";
        Serial.println(">>> VORWAERTS");
        break;
      case 's': case 'S':
        goBackward();
        currentCmd = "RUECKWAERTS";
        Serial.println(">>> RUECKWAERTS");
        break;
      case 'a': case 'A':
        turnLeft();
        currentCmd = "LINKS";
        Serial.println(">>> DREHEN LINKS");
        break;
      case 'd': case 'D':
        turnRight();
        currentCmd = "RECHTS";
        Serial.println(">>> DREHEN RECHTS");
        break;
      case ' ': case 'x': case 'X':
        allStop();
        currentCmd = "STOP";
        Serial.println(">>> STOP");
        break;
      // Einzelne Pins testen
      case '1':
        allStop(); digitalWrite(PIN_IN1, HIGH);
        currentCmd = "IN1(IO2)";
        Serial.println(">>> NUR IN1 (IO2) HIGH");
        break;
      case '2':
        allStop(); digitalWrite(PIN_IN2, HIGH);
        currentCmd = "IN2(IO14)";
        Serial.println(">>> NUR IN2 (IO14) HIGH");
        break;
      case '3':
        allStop(); digitalWrite(PIN_IN3, HIGH);
        currentCmd = "IN3(IO15)";
        Serial.println(">>> NUR IN3 (IO15) HIGH");
        break;
      case '4':
        allStop(); digitalWrite(PIN_IN4, HIGH);
        currentCmd = "IN4(IO13)";
        Serial.println(">>> NUR IN4 (IO13) HIGH");
        break;
      case '0':
        allStop();
        currentCmd = "STOP";
        Serial.println(">>> ALLE LOW");
        break;
    }
  }

  // Kamera-Offset alle 2 Sekunden
  static unsigned long lastCam = 0;
  if (millis() - lastCam > 2000) {
    lastCam = millis();
    Serial.printf("[Status: %s] ", currentCmd);
    printLineOffset();
  }

  delay(20);
}
