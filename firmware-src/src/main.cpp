
#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "config.h"
#include "action_pingpong.h"
#include "action_yawn.h"

Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);
WebServer server(80);
Preferences prefs;

enum UiMode { UI_SETUP, UI_FACE, UI_WIFI_LOST, UI_STATUS, UI_MENU, UI_SCREENSAVER };
UiMode uiMode = UI_SETUP;

bool oledOK=false, lastTouch=false, menuOpen=false;
uint32_t touchStart=0, setupLastActivity=0, lastWiFiTry=0, lastFaceActivity=0;
uint32_t saverMs=60000UL;
String savedSSID, savedPASS, apPassword, macID;
int menuIndex=0;
const char* menuItems[]={"STATUS","TELA ESPERA","VIBRACAO ON","BUZZER ON","SAIR"};
const int MENU_COUNT=5;
bool vibrationEnabled=true, buzzerEnabled=true;

IPAddress setupIP(SETUP_IP_A,SETUP_IP_B,SETUP_IP_C,SETUP_IP_D);
IPAddress setupGateway(SETUP_IP_A,SETUP_IP_B,SETUP_IP_C,SETUP_IP_D);
IPAddress setupSubnet(255,255,255,0);

void centerText(const String &s,int y,int size=1){
  display.setTextSize(size); display.setTextColor(SSD1306_WHITE);
  int16_t x1,y1; uint16_t w,h; display.getTextBounds(s,0,y,&x1,&y1,&w,&h);
  display.setCursor(max(0,(128-(int)w)/2),y); display.print(s);
}
void showLines(const String&a,const String&b="",const String&c="",const String&d=""){
  if(!oledOK)return; display.clearDisplay(); centerText(a,3,1);
  if(b.length()) centerText(b,19,1); if(c.length()) centerText(c,35,1); if(d.length()) centerText(d,51,1);
  display.display();
}
void drawEyes(){
  if(!oledOK)return; display.clearDisplay();
  display.fillRoundRect(20,18,30,25,8,SSD1306_WHITE); display.fillCircle(35,31,6,SSD1306_BLACK);
  display.fillRoundRect(78,18,30,25,8,SSD1306_WHITE); display.fillCircle(93,31,6,SSD1306_BLACK);
  display.drawLine(50,50,58,54,SSD1306_WHITE); display.drawLine(58,54,70,54,SSD1306_WHITE); display.drawLine(70,54,78,50,SSD1306_WHITE);
  display.display();
}
void playQgifFrame(const uint8_t* data,const uint16_t* delays,uint16_t frame){
  if(!oledOK)return;
  memcpy_P(display.getBuffer(), data + ((uint32_t)frame*1024UL), 1024);
  display.display();
}
void playQgifOnce(const uint8_t* data,const uint16_t* delays,uint16_t frames){
  for(uint16_t i=0;i<frames;i++){ playQgifFrame(data,delays,i); delay(pgm_read_word(&delays[i])); server.handleClient(); }
}
String macPassword(){
  String m=WiFi.macAddress(); m.replace(":",""); m.toUpperCase();
  if(m.length()>=8) return m.substring(m.length()-8);
  return "12345678";
}
float batteryVoltage(){ uint32_t mv=analogReadMilliVolts(PIN_BATTERY); return (mv/1000.0f)*2.0f; }
int batteryPercent(){ return constrain((int)((batteryVoltage()-BATTERY_MIN_V)*100.0f/(BATTERY_MAX_V-BATTERY_MIN_V)),0,100); }
int signalQuality(){
  if(WiFi.status()!=WL_CONNECTED)return 0; int r=WiFi.RSSI();
  if(r<=-100)return 0; if(r>=-50)return 100; return 2*(r+100);
}
void vibrate(uint16_t ms=70){ if(!vibrationEnabled)return; digitalWrite(PIN_VIBRATION,HIGH); delay(ms); digitalWrite(PIN_VIBRATION,LOW); }
void beep(){ if(buzzerEnabled) tone(PIN_BUZZER,1800,55); }

void showSetup(){
  uiMode=UI_SETUP; setupLastActivity=millis();
  showLines("Conecte no WiFi","Kapibatchi","PASS: "+apPassword,"192.168.4.199");
}
void showWelcome(){
  showLines("Bem-vindo!","Wi-Fi conectado",WiFi.SSID(),"Kapibatchi");
  delay(1800); drawEyes(); uiMode=UI_FACE; lastFaceActivity=millis();
}
void showStatus(){
  uiMode=UI_STATUS; display.clearDisplay(); display.setTextSize(1); display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0); display.print("STATUS KAPIBATCHI");
  display.setCursor(0,13); display.print("WiFi: "); display.print(WiFi.status()==WL_CONNECTED?WiFi.SSID():"DESCONECTADO");
  display.setCursor(0,25); display.print("IP: "); display.print(WiFi.status()==WL_CONNECTED?WiFi.localIP().toString():"---");
  display.setCursor(0,37); display.print("Sinal: "); if(WiFi.status()==WL_CONNECTED){display.print(WiFi.RSSI());display.print("dBm ");display.print(signalQuality());display.print("%");}else display.print("---");
  display.setCursor(0,49); display.print("ID: "); display.print(macID);
  display.display();
}
void showMenu(){
  uiMode=UI_MENU; display.clearDisplay(); display.setTextSize(1); display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0); display.print("CONFIGURACOES");
  for(int i=0;i<MENU_COUNT;i++){ display.setCursor(0,13+i*10); display.print(i==menuIndex?"> ":"  "); display.print(menuItems[i]); }
  display.display();
}
void startSetupAP(){
  WiFi.mode(WIFI_AP_STA); WiFi.softAPConfig(setupIP,setupGateway,setupSubnet);
  bool ok=WiFi.softAP(SETUP_AP_SSID,apPassword.c_str());
  Serial.println(ok?"AP Kapibatchi iniciado.":"ERRO ao iniciar AP.");
  Serial.print("PASS: ");Serial.println(apPassword); Serial.print("IP: ");Serial.println(WiFi.softAPIP());
  showSetup();
}
void connectSaved(){
  if(!savedSSID.length()){startSetupAP();return;}
  WiFi.mode(WIFI_STA); WiFi.begin(savedSSID.c_str(),savedPASS.c_str());
  showLines("Kapibatchi","Conectando Wi-Fi...",savedSSID);
  uint32_t t=millis(); while(WiFi.status()!=WL_CONNECTED && millis()-t<12000){delay(150);}
  if(WiFi.status()==WL_CONNECTED){Serial.println("Wi-Fi conectado.");showWelcome();}
  else {Serial.println("Wi-Fi salvo indisponivel."); uiMode=UI_WIFI_LOST;}
}
String htmlHead(){
return R"HTML(<!doctype html><html lang='pt-BR'><meta name='viewport' content='width=device-width,initial-scale=1'>
<style>body{font-family:system-ui;background:#081018;color:#eef;max-width:900px;margin:auto;padding:20px}.c{background:#111b27;border:1px solid #26384a;border-radius:18px;padding:18px;margin:14px 0}input,select,button{padding:12px;border-radius:10px;border:1px solid #345;margin:5px;background:#0d1722;color:white}button{background:#5de0bd;color:#001;font-weight:700}a{color:#65d9ff}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(240px,1fr));gap:12px}.wire{font-family:monospace;background:#081018;padding:12px;border-radius:10px;line-height:1.7}</style>)HTML";
}
String assemblyPage(){
 String s=htmlHead()+R"HTML(<h1>Kapibatchi - Montagem</h1><div class='c'><a href='/'>← Voltar</a><p>Escolha a montagem:</p>
<select id='m' onchange='d()'><option value='display'>Somente Display</option><option value='touch'>Display + Touch</option><option value='vibro'>Display + Touch + Vibração</option><option value='battery'>Display + Touch + Bateria/Carregador</option><option value='all'>Todos os hardwares</option></select>
<div id='x' class='wire'></div></div><script>
function d(){let m=document.getElementById('m').value;let a=['OLED VCC → 3.3V','OLED GND → GND','OLED SDA → GPIO 8','OLED SCL → GPIO 9'];
if(m!='display')a.push('TTP223 SIG → GPIO 7','TTP223 VCC → 3.3V','TTP223 GND → GND');
if(m=='vibro'||m=='all')a.push('Vibração controle → GPIO 10');
if(m=='battery'||m=='all')a.push('Bateria → TP4056 USB-C','Divisor 100k/100k → GPIO 3 ADC');
if(m=='all')a.push('Buzzer → GPIO 5');
document.getElementById('x').innerHTML=a.join('<br>')}d()</script>)HTML";
 return s;
}
String mainPage(){
 String st=WiFi.status()==WL_CONNECTED?WiFi.SSID():"Desconectado";
 String ip=WiFi.status()==WL_CONNECTED?WiFi.localIP().toString():setupIP.toString();
 return htmlHead()+"<h1>Kapibatchi</h1><div class='grid'><div class='c'><h2>Wi-Fi</h2><p>Status: "+st+"</p><p>IP: "+ip+"</p><p>ID: "+macID+"</p><p>Sinal: "+String(signalQuality())+"%</p><form action='/wifi' method='POST'><input name='ssid' placeholder='SSID' required><input name='pass' type='password' placeholder='Senha'><button>Salvar Wi-Fi</button></form></div>"
 "<div class='c'><h2>Display</h2><form action='/saver' method='POST'><select name='t'><option value='30'>30 segundos</option><option value='60'>1 minuto</option><option value='300'>5 minutos</option><option value='600'>10 minutos</option></select><button>Tempo proteção de tela</button></form><p>Proteção: action_yawn.qgif</p></div>"
 "<div class='c'><h2>Hardware</h2><p>OLED SDA GPIO 8 / SCL GPIO 9<br>Touch GPIO 7<br>Buzzer GPIO 5<br>Vibração GPIO 10<br>Bateria ADC GPIO 3</p><a href='/montagem'>Detalhes de montagem</a></div></div><div class='c'><p>Firmware "+String(FW_VERSION)+"</p></div></html>";
}
void setupWeb(){
 server.on("/",[](){server.send(200,"text/html",mainPage());});
 server.on("/montagem",[](){server.send(200,"text/html",assemblyPage());});
 server.on("/wifi",HTTP_POST,[](){
   String s=server.arg("ssid"),p=server.arg("pass");
   if(s.length()){prefs.putString("ssid",s);prefs.putString("pass",p);savedSSID=s;savedPASS=p;server.send(200,"text/html",htmlHead()+"<h2>Wi-Fi salvo. Kapibatchi vai conectar...</h2>");delay(500);WiFi.softAPdisconnect(true);connectSaved();}
   else server.send(400,"text/plain","SSID invalido");
 });
 server.on("/saver",HTTP_POST,[](){uint32_t sec=server.arg("t").toInt(); if(sec==30||sec==60||sec==300||sec==600){saverMs=sec*1000UL;prefs.putUInt("saver",sec);}server.sendHeader("Location","/");server.send(303);});
 server.begin();
}
void wifiLostLoop(){
 static uint32_t phase=0; static bool textPhase=true;
 if(millis()-phase>2500){phase=millis();textPhase=!textPhase;if(textPhase)showLines("Aguardando","conexao Wi-Fi...");else playQgifOnce(PINGPONG_DATA,PINGPONG_DELAYS,PINGPONG_FRAMES);}
 if(millis()-lastWiFiTry>WIFI_RETRY_MS){lastWiFiTry=millis();WiFi.reconnect();}
 if(WiFi.status()==WL_CONNECTED)showWelcome();
}
void setup(){
 Serial.begin(115200);delay(600);
 pinMode(PIN_TOUCH,INPUT);pinMode(PIN_VIBRATION,OUTPUT);pinMode(PIN_BUZZER,OUTPUT);analogReadResolution(12);
 Wire.begin(PIN_SDA,PIN_SCL);oledOK=display.begin(SSD1306_SWITCHCAPVCC,OLED_ADDR);
 WiFi.mode(WIFI_STA); macID=WiFi.macAddress(); apPassword=macPassword();
 prefs.begin("kapibatchi",false);savedSSID=prefs.getString("ssid","");savedPASS=prefs.getString("pass","");
 uint32_t sv=prefs.getUInt("saver",60);saverMs=(sv==30||sv==60||sv==300||sv==600)?sv*1000UL:60000UL;
 Serial.println("\n=== KAPIBATCHI V"+String(FW_VERSION)+" ===");Serial.println("MAC: "+macID);Serial.println("AP PASS: "+apPassword);
 setupWeb(); connectSaved();
}
void loop(){
 server.handleClient();
 if(uiMode==UI_WIFI_LOST){wifiLostLoop();return;}
 if(WiFi.status()!=WL_CONNECTED && savedSSID.length() && uiMode!=UI_SETUP){uiMode=UI_WIFI_LOST;return;}

 bool t=digitalRead(PIN_TOUCH);
 if(t&&!lastTouch)touchStart=millis();
 if(!t&&lastTouch){
   uint32_t held=millis()-touchStart; setupLastActivity=lastFaceActivity=millis();
   if(uiMode==UI_SETUP||uiMode==UI_SCREENSAVER){showSetup();}
   else if(uiMode==UI_STATUS){drawEyes();uiMode=UI_FACE;}
   else if(uiMode==UI_MENU){
     if(held>800){
       if(menuIndex==0)showStatus();
       else if(menuIndex==1){playQgifOnce(YAWN_DATA,YAWN_DELAYS,YAWN_FRAMES);showMenu();}
       else if(menuIndex==2){vibrationEnabled=!vibrationEnabled;showMenu();}
       else if(menuIndex==3){buzzerEnabled=!buzzerEnabled;showMenu();}
       else {drawEyes();uiMode=UI_FACE;}
     } else {menuIndex=(menuIndex+1)%MENU_COUNT;showMenu();}
   } else if(held>1200){menuIndex=0;showMenu();}
   else {vibrate();beep();drawEyes();}
 }
 lastTouch=t;

 if(uiMode==UI_SETUP && millis()-setupLastActivity>WIFI_SETUP_IDLE_MS){
   uiMode=UI_SCREENSAVER; playQgifOnce(PINGPONG_DATA,PINGPONG_DELAYS,PINGPONG_FRAMES); setupLastActivity=millis();
 }
 if(uiMode==UI_SCREENSAVER && millis()-setupLastActivity>200){
   playQgifOnce(PINGPONG_DATA,PINGPONG_DELAYS,PINGPONG_FRAMES); setupLastActivity=millis();
 }
 if(uiMode==UI_FACE && millis()-lastFaceActivity>saverMs){
   playQgifOnce(YAWN_DATA,YAWN_DELAYS,YAWN_FRAMES); lastFaceActivity=millis();
 }
}
