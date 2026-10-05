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
UiMode uiMode=UI_SETUP;

bool oledOK=false,lastTouch=false;
bool vibrationEnabled=true,buzzerEnabled=true;
uint32_t touchStart=0,setupLastActivity=0,lastWiFiTry=0,lastFaceActivity=0;
uint32_t saverMs=60000UL;
String savedSSID,savedPASS,apPassword,macID;
int menuIndex=0;
const char* menuItems[]={"STATUS","TELA ESPERA","VIBRACAO ON","BUZZER ON","SAIR"};
const int MENU_COUNT=5;

IPAddress setupIP(SETUP_IP_A,SETUP_IP_B,SETUP_IP_C,SETUP_IP_D);
IPAddress setupGateway(SETUP_IP_A,SETUP_IP_B,SETUP_IP_C,SETUP_IP_D);
IPAddress setupSubnet(255,255,255,0);

// Animacao nao bloqueante
const uint8_t* animData=nullptr;
const uint16_t* animDelays=nullptr;
uint16_t animFrames=0,animFrame=0;
uint32_t animNext=0;
bool animActive=false,animLoop=false;
UiMode animReturnMode=UI_FACE;

void centerText(const String&s,int y,int size=1){
  display.setTextSize(size);display.setTextColor(SSD1306_WHITE);
  int16_t x1,y1;uint16_t w,h;display.getTextBounds(s,0,y,&x1,&y1,&w,&h);
  display.setCursor(max(0,(128-(int)w)/2),y);display.print(s);
}
void showLines(const String&a,const String&b="",const String&c="",const String&d=""){
  if(!oledOK)return;display.clearDisplay();centerText(a,3);
  if(b.length())centerText(b,19);if(c.length())centerText(c,35);if(d.length())centerText(d,51);
  display.display();
}
void drawEyes(){
  if(!oledOK)return;display.clearDisplay();
  display.fillRoundRect(20,18,30,25,8,SSD1306_WHITE);display.fillCircle(35,31,6,SSD1306_BLACK);
  display.fillRoundRect(78,18,30,25,8,SSD1306_WHITE);display.fillCircle(93,31,6,SSD1306_BLACK);
  display.drawLine(50,50,58,54,SSD1306_WHITE);display.drawLine(58,54,70,54,SSD1306_WHITE);display.drawLine(70,54,78,50,SSD1306_WHITE);
  display.display();
}

// Os headers QGIF foram gerados em bitmap 1-bit por linhas.
// O SSD1306 usa paginas verticais de 8 pixels. Esta funcao faz a conversao.
void drawConvertedFrame(const uint8_t*data,uint16_t frame){
  if(!oledOK)return;
  uint8_t*dst=display.getBuffer();
  memset(dst,0,1024);
  uint32_t base=(uint32_t)frame*1024UL;
  for(uint16_t y=0;y<64;y++){
    uint32_t row=base+(uint32_t)y*16UL;
    uint16_t page=(y>>3)*128;
    uint8_t mask=(1U<<(y&7));
    for(uint16_t xb=0;xb<16;xb++){
      uint8_t b=pgm_read_byte(data+row+xb);
      if(!b)continue;
      uint16_t x0=xb*8;
      for(uint8_t bit=0;bit<8;bit++){
        if(b&(0x80>>bit))dst[page+x0+bit]|=mask;
      }
    }
  }
  display.display();
}
void stopAnimation(){animActive=false;animData=nullptr;}
void startAnimation(const uint8_t*data,const uint16_t*delays,uint16_t frames,bool loop,UiMode returnMode){
  animData=data;animDelays=delays;animFrames=frames;animFrame=0;animLoop=loop;animReturnMode=returnMode;animActive=true;animNext=0;
}
void animationTick(){
  if(!animActive||millis()<animNext)return;
  drawConvertedFrame(animData,animFrame);
  uint16_t wait=pgm_read_word(&animDelays[animFrame]);
  animNext=millis()+max((uint16_t)20,wait);
  animFrame++;
  if(animFrame>=animFrames){
    if(animLoop)animFrame=0;
    else{
      animActive=false;
      if(animReturnMode==UI_MENU){} // menu sera redesenhado pelo loop
      else if(animReturnMode==UI_FACE){drawEyes();uiMode=UI_FACE;lastFaceActivity=millis();}
    }
  }
}

String macPassword(){
  String m=WiFi.macAddress();m.replace(":","");m.toUpperCase();
  return m.length()>=8?m.substring(m.length()-8):"12345678";
}
float batteryVoltage(){uint32_t mv=analogReadMilliVolts(PIN_BATTERY);return(mv/1000.0f)*2.0f;}
int batteryPercent(){return constrain((int)((batteryVoltage()-BATTERY_MIN_V)*100.0f/(BATTERY_MAX_V-BATTERY_MIN_V)),0,100);}
int signalQualityFromRSSI(int r){if(r<=-100)return 0;if(r>=-50)return 100;return 2*(r+100);}
int signalQuality(){return WiFi.status()==WL_CONNECTED?signalQualityFromRSSI(WiFi.RSSI()):0;}
String qualityName(int r){if(r>=-55)return"Excelente";if(r>=-67)return"Bom";if(r>=-75)return"Regular";return"Fraco";}
void vibrate(uint16_t ms=70){if(!vibrationEnabled)return;digitalWrite(PIN_VIBRATION,HIGH);delay(ms);digitalWrite(PIN_VIBRATION,LOW);}
void beep(){if(buzzerEnabled)tone(PIN_BUZZER,1800,55);}

void showSetup(){
  stopAnimation();uiMode=UI_SETUP;setupLastActivity=millis();
  showLines("Conecte no WiFi","Kapibatchi","PASS: "+apPassword,"192.168.4.199");
}
void showWelcome(){
  stopAnimation();showLines("Bem-vindo!","Wi-Fi conectado",WiFi.SSID(),"Kapibatchi");
  delay(1000);drawEyes();uiMode=UI_FACE;lastFaceActivity=millis();
}
void showStatus(){
  stopAnimation();uiMode=UI_STATUS;display.clearDisplay();display.setTextSize(1);display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0);display.print("STATUS KAPIBATCHI");
  display.setCursor(0,13);display.print("WiFi: ");display.print(WiFi.status()==WL_CONNECTED?WiFi.SSID():"DESCONECTADO");
  display.setCursor(0,25);display.print("IP: ");display.print(WiFi.status()==WL_CONNECTED?WiFi.localIP().toString():"---");
  display.setCursor(0,37);display.print("Sinal: ");if(WiFi.status()==WL_CONNECTED){display.print(WiFi.RSSI());display.print("dBm ");display.print(signalQuality());display.print("%");}else display.print("---");
  display.setCursor(0,49);display.print("ID: ");display.print(macID);display.display();
}
void showMenu(){
  stopAnimation();uiMode=UI_MENU;display.clearDisplay();display.setTextSize(1);display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0);display.print("CONFIGURACOES");
  for(int i=0;i<MENU_COUNT;i++){display.setCursor(0,13+i*10);display.print(i==menuIndex?"> ":"  ");display.print(menuItems[i]);}
  display.display();
}
void ensureSetupAP(){
  WiFi.mode(WIFI_AP_STA);WiFi.softAPConfig(setupIP,setupGateway,setupSubnet);
  if(WiFi.softAPIP()!=setupIP)WiFi.softAP(SETUP_AP_SSID,apPassword.c_str());
  else if(WiFi.softAPSSID()!=String(SETUP_AP_SSID))WiFi.softAP(SETUP_AP_SSID,apPassword.c_str());
}
void startSetupAP(){
  WiFi.mode(WIFI_AP_STA);WiFi.softAPConfig(setupIP,setupGateway,setupSubnet);
  bool ok=WiFi.softAP(SETUP_AP_SSID,apPassword.c_str());
  Serial.println(ok?"AP Kapibatchi iniciado.":"ERRO ao iniciar AP.");
  Serial.print("PASS: ");Serial.println(apPassword);Serial.print("IP: ");Serial.println(WiFi.softAPIP());
  showSetup();
}
void connectSaved(){
  if(!savedSSID.length()){startSetupAP();return;}
  WiFi.mode(WIFI_STA);WiFi.begin(savedSSID.c_str(),savedPASS.c_str());
  showLines("Kapibatchi","Conectando Wi-Fi...",savedSSID);
  uint32_t t=millis();while(WiFi.status()!=WL_CONNECTED&&millis()-t<12000){server.handleClient();delay(50);}
  if(WiFi.status()==WL_CONNECTED){Serial.println("Wi-Fi conectado.");showWelcome();}
  else{Serial.println("Wi-Fi salvo indisponivel.");ensureSetupAP();uiMode=UI_WIFI_LOST;showLines("Aguardando","conexao Wi-Fi...");}
}

String htmlEscape(String s){
  s.replace("&","&amp;");s.replace("<","&lt;");s.replace(">","&gt;");s.replace("\"","&quot;");s.replace("'","&#39;");return s;
}
String htmlHead(){
return R"HTML(<!doctype html><html lang="pt-BR"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<style>
body{font-family:system-ui;background:#081018;color:#eef;max-width:900px;margin:auto;padding:20px}.c{background:#111b27;border:1px solid #26384a;border-radius:18px;padding:18px;margin:14px 0}
input,select,button{padding:12px;border-radius:10px;border:1px solid #345;margin:5px;background:#0d1722;color:white}button{background:#5de0bd;color:#001;font-weight:700;cursor:pointer}
a{color:#65d9ff}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(240px,1fr));gap:12px}.net{display:flex;align-items:center;justify-content:space-between;gap:10px;width:100%;margin:7px 0;text-align:left}
.net small{opacity:.75}.selected{outline:2px solid #5de0bd}.hidden{display:none}.muted{color:#9fb1c7}.wire{font-family:monospace;background:#081018;padding:12px;border-radius:10px;line-height:1.7}
</style></head><body>)HTML";
}
String assemblyPage(){
 String s=htmlHead()+R"HTML(<h1>Kapibatchi - Montagem</h1><div class='c'><a href='/'>← Voltar</a><p>Escolha a montagem:</p>
<select id='m' onchange='d()'><option value='display'>Somente Display</option><option value='touch'>Display + Touch</option><option value='vibro'>Display + Touch + Vibração</option><option value='battery'>Display + Touch + Bateria/Carregador</option><option value='all'>Todos os hardwares</option></select>
<div id='x' class='wire'></div></div><script>
function d(){let m=document.getElementById('m').value;let a=['OLED VCC → 3.3V','OLED GND → GND','OLED SDA → GPIO 8','OLED SCL → GPIO 9'];
if(m!='display')a.push('TTP223 SIG → GPIO 7','TTP223 VCC → 3.3V','TTP223 GND → GND');
if(m=='vibro'||m=='all')a.push('Vibração controle → GPIO 10');
if(m=='battery'||m=='all')a.push('Bateria → TP4056 USB-C','Divisor 100k/100k → GPIO 3 ADC');
if(m=='all')a.push('Buzzer → GPIO 5');document.getElementById('x').innerHTML=a.join('<br>')}d()</script></body></html>)HTML";
 return s;
}
String wifiListHtml(){
  ensureSetupAP();
  int n=WiFi.scanNetworks(false,true);
  struct Net{String ssid;int rssi;wifi_auth_mode_t enc;};
  Net nets[24];int count=0;
  for(int i=0;i<n&&count<24;i++){
    String s=WiFi.SSID(i);if(!s.length())continue;
    int found=-1;for(int j=0;j<count;j++)if(nets[j].ssid==s){found=j;break;}
    if(found>=0){if(WiFi.RSSI(i)>nets[found].rssi){nets[found].rssi=WiFi.RSSI(i);nets[found].enc=WiFi.encryptionType(i);}continue;}
    nets[count++]={s,WiFi.RSSI(i),WiFi.encryptionType(i)};
  }
  for(int a=0;a<count;a++)for(int b=a+1;b<count;b++)if(nets[b].rssi>nets[a].rssi){Net t=nets[a];nets[a]=nets[b];nets[b]=t;}
  String h;
  if(!count)h="<p class='muted'>Nenhuma rede encontrada. Toque em Atualizar redes.</p>";
  for(int i=0;i<count;i++){
    bool open=nets[i].enc==WIFI_AUTH_OPEN;int q=signalQualityFromRSSI(nets[i].rssi);
    String ss=htmlEscape(nets[i].ssid);
    h+="<button type='button' class='net' data-ssid=\""+ss+"\" data-open='"+String(open?1:0)+"' onclick='pick(this)'><span><b>"+ss+"</b><br><small>"+String(nets[i].rssi)+" dBm • "+qualityName(nets[i].rssi)+"</small></span><span>"+String(q)+"%</span></button>";
  }
  WiFi.scanDelete();return h;
}
String mainPage(){
 String st=WiFi.status()==WL_CONNECTED?htmlEscape(WiFi.SSID()):"Desconectado";
 String ip=WiFi.status()==WL_CONNECTED?WiFi.localIP().toString():setupIP.toString();
 String h=htmlHead()+"<h1>Kapibatchi</h1><div class='grid'><div class='c'><h2>Wi-Fi</h2><p>Status: "+st+"</p><p>IP: "+ip+"</p><p>ID: "+macID+"</p><p>Sinal: "+String(signalQuality())+"%</p>";
 h+=R"HTML(<div id="nets"><p class="muted">Toque em <b>Procurar redes</b> para listar as redes Wi-Fi próximas.</p></div>
<button type="button" onclick="scan()">↻ Procurar / atualizar redes</button>
<form action="/wifi" method="POST" id="wf"><input type="hidden" name="ssid" id="ssid"><p id="chosen" class="muted">Nenhuma rede selecionada.</p>
<input id="pass" name="pass" type="password" placeholder="Senha" disabled><button id="save" disabled>Conectar</button></form>
<script>
async function scan(){let x=document.getElementById('nets');x.innerHTML='<p>Procurando redes...</p>';try{let r=await fetch('/scan');x.innerHTML=await r.text()}catch(e){x.innerHTML='<p>Falha ao procurar redes.</p>'}}
function pick(b){document.querySelectorAll('.net').forEach(x=>x.classList.remove('selected'));b.classList.add('selected');let s=b.dataset.ssid,o=b.dataset.open==='1';document.getElementById('ssid').value=s;document.getElementById('chosen').textContent='Rede selecionada: '+s+(o?' (aberta)':'');let p=document.getElementById('pass');p.disabled=o;p.required=!o;if(o)p.value='';document.getElementById('save').disabled=false;if(!o)p.focus()}
</script>)HTML";
 h+="</div><div class='c'><h2>Display</h2><form action='/saver' method='POST'><select name='t'><option value='30'>30 segundos</option><option value='60'>1 minuto</option><option value='300'>5 minutos</option><option value='600'>10 minutos</option></select><button>Tempo proteção de tela</button></form><p>Proteção: action_yawn.qgif</p></div>";
 h+="<div class='c'><h2>Hardware</h2><p>OLED SDA GPIO 8 / SCL GPIO 9<br>Touch GPIO 7<br>Buzzer GPIO 5<br>Vibração GPIO 10<br>Bateria ADC GPIO 3</p><a href='/montagem'>Detalhes de montagem</a></div></div><div class='c'><p>Firmware "+String(FW_VERSION)+"</p></div></body></html>";
 return h;
}
void sendHtml(const String&s){server.send(200,"text/html; charset=utf-8",s);}
void setupWeb(){
 server.on("/",[](){sendHtml(mainPage());});
 server.on("/montagem",[](){sendHtml(assemblyPage());});
 server.on("/scan",HTTP_GET,[](){server.send(200,"text/html; charset=utf-8",wifiListHtml());});
 server.on("/wifi",HTTP_POST,[](){
   String s=server.arg("ssid"),p=server.arg("pass");
   if(s.length()){
     prefs.putString("ssid",s);prefs.putString("pass",p);savedSSID=s;savedPASS=p;
     sendHtml(htmlHead()+"<h2>Wi-Fi salvo.</h2><p>Kapibatchi vai tentar conectar a <b>"+htmlEscape(s)+"</b>...</p></body></html>");
     delay(250);WiFi.softAPdisconnect(true);connectSaved();
   }else server.send(400,"text/plain; charset=utf-8","SSID inválido");
 });
 server.on("/saver",HTTP_POST,[](){uint32_t sec=server.arg("t").toInt();if(sec==30||sec==60||sec==300||sec==600){saverMs=sec*1000UL;prefs.putUInt("saver",sec);}server.sendHeader("Location","/");server.send(303);});
 server.begin();
}

void wifiLostLoop(){
 static uint32_t phase=0;static bool textPhase=true;
 if(WiFi.status()==WL_CONNECTED){WiFi.softAPdisconnect(true);showWelcome();return;}
 if(millis()-lastWiFiTry>WIFI_RETRY_MS){lastWiFiTry=millis();WiFi.reconnect();}
 if(!animActive&&millis()-phase>2500){
   phase=millis();textPhase=!textPhase;
   if(textPhase)showLines("Aguardando","conexao Wi-Fi...");
   else startAnimation(PINGPONG_DATA,PINGPONG_DELAYS,PINGPONG_FRAMES,false,UI_WIFI_LOST);
 }
 animationTick();
}

void setup(){
 Serial.begin(115200);delay(600);
 pinMode(PIN_TOUCH,INPUT);pinMode(PIN_VIBRATION,OUTPUT);pinMode(PIN_BUZZER,OUTPUT);analogReadResolution(12);
 Wire.begin(PIN_SDA,PIN_SCL);oledOK=display.begin(SSD1306_SWITCHCAPVCC,OLED_ADDR);
 WiFi.mode(WIFI_STA);macID=WiFi.macAddress();apPassword=macPassword();
 prefs.begin("kapibatchi",false);savedSSID=prefs.getString("ssid","");savedPASS=prefs.getString("pass","");
 uint32_t sv=prefs.getUInt("saver",60);saverMs=(sv==30||sv==60||sv==300||sv==600)?sv*1000UL:60000UL;
 Serial.println("\n=== KAPIBATCHI V"+String(FW_VERSION)+" ===");Serial.println("MAC: "+macID);Serial.println("AP PASS: "+apPassword);
 setupWeb();connectSaved();
}
void loop(){
 server.handleClient();

 bool t=digitalRead(PIN_TOUCH);
 // Wake imediato no INICIO do toque, sem esperar soltar o dedo.
 if(t&&!lastTouch){
   touchStart=millis();
   if(uiMode==UI_SCREENSAVER){showSetup();lastTouch=t;return;}
 }
 if(!t&&lastTouch){
   uint32_t held=millis()-touchStart;setupLastActivity=lastFaceActivity=millis();
   if(uiMode==UI_SETUP){showSetup();}
   else if(uiMode==UI_STATUS){drawEyes();uiMode=UI_FACE;}
   else if(uiMode==UI_MENU){
     if(held>800){
       if(menuIndex==0)showStatus();
       else if(menuIndex==1){uiMode=UI_SCREENSAVER;startAnimation(YAWN_DATA,YAWN_DELAYS,YAWN_FRAMES,false,UI_MENU);}
       else if(menuIndex==2){vibrationEnabled=!vibrationEnabled;showMenu();}
       else if(menuIndex==3){buzzerEnabled=!buzzerEnabled;showMenu();}
       else{drawEyes();uiMode=UI_FACE;}
     }else{menuIndex=(menuIndex+1)%MENU_COUNT;showMenu();}
   }else if(held>1200){menuIndex=0;showMenu();}
   else{vibrate();beep();drawEyes();}
 }
 lastTouch=t;

 if(uiMode==UI_WIFI_LOST){wifiLostLoop();return;}
 if(WiFi.status()!=WL_CONNECTED&&savedSSID.length()&&uiMode!=UI_SETUP&&uiMode!=UI_SCREENSAVER){
   ensureSetupAP();uiMode=UI_WIFI_LOST;showLines("Aguardando","conexao Wi-Fi...");return;
 }

 if(uiMode==UI_SETUP&&millis()-setupLastActivity>WIFI_SETUP_IDLE_MS){
   uiMode=UI_SCREENSAVER;startAnimation(PINGPONG_DATA,PINGPONG_DELAYS,PINGPONG_FRAMES,true,UI_SCREENSAVER);
 }
 if(uiMode==UI_SCREENSAVER){animationTick();return;}

 if(uiMode==UI_FACE&&millis()-lastFaceActivity>saverMs&&!animActive){
   startAnimation(YAWN_DATA,YAWN_DELAYS,YAWN_FRAMES,false,UI_FACE);
 }
 animationTick();

 // Se a animacao do menu terminou, volta ao menu.
 if(!animActive&&uiMode==UI_SCREENSAVER&&animReturnMode==UI_MENU)showMenu();
}
