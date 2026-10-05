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
uint8_t faceAnimFrame=0;
uint32_t lastFaceFrame=0;
const char* faceNames[]={"Neutral","Serious","Sleepy","Crying","Sad","Cute","Kissou","Mini Love","Anime Love","Fire","Angry","Love"};
const bool faceAnimated[]={true,true,false,false,false,false,false,true,true,true,true,false};
const uint8_t FACE_COUNT=12;
bool faceEnabled[12]={true,true,true,true,true,true,true,true,true,true,true,true};
bool invertOLED=false,displayEnabled=true,clock24=true,soundEnabled=true,vibrationEnabled=true;

// telas informativas habilitaveis
enum InfoScreen{I_CLOCK,I_CALENDAR,I_WEATHER,I_AIR,I_SUN,I_CURRENCY,I_MOON,I_COUNT};
uint8_t infoOrder[I_COUNT]={0,1,2,3,4,5,6};
bool infoEnabled[I_COUNT]={true,true,true,true,true,true,true};
bool showSeconds=true, showWeekday=true, showFeeling=true, tempRounded=false;
bool showHumidity=true, showWind=true, showAQI=true, showPM25=true, showPM10=true, showOzone=true;
bool showDayLength=true, showMoonAge=true;
const char* infoKey[I_COUNT]={"hora","cal","clima","ar","sol","cambio","lua"};
const char* infoName[I_COUNT]={"Hora e data","Calendario","Clima","Qualidade do ar","Luz do dia","Cambio USD/BRL","Lua"};

// localizacao configuravel (nao usa localizacao precisa do aparelho)
String city="Foz do Iguacu";
float latitude=-25.5163f,longitude=-54.5854f;
String tz="America/Sao_Paulo";

// cache internet
float tempC=NAN,humidity=NAN,wind=NAN,aqi=NAN,pm25=NAN,pm10=NAN,ozone=NAN,usdbrl=NAN;
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
 showSeconds=prefs.getBool("seconds",true);showWeekday=prefs.getBool("weekday",true);showFeeling=prefs.getBool("feeling",true);
 tempRounded=prefs.getBool("tround",false);showHumidity=prefs.getBool("humid",true);showWind=prefs.getBool("wind",true);
 showAQI=prefs.getBool("aqi",true);showPM25=prefs.getBool("pm25",true);showPM10=prefs.getBool("pm10",true);
 for(int i=0;i<FACE_COUNT;i++){String k="face"+String(i);faceEnabled[i]=prefs.getBool(k.c_str(),true);}
 String ord=prefs.getString("order","0,1,2,3,4,5,6");int pos=0,idx=0;
 while(idx<I_COUNT){int comma=ord.indexOf(',',pos);String part=comma<0?ord.substring(pos):ord.substring(pos,comma);
   int v=part.toInt();if(v>=0&&v<I_COUNT)infoOrder[idx++]=v;if(comma<0)break;pos=comma+1;}
 showOzone=prefs.getBool("ozone",true);showDayLength=prefs.getBool("daylen",true);showMoonAge=prefs.getBool("moonage",true);
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
void heart(int x,int y){
 display.fillCircle(x-3,y,3,SSD1306_WHITE);display.fillCircle(x+3,y,3,SSD1306_WHITE);
 display.fillTriangle(x-6,y+1,x+6,y+1,x,y+8,SSD1306_WHITE);
}
void eyeBox(int x,int y,int w,int h,int px=0,int py=0){
 display.fillRoundRect(x,y,w,h,7,SSD1306_WHITE);
 display.fillCircle(x+w/2+px,y+h/2+py,5,SSD1306_BLACK);
}
void drawFaceFrame(uint8_t f,uint8_t fr){
 if(!oledOK||!displayEnabled)return;
 display.clearDisplay();
 switch(f%FACE_COUNT){
  case 0: { // Neutral - pisca/move pupila
   int blink=(fr%6==5)?3:24; int yy=(fr%3)-1;
   if(blink<=3){display.fillRoundRect(20,29,30,3,2,SSD1306_WHITE);display.fillRoundRect(78,29,30,3,2,SSD1306_WHITE);}
   else {eyeBox(20,18,30,blink,(fr%3)-1,yy);eyeBox(78,18,30,blink,(fr%3)-1,yy);}
   display.drawLine(56,49,61,53,SSD1306_WHITE);display.drawLine(61,53,67,53,SSD1306_WHITE);display.drawLine(67,53,72,49,SSD1306_WHITE);
  } break;
  case 1: { // Serious
   int d=fr%3;display.fillTriangle(18,20,51,25+d,48,34,SSD1306_WHITE);display.fillTriangle(110,20,77,25+d,80,34,SSD1306_WHITE);
   display.fillCircle(36,31,4,SSD1306_BLACK);display.fillCircle(92,31,4,SSD1306_BLACK);display.drawLine(53,53,75,53,SSD1306_WHITE);
  } break;
  case 2: // Sleepy
   display.drawLine(18,31,48,31,SSD1306_WHITE);display.drawLine(80,31,110,31,SSD1306_WHITE);
   center("ZzZ...",3,2);display.drawCircle(64,51,5,SSD1306_WHITE);display.fillCircle(69,58,2,SSD1306_WHITE);break;
  case 3: // Crying
   display.drawLine(18,23,31,29,SSD1306_WHITE);display.drawLine(31,29,45,22,SSD1306_WHITE);
   display.drawLine(83,22,97,29,SSD1306_WHITE);display.drawLine(97,29,110,23,SSD1306_WHITE);
   display.drawCircle(64,50,10,SSD1306_WHITE);display.fillRect(52,50,24,12,SSD1306_BLACK);
   display.drawLine(25,34,22,50,SSD1306_WHITE);display.drawLine(103,34,106,50,SSD1306_WHITE);break;
  case 4: // Sad
   display.fillRoundRect(20,25,29,10,4,SSD1306_WHITE);display.fillRoundRect(79,25,29,10,4,SSD1306_WHITE);
   display.fillCircle(35,27,5,SSD1306_BLACK);display.fillCircle(93,27,5,SSD1306_BLACK);
   display.drawCircle(64,62,14,SSD1306_WHITE);display.fillRect(47,62,34,8,SSD1306_BLACK);break;
  case 5: // Cute
   display.fillCircle(29,28,7,SSD1306_WHITE);display.fillCircle(99,28,7,SSD1306_WHITE);
   display.fillCircle(25,20,3,SSD1306_WHITE);display.fillCircle(33,18,3,SSD1306_WHITE);
   display.fillCircle(95,18,3,SSD1306_WHITE);display.fillCircle(103,20,3,SSD1306_WHITE);
   display.fillRoundRect(55,43,18,8,4,SSD1306_WHITE);break;
  case 6: // Kissou
   display.fillCircle(28,29,7,SSD1306_WHITE);display.fillCircle(100,29,7,SSD1306_WHITE);
   center("3",40,2);break;
  case 7: { // Mini Love
   int dy=(fr%4==1)?-2:0;heart(38,18+dy);heart(91,18-dy);
   display.drawCircle(51,42,8,SSD1306_WHITE);display.drawCircle(77,42,8,SSD1306_WHITE);
   display.fillRect(43,34,16,8,SSD1306_BLACK);display.fillRect(69,34,16,8,SSD1306_BLACK);
   display.drawLine(59,49,64,53,SSD1306_WHITE);display.drawLine(64,53,69,49,SSD1306_WHITE);
  } break;
  case 8: { // Anime Love
   int dy=(fr%4==2)?-2:0;heart(35,25+dy);heart(93,25+dy);
   display.drawLine(56,46,61,51,SSD1306_WHITE);display.drawLine(61,51,67,51,SSD1306_WHITE);display.drawLine(67,51,72,46,SSD1306_WHITE);
  } break;
  case 9: { // Fire
   int q=fr%3;
   display.drawTriangle(18,39,31,10+q*2,45,39,SSD1306_WHITE);display.drawTriangle(83,39,97,10+(2-q)*2,111,39,SSD1306_WHITE);
   display.drawTriangle(24,37,32,21,39,37,SSD1306_WHITE);display.drawTriangle(89,37,97,21,105,37,SSD1306_WHITE);
   display.fillCircle(64,51,3,SSD1306_WHITE);
  } break;
  case 10: { // Angry
   int q=fr%3;display.fillRoundRect(20,27,30,18,5,SSD1306_WHITE);display.fillRoundRect(78,27,30,18,5,SSD1306_WHITE);
   display.drawLine(18,18+q,51,28,SSD1306_WHITE);display.drawLine(110,18+q,77,28,SSD1306_WHITE);
   display.fillCircle(36,35,4,SSD1306_BLACK);display.fillCircle(92,35,4,SSD1306_BLACK);
   display.drawCircle(64,63,13,SSD1306_WHITE);display.fillRect(48,63,32,8,SSD1306_BLACK);
  } break;
  case 11: // Love - long press special, also available in library
   heart(31,25);heart(97,25);display.drawLine(53,48,59,53,SSD1306_WHITE);display.drawLine(59,53,64,49,SSD1306_WHITE);
   display.drawLine(64,49,69,53,SSD1306_WHITE);display.drawLine(69,53,75,48,SSD1306_WHITE);break;
 }
 display.display();uiMode=UI_FACE;
}
void drawFace(uint8_t f){faceAnimFrame=0;drawFaceFrame(f,faceAnimFrame);}
void nextFace(){
 for(int n=1;n<=FACE_COUNT;n++){uint8_t x=(faceIndex+n)%FACE_COUNT;if(faceEnabled[x]){faceIndex=x;drawFace(faceIndex);return;}}
 drawFace(faceIndex);
}
void animateFace(){
 if(uiMode!=UI_FACE||!faceAnimated[faceIndex])return;
 if(millis()-lastFaceFrame<280)return;
 lastFaceFrame=millis();faceAnimFrame++;drawFaceFrame(faceIndex,faceAnimFrame);
}
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
 String air="https://air-quality-api.open-meteo.com/v1/air-quality?latitude="+String(latitude,5)+"&longitude="+String(longitude,5)+"&current=us_aqi,pm2_5,pm10,ozone&timezone=auto";
 if(getJson(air,d)){aqi=d["current"]["us_aqi"]|NAN;pm25=d["current"]["pm2_5"]|NAN;pm10=d["current"]["pm10"]|NAN;ozone=d["current"]["ozone"]|NAN;}
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
 int pos=-1;for(int k=0;k<I_COUNT;k++)if(infoOrder[k]==from){pos=k;break;}
 for(int n=1;n<=I_COUNT;n++){int k=(pos+n+I_COUNT)%I_COUNT;int i=infoOrder[k];if(infoEnabled[i])return i;}
 return -1;
}
void drawInfo(int i){
 if(i<0||!infoEnabled[i]){drawFace(faceIndex);return;}
 uiMode=UI_INFO;infoIndex=i;lastInteraction=millis();
 time_t now=time(nullptr);struct tm t={};if(now>100000)localtime_r(&now,&t);
 char a[32],b[32];
 switch(i){
  case I_CLOCK:
   if(clock24)strftime(a,sizeof(a),showSeconds?"%H:%M:%S":"%H:%M",&t);
   else strftime(a,sizeof(a),showSeconds?"%I:%M:%S %p":"%I:%M %p",&t);
   strftime(b,sizeof(b),"%d/%m/%Y",&t);lines("HORA E DATA",a,b);break;
  case I_CALENDAR:
   strftime(a,sizeof(a),showWeekday?"%A":"%d/%m/%Y",&t);strftime(b,sizeof(b),"%d/%m/%Y",&t);
   lines("CALENDARIO",a,showWeekday?String(b):String(""));break;
  case I_WEATHER:
   {String l2=(tempRounded?String((int)round(tempC)):String(tempC,1))+" C";
    if(showHumidity)l2+="  "+String((int)humidity)+"%";
    String l4=showWind?"Vento "+String(wind,1)+" km/h":"";
    lines("CLIMA - "+city,l2,weatherText,l4);}break;
  case I_AIR:
   {String l2=showAQI?"AQI "+String(aqi,0):"Qualidade do ar";String l3="";
    if(showPM25)l3+="PM2.5 "+String(pm25,1);if(showPM10)l3+=" PM10 "+String(pm10,1);
    String l4=showOzone?"O3 "+String(ozone,1)+" ug/m3":"";
    lines("QUALIDADE DO AR",l2,l3,l4);}break;
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
 String s=head()+"<h1>Kapibatchi <span class='small'>v0.6.1</span></h1>";
 s+="<div class='card'><h2>Biblioteca de rostos</h2><p class='small'>Toque simples alterna entre os rostos. Os marcados ANIMATED possuem movimento no OLED.</p><div class='grid'>";
 for(int i=0;i<FACE_COUNT;i++){
   s+="<div class='row'><b>"+String(faceNames[i])+"</b><br><span class='small'>"+String(faceAnimated[i]?"ANIMATED":"STATIC")+" • TAP</span></div>";
 }
 s+="</div></div>";

 s+="<form method='POST' action='/screens'><div class='card'><h2>Tela de hora</h2>";
 s+="<label><input type='checkbox' name='hora'"+checked(infoEnabled[I_CLOCK])+"> Ativar tela de hora</label>";
 s+="<div class='grid'><div class='row'>Hora atual<br><b>"+String(clock24?"24 horas":"12 horas")+"</b></div><div class='row'>Fuso horario<br><b>"+tz+"</b></div></div>";
 s+="<label><input type='checkbox' name='seconds'"+checked(showSeconds)+"> Exibir segundos</label></div>";

 s+="<div class='card'><h2>Tela do calendario</h2><label><input type='checkbox' name='cal'"+checked(infoEnabled[I_CALENDAR])+"> Ativar calendario</label>";
 s+="<div class='grid'><div class='row'>Data atual<br><b>Calendario</b></div><div class='row'>Dia da semana<br><b>"+String(showWeekday?"Visivel":"Oculto")+"</b></div></div>";
 s+="<label><input type='checkbox' name='weekday'"+checked(showWeekday)+"> Mostrar dia da semana</label></div>";

 s+="<div class='card'><h2>Tela de previsao do tempo</h2><label><input type='checkbox' name='clima'"+checked(infoEnabled[I_WEATHER])+"> Ativar clima</label>";
 s+="<div class='grid'><div class='row'>Temperatura<br><b>"+String(tempC,1)+" C</b></div><div class='row'>Condicao<br><b>"+weatherText+"</b></div><div class='row'>Umidade<br><b>"+String((int)humidity)+"%</b></div><div class='row'>Vento<br><b>"+String(wind,1)+" km/h</b></div></div>";
 s+="<label><input type='checkbox' name='humid'"+checked(showHumidity)+"> Mostrar umidade</label><label><input type='checkbox' name='wind'"+checked(showWind)+"> Mostrar vento</label><label><input type='checkbox' name='tround'"+checked(tempRounded)+"> Temperatura arredondada</label></div>";

 s+="<div class='card'><h2>Filtro de qualidade do ar</h2><label><input type='checkbox' name='ar'"+checked(infoEnabled[I_AIR])+"> Ativar qualidade do ar</label>";
 s+="<div class='grid'><div class='row'>AQI<br><b>"+String(aqi,0)+"</b></div><div class='row'>PM2.5<br><b>"+String(pm25,1)+" ug</b></div><div class='row'>PM10<br><b>"+String(pm10,1)+" ug</b></div><div class='row'>Ozonio<br><b>"+String(ozone,1)+" ug</b></div></div>";
 s+="<label><input type='checkbox' name='aqi'"+checked(showAQI)+"> AQI</label><label><input type='checkbox' name='pm25'"+checked(showPM25)+"> PM2.5</label><label><input type='checkbox' name='pm10'"+checked(showPM10)+"> PM10</label><label><input type='checkbox' name='ozone'"+checked(showOzone)+"> Ozonio</label></div>";

 s+="<div class='card'><h2>Tela da luz do dia</h2><label><input type='checkbox' name='sol'"+checked(infoEnabled[I_SUN])+"> Ativar luz do dia</label>";
 s+="<div class='grid'><div class='row'>Nascer do sol<br><b>"+sunrise+"</b></div><div class='row'>Por do sol<br><b>"+sunset+"</b></div></div></div>";

 s+="<div class='card'><h2>Cambio de moedas</h2><label><input type='checkbox' name='cambio'"+checked(infoEnabled[I_CURRENCY])+"> Ativar USD -> BRL</label>";
 s+="<div class='grid'><div class='row'>1 USD<br><b>R$ "+String(usdbrl,2)+"</b></div></div></div>";

 s+="<div class='card'><h2>Informacoes sobre a Lua</h2><label><input type='checkbox' name='lua'"+checked(infoEnabled[I_MOON])+"> Ativar Lua</label>";
 s+="<div class='grid'><div class='row'>Fase atual<br><b>"+moonPhase()+"</b></div></div></div>";
 s+="<button>Salvar opcoes das telas</button></form>";

 s+="<div class='card'><h2>Rostos / Expressoes</h2><p class='small'>Inspirado no conceito de biblioteca de animacoes do QBIT/Mochi. Escolha quais rostos entram no toque simples.</p><form method='POST' action='/faces'><div class='grid'>";
 for(int i=0;i<FACE_COUNT;i++)s+="<div class='row'><label><input type='checkbox' name='f"+String(i)+"'"+checked(faceEnabled[i])+"> <b>"+String(faceNames[i])+"</b><br><span class='small'>"+String(faceAnimated[i]?"ANIMATED":"STATIC")+"</span></label></div>";
 s+="</div><button>Salvar rostos</button></form></div>";

 s+="<div class='card'><h2>ORDEM DE EXIBICAO NA TELA</h2><p class='small'>Arraste e solte para reorganizar. As telas desativadas continuam salvas, mas sao ignoradas pelo toque duplo.</p><div id='order'>";
 for(int k=0;k<I_COUNT;k++){int i=infoOrder[k];s+="<div class='row drag' draggable='true' data-id='"+String(i)+"'>☰ &nbsp; <b>"+String(infoName[i])+"</b></div>";}
 s+="</div><button type='button' onclick='saveOrder()'>Salvar ordem</button></div>";
 s+=R"JS(<script>
let drag=null;
document.querySelectorAll('.drag').forEach(e=>{
 e.addEventListener('dragstart',()=>drag=e);
 e.addEventListener('dragover',ev=>ev.preventDefault());
 e.addEventListener('drop',ev=>{ev.preventDefault();if(drag&&drag!==e){let r=e.getBoundingClientRect();e.parentNode.insertBefore(drag,ev.clientY<r.top+r.height/2?e:e.nextSibling);}});
});
function saveOrder(){
 let v=[...document.querySelectorAll('.drag')].map(e=>e.dataset.id).join(',');
 fetch('/order',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'value='+encodeURIComponent(v)})
 .then(()=>alert('Ordem salva na NVS'));
}
</script>)JS";

 s+="<div class='card'><h2>Display e interacao</h2><form method='POST' action='/display'><label>Brilho <input type='range' min='1' max='255' name='brightness' value='"+String(brightness)+"'></label><label><input type='checkbox' name='invert'"+checked(invertOLED)+"> Inverter OLED</label><label><input type='checkbox' name='clock24'"+checked(clock24)+"> Relogio 24 h</label><label><input type='checkbox' name='sound'"+checked(soundEnabled)+"> Som</label><label><input type='checkbox' name='vibration'"+checked(vibrationEnabled)+"> Vibracao</label><button>Salvar display</button></form></div>";

 s+="<div class='card'><h2>Internet / Localizacao</h2><form method='POST' action='/location'><label>Cidade <input name='city' value='"+city+"'></label><label>Latitude <input name='lat' value='"+String(latitude,5)+"'></label><label>Longitude <input name='lon' value='"+String(longitude,5)+"'></label><label>Fuso IANA <input name='tz' value='"+tz+"'></label><button>Salvar e atualizar</button></form></div>";
 s+="<div class='card'><h2>Wi-Fi</h2><form method='POST' action='/wifi'><input name='ssid' placeholder='SSID' required><input type='password' name='pass' placeholder='Senha'><button>Salvar Wi-Fi</button></form></div>";
 return s+"</html>";
}
void redirect(){server.sendHeader("Location","/");server.send(303);}
void setupWeb(){
 server.on("/",[](){server.send(200,"text/html",page());});
 server.on("/display",HTTP_POST,[](){brightness=constrain(server.arg("brightness").toInt(),1,255);invertOLED=server.hasArg("invert");clock24=server.hasArg("clock24");soundEnabled=server.hasArg("sound");vibrationEnabled=server.hasArg("vibration");saveDisplayNVS();applyOLED();if(uiMode==UI_FACE)drawFace(faceIndex);redirect();});
 server.on("/screens",HTTP_POST,[](){
 for(int i=0;i<I_COUNT;i++){infoEnabled[i]=server.hasArg(infoKey[i]);prefs.putBool(infoKey[i],infoEnabled[i]);}
 showSeconds=server.hasArg("seconds");showWeekday=server.hasArg("weekday");tempRounded=server.hasArg("tround");
 showHumidity=server.hasArg("humid");showWind=server.hasArg("wind");showAQI=server.hasArg("aqi");
 showPM25=server.hasArg("pm25");showPM10=server.hasArg("pm10");showOzone=server.hasArg("ozone");
 prefs.putBool("seconds",showSeconds);prefs.putBool("weekday",showWeekday);prefs.putBool("tround",tempRounded);
 prefs.putBool("humid",showHumidity);prefs.putBool("wind",showWind);prefs.putBool("aqi",showAQI);
 prefs.putBool("pm25",showPM25);prefs.putBool("pm10",showPM10);prefs.putBool("ozone",showOzone);redirect();
 });
 server.on("/location",HTTP_POST,[](){city=server.arg("city");latitude=server.arg("lat").toFloat();longitude=server.arg("lon").toFloat();tz=server.arg("tz");prefs.putString("city",city);prefs.putFloat("lat",latitude);prefs.putFloat("lon",longitude);prefs.putString("tz",tz);configTzTime(tz.c_str(),"pool.ntp.org","time.nist.gov");updateInternet();redirect();});
 server.on("/wifi",HTTP_POST,[](){savedSSID=server.arg("ssid");savedPASS=server.arg("pass");prefs.putString("ssid",savedSSID);prefs.putString("pass",savedPASS);redirect();delay(250);WiFi.disconnect(true);connectSaved();});

 server.on("/faces",HTTP_POST,[](){
   for(int i=0;i<FACE_COUNT;i++){String k="f"+String(i);faceEnabled[i]=server.hasArg(k);String nk="face"+String(i);prefs.putBool(nk.c_str(),faceEnabled[i]);}
   if(!faceEnabled[faceIndex])nextFace();redirect();
 });
 server.on("/order",HTTP_POST,[](){
   String o=server.arg("value");int vals[I_COUNT];bool used[I_COUNT]={0};int count=0,pos=0;
   while(count<I_COUNT){int c=o.indexOf(',',pos);String part=c<0?o.substring(pos):o.substring(pos,c);int v=part.toInt();
     if(v>=0&&v<I_COUNT&&!used[v]){vals[count++]=v;used[v]=true;}if(c<0)break;pos=c+1;}
   if(count==I_COUNT){String saved="";for(int i=0;i<I_COUNT;i++){infoOrder[i]=vals[i];if(i)saved+=",";saved+=String(vals[i]);}prefs.putString("order",saved);}
   server.send(200,"text/plain","OK");
 });
 server.on("/api/status",[](){
   JsonDocument d;d["version"]="0.6.0";d["wifi"]=WiFi.status()==WL_CONNECTED;d["ip"]=WiFi.localIP().toString();
   d["face"]=faceNames[faceIndex];d["mode"]=uiMode==UI_FACE?"face":uiMode==UI_INFO?"info":"system";
   d["temp"]=tempC;d["humidity"]=humidity;d["aqi"]=aqi;d["usdbrl"]=usdbrl;String out;serializeJson(d,out);server.send(200,"application/json",out);
 });
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
 server.handleClient();processPendingTap();animateFace();
 bool t=digitalRead(PIN_TOUCH);
 if(t&&!lastTouch)touchDown=millis();
 if(!t&&lastTouch){
  uint32_t held=millis()-touchDown;lastInteraction=millis();
  if(held>=900){tapPending=false;faceIndex=11;vib(80);beep();drawFace(faceIndex);}
  else if(held>30){registerTap();}
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
