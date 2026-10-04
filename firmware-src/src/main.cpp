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
uint32_t lastBlink=0, blinkUntil=0, touchStart=0;
bool lastTouch=false;

void eye(int x,int y,bool closed=false){
  if(closed) display.fillRoundRect(x,y+10,30,3,2,SSD1306_WHITE);
  else { display.fillRoundRect(x,y,30,25,8,SSD1306_WHITE); display.fillCircle(x+15,y+13,6,SSD1306_BLACK); }
}
void drawFace(Face f, bool blink=false){
  display.clearDisplay();
  if(f==LOVE){
    display.setTextSize(3); display.setTextColor(SSD1306_WHITE); display.setCursor(18,15); display.print("<3 <3");
  } else if(f==SLEEPY){
    eye(20,18,true); eye(78,18,true); display.setTextSize(1); display.setCursor(106,5); display.print("zZ");
  } else {
    eye(20,18,blink); eye(78,18,blink);
    if(f==HAPPY){ display.drawLine(50,48,58,53,SSD1306_WHITE); display.drawLine(58,53,70,53,SSD1306_WHITE); display.drawLine(70,53,78,48,SSD1306_WHITE); }
    if(f==SAD){ display.drawLine(50,55,58,50,SSD1306_WHITE); display.drawLine(58,50,70,50,SSD1306_WHITE); display.drawLine(70,50,78,55,SSD1306_WHITE); }
    if(f==SURPRISED) display.drawCircle(64,51,6,SSD1306_WHITE);
  }
  display.display();
}
float batteryVoltage(){
  uint32_t mv=analogReadMilliVolts(PIN_BATTERY);
  return (mv/1000.0f)*((BATTERY_R1+BATTERY_R2)/BATTERY_R2);
}
int batteryPercent(){ return constrain((int)((batteryVoltage()-BATTERY_MIN_V)*100.0f/(BATTERY_MAX_V-BATTERY_MIN_V)),0,100); }
void vibrate(uint16_t ms=80){ digitalWrite(PIN_VIBRATION,HIGH); delay(ms); digitalWrite(PIN_VIBRATION,LOW); }
void beep(uint16_t hz=1800,uint16_t ms=60){ tone(PIN_BUZZER,hz,ms); }

String page(){
  return String("<!doctype html><html lang='pt-BR'><meta name='viewport' content='width=device-width'><style>body{font-family:system-ui;background:#090b10;color:#fff;max-width:700px;margin:40px auto;padding:20px}button{padding:14px;margin:6px;border:0;border-radius:12px} .card{background:#151922;padding:18px;border-radius:18px}</style><h1>Robo Expressivo</h1><div class='card'><p>Firmware ")+FW_VERSION+"</p><p>Bateria: "+batteryPercent()+"% / "+String(batteryVoltage(),2)+" V</p><button onclick=\"fetch('/face?f=happy')\">Feliz</button><button onclick=\"fetch('/face?f=love')\">Amor</button><button onclick=\"fetch('/face?f=surprised')\">Surpreso</button><button onclick=\"fetch('/face?f=sleepy')\">Sono</button></div></html>";
}
void setupWeb(){
  server.on("/",[]{server.send(200,"text/html",page());});
  server.on("/face",[]{String f=server.arg("f"); if(f=="happy")face=HAPPY; else if(f=="love")face=LOVE; else if(f=="surprised")face=SURPRISED; else if(f=="sleepy")face=SLEEPY; else face=NEUTRAL; drawFace(face); vibrate(); server.send(200,"text/plain","OK");});
  server.begin();
}
void setup(){
  Serial.begin(115200); pinMode(PIN_TOUCH,INPUT); pinMode(PIN_VIBRATION,OUTPUT); pinMode(PIN_BUZZER,OUTPUT);
  analogReadResolution(12); Wire.begin(PIN_SDA,PIN_SCL);
  display.begin(SSD1306_SWITCHCAPVCC,OLED_ADDR); drawFace(NEUTRAL);
  prefs.begin("robo",false); String ssid=prefs.getString("ssid",""); String pass=prefs.getString("pass","");
  if(ssid.length()){ WiFi.mode(WIFI_STA); WiFi.begin(ssid.c_str(),pass.c_str()); uint32_t t=millis(); while(WiFi.status()!=WL_CONNECTED && millis()-t<10000) delay(200); }
  if(WiFi.status()!=WL_CONNECTED){ WiFi.mode(WIFI_AP); WiFi.softAP("Robo-Setup"); }
  setupWeb();
}
void loop(){
  server.handleClient(); static uint32_t lastBatCheck=0; if(millis()-lastBatCheck>5000){ lastBatCheck=millis(); if(batteryPercent()<15 && face!=SAD){ face=SAD; drawFace(face); } } bool t=digitalRead(PIN_TOUCH);
  if(t && !lastTouch) touchStart=millis();
  if(!t && lastTouch){ uint32_t d=millis()-touchStart; face=d>800?LOVE:HAPPY; drawFace(face); vibrate(); beep(); }
  lastTouch=t;
  if(millis()-lastBlink>3500 && blinkUntil==0){ blinkUntil=millis()+120; drawFace(face,true); }
  if(blinkUntil && millis()>blinkUntil){ blinkUntil=0; lastBlink=millis(); drawFace(face,false); }
}
