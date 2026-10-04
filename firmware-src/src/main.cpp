#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "config.h"

Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);
WebServer server(80);
Preferences prefs;

enum Face { NEUTRAL, HAPPY, SAD, LOVE, SURPRISED, SLEEPY };
Face face = NEUTRAL;

uint32_t lastBlink = 0;
uint32_t blinkUntil = 0;
uint32_t touchStart = 0;
bool lastTouch = false;
bool oledOK = false;

IPAddress setupIP(SETUP_IP_A, SETUP_IP_B, SETUP_IP_C, SETUP_IP_D);
IPAddress setupGateway(SETUP_IP_A, SETUP_IP_B, SETUP_IP_C, SETUP_IP_D);
IPAddress setupSubnet(255, 255, 255, 0);

void eye(int x, int y, bool closed = false) {
  if (!oledOK) return;
  if (closed) {
    display.fillRoundRect(x, y + 10, 30, 3, 2, SSD1306_WHITE);
  } else {
    display.fillRoundRect(x, y, 30, 25, 8, SSD1306_WHITE);
    display.fillCircle(x + 15, y + 13, 6, SSD1306_BLACK);
  }
}

void drawFace(Face f, bool blink = false) {
  if (!oledOK) return;
  display.clearDisplay();

  if (f == LOVE) {
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(22, 20);
    display.print("<3  <3");
  } else if (f == SLEEPY) {
    eye(20, 18, true);
    eye(78, 18, true);
    display.setTextSize(1);
    display.setCursor(106, 5);
    display.print("zZ");
  } else {
    eye(20, 18, blink);
    eye(78, 18, blink);

    if (f == HAPPY) {
      display.drawLine(50, 48, 58, 53, SSD1306_WHITE);
      display.drawLine(58, 53, 70, 53, SSD1306_WHITE);
      display.drawLine(70, 53, 78, 48, SSD1306_WHITE);
    } else if (f == SAD) {
      display.drawLine(50, 55, 58, 50, SSD1306_WHITE);
      display.drawLine(58, 50, 70, 50, SSD1306_WHITE);
      display.drawLine(70, 50, 78, 55, SSD1306_WHITE);
    } else if (f == SURPRISED) {
      display.drawCircle(64, 51, 6, SSD1306_WHITE);
    }
  }

  display.display();
}

float batteryVoltage() {
  uint32_t mv = analogReadMilliVolts(PIN_BATTERY);
  return (mv / 1000.0f) * ((BATTERY_R1 + BATTERY_R2) / BATTERY_R2);
}

int batteryPercent() {
  float v = batteryVoltage();
  return constrain((int)((v - BATTERY_MIN_V) * 100.0f /
                         (BATTERY_MAX_V - BATTERY_MIN_V)), 0, 100);
}

bool batteryMeasurementValid() {
  float v = batteryVoltage();
  return v > 2.8f && v < 4.35f;
}

void vibrate(uint16_t ms = 80) {
  digitalWrite(PIN_VIBRATION, HIGH);
  delay(ms);
  digitalWrite(PIN_VIBRATION, LOW);
}

void beep(uint16_t hz = 1800, uint16_t ms = 60) {
  tone(PIN_BUZZER, hz, ms);
}

String page() {
  String battery = batteryMeasurementValid()
    ? String(batteryPercent()) + "% / " + String(batteryVoltage(), 2) + " V"
    : "sensor ainda nao conectado";

  return String(
    "<!doctype html><html lang='pt-BR'><meta name='viewport' content='width=device-width'>"
    "<style>body{font-family:system-ui;background:#090b10;color:#fff;max-width:700px;"
    "margin:40px auto;padding:20px}button{padding:14px;margin:6px;border:0;border-radius:12px}"
    ".card{background:#151922;padding:18px;border-radius:18px}</style>"
    "<h1>Robo Solgotchi</h1><div class='card'><p>Firmware ") +
    FW_VERSION + "</p><p>Bateria: " + battery +
    "</p><p>OLED: SDA GPIO 8 / SCL GPIO 9</p>"
    "<button onclick=\"fetch('/face?f=happy')\">Feliz</button>"
    "<button onclick=\"fetch('/face?f=love')\">Amor</button>"
    "<button onclick=\"fetch('/face?f=surprised')\">Surpreso</button>"
    "<button onclick=\"fetch('/face?f=sleepy')\">Sono</button>"
    "<button onclick=\"fetch('/face?f=sad')\">Triste</button>"
    "</div></html>";
}

void setupWeb() {
  server.on("/", []() { server.send(200, "text/html", page()); });

  server.on("/face", []() {
    String f = server.arg("f");
    if (f == "happy") face = HAPPY;
    else if (f == "love") face = LOVE;
    else if (f == "surprised") face = SURPRISED;
    else if (f == "sleepy") face = SLEEPY;
    else if (f == "sad") face = SAD;
    else face = NEUTRAL;

    drawFace(face);
    vibrate();
    server.send(200, "text/plain", "OK");
  });

  server.begin();
  Serial.println("Servidor web: OK");
}

void startSetupAP() {
  WiFi.mode(WIFI_AP);

  if (!WiFi.softAPConfig(setupIP, setupGateway, setupSubnet)) {
    Serial.println("AVISO: falha ao configurar IP estatico do AP.");
  }

  if (WiFi.softAP(SETUP_AP_SSID)) {
    Serial.println();
    Serial.println("Modo de configuracao iniciado");
    Serial.print("Rede: ");
    Serial.println(SETUP_AP_SSID);
    Serial.print("IP: ");
    Serial.println(WiFi.softAPIP());
    Serial.println("Acesse: http://192.168.4.199");
  } else {
    Serial.println("ERRO: nao foi possivel iniciar o Access Point.");
  }
}

void printBootInfo() {
  Serial.println();
  Serial.println("=============================");
  Serial.println("        ROBO SOLGOTCHI");
  Serial.print("        Firmware V");
  Serial.println(FW_VERSION);
  Serial.println("=============================");
  Serial.println();
  Serial.println("ESP32-C3 iniciado");
  Serial.println("OLED: SDA GPIO 8 / SCL GPIO 9");
  Serial.println("Touch: GPIO 7");
  Serial.println("Buzzer: GPIO 5");
  Serial.println("Vibracao: GPIO 10");
  Serial.println("Bateria ADC: GPIO 3");
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  delay(700);
  printBootInfo();

  pinMode(PIN_TOUCH, INPUT);
  pinMode(PIN_VIBRATION, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  analogReadResolution(12);

  Wire.begin(PIN_SDA, PIN_SCL);
  oledOK = display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);

  if (oledOK) {
    Serial.println("OLED: OK (endereco 0x3C)");
    drawFace(NEUTRAL);
  } else {
    Serial.println("OLED: NAO ENCONTRADO");
    Serial.println("Verifique SDA GPIO 8, SCL GPIO 9, VCC, GND e endereco I2C.");
  }

  prefs.begin("solgotchi", false);
  String ssid = prefs.getString("ssid", "");
  String pass = prefs.getString("pass", "");

  if (ssid.length()) {
    Serial.print("Wi-Fi salvo: ");
    Serial.println(ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());

    uint32_t started = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - started < 10000) {
      delay(200);
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("Wi-Fi conectado.");
      Serial.print("IP: ");
      Serial.println(WiFi.localIP());
    } else {
      Serial.println("Falha ao conectar no Wi-Fi salvo.");
      startSetupAP();
    }
  } else {
    Serial.println("Wi-Fi nao configurado.");
    startSetupAP();
  }

  setupWeb();
  Serial.println("Solgotchi iniciado!");
}

void loop() {
  server.handleClient();

  static uint32_t lastBatCheck = 0;
  if (millis() - lastBatCheck > 5000) {
    lastBatCheck = millis();

    if (batteryMeasurementValid()) {
      int pct = batteryPercent();
      if (pct < BATTERY_LOW_PERCENT && face != SAD) {
        Serial.print("Bateria baixa: ");
        Serial.print(pct);
        Serial.println("% - rosto triste.");
        face = SAD;
        drawFace(face);
      }
    }
  }

  bool t = digitalRead(PIN_TOUCH);
  if (t && !lastTouch) touchStart = millis();

  if (!t && lastTouch) {
    uint32_t duration = millis() - touchStart;
    face = duration > 800 ? LOVE : HAPPY;
    drawFace(face);
    vibrate();
    beep();
  }
  lastTouch = t;

  if (millis() - lastBlink > 3500 && blinkUntil == 0) {
    blinkUntil = millis() + 120;
    drawFace(face, true);
  }

  if (blinkUntil && millis() > blinkUntil) {
    blinkUntil = 0;
    lastBlink = millis();
    drawFace(face, false);
  }
}
