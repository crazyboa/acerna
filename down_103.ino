// 220810 down-awsLight-101, 102 기본 plc test ok, 8/16 103 test start  XR10 센서쇼트, 센서오픈, 제어출력, 온도단위 상태 추가 표현
// 아두이노버젼 : 1.8.19
#include <ArduinoJson.h>
#include <CRC.h>
#include "credentials.h"
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <FS.h>
#include <PubSubClient.h>                                          // version 2.7.0 ,by Nick O'leary, A client library for MQTT messaging
#include <SoftwareSerial.h>
#include <Ticker.h>
#include <WebSocketsServer.h>
#include <WiFiClient.h>

#define TRIGGER_PIN 0                                              // trigger pin 0(D3)
//#define URL_fw_Bin "http://i2r.link/download/"                     // fw_bin 파일 download 할 URL
#define URL_fw_Bin "https://github.com/crazyboa/acerna/"               // fw_bin 파일 download 할 URL
#define PUB_MSG_BUFFER_SIZE 120                                    // 7.3 100 (약 71)
#define Slave_id 0xFF                                              // plc default address 255, 0xFF 
#define PIN_TXEN 4                                                 // LOW(0):수신모드 HIGH(1):송신모드
#define RTU_TIMEOUT_TIME 450                                       // 센서 MODBUS RTU통신 TIMEOUT 설정 500ms
#define RTU_OK -1
#define RTU_TIMEOUT 1
#define RTU_CRC_ERR 2
#define RTU_DATA_ERR 3
//#define fileName down_103.bin
SoftwareSerial modbus(D7, D4);                                // RX, TX
HTTPClient http;
ESP8266WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);
WiFiClient espClient;
PubSubClient mqtt_client(espClient);
IPAddress apIP(192, 168, 4, 1);                              // AP mode IP address 192.168.4.1
IPAddress netMsk(255, 255, 255, 0);                          // Subnetmask address 255,255,255,0

const char* mqtt_server = "broker.mqtt-dashboard.com";             // MQTT 브로커 서버주소
const char* outTopic = "/acerna1/outTopic3/localcontrol";          // 7.3 변경 
const char* inTopic = "/acerna1/inTopic3/local/cmd";               // 7.3 변경 
char ssid[40] = "";
char password[50] = "";
bool reboot = false;                                               // 7.14 reboot 가 "1" 이면 원격재부팅
bool rmsetDO = false;                                              // 7.19 rmsetDO 가 "1" 이면 원격으로 릴레이 출력 ON/OFF 설정 가능
bool rmsetSV1 = false;
String setRO;
int remSV1 = 3;                                                    // defalt 원격 SV1
unsigned int mqtt_connect_fail_cnt = 0;                            // 7.8 fail_cnt 10 이면 reboot
int type=18; // 기기 인식번호 -> display에 사용 6= LS PLC XEC-DR14E
unsigned int countTick=0;
unsigned int countMqtt=0;
unsigned int countMeasure=0;
const char *clientName = "";                              // 사물 이름 (thing ID) 자동생성
//드라이브텍
const char *host = "***-ats.iot.us-east-2.amazonaws.com"; // AWS IoT Core 주소
bool mqttConnected = false;                               // true: 연결, false: 끊김
unsigned int mqtt_cnt;
String ipAct = "";
char mac[20];                                             // mac address
String sMac;
int act = 0;                                              // act 0: 멈춤, 1: 파일다운로드 실행, 2: plc 릴레이출력 실행 
int outPlc = 0;
int bootMode = 0;                                         // 0:station  1:AP
int Out[8]={0}, In[10]={0};                               // plc 입력과 출력 저장 
String sDI = "0000", pre_sDI = "0000";                    // 입력값이 달라질 때만 mqtt로 송신
String sRO = "0000", pre_sRO = "0000";
unsigned int ST1 = 0, pre_ST1 = 0;
unsigned int PM = 0, pre_PM = 0;
//bool status_bit0_XR1=false, status_bit1_XR1=false,status_bit2_XR1=false, status_bit3_XR1=false; // XR-1 [0]-FALSE:썹씨, TRUE:화씨, [1]-제어출력 TRUE:ON,FALSE:OFF [2]-센서오픈 TRUE:에러, FALSE:정상, [3]-센서쇼트 TRUE:에러, FALSE:정상
byte noOut = 0x00;                                        // mqtt로 수신된 plc출력 명령 noOut : 출력핀번호 
bool valueOut = false;                                    // mqtt로 수신된 plc출력 명령 valueOut : 출력값
unsigned long previousMillis = 0;     
const long interval = 1000;                               // 1초마다 실행되는 time 간격
byte buff[50];                                            // Modbus RTU Response에 사용되는 버퍼 메모리 할당
unsigned long prev_POLL;                                  // modbus rtu poll time check (500ms)
unsigned long prev_REPORT;                                // publish time check (1min)  현재 미사용
byte plc_addr;
unsigned int plc_baud;
unsigned int rtu_ok_cnt = 0;                              //[추가] modbus rtu 통신 정상, 실패, 회수 체크
unsigned int rtu_fail_cnt = 0;
unsigned int rtu_timeout_cnt = 0;
unsigned int rtu_dataerr_cnt = 0;
unsigned int rtu_crcerr_cnt = 0;
signed int PV1, SV1;  
signed int pre_PV1 = 200, pre_SV1 = 50;
bool DI[]={false,false,false,false};
bool RO[]={false,false,false,false};
int re1 = -1, re2 = -1, re3 = -1, re4 = -1;
unsigned int poll_cnt = 0;
//const unsigned long REPORTING_PERIOD_MS = 5000;           // 10sec MQTT PUBLISH INTERVAL 
const unsigned long POLLING_PERIOD_MS = 1000;               // Default 500msec  
volatile unsigned long timer0_millis;                       // 22.7.3 timer 전역변수 사용자의도에 따라 변경가능한 vloatile 타입으로 시간변수 추가
unsigned long POLL_PREV_MS = 0;                             // modbus rtu poll time check
unsigned long REPORT_PREV_MS = 0;                           // publish time check (2sec)
unsigned long TIMEOUT_PREV_MS = 0;                          // sensor modbus rtu timeout time check
//const unsigned long WIFI_CHECK_INTERVAL = 60000;          // wifi 접속확인 간격, mqtt publish time cycle 1분
unsigned long WIFI_CHECK_PREV_MS = 0;
const char* wl_status_to_string(wl_status_t status) {              // 7.4 추가
  switch (status) {
    case WL_NO_SHIELD: return "WL_NO_SHIELD";
    case WL_IDLE_STATUS: return "WL_IDLE_STATUS";
    case WL_NO_SSID_AVAIL: return "WL_NO_SSID_AVAIL";
    case WL_SCAN_COMPLETED: return "WL_SCAN_COMPLETED";
    case WL_CONNECTED: return "WL_CONNECTED";
    case WL_CONNECT_FAILED: return "WL_CONNECT_FAILED";
    case WL_CONNECTION_LOST: return "WL_CONNECTION_LOST";
    case WL_DISCONNECTED: return "WL_DISCONNECTED";
  }
}
//json을 위한 설정
StaticJsonDocument<130> Doc;
DeserializationError error;
JsonObject root;

//void crd16Rtu();
void factoryDefault();
void GoHome();
void GoHomeWifi();
void handleDownload();
void handleNotFound();
void handleRoot();
void handleOn();
void handleScan();
void handleWifi();
void handleWifiSave();
void readConfig();
void mqtt_reconnect();
void saveConfig();
void serialEvent();
//void setClock();
void tick();
void tickMeasure();
void tickMqtt();
void upWebSocket();
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length);

int modbus_rtu(byte req[], int req_len, byte res[], int res_length);
void init_values();
int read_plc_DI_status(byte slave_id);
int read_plc_RO_status(byte slave_id);
int read_plc_baud(byte slave_id);
int set_plc_baud(byte slave_id, byte baud);
int set_plc_RO(byte slave_id, byte ch_num, bool sw);
int set_all_RO(byte slave_id, bool sw);
void set_int_XR10();
void remote_set_SV1_XR10();
int set_SV1_XR10(byte slave_id, signed int setSV);
int read_PV_XR10(byte slave_id);
int read_XR10(byte slave_id);

void control_alarm();
void local_control();
/*
void callback(char* topic, byte* payload, unsigned int length) {                         // Mqtt message 수신받았을 때 처리
  //Serial.print("Topic [");  Serial.print(topic);  Serial.println("] ");
  
  String recv_msg = "";
  for (int i = 0; i < length; i++) {
    recv_msg += (char)payload[i];
  }
  Serial.println(recv_msg);
  //StaticJsonDocument<128> Doc;
  //DeserializationError error = deserializeJson(Doc, recv_msg);
  error = deserializeJson(Doc, recv_msg);
  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
    return;
  }
  if(Doc["pid"] == sMac) {                                           // 7.13 pid 가 같고 reboot = 1 일 때 원격 재부팅  
    //Serial.println(sMac);
    if(Doc["reboot"] == "1") {
      reboot = true;
    } else { 
      reboot = false;
      if (Doc["set_RO"] != NULL && Doc["rmsetDO"] == "1") {
        rmsetDO = true;
        if (Doc["set_RO"] == "0") { setRO = "0"; } 
        else if (Doc["set_RO"] == "1") { setRO = "1"; } 
        else if (Doc["set_RO"] == "2") { setRO = "2"; } 
        else if (Doc["set_RO"] == "3") { setRO = "3"; } 
        else if (Doc["set_RO"] == "4") { setRO = "4"; } 
        else if (Doc["set_RO"] == "5") { setRO = "5"; } 
        else if (Doc["set_RO"] == "6") { setRO = "6"; } 
        else if (Doc["set_RO"] == "7") { setRO = "7"; } 
        else if (Doc["set_RO"] == "8") { setRO = "8"; }
        else if (Doc["set_RO"] == "9") { setRO = "9"; }
        else if (Doc["set_RO"] == "a") { setRO = "a"; }
        else if (Doc["set_RO"] == "b") { setRO = "b"; }
        else if (Doc["set_RO"] == "c") { setRO = "c"; }
        else if (Doc["set_RO"] == "d") { setRO = "d"; }
        else if (Doc["set_RO"] == "e") { setRO = "e"; }
        else if (Doc["set_RO"] == "f") { setRO = "f"; }
        Serial.print("setRO: "); Serial.println(setRO);  
      } else if (Doc["rmsetDO"] != "1") {
        rmsetDO = false;
      }
      if (Doc["setSV1"] != NULL && Doc["rmsetSV1"] == "1") {
        rmsetSV1 = true; 
        remSV1 = Doc["setSV1"];
        //Serial.print("remSV1: "); Serial.println(remSV1); //delay(300);
      } else if (Doc["rmsetSV1"] != "1") {
        rmsetSV1 = false; 
      }
    }
  } 
}
*/
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.print(" ");
  Serial.println("callback func enter");
  String s;
  deserializeJson(Doc,payload);
  root = Doc.as<JsonObject>();
  const char* macIn = root["pid"];
  if( sMac.equals(String(macIn))) {
    noOut = root["outNo"];                    // noOut : 출력핀번호
    int value = root["value"];
    outPlc = 1;
    RO[noOut] = value;
  }
} 
// AWS IOT 사물 인증서 인증 시작
X509List ca(ca_str);
X509List cert(cert_str);
PrivateKey key(key_str);
WiFiClientSecure wifiClient;
//PubSubClient client(host, 8883, callback, wifiClient); //set  MQTT port number to 8883 as per //standard
// AWS IOT 사물 인증서 인증 완료

void tick() {
  countTick++;
  if(countTick > 10000)
    countTick=0;
  //if((countTick%5)==0)
    tickMeasure();
  // 접속하면 서버에 자동 등록하기 위해 10회 통신하고 그 다음 부터는 값이 변할 때만 전송한다.
  if((countTick%3)==0 && countMqtt<5)
    tickMqtt();
}

void tickMeasure() {
  //Serial.println ( WiFi.localIP() );
 // crd16Rtu();
}

void upWebSocket() {
  //HTML로 보냄
  String json = "{\"in0\":"; json += (String)DI[0];
  json += ",\"in1\":"; json += (String)DI[1];
  json += ",\"in2\":"; json += (String)DI[2];
  json += ",\"in3\":"; json += (String)DI[3];
  json += ",\"out0\":"; json += (String)RO[0];
  json += ",\"out1\":"; json += (String)RO[1];
  json += ",\"out2\":"; json += (String)RO[2];
  json += ",\"out3\":"; json += (String)RO[3];
  json += ",\"PV1\":"; json += (String)PV1;  
  json += ",\"SV1\":"; json += (String)SV1;    
  json += ",\"PM\":"; json += (String)PM;    
  json += ",\"ST1\":"; json += (String)ST1;    
  json += "}";
  webSocket.broadcastTXT(json.c_str(), json.length());
  Serial.print("upWebSocket buf size:"); Serial.println(json.length()+1);
  Serial.println(json);  
}

void tickMqtt() { 
  if(mqttConnected != true)
    return;
  char msg[75];  
  String json;
  //MQTT로 보냄
  json = "{";
  json += "\"pid\":\""; json += sMac;  json += "\"";
  json += ",\"ip\":\""; json += WiFi.localIP().toString();  json += "\"";
  json += ",\"type\":"; json += type;
  json += ",\"DI\":\""; json += sDI+"\"";
  //String sRO="";
  sRO=String(RO[0])+String(RO[1])+String(RO[2])+String(RO[3]);
  json += ",\"RO\":\""; json += sRO+"\"";  json += "}";
  json.toCharArray(msg, json.length()+1);
  Serial.print("tickMqtt:");  //임시
  Serial.println(msg);
  mqtt_client.publish(outTopic, msg);
  countMqtt++;
}

void setup() {
  Serial.begin(115200);
  modbus.begin(9600);
  pinMode(PIN_TXEN,OUTPUT);
  //Serial.println("mac address");
  //이름 자동으로 생성
  uint8_t macH[6]="";
  WiFi.macAddress(macH);
  //sprintf(mac,"%02x%02x%02x%02x%02x%02x%c",macH[5], macH[4], macH[3], macH[2], macH[1], macH[0],0);
  sprintf(mac,"%02x%02x%02x%c",macH[3], macH[4], macH[5],0);
  sMac = mac;
  clientName = mac;
  
  readConfig();
  if(ssid[0]==0)                                 // 저장된 ssid 가 없으면 AP mode로 부팅
    bootWifiAp();
  else {
    bootWifiStation();                           // 저장된 ssid 가 있으면 Station mode로 부팅
  }

  server.on("/", handleRoot);
  server.on("/download", handleDownload);
  server.on("/wifi", handleWifi);
  server.on("/wifisave", handleWifiSave);
  server.on("/scan", handleScan);
  server.onNotFound(handleNotFound);

  server.begin();
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  Serial.println(F("HTTP server started"));

  if(bootMode != 1) {                                   // bootmode 0 : station mode, 1: AP mode 
    // mqtt 설정
    wifiClient.setTrustAnchors(&ca);                    // AWS ATS 엔드포인트 CA 인증서(서버인증> "RSA 2048비트 키: Amazon Root CA 1" 다운로드)  
    wifiClient.setClientRSACert(&cert, &key);           // AWS IOT 사물인증서, 프라이빗 키
    Serial.println(F("Certifications and key are set"));
  
    //setClock();
    //client.setServer(host, 8883);
    mqtt_client.setServer(mqtt_server, 1883);
    mqtt_client.setCallback(callback);
    delay(3000);
    mqtt_reconnect();
  }
}
void bootWifiAp() {
  bootMode = 1;                                 //0:station  1:AP
  /* Soft AP network parameters */
  Serial.println("AP Mode");
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, netMsk);
  char aceMac[30];                              // 8.16 update
  sMac = "aceAP-"+sMac;
  sMac.toCharArray(aceMac, sMac.length()+1);
  WiFi.softAP(aceMac, "");
    ipAct = WiFi.softAPIP().toString();
  delay(500);                                 // Without delay I've seen the IP address blank
  Serial.print("AP IP address: ");
  Serial.println(ipAct);
}

void bootWifiStation() {
  //referance: https://www.arduino.cc/en/Reference/WiFiStatus
  //WL_NO_SHIELD:255 WL_IDLE_STATUS:0 WL_NO_SSID_AVAIL:1 WL_SCAN_COMPLETED:2
  //WL_CONNECTED:3 WL_CONNECT_FAILED:4 WL_CONNECTION_LOST:5 WL_DISCONNECTED:6
  //WiFi 연결
  bootMode=0; //0:station  1:AP
  Serial.println("Station Mode");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    //공장리셋
    if ( digitalRead(TRIGGER_PIN) == LOW ) 
      factoryDefault();
  }
  ipAct=WiFi.localIP().toString();          // 접속중인 local IP address
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(ipAct);
}

void update_started() {                         // ota update 시작됨
  Serial.println(F("CALLBACK:  HTTP update process started"));
}

void update_finished() {                        // ota update 완료됨
  Serial.println(F("CALLBACK:  HTTP update process finished"));
}

void update_progress(int cur, int total) {      // ota update 진행상태
  Serial.printf("CALLBACK:  HTTP update process at %d of %d bytes...\n", cur, total);
}

void update_error(int err) {                    // ota update 진행중 에러 발생코드
  Serial.printf("CALLBACK:  HTTP update fatal error code %d\n", err);
}

void download_program(String fileName) {        // ota 프로그램 파일 다운로드
  Serial.println(fileName);
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClient client;
    // The line below is optional. It can be used to blink the LED on the board during flashing
    // The LED will be on during download of one buffer of data from the network. The LED will
    // be off during writing that buffer to flash
    // On a good connection the LED should flash regularly. On a bad connection the LED will be
    // on much longer than it will be off. Other pins than LED_BUILTIN may be used. The second
    // value is used to put the LED on. If the LED is on with HIGH, that value should be passed
    ESPhttpUpdate.setLedPin(LED_BUILTIN, LOW);

    // Add optional callback notifiers
    ESPhttpUpdate.onStart(update_started);
    ESPhttpUpdate.onEnd(update_finished);
    ESPhttpUpdate.onProgress(update_progress);
    ESPhttpUpdate.onError(update_error);

    String ss;
    //ss=(String)URL_fw_Bin+fileName;                // ota 업데이트할 bin 파일 저장된 URL
    //ss="http://i2r.link/download/"+fileName;
    ss="https://github.crazyboa/acerna/"+fileName;
    Serial.println(ss);
    t_httpUpdate_return ret = ESPhttpUpdate.update(client, ss);
    //t_httpUpdate_return ret = ESPhttpUpdate.update(client, URL_fw_Bin);
    // Or:
    //t_httpUpdate_return ret = ESPhttpUpdate.update(client, "server", 80, "file.bin");
    
    switch (ret) {
      case HTTP_UPDATE_FAILED:
        Serial.printf("HTTP_UPDATE_FAILD Error (%d): %s\n", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
        break;
      case HTTP_UPDATE_NO_UPDATES:
        Serial.println("HTTP_UPDATE_NO_UPDATES");
        break;
      case HTTP_UPDATE_OK:
        Serial.println("HTTP_UPDATE_OK");
        break;
    }
  }
}

void mqtt_reconnect() {
  reboot = false;
  rmsetDO = false;
  rmsetSV1 = false;
  char msg[PUB_MSG_BUFFER_SIZE];
  //mac = String(ESP.getChipId(),HEX).c_str();                        // 7.14  추가  
  Serial.println(mac);
  //mqtt_client.setServer(mqtt_server, 1883);
  mqtt_client.setKeepAlive(61);                                       // 12.20 기존 2초 ->  7.7 61s 로 변경 
  delay(111);
  Serial.println("Attempting MQTT connection");
  if (mqtt_client.connect(String(ESP.getChipId(),HEX).c_str())) {
    // boolean connect(clientID) clientID = PID, Returns : true(connection succeeded), false(connection faild)  
    mqttConnected = true;
    Serial.println("MQTT connected.");
    long rssi = WiFi.RSSI();
    String json;
    json = "{\"pid\":\"";
    json += sMac; json += "\"";
    json += ",\"ip\":\""; json += WiFi.localIP().toString();  json += "\"";
    json += ",\"rssi\":"; json += (String) rssi;  json += "}";
    if(PUB_MSG_BUFFER_SIZE >= json.length()+1) {
      Serial.print("msg size:"); Serial.println(json.length()+1);
      json.toCharArray(msg, json.length()+1);
      Serial.println(msg);
      mqtt_client.publish(outTopic, msg);
    }
    mqtt_client.subscribe(inTopic);                            // 7.14 추가 inTopic = "/acerna1/inTopic2/local/cmd"
    } else {
    mqttConnected = false;
    Serial.print("failed, rc=");
    int mqtt_state = mqtt_client.state(); 
    if (mqtt_state != 0) {
      mqtt_connect_fail_cnt++;
    }
    if (mqtt_connect_fail_cnt > 10) {
      Serial.println("mqtt fail Reboot");
      delay(500);
      ESP.restart();
    }
    // Serial.print(mqtt_state);    Serial.print(" : "); 
    switch (mqtt_state) {
      case -4 : Serial.println("MQTT_CONNECTION_TIMEOUT"); break;
      case -3 : Serial.println("MQTT_CONNECTION_LOST"); break;
      case -2 : Serial.println("MQTT_CONNECTION_FAILED"); break;
      case -1 : Serial.println("MQTT_CONNECTION_DISCONNECTED"); break;
      case 0 : Serial.println("MQTT_CONNECTION_CONNECTED"); mqtt_connect_fail_cnt = 0; break;
      case 1 : Serial.println("MQTT_CONNECT_BAD_PROTOCOL"); break;
      case 2 : Serial.println("MQTT_CONNECT_BAD_CLIENT_ID"); break;
      case 3 : Serial.println("MQTT_CONNECT_UNAVAILABLE"); break;
      case 4 : Serial.println("MQTT_CONNECT_BAD_CREDENTIALS"); break;
      case 5 : Serial.println("MQTT_CONNECT_UNAUTHORIZED"); break;
      default : mqtt_connect_fail_cnt = 0;
    }
    Serial.println(" try again in 5 seconds");
    // Wait 5 seconds before retrying
    int no_busy = random(0,1999) + 5000;
  //    Serial.print("no_busy:"); Serial.println(no_busy);
    delay(no_busy);
  }
}
/*
void reconnect() {
  if(WiFi.status() != WL_CONNECTED)
    return;
  // Loop until we're reconnected
  //while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect(clientName)) {
      Serial.println("connected");
      // Once connected, publish an announcement...
      //client.publish(outTopic, "Reconnected");
      // ... and resubscribe
      client.subscribe(inTopic);
      mqttConnected=1;
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      mqttConnected=0;
      // Wait 5 seconds before retrying
      delay(5000);
    }
  //}
} */

// Set time via NTP, as required for x.509 validation
void setClock() {
  configTime(3 * 3600, 0, "pool.ntp.org", "time.nist.gov");      // 22.08.08 Timezone 9 for Korea 

  Serial.print("Waiting for NTP time sync: ");
  time_t now = time(nullptr);
  while (now < 8 * 3600 * 2) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println("");
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  Serial.print("Current time: ");
  Serial.print(asctime(&timeinfo));
}

//1초 마다 실행되는 시간함수
void doTick() {
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    // save the last time you blinked the LED
    previousMillis = currentMillis;
    tick();
  }  
}

//웹페이지에서 출력버튼을 실행한다.
// act=1 basic firmware를 내려받는다.
// act=2 plc 출력 실행
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length){
  if(type == WStype_TEXT){
    for(int i = 0; i < length; i++)
      Serial.print((char) payload[i]);
    Serial.println();
  }
  deserializeJson(Doc,payload);
  root = Doc.as<JsonObject>();
  act = root["act"];
  Serial.println(act);
  if(act == 1) {
    //download_program("down-permwareBasic.bin");       // 다운로드받을 바이너리 프로그램 파일
    download_program("down_103.bin");       // 다운로드받을 바이너리 프로그램 파일
  }
  if(act == 2) {                     //plc relay 출력
    noOut=root["no"];
    valueOut=root["value"];
    //Serial.print("act: ");  Serial.print(act); Serial.print(", ");
    Serial.print("no: ");   Serial.print(noOut); Serial.print(", ");
    Serial.print("valueOut: ");  Serial.println(valueOut);
    outPlc = 1;                                                // plc 통신으로 릴레이 출력 실행 명령
    Out[noOut] = valueOut;                                     // valueOut 출력값
    if(-1 == set_plc_RO(0xFF, (byte) noOut, valueOut)) {
      outPlc = 0;
      RO[noOut] = valueOut;                     // valueOut 출력값
      Serial.println("act:2 ok");
    } else {
      Serial.println("act:2 fail");
    }
    upWebSocket();                                   
  }
}

void tx_mode(){
  digitalWrite(PIN_TXEN,HIGH);
}
void rx_mode(){
  digitalWrite(PIN_TXEN,LOW);
}

int modbus_rtu(byte req[], int req_len, byte res[], int res_length){   
  // req[] : request 요청(crc는 제외), req_len : request 요청 길이(crc는 제외), res[] : response용 buffer, res_length : crc 포함된 응답 바이트 길이 
  int res_len = res_length;
  uint16_t req_crc = crc16(req,req_len,0x8005,0xFFFF,0,true,true);
  
  //2바이트로 표현된 result값을 1byte씩 쪼개서 2byte로 나눠담는 과정~!
  byte req_crc_high = req_crc >> 8;
  byte req_crc_low = req_crc & 0xFF;
  tx_mode();                           // request 송신
  modbus.write(req, req_len);
  modbus.write(req_crc_low);
  modbus.write(req_crc_high);
  modbus.flush();
  rx_mode();                           // request에 대한 response 수신모드로 변경
  //Serial.println("Request!");  
  unsigned long prev_TIMEOUT = millis();        // timeout 시간체크
  bool is_timeout = false;
  // 수신한다!
  while(!(modbus.available() == res_len)) {
    if(millis() - prev_TIMEOUT > RTU_TIMEOUT_TIME) {
      is_timeout = true;
      break;
    }
  }  
  if(is_timeout) {
    if(rtu_timeout_cnt < 50000) {
      rtu_timeout_cnt++;
    } else {
      rtu_timeout_cnt = 1;
    }    
    return RTU_TIMEOUT;
  }  

  int len = 0;
  while(modbus.available()) {    
    res[len] = modbus.read();
    len++;
  }
  if(len != res_len) {
    if(rtu_dataerr_cnt < 50000) {
      rtu_dataerr_cnt++;
    } else {
      rtu_dataerr_cnt = 1;
    }
    return RTU_DATA_ERR;
  }
  
  //response안에 들어있는 값을 길이 맞는 byte array에 담는 과정!
  byte data_frame[len-2];
  for(int i=0;i<len-2;i++) {
    data_frame[i] = res[i];
  }
  // 수신 CRC 체크
  uint16_t res_crc = crc16(data_frame,len-2,0x8005,0xFFFF,0,true,true);
  
  //2바이트로 표현된 result값을 1byte씩 쪼개서 2byte로 나눠담는 과정~!
  byte res_crc_high = res_crc >> 8;
  byte res_crc_low = res_crc & 0xFF;
/*                                                      22.4.15 주석처리 
  Serial.print("Res CRC=");
  Serial.print(res[len-2],HEX);
  Serial.print(res[len-1],HEX); Serial.print("\t");
  
  Serial.print("Calc CRC=");
  Serial.print(res_crc_low, HEX);
  Serial.println(res_crc_high, HEX);
*/  
  if(res_crc_low != res[len-2] || res_crc_high != res[len-1]) {
    if(rtu_crcerr_cnt < 50000) {
      rtu_crcerr_cnt++;
    } else {
      rtu_crcerr_cnt = 1;
    }
    return RTU_CRC_ERR;
  }
  //Serial.print("Response : ");  
  
  // modbus rtu Hex data 출력
  // for(int i=0;i<len-2;i++) {
  //  Serial.print(res[i],HEX);
  //  Serial.print(" ");
  //}
  //Serial.println();
  if(rtu_ok_cnt < 50000) {
    rtu_ok_cnt++;
  } else {
    rtu_ok_cnt = 1;
  }
  return RTU_OK;
}

void init_values() {
  if(poll_cnt > 60000) {
    poll_cnt = 0; rtu_ok_cnt=0;
  }
  if(rtu_ok_cnt > 60000) {
    rtu_fail_cnt = 0; rtu_timeout_cnt=0; rtu_crcerr_cnt=0; rtu_dataerr_cnt=0;
  }
  if((rtu_fail_cnt < 1) || (rtu_fail_cnt >= 50000)) {
    rtu_timeout_cnt = 0; rtu_crcerr_cnt=0; rtu_dataerr_cnt=0;
  }
  if(rtu_timeout_cnt > 50000) {
    rtu_timeout_cnt = 0;
  }
  if(rtu_crcerr_cnt > 50000) {
    rtu_crcerr_cnt = 0;
  }
  if(rtu_dataerr_cnt > 50000) {
    rtu_dataerr_cnt = 0;
  }
}
int read_plc_DI_status(byte slave_id) {
  // PLC DI ch별 입력상태 확인하기
  // REQ : slaveid 02 00 00 00 08 90 60, RES 4번째 BYTE 00 이면 ALL OFF 
  // DI[0]: 차압스위치 접점입력, DI[1]: SPARE, DI[2]: 온수순환펌프수동입력, DI[3]: FAN운전중 인터록 접점입력 
  byte request[] = {slave_id,0x02,0x00,0x00,0x00,0x08};
  int re = modbus_rtu(request,6,buff,6);
  if(re == RTU_OK) {
/*    for(int i=0;i<6;i++){
      Serial.print(buff[i],HEX);
      Serial.print(" "); 
    } */
    switch (buff[3]) {
      case 0x00:  DI[0] = false; DI[1] = false; DI[2] = false; DI[3] = false; break;
      case 0x01:  DI[0] = true;  DI[1] = false; DI[2] = false; DI[3] = false; break;
      case 0x02:  DI[0] = false; DI[1] = true;  DI[2] = false; DI[3] = false; break;
      case 0x03:  DI[0] = true;  DI[1] = true;  DI[2] = false; DI[3] = false; break;
      case 0x04:  DI[0] = false; DI[1] = false; DI[2] = true;  DI[3] = false; break;
      case 0x05:  DI[0] = true;  DI[1] = false; DI[2] = true;  DI[3] = false; break;
      case 0x06:  DI[0] = false; DI[1] = true;  DI[2] = true;  DI[3] = false; break;
      case 0x07:  DI[0] = true;  DI[1] = true;  DI[2] = true;  DI[3] = false; break;
      case 0x08:  DI[0] = false; DI[1] = false; DI[2] = false; DI[3] = true;  break;
      case 0x09:  DI[0] = true;  DI[1] = false; DI[2] = false; DI[3] = true;  break;
      case 0x0A:  DI[0] = false; DI[1] = true;  DI[2] = false; DI[3] = true;  break;
      case 0x0B:  DI[0] = true;  DI[1] = true;  DI[2] = false; DI[3] = true;  break;
      case 0x0C:  DI[0] = false; DI[1] = false; DI[2] = true;  DI[3] = true;  break;
      case 0x0D:  DI[0] = true;  DI[1] = false; DI[2] = true;  DI[3] = true;  break;
      case 0x0E:  DI[0] = false; DI[1] = true;  DI[2] = true;  DI[3] = true;  break;       
      case 0x0F:  DI[0] = true;  DI[1] = true;  DI[2] = true;  DI[3] = true;  break;
    }  
    // Serial.print(" ");
    Serial.print("DI:"); 
    //Serial.print(DI[0]);  Serial.print(" "); Serial.print(DI[1]); Serial.print(" "); Serial.print(DI[2]); Serial.print(" "); Serial.println(DI[3]);
    sDI = (String)DI[0];
    sDI += (String)DI[1];
    sDI += (String)DI[2];  
    sDI += (String)DI[3];
    Serial.println(sDI);
    if (pre_sDI != sDI) {
      upWebSocket();
      pre_sDI = sDI;
    } 
  } 
  else if (re != RTU_OK && rtu_fail_cnt < 50000) {
    rtu_fail_cnt++;
    Serial.println("read_plc_DI_status Failed.");
  } else { rtu_fail_cnt = 1; }
  return re;
}

int read_plc_RO_status(byte slave_id) {
  // PLC 모듈 ch별 출력상태 확인하기
  // REQ : slaveid 01 00 00 00 08 28 12, RES 4번째 BYTE 00 이면 ALL OFF 
  
  byte request[] = {slave_id,0x01,0x00,0x00,0x00,0x08};
  int re = modbus_rtu(request,6,buff,6);
  if(re == RTU_OK) {
/*    for(int i=0;i<6;i++){
      Serial.print(buff[i],HEX);
      Serial.print(" ");
    } */
    switch (buff[3]) {
      case 0x00:  RO[0] = false; RO[1] = false; RO[2] = false; RO[3] = false; break;
      case 0x01:  RO[0] = true;  RO[1] = false; RO[2] = false; RO[3] = false; break;
      case 0x02:  RO[0] = false; RO[1] = true;  RO[2] = false; RO[3] = false; break;
      case 0x03:  RO[0] = true;  RO[1] = true;  RO[2] = false; RO[3] = false; break;
      case 0x04:  RO[0] = false; RO[1] = false; RO[2] = true;  RO[3] = false; break;
      case 0x05:  RO[0] = true;  RO[1] = false; RO[2] = true;  RO[3] = false; break;
      case 0x06:  RO[0] = false; RO[1] = true;  RO[2] = true;  RO[3] = false; break;
      case 0x07:  RO[0] = true;  RO[1] = true;  RO[2] = true;  RO[3] = false; break;
      case 0x08:  RO[0] = false; RO[1] = false; RO[2] = false; RO[3] = true;  break;
      case 0x09:  RO[0] = true;  RO[1] = false; RO[2] = false; RO[3] = true;  break;
      case 0x0A:  RO[0] = false; RO[1] = true;  RO[2] = false; RO[3] = true;  break;
      case 0x0B:  RO[0] = true;  RO[1] = true;  RO[2] = false; RO[3] = true;  break;
      case 0x0C:  RO[0] = false; RO[1] = false; RO[2] = true;  RO[3] = true;  break;
      case 0x0D:  RO[0] = true;  RO[1] = false; RO[2] = true;  RO[3] = true;  break;
      case 0x0E:  RO[0] = false; RO[1] = true;  RO[2] = true;  RO[3] = true;  break;       
      case 0x0F:  RO[0] = true;  RO[1] = true;  RO[2] = true;  RO[3] = true;  break;
    }
    //Serial.print(" ");
    Serial.print("RO:"); 
    //Serial.print(RO[0]); Serial.print(" "); Serial.print(RO[1]); Serial.print(" "); Serial.print(RO[2]); Serial.print(" "); Serial.println(RO[3]);
    sRO = (String)RO[0];
    sRO += (String)RO[1];
    sRO += (String)RO[2];  
    sRO += (String)RO[3];
    Serial.println(sRO);
    if (pre_sRO != sRO) {
      upWebSocket();
      pre_sRO = sRO;
    } 
  } 
  else if (re != RTU_OK && rtu_fail_cnt < 50000) {
    rtu_fail_cnt++;
    Serial.println("read_plc_RO_status Failed.");
  } else { rtu_fail_cnt = 1; }
  return re;
}

int read_plc_baud(byte slave_id) {
  // PLC 통신 BAUD RATE 읽기 (4800, 9600, 19200) DEFAULT 9600
  // REQ : slaveid 03 03 E8 00 01 11 A4, RES : FF 03 02 00 03 D1 91
  // RES 5번째 BYTE 02,03,04이면 각각 4800, 9600, 19200 
  byte request[] = {slave_id,0x03,0x03,0xE8,0x00,0x01};
  int re = modbus_rtu(request,6,buff,7);
  if(re == RTU_OK) {
  // request가 성공적으로 수행되면 buff[4]
/*  for(int i=0;i<7;i++) {
      Serial.print(buff[i],HEX);
      Serial.print(" ");
    } */
    Serial.println();
    switch (buff[4]) {
      case 0x02 : plc_baud = 4800;
      case 0x03 : plc_baud = 9600;
      case 0x04 : plc_baud = 19200;
      default : plc_baud = 9600;      
    }
    Serial.print("plc baud : "); Serial.print(buff[4]); Serial.print("\t"); Serial.println(plc_baud);
  } else if (re != RTU_OK) {
    if(rtu_fail_cnt < 50000) { 
      rtu_fail_cnt++; 
    } else { rtu_fail_cnt = 1; }
    Serial.println("read_plc_baud failed."); 
  } 
  return re;
}

int set_plc_baud(byte slave_id, byte baud) {
  // 통신 BAUD RATE 설정하기 (4800, 9600, 19200) DEFAULT 9600
  // REQ : slaveid 10 03 E9 00 01 02 00 02 4A 0C, RES 9번째 BYTE 02,03,04이면 각각 4800,9600,19200
  // RES : FF 10 03 E9 00 01 C5 A7
  byte request[] = {slave_id,0x10,0x03,0xE9,0x00,0x01,0x02,0x00,baud};
  int re = modbus_rtu(request,9,buff,8);
  if(re == RTU_OK) {
   /* for(int i=0;i<8;i++){
      Serial.print(buff[i],HEX);
      Serial.print(" ");
    } 
    Serial.println("set_plc_baud Setting finished");  */
  } else if (re != RTU_OK) {
    if(rtu_fail_cnt < 50000) { 
      rtu_fail_cnt++; 
    } else { rtu_fail_cnt = 1; }
    Serial.println("set_plc_baud Failed.");
  }
  return re;
}

int set_plc_RO(byte slave_id, byte ch_num, bool sw) {
  // PLC RO 개별 채널별 on/off 제어 [slave_id : PLC rtu id(00~255), ch_num : ch_num number(0~3), sw : on or off]
  byte state = sw?0xFF:0x00;
  byte request_set[] = {slave_id,0x05,0x00,ch_num,state,0x00};
  int re = modbus_rtu(request_set,6,buff,8);
  if(re == RTU_OK) {
 /*   for(int i=0;i<8;i++) {
      Serial.print(buff[i],HEX);
      Serial.print(" ");
    }
    Serial.println("set_plc_RO finished!");  */
  } else if (re != RTU_OK) {
    if(rtu_fail_cnt < 50000) { 
      rtu_fail_cnt++; 
    } else { rtu_fail_cnt = 1; }
    Serial.println("set_plc_RO failed.");
  }
  return re;
}

int set_all_RO(byte slave_id, bool sw) {
  // all RO ch control [slave_id : relay rtu id(01~255), sw : true or false]
  // all RO ch  on  - REQ : FF 0F 00 00 00 08 01 FF 30 1D, RES : FF 0F 00 00 00 08 41 D3 
  // all RO ch off  - REQ : FF 0F 00 00 00 08 01 00 70 5D, RES : FF 0F 00 00 00 08 41 D3  
  byte state = sw?0xFF:0x00;
  byte request_all_set[] = {slave_id,0x0F,0x00,0x00,0x00,0x08,0x01,state};
  int re = modbus_rtu(request_all_set,8,buff,8);
  if(re == RTU_OK) {
    for(int i=0;i<8;i++) {
      Serial.print(buff[i],HEX);
      Serial.print(" ");
    } 
    Serial.println();
  } else if (re != RTU_OK) {
    if(rtu_fail_cnt < 50000) { 
      rtu_fail_cnt++; 
    } else { rtu_fail_cnt = 1; }
    Serial.println("set_all_RO failed.");
  }
  return re;   
}
void read_plc_addr() {
  byte read_addr_req[] = {0x00,0x03,0x00,0x00,0x00,0x01};     // 입출력모듈(DI/RO) 장치 주소 읽기
  int re = modbus_rtu(read_addr_req, 6, buff, 7); 
  //Serial.print("re:"); Serial.print(re); Serial.print("\t");
  if (re == RTU_OK) {                          // Modbus rtu 릴레이모듈 주소 읽기가 정상일 때
    plc_addr = buff[4];    
    // Serial.print("\t"); Serial.print("addr : "); Serial.print("\t"); Serial.print(plc_addr);
    // Serial.print(","); Serial.println(plc_addr, HEX);
    Serial.print("PLC Address: "); 
    Serial.println(buff[4], HEX);
  } else if (re != RTU_OK) {
    if(rtu_fail_cnt < 50000) { 
      rtu_fail_cnt++; 
    } else { rtu_fail_cnt = 1; }
    Serial.println("read_plc_addr failed.");
  }
}

void set_int_XR10(byte slave_id) {
   // REQ(int) : 국번 01 Func 06 시작주소상위 00 시작주소하위 00 데이터개수상위 00 데이터개수하위 01 crc16Low xx crc16High xx, 1번째 byte slave_id[default 0x01]
   // RES : 국번 01 Func 06 데이터개수 02 데이터1상위 00 데이터1하위 00 CRC16L CRC16H
  byte request[] = {0x01, 0x06, 0x00, 0x00, 0x00, 0x01};
  int re = modbus_rtu(request,6,buff,8);
  if(re == RTU_OK) {
    for(int i=0;i<8;i++) {
      Serial.print(buff[i],HEX);
      Serial.print(" ");
    } 
    Serial.println("set_int_XR10 success.");
  } else if (re != RTU_OK) {
    if(rtu_fail_cnt < 50000) { 
      rtu_fail_cnt++; 
    } else { rtu_fail_cnt = 1; }
    Serial.println("set_int_XR10 failed");
  }
}

void remote_set_SV1_XR10() {
  signed int setSV = remSV1;
  //Serial.println(setSV);
  if(RTU_OK == set_SV1_XR10(0x01, setSV)) {
    rmsetSV1 = false;
  } 
}

int set_SV1_XR10(byte slave_id, signed int setSV) {
  // REQ(int) : 국번 01 Func 06 시작주소상위 00 시작주소하위 02 데이터개수상위 01 데이터개수하위 2C crc16Low 28 crc16High 47, 1번째 byte slave_id[default 0x01]
  // 0도 설정 01 06 00 02 00 00,  10.0도 설정 01 06 00 02 01 2C, 25.5도 01 06 00 02 00 FF
  // RES = REQ 와 동일
  // RES 3번째 BYTE * 256 + 4번째 BYTE => 센서1 설정온도값
  byte SV1H = setSV/256;                                            // 8.16 255 -> 256 update
  //Serial.print("SV1H: "); Serial.println(SV1H, HEX); 
  byte SV1L = setSV%256;                                            // 8.16 255 -> 256 update
  //Serial.print("SV1L: "); Serial.println(SV1L, HEX);
  byte request[] = {0x01, 0x06, 0x00, 0x02, SV1H, SV1L};
  int re = modbus_rtu(request,6,buff,8);
  if(re == RTU_OK) {
    for(int i=0;i<8;i++) {
      Serial.print(buff[i],HEX);
      Serial.print(" ");
    }  
    rmsetSV1 = false;
    SV1 = buff[4]*256+buff[5];                                     // DSFOX-XR10 NTC 온도센서1 설정온도값  8.16 255 -> 256 update
    if (pre_SV1 != SV1) {
      upWebSocket();
      pre_SV1 = SV1;
    }
    Serial.print("SV1: ");  Serial.println(SV1); 
  } 
  else if (re != RTU_OK) {
    if(rtu_fail_cnt < 50000) { 
      rtu_fail_cnt++; 
    } else { rtu_fail_cnt = 1; }
    Serial.println("set_SV1_XR10 failed.");
    rmsetSV1 = false;
  }
  //delay(400);
  return re; 
}

int read_PV_XR10(byte slave_id) {
  // B3950 10K 1% NTC Thermal sensor(Resistance 25℃ 일때 10㏀, +- 1%) 측정온도범위 : -20℃ ~ 125℃
  // REQ(int) : 국번 01 Func 04 시작주소상위 00 시작주소하위 64 데이터개수상위 00 데이터개수하위 01 crc16Low 70 crc16High 15, 1번째 byte slave_id[default 0x01]
  // RES : 국번 01 Func 04 데이터개수 02 데이터1상위 00 데이터1하위 E8 CRC16L CRC16H
  // RES 3번째 BYTE * 256 + 4번째 BYTE => 센서1 현재온도값
  byte request[] = {0x01, 0x04, 0x00, 0x64, 0x00, 0x02};
  
  bool st_bit0_XR1, st_bit1_XR1, st_bit2_XR1, st_bit3_XR1; 
  
  int re = modbus_rtu(request,6,buff,9);
  if(re == RTU_OK) {
   /* for(int i=0;i<7;i++){
      Serial.print(buff[i],HEX);
      Serial.print(" ");
    } */ 
    PV1 = buff[3]*256+buff[4];                           // DSFOX-XR10 NTC 온도센서1 현재온도값  8.16 255 -> 256 update

    Serial.print("PV1: ");  Serial.println(PV1); 
    //Serial.print("status_XR1:"); Serial.println(buff[6], HEX);
    if((buff[6]&0x8) == 0x0) {
      st_bit3_XR1 = true;                            // true : 센서 쇼트, false : 정상 
      //Serial.println("센서 쇼트");
    } else {
      st_bit3_XR1 = false;                            // true : 센서 쇼트, false : 정상 
    }
    if((buff[6]&0x4) == 0x0) {
      st_bit2_XR1 = true;                            // true : 센서 오픈, false : 정상 
      //Serial.println("센서 오픈");
    } else {
      st_bit2_XR1 = false;                            // true : 센서 오픈, false : 정상 
    }
    if((buff[6]&0x2) == 0x0) {
      st_bit1_XR1 = true;                            // true : 제어출력 ON, false : OFF 
      PM = 1;
      //Serial.println("제어출력 ON");
    } else {
      st_bit1_XR1 = false;                            // true : 제어출력 ON, false : OFF 
      PM = 0;
    }
    if(st_bit2_XR1 && st_bit3_XR1) {
      ST1 = 1;                                        // 센서 이상
    } else {
      ST1 = 0;                                        // 센서 이상
    }
    if((buff[6]&0x1) != 0x0) {
      st_bit0_XR1 = true;                            // true : 화씨온도 설정상태, false : 섭씨온도 설정상태 
      //Serial.println("화씨온도");
    } else {
      st_bit0_XR1 = false;                            // true : 화씨온도 설정상태, false : 섭씨온도 설정상태 
    }
    //Serial.print("ST1: "); Serial.println(ST1);
    if ((pre_PV1 != PV1) || (pre_PM != PM) || (pre_ST1 != ST1)) {
      upWebSocket();
      pre_PV1 = PV1;
      pre_ST1 = ST1;
      pre_PM = PM;
    }
  } 
  else {
    if(rtu_fail_cnt < 50000) { 
      rtu_fail_cnt++; 
    } else { rtu_fail_cnt = 1; }
    Serial.println("read_PV_XR10 failed.");
  }
  return re;
}

int read_XR10(byte slave_id) {
  // Func 0x03 : Reading Holding Registers - 제품 내부 설정값을 확인
  // REQ(int) : 국번 01 Func 03 시작주소상위 00 시작주소하위 00 데이터개수상위 00 데이터개수하위 0B crc16Low C5 crc16High CD, 1번째 byte slave_id[default 0x01]
  // RES(int) : 국번 01 Func 03 데이터개수 16 데이터1상위 00 데이터1하위 00 데이터2상위 00 데이터2하위 00 ~ 데이터N상위 00 데이터N하위 00 CRC16L CRC16H
  // RES(int) 정상(예) - 1 3 16 0 1 0 0 0 E1 0 1 0 A 0 0 0 A 0 0 0 1 0 3 0 1 3 7E
  // Register Map [No, 주소, 읽기/쓰기, 구분, 설명, 단위, 비고]                         
  // 40001, 0000, 읽기/쓰기, 통신자료형, 0:Float / 1:Int,                               buff[3]*256+buff[4]
  // 40002, 0001, 읽기/쓰기, 온도단위설정, 0:℃ / 1:℉,                                  buff[5]*256+buff[6]
  // 40003, 0002, 읽기/쓰기, 설정온도(SV1), -55.0~99.9, ℃, -550~999(Int),              buff[7]*256+buff[8]
  // 40004, 0003, 읽기/쓰기, 출력기능선택, 0:COOL / 1:HEAT,                             buff[9]*256+buff[10]
  // 40005, 0004, 읽기/쓰기, 온도편차설정, 0.1~25.0, ℃, 1~250(Int),                    buff[11]*256+buff[12]
  // 40006, 0005, 읽기/쓰기, 출력지연시간설정, 0~60, 시간,                               buff[13]*256+buff[14] 
  // 40007, 0006, 읽기/쓰기, 출력지연시간설정, 0~59, 초,                                 buff[15]*256+buff[16]
  // 40008, 0007, 읽기/쓰기, 온도보정설정, -10.0~10.0, ℃, -100~100(Int),               buff[17]*256+buff[18]
  // 40009, 0008, 읽기/쓰기, 통신국번설정, 1~99, 번,                                    buff[19]*256+buff[20]
  // 40010, 0009, 읽기/쓰기, 통신속도설정, 0:1200/1:2400/2:4800/3:9600/4:19200, BPS,    buff[21]*256+buff[22]
  // 40011, 000A, 읽기/쓰기, 잠금설정, 0:잠금 / 1:미사용 ,                               buff[23]*256+buff[24]
  byte request[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x0B};
  bool outputmode;
  int re = modbus_rtu(request,6,buff,27);
  if(re == RTU_OK) {
  //  Serial.print("TIC: ");
  /*  for(int i=0;i<27;i++){
      Serial.print(buff[i],HEX);
      Serial.print(" ");
    } */
    outputmode = (bool)(buff[9]*256+buff[10]);
    Serial.print("outputmode:"); Serial.println(outputmode);   
    SV1 = buff[7]*256+buff[8];                              // TIC 온수순환펌프 제어출력 설정온도값 8.16 255 -> 256 update

    if (pre_SV1 != SV1) {
      upWebSocket();
      pre_SV1 = SV1;
    }
    Serial.print("SV1: ");  Serial.println(SV1); 
  } else if (re != RTU_OK) {
    if(rtu_fail_cnt < 50000) { 
      rtu_fail_cnt++; 
    } else { rtu_fail_cnt = 1; }
    Serial.println("read_XR10 failed.");
  }
  return re;
}
int set_off_plc_RO0() {
  int re00;
  if (RO[0] == true) {                            // RO[0] OFF
    int re00 = set_plc_RO(Slave_id, 0x00, false); 
    if (re00 == RTU_OK) {
      delay(50);
    }
  }
  return re00;
}
int set_on_plc_RO0() {
  int re00;
  if (RO[0] == false) {                           // RO[0] ON
    int re00 = set_plc_RO(Slave_id, 0x00, true); 
    if (re00 == RTU_OK) {
      delay(50);
    }
  }
  return re00;
}
int set_off_plc_RO1() {
  int re01;
  if (RO[1] == true) {                            // RO[1] OFF
    int re01 = set_plc_RO(Slave_id, 0x01, false); 
    if (re01 == RTU_OK) {
     delay(50);
    }
  }
  return re01;
}
int set_on_plc_RO1() {
  int re01;
  if (RO[1] == false) {                           // RO[1] ON
    int re01 = set_plc_RO(Slave_id, 0x01, true); 
    if (re01 == RTU_OK) {
      delay(50);
    }
  }
  return re01;
}
int set_off_plc_RO2() {
  int re02;
  if (RO[2] == true) {                            // RO[2] OFF
    int re02 = set_plc_RO(Slave_id, 0x02, false); 
    if (re02 == RTU_OK) {
      delay(50);
    }
  }
  return re02;
}
int set_on_plc_RO2() {
  int re02;
  if (RO[2] == false) {                           // RO[2] ON
    int re02 = set_plc_RO(Slave_id, 0x02, true); 
    if (re02 == RTU_OK) {
      delay(50);
    }
  }
  return re02;
}
int set_off_plc_RO3() {
  int re03;
  if (RO[3] == true) {                            // RO[3] OFF
    int re03 = set_plc_RO(Slave_id, 0x03, false); 
    if (re03 == RTU_OK) {
      delay(50);
    }
  }
  return re03;
}
int set_on_plc_RO3() {
  int re03;
  if (RO[3] == false) {                           // RO[3] ON
    re03 = set_plc_RO(Slave_id, 0x03, true); 
    if (re03 == RTU_OK) {
      delay(50);
    }
  }
  return re03;
}

void rmset_plc_RO() {
  if (setRO == "0" ) {                        // RO 0000   
    if( -1 == set_all_RO(Slave_id, false)) {
      rmsetDO = false; 
    } else rmsetDO = true;
  } else if (setRO == "1") {                 // RO 1000
    if ((-1 == set_on_plc_RO0()) && (-1 == set_off_plc_RO1()) && (-1 == set_off_plc_RO2()) && (-1 == set_off_plc_RO3())) {
      rmsetDO = false; 
    } else rmsetDO = true;
  } else if (setRO == "2") {                 // RO 0100
    if ((-1 == set_off_plc_RO0()) && (-1 == set_on_plc_RO1()) && (-1 == set_off_plc_RO2()) && (-1 == set_off_plc_RO3())) {
      rmsetDO = false; 
    } else rmsetDO = true;
  } else if (setRO == "3") {                 // RO 1100
    if ((-1 == set_on_plc_RO0()) && (-1 == set_on_plc_RO1()) && (-1 == set_off_plc_RO2()) && (-1 == set_off_plc_RO3())) {
      rmsetDO = false; 
    } else rmsetDO = true;    
  } else if (setRO == "4") {                // RO 0010
    if ((-1 == set_off_plc_RO0()) && (-1 == set_off_plc_RO1()) && (-1 == set_on_plc_RO2()) && (-1 == set_off_plc_RO3())) {
      rmsetDO = false; 
    } else rmsetDO = true;    
  } else if (setRO == "5") {               // RO 1010
    if ((-1 == set_on_plc_RO0()) && (-1 == set_off_plc_RO1()) && (-1 == set_on_plc_RO2()) && (-1 == set_off_plc_RO3())) {
      rmsetDO = false; 
    } else rmsetDO = true;    
  } else if (setRO == "6") {     // RO 0110
    if ((-1 == set_off_plc_RO0()) && (-1 == set_on_plc_RO1()) && (-1 == set_on_plc_RO2()) && (-1 == set_off_plc_RO3())) {
      rmsetDO = false; 
    } else rmsetDO = true;
  } else if (setRO == "7") {     // RO 1110    
    if ((-1 == set_on_plc_RO0()) && (-1 == set_on_plc_RO1()) && (-1 == set_on_plc_RO2()) && (-1 == set_off_plc_RO3())) {
      rmsetDO = false; 
    } else rmsetDO = true;
  } else if (setRO == "8") {     // RO 0001
    if ((-1 == set_off_plc_RO0()) && (-1 == set_off_plc_RO1()) && (-1 == set_off_plc_RO2()) && (-1 == set_on_plc_RO3())) {
      rmsetDO = false; 
    } else rmsetDO = true;
  } else if (setRO == "9") {     // RO 1001 
    if ((-1 == set_on_plc_RO0()) && (-1 == set_off_plc_RO1()) && (-1 == set_off_plc_RO2()) && (-1 == set_on_plc_RO3())) {
      rmsetDO = false; 
    } else rmsetDO = true;
  } else if (setRO == "a") {     // RO 0101
    if ((-1 == set_off_plc_RO0()) && (-1 == set_on_plc_RO1()) && (-1 == set_off_plc_RO2()) && (-1 == set_on_plc_RO3())) {
      rmsetDO = false; 
    } else rmsetDO = true;
  } else if (setRO == "b") {     // RO 1101
    if ((-1 == set_on_plc_RO0()) && (-1 == set_on_plc_RO1()) && (-1 == set_off_plc_RO2()) && (-1 == set_on_plc_RO3())) {
      rmsetDO = false; 
    } else rmsetDO = true;
  } else if (setRO == "c") {     // RO 0011
    if ((-1 == set_off_plc_RO0()) && (-1 == set_off_plc_RO1()) && (-1 == set_on_plc_RO2()) && (-1 == set_on_plc_RO3())) {
      rmsetDO = false; 
    } else rmsetDO = true;
  } else if (setRO == "d") {     // RO 1011
    if ((-1 == set_on_plc_RO0()) && (-1 == set_off_plc_RO1()) && (-1 == set_on_plc_RO2()) && (-1 == set_on_plc_RO3())) {
      rmsetDO = false; 
    } else rmsetDO = true;
  } else if (setRO == "e") {     // RO 0111
    if ((-1 == set_off_plc_RO0()) && (-1 == set_on_plc_RO1()) && (-1 == set_on_plc_RO2()) && (-1 == set_on_plc_RO3())) {
      rmsetDO = false; 
    } else rmsetDO = true;
  } else if (setRO == "f") {                       // RO 1111
    if( -1 == set_all_RO(Slave_id, true)) {
      rmsetDO = false; 
    } else rmsetDO = true;    
  }
}

void control_alarm() {
  // 필터 차압스위치 A접점 시그널이 DI 1번으로 입력될 때 또는 온도센서이상 발생되었을 때 릴레이(R0) (램프알람)출력 ON
  if (((DI[0]==true) || (ST1==1)) && (RO[0]==false)) {                           // RO[0] ON
    set_plc_RO(Slave_id, 0x00, true);
    RO[0] = 1; 
    upWebSocket();
  }
  else if ((DI[0]==false) && (ST1==0) && (RO[0]==true)) {                      // RO[0] OFF
    set_plc_RO(Slave_id, 0x00, false);
    RO[0] = 0; 
    upWebSocket();
  }
  //Serial.print("RO[0]:"); Serial.println(RO[0]); 
}

/* void control_coil_temp() {
  // 코일 현재온도에 따른 온수순환펌프 R2 출력 ON 제어, 현재온도가 설정온도보다 낮을 때 출력 ON, DI[1] : FAN 인터록 시그널 (강제정지)
  if ((DI[1]==0)&&(PV1 < SV1)&&(RO[1]!=true)) {
    set_plc_RO(Slave_id, 0x01, true);
    RO[1] = 1; 
    upWebSocket();    
  }
  if (((DI[1]==1)||(PV1>SV1))&&(RO[1]!=false)) {
    set_plc_RO(Slave_id, 0x01, false);
    RO[1] = 0; 
    upWebSocket();    
  }
  //Serial.print("RO[1]:"); Serial.println(RO[1]); 
} */
unsigned long prev_Millis = 0;
void mqtt_pub() {    // 모든 RTU 통신이 정상이면 제어데이타 MQTT 전송하고 MODBUS RTU 에 어떤 이상이 있으면 통신실패 회수를 MQTT로 전송
  if(mqttConnected != true)  mqtt_reconnect();

  unsigned long currentMillis = millis();
  if (currentMillis - prev_Millis >= 60000) {
    if(re1 == RTU_OK && re2 == RTU_OK && re3 == RTU_OK && re4 == RTU_OK) {
      mqtt_plc_publish();
    } else {
      mqtt_publish2();
    }
    prev_Millis = currentMillis;
  }  
}
void mqtt_plc_publish() {
  char msg[PUB_MSG_BUFFER_SIZE];
  
  //increse tx count
  if (mqtt_cnt < 60000) {
    mqtt_cnt++;  
  }
  else {
    mqtt_cnt = 1;
  }
  //make msg
  String json;
  json = "{\"pid\":\"";
  json += mac;  json += "\"";
  json += ",\"cnt\":";   json += mqtt_cnt;
  json += ",\"PV1\":";  json += (String)PV1; 
  json += ",\"SV1\":";  json += (String)SV1; 
  json += ",\"DI\":\"";  json += (String)DI[0];  json += (String)DI[1];  json += (String)DI[2];    json += (String)DI[3];
  json += "\"";
  json += ",\"RO\":\"";  json += (String)RO[0];  json += (String)RO[1];    json += (String)RO[2];   json += (String)RO[3];
  json += "\"";  
  json += ",\"PM\":";  json += (String)PM;
  json += ",\"ST1\":";  json += (String)ST1;
  json += "}";
  if(PUB_MSG_BUFFER_SIZE >= json.length()+1) {
    Serial.print("mqtt msg size : "); Serial.println(json.length()+1);
    json.toCharArray(msg, json.length()+1);
    if(true == mqtt_client.publish(outTopic, msg)){
      mqttConnected = true;
    }
    else {
      mqttConnected = false;
      Serial.println("mqtt publish failed");
    }
    Serial.println(msg);
  }
  else {
    Serial.flush();
    Serial.println("msg buffer overflow");
  }
  pre_PV1 = PV1;
  pre_SV1 = SV1;
  pre_sDI = sDI;
  pre_sRO = sRO;
  pre_ST1 = ST1;
}
void mqtt_publish2() {                                      // Modbus RTU 통신에 이상이 있을 때 mqtt로 전송
  char msg[PUB_MSG_BUFFER_SIZE];
  String json;
  json = "{\"pid\":\"";
  json += mac;  json += "\"";
  json += ",\"cnt\":";
  json += mqtt_cnt;
  json += ",\"ip\":\""; json += WiFi.localIP().toString();  json += "\"";  
  json += ",\"OK_C\":";
  json += (String)rtu_ok_cnt;
  json += ",\"T_OUT\":";
  json += (String)rtu_timeout_cnt;
  json += ",\"CRC\":";
  json += (String)rtu_crcerr_cnt;
  json += ",\"rssi\":";
  json += (String)WiFi.RSSI(); 
  json += "}";
  if(PUB_MSG_BUFFER_SIZE >= json.length()+1) {
    Serial.print("msg size:"); Serial.println(json.length()+1);
    json.toCharArray(msg, json.length()+1);
    // publish msg
    if(true != mqtt_client.publish(outTopic, msg)){
      Serial.println("mqtt msg failed");
    }
    Serial.print("Ok_c:");  Serial.print(rtu_ok_cnt);      Serial.print("\t");
    Serial.print("F_c:");   Serial.print(rtu_fail_cnt);    Serial.print("\t");
    Serial.print("T_OUT:"); Serial.print(rtu_timeout_cnt); Serial.print("\t"); 
    Serial.print("CRC:");   Serial.print(rtu_crcerr_cnt);  Serial.print("\t");
    Serial.print("poll_c: "); Serial.println(poll_cnt);
    Serial.println(msg);
  }
  else {
    Serial.flush();
    Serial.println("msg buffer overflow");
  }
}

void local_control() {
  if(millis() - prev_POLL >= POLLING_PERIOD_MS) {      // POLLING_PERIOD_MS 500ms
    prev_POLL = millis();  //update prev time     
    poll_cnt++; 
    unsigned int control_sw = poll_cnt%7;
    //Serial.print(poll_cnt); Serial.print("\t"); Serial.print(control_sw); Serial.print("\t");
   
    switch (control_sw) {
      //case 0: set_int_XR10(0x01); break;
      case 0: re1 = read_PV_XR10(0x01); break;                      // PV1값 읽기
      case 1: re2 = read_XR10(0x01); break;                         // SV1값 읽기
      case 2: re3 = read_plc_DI_status(Slave_id); break;
      case 3: re4 = read_plc_RO_status(Slave_id); break; 
      case 4: control_alarm(); break; 
      case 5: upWebSocket(); break;
      case 6: break;
      //case 7: modbus_rtu_return_status(); break;
      default : ;
    }
  }
}

void loop() {
  webSocket.loop();
  server.handleClient();
  mqtt_client.loop();
  doTick();                                // 1초 마다 실행되는 시간함수

  //공장리셋
  if ( digitalRead(TRIGGER_PIN) == LOW ) {
    factoryDefault();
  }
  if(bootMode !=1) {    // act == 1
    //serialEvent();
    if(mqttConnected == 1) {
      mqtt_pub();
      local_control();
    } else {
      mqtt_reconnect();  
    }
  }
}
