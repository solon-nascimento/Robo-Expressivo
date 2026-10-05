#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <time.h>
#include "config.h"
#include "action_pingpong.h"
#include "action_yawn.h"

Adafruit_SSD1306 display(OLED_WIDTH,OLED_HEIGHT,&Wire,-1);
WebServer server(80);
Preferences prefs;

enum UiMode{UI_SETUP,UI_FACE,UI_INFO,UI_WIFI_LOST};
UiMode uiMode=UI_SETUP;

bool oledOK=false,lastTouch=false;
uint32_t touchDown=0,lastInteraction=0,lastTapAt=0;
bool tapPending=false;
const uint32_t DOUBLE_MS=420;
const uint32_t INFO_TIMEOUT=60000UL;

String savedSSID,savedPASS,apPassword,macID;
uint8_t brightness=255,faceIndex=0,infoIndex=0;
bool invertOLED=false,displayEnabled=true,clock24=true,soundEnabled=true,vibrationEnabled=true;

// telas informativas habilitaveis
enum InfoScreen{I_CLOCK,I_CALENDAR,I_WEATHER,I_AIR,I_SUN,I_CURRENCY,I_MOON,I_COUNT};
bool infoEnabled[I_COUNT]={true,true,true,true,true,true,true};
const char* infoKey[I_COUNT]={"hora","cal","clima","ar","sol","cambio","lua"};
const char* infoName[I_COUNT]={"Hora e data","Calendario","Clima","Qualidade do ar","Luz do dia","Cambio USD/BRL","Lua"};

// localizacao configuravel (nao usa localizacao precisa do aparelho)
String city="Foz do Iguacu";
float latitude=-25.5163f,longitude=-54.5854f;
String tz="America/Sao_Paulo";

// cache internet
float tempC=NAN,humidity=NAN,wind=NAN,aqi=NAN,usdbrl=NAN;
String weatherText="--",sunrise="--:--",sunset="--:--";
uint32_t lastInternetUpdate=0;

IPAddress setupIP(SETUP_IP_A,SETUP_IP_B,SETUP_IP_C,SETUP_IP_D);
IPAddress setupGateway(SETUP_IP_A,SETUP_IP_B,SETUP_IP_C,SETUP_IP_D);
IPAddress setupSubnet(255,255,255,0);

void applyOLED(){
 if(!oledOK)return;
 display.ssd1306_command(SSD1306_SETCONTRAST);display.ssd1306_command(brightness);
 display.invertDisplay(invertOLED);
 display.ssd1306_command(displayEnabled?SSD1306_DISPLAYON:SSD1306_DISPLAYOFF);
}
void loadNVS(){
 savedSSID=prefs.getString("ssid","");savedPASS=prefs.getString("pass","");
 brightness=prefs.getUChar("brightness",255);invertOLED=prefs.getBool("invert",false);
 displayEnabled=prefs.getBool("display",true);clock24=prefs.getBool("clock24",true);
 soundEnabled=prefs.getBool("sound",true);vibrationEnabled=prefs.getBool("vibration",true);
 city=prefs.getString("city","Foz do Iguacu");latitude=prefs.getFloat("lat",-25.5163f);
 longitude=prefs.getFloat("lon",-54.5854f);tz=prefs.getString("tz","America/Sao_Paulo");
 for(int i=0;i<I_COUNT;i++)infoEnabled[i]=prefs.getBool(infoKey[i],true);
}
void saveDisplayNVS(){
 prefs.putUChar("brightness",brightness);prefs.putBool("invert",invertOLED);prefs.putBool("display",displayEnabled);
 prefs.putBool("clock24",clock24);prefs.putBool("sound",soundEnabled);prefs.putBool("vibration",vibrationEnabled);
}
void center(const String&s,int y,int sz=1){
 display.setTextSize(sz);display.setTextColor(SSD1306_WHITE);int16_t x1,y1;uint16_t w,h;
 display.getTextBounds(s,0,y,&x1,&y1,&w,&h);display.setCursor(max(0,(128-(int)w)/2),y);display.print(s);
}
void lines(const String&a,const String&b="",const String&c="",const String&d=""){
 if(!oledOK||!displayEnabled)return;display.clearDisplay();center(a,2);
 if(b.length())center(b,18);if(c.length())center(c,34);if(d.length())center(d,50);display.display();
}
void vib(uint16_t ms=45){if(!vibrationEnabled)return;digitalWrite(PIN_VIBRATION,HIGH);delay(ms);digitalWrite(PIN_VIBRATION,LOW);}
void beep(){if(soundEnabled)tone(PIN_BUZZER,1800,45);}

// -------- ROSTOS --------
void eye(int x,int y,int w,int h,int pupilX,int pupilY){
 display.fillRoundRect(x,y,w,h,7,SSD1306_WHITE);display.fillCircle(x+w/2+pupilX,y+h/2+pupilY,5,SSD1306_BLACK);
}
void drawFace(uint8_t f){
 if(!oledOK||!displayEnabled)return;display.clearDisplay();
 switch(f%6){
  case 0: // normal
   eye(20,17,30,25,0,0);eye(78,17,30,25,0,0);
   display.drawLine(50,50,58,54,SSD1306_WHITE);display.drawLine(58,54,70,54,SSD1306_WHITE);display.drawLine(70,54,78,50,SSD1306_WHITE);break;
  case 1: // feliz
   display.drawLine(20,30,28,23,SSD1306_WHITE);display.drawLine(28,23,38,30,SSD1306_WHITE);
   display.drawLine(80,30,90,23,SSD1306_WHITE);display.drawLine(90,23,108,30,SSD1306_WHITE);
   display.drawCircle(64,43,13,SSD1306_WHITE);display.fillRect(49,30,30,14,SSD1306_BLACK);break;
  case 2: // sonolento
   display.drawLine(20,29,50,29,SSD1306_WHITE);display.drawLine(78,29,108,29,SSD1306_WHITE);
   display.drawCircle(64,48,6,SSD1306_WHITE);break;
  case 3: // bravo
   display.drawLine(20,18,50,28,SSD1306_WHITE);display.drawLine(78,28,108,18,SSD1306_WHITE);
   eye(23,28,27,19,2,0);eye(78,28,27,19,-2,0);display.drawLine(53,55,75,55,SSD1306_WHITE);break;
  case 4: // surpreso
   eye(18,15,32,30,0,0);eye(78,15,32,30,0,0);display.drawCircle(64,53,7,SSD1306_WHITE);break;
  case 5: // triste
   display.drawLine(20,25,35,18,SSD1306_WHITE);display.drawLine(35,18,50,25,SSD1306_WHITE);
   display.drawLine(78,25,93,18,SSD1306_WHITE);display.drawLine(93,18,108,25,SSD1306_WHITE);
   display.drawCircle(64,61,13,SSD1306_WHITE);display.fillRect(48,61,32,8,SSD1306_BLACK);break;
 }
 display.display();uiMode=UI_FACE;
}
void nextFace(){faceIndex=(faceIndex+1)%6;drawFace(faceIndex);}

// -------- INTERNET --------
String hhmm(const String&iso){int p=iso.indexOf('T');return(p>=0&&iso.length()>=p+6)?iso.substring(p+1,p+6):iso;}
String weatherCode(int c){
 if(c==0)return "Ceu limpo";if(c<=3)return "Nublado";if(c==45||c==48)return "Neblina";
 if(c>=51&&c<=67)return "Chuva";if(c>=71&&c<=77)return "Neve";if(c>=80&&c<=82)return "Pancadas";
 if(c>=95)return "Temporal";return "Variavel";
}
bool getJson(const String&url,JsonDocument&doc){
 if(WiFi.status()!=WL_CONNECTED)return false;HTTPClient h;h.setTimeout(6500);h.begin(url);
 int rc=h.GET();if(rc!=200){h.end();return false;}DeserializationError e=deserializeJson(doc,h.getStream());h.end();return !e;
}
void updateInternet(){
 if(WiFi.status()!=WL_CONNECTED)return;
 JsonDocument d;
 String base="https://api.open-meteo.com/v1/forecast?latitude="+String(latitude,5)+"&longitude="+String(longitude,5)+
 "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m&daily=sunrise,sunset&timezone=auto&forecast_days=1";
 if(getJson(base,d)){
  tempC=d["current"]["temperature_2m"]|NAN;humidity=d["current"]["relative_humidity_2m"]|NAN;
  wind=d["current"]["wind_speed_10m"]|NAN;weatherText=weatherCode(d["current"]["weather_code"]|0);
  sunrise=hhmm(String((const char*)(d["daily"]["sunrise"][0]|"")));sunset=hhmm(String((const char*)(d["daily"]["sunset"][0]|"")));
 }
 d.clear();
 String air="https://air-quality-api.open-meteo.com/v1/air-quality?latitude="+String(latitude,5)+"&longitude="+String(longitude,5)+"&current=us_aqi&timezone=auto";
 if(getJson(air,d))aqi=d["current"]["us_aqi"]|NAN;
 d.clear();
 if(getJson("https://api.frankfurter.app/latest?from=USD&to=BRL",d))usdbrl=d["rates"]["BRL"]|NAN;
 lastInternetUpdate=millis();
}
String moonPhase(){
 time_t now=time(nullptr);if(now<100000)return "--";
 double days=(double)(now-947182440)/86400.0;double phase=fmod(days,29.53058867)/29.53058867;
 if(phase<0.03||phase>0.97)return "Lua nova";if(phase<0.22)return "Crescente";if(phase<0.28)return "Quarto crescente";
 if(phase<0.47)return "Gibosa crescente";if(phase<0.53)return "Lua cheia";if(phase<0.72)return "Gibosa minguante";
 if(phase<0.78)return "Quarto minguante";return "Minguante";
}

// -------- TELAS INFORMATIVAS --------
int nextEnabled(int from){
 for(int n=1;n<=I_COUNT;n++){int i=(from+n)%I_COUNT;if(infoEnabled[i])return i;}return -1;
}
void drawInfo(int i){
 if(i<0||!infoEnabled[i]){drawFace(faceIndex);return;}
 uiMode=UI_INFO;infoIndex=i;lastInteraction=millis();
 time_t now=time(nullptr);struct tm t={};if(now>100000)localtime_r(&now,&t);
 char a[32],b[32];
 switch(i){
  case I_CLOCK:
   if(clock24)strftime(a,sizeof(a),"%H:%M:%S",&t);else strftime(a,sizeof(a),"%I:%M %p",&t);
   strftime(b,sizeof(b),"%d/%m/%Y",&t);lines("HORA E DATA",a,b);break;
  case I_CALENDAR:
   strftime(a,sizeof(a),"%A",&t);strftime(b,sizeof(b),"%d/%m/%Y",&t);lines("CALENDARIO",a,b);break;
  case I_WEATHER:
   lines("CLIMA - "+city,String(tempC,1)+" C  "+String((int)humidity)+"%",weatherText,"Vento "+String(wind,1)+" km/h");break;
  case I_AIR:
   lines("QUALIDADE DO AR","AQI "+String(aqi,0),isnan(aqi)?"Sem dados":(aqi<=50?"Boa":aqi<=100?"Moderada":"Ruim"));break;
  case I_SUN:
   lines("LUZ DO DIA","Nascer "+sunrise,"Por "+sunset);break;
  case I_CURRENCY:
   lines("CAMBIO","1 USD",isnan(usdbrl)?"Sem dados":"R$ "+String(usdbrl,2),"USD -> BRL");break;
  case I_MOON:
   lines("LUA",moonPhase());break;
 }
}
void openInfo(){int i=infoEnabled[infoIndex]?infoIndex:nextEnabled(I_COUNT-1);drawInfo(i);}
void nextInfo(){int n=nextEnabled(infoIndex);if(n>=0)drawInfo(n);else drawFace(faceIndex);}

// -------- TOQUE: simples=rosto, duplo=informacoes --------
void registerTap(){
 uint32_t now=millis();
 if(tapPending&&now-lastTapAt<=DOUBLE_MS){
  tapPending=false;vib();beep();
  if(uiMode==UI_INFO)nextInfo();else openInfo();
 }else{tapPending=true;lastTapAt=now;}
}
void processPendingTap(){
 if(tapPending&&millis()-lastTapAt>DOUBLE_MS){
  tapPending=false;vib();beep();nextFace();
 }
}

// -------- WIFI --------
String macPassword(){String m=WiFi.macAddress();m.replace(":","");m.toUpperCase();return m.length()>=8?m.substring(m.length()-8):"12345678";}
void startAP(){WiFi.mode(WIFI_AP_STA);WiFi.softAPConfig(setupIP,setupGateway,setupSubnet);WiFi.softAP(SETUP_AP_SSID,apPassword.c_str());lines("Conecte no WiFi","Kapibatchi","PASS: "+apPassword,"192.168.4.199");uiMode=UI_SETUP;}
void connectSaved(){
 if(!savedSSID.length()){startAP();return;}WiFi.mode(WIFI_STA);WiFi.begin(savedSSID.c_str(),savedPASS.c_str());lines("Kapibatchi","Conectando...",savedSSID);
 uint32_t s=millis();while(WiFi.status()!=WL_CONNECTED&&millis()-s<12000)delay(100);
 if(WiFi.status()==WL_CONNECTED){configTzTime(tz.c_str(),"pool.ntp.org","time.nist.gov");updateInternet();drawFace(faceIndex);}
 else{uiMode=UI_WIFI_LOST;lines("Wi-Fi indisponivel",savedSSID,"Tentando novamente...");}
}

// -------- WEB --------
String checked(bool v){return v?" checked":"";}
String head(){return R"HTML(<!doctype html><html lang='pt-BR'><meta name='viewport' content='width=device-width,initial-scale=1'>
<style>body{font-family:system-ui;background:#111;color:#eee;max-width:900px;margin:auto;padding:24px}h1,h2{font-weight:800}.card{background:#242424;border:1px solid #444;border-radius:10px;padding:18px;margin:14px 0}.row{padding:14px;border-bottom:1px solid #444}input,button{padding:10px;margin:5px;border-radius:8px;border:1px solid #555;background:#333;color:#fff}button{background:#eee;color:#111;font-weight:800}.small{color:#aaa}label{display:block}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(260px,1fr));gap:12px}</style>)HTML";}
String page(){
 String s=head()+"<h1>KAPIBATCHI</h1><div class='grid'><div class='card'><h2>Status</h2><p>Wi-Fi: "+String(WiFi.status()==WL_CONNECTED?WiFi.SSID():"desconectado")+"</p><p>IP: "+String(WiFi.status()==WL_CONNECTED?WiFi.localIP().toString():setupIP.toString())+"</p><p>Modo: "+String(uiMode==UI_FACE?"Rosto":uiMode==UI_INFO?"Informacoes":"Sistema")+"</p></div>";
 s+="<div class='card'><h2>Display</h2><form method='POST' action='/display'><label>Brilho <input type='range' min='1' max='255' name='brightness' value='"+String(brightness)+"'></label><label><input type='checkbox' name='invert'"+checked(invertOLED)+"> Inverter OLED</label><label><input type='checkbox' name='clock24'"+checked(clock24)+"> Relogio 24 h</label><label><input type='checkbox' name='sound'"+checked(soundEnabled)+"> Som</label><label><input type='checkbox' name='vibration'"+checked(vibrationEnabled)+"> Vibracao</label><button>Salvar</button></form></div></div>";
 s+="<div class='card'><h2>ORDEM DE EXIBICAO NA TELA</h2><p class='small'>Ative ou desative as telas. Toque duplo abre/avanca informacoes. Apos 1 minuto sem toque volta aos rostos.</p><form method='POST' action='/screens'>";
 for(int i=0;i<I_COUNT;i++)s+="<div class='row'><label><input type='checkbox' name='"+String(infoKey[i])+"'"+checked(infoEnabled[i])+"> "+String(infoName[i])+"</label></div>";
 s+="<button>Salvar telas</button></form></div>";
 s+="<div class='card'><h2>Internet / Localizacao</h2><form method='POST' action='/location'><label>Cidade <input name='city' value='"+city+"'></label><label>Latitude <input name='lat' value='"+String(latitude,5)+"'></label><label>Longitude <input name='lon' value='"+String(longitude,5)+"'></label><label>Fuso IANA <input name='tz' value='"+tz+"'></label><button>Salvar e atualizar</button></form><p class='small'>Clima/AQI: Open-Meteo. Cambio: Frankfurter. Sem chave de API.</p></div>";
 s+="<div class='card'><h2>Wi-Fi</h2><form method='POST' action='/wifi'><input name='ssid' placeholder='SSID' required><input type='password' name='pass' placeholder='Senha'><button>Salvar Wi-Fi</button></form></div>";
 return s+"</html>";
}
void redirect(){server.sendHeader("Location","/");server.send(303);}
void setupWeb(){
 server.on("/",[](){server.send(200,"text/html",page());});
 server.on("/display",HTTP_POST,[](){brightness=constrain(server.arg("brightness").toInt(),1,255);invertOLED=server.hasArg("invert");clock24=server.hasArg("clock24");soundEnabled=server.hasArg("sound");vibrationEnabled=server.hasArg("vibration");saveDisplayNVS();applyOLED();if(uiMode==UI_FACE)drawFace(faceIndex);redirect();});
 server.on("/screens",HTTP_POST,[](){for(int i=0;i<I_COUNT;i++){infoEnabled[i]=server.hasArg(infoKey[i]);prefs.putBool(infoKey[i],infoEnabled[i]);}redirect();});
 server.on("/location",HTTP_POST,[](){city=server.arg("city");latitude=server.arg("lat").toFloat();longitude=server.arg("lon").toFloat();tz=server.arg("tz");prefs.putString("city",city);prefs.putFloat("lat",latitude);prefs.putFloat("lon",longitude);prefs.putString("tz",tz);configTzTime(tz.c_str(),"pool.ntp.org","time.nist.gov");updateInternet();redirect();});
 server.on("/wifi",HTTP_POST,[](){savedSSID=server.arg("ssid");savedPASS=server.arg("pass");prefs.putString("ssid",savedSSID);prefs.putString("pass",savedPASS);redirect();delay(250);WiFi.disconnect(true);connectSaved();});
 server.begin();
}

void setup(){
 Serial.begin(115200);delay(400);pinMode(PIN_TOUCH,INPUT);pinMode(PIN_VIBRATION,OUTPUT);pinMode(PIN_BUZZER,OUTPUT);
 prefs.begin("kapibatchi",false);loadNVS();
 Wire.begin(PIN_SDA,PIN_SCL);oledOK=display.begin(SSD1306_SWITCHCAPVCC,OLED_ADDR);applyOLED();
 WiFi.mode(WIFI_STA);macID=WiFi.macAddress();apPassword=macPassword();setupWeb();lastInteraction=millis();connectSaved();
 Serial.println("Kapibatchi: rostos + toque duplo + telas Internet iniciado.");
}
void loop(){
 server.handleClient();processPendingTap();
 bool t=digitalRead(PIN_TOUCH);
 if(t&&!lastTouch)touchDown=millis();
 if(!t&&lastTouch){
  uint32_t held=millis()-touchDown;
  if(held>30&&held<900){lastInteraction=millis();registerTap();}
 }
 lastTouch=t;

 // informacoes sempre retornam ao rosto apos 1 minuto sem interacao
 if(uiMode==UI_INFO&&millis()-lastInteraction>=INFO_TIMEOUT){tapPending=false;drawFace(faceIndex);lastInteraction=millis();}

 // atualiza dados a cada 15 min
 if(WiFi.status()==WL_CONNECTED&&millis()-lastInternetUpdate>900000UL)updateInternet();

 if(uiMode==UI_WIFI_LOST&&millis()-lastInternetUpdate>15000UL){
  lastInternetUpdate=millis();WiFi.reconnect();
  if(WiFi.status()==WL_CONNECTED){configTzTime(tz.c_str(),"pool.ntp.org","time.nist.gov");updateInternet();drawFace(faceIndex);}
 }
}
