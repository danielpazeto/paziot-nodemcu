#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <WebSocketsClient.h>
#include <SPI.h>
#include <WiFiUdp.h>
#include <TimeLib.h>
#include <ArduinoJson.h>
#include <Hash.h>

ESP8266WebServer server(80);
WiFiManager wifiManager;
ESP8266WiFiMulti WiFiMulti;
WebSocketsClient webSocket;
char chipID[16];

/****NTP VARIABLES******/
unsigned int localPort = 2390;      // local port to listen for UDP packets
IPAddress timeServer(129, 6, 15, 28); // time.nist.gov NTP server
// A UDP instance to let us send and receive packets over UDP
WiFiUDP Udp;
//const int timeZone = 1;     // Central European Time
const int timeZone = -5;  // Eastern Standard Time (USA)
//const int timeZone = -4;  // Eastern Daylight Time (USA)
//const int timeZone = -8;  // Pacific Standard Time (USA)
//const int timeZone = -7;  // Pacific Daylight Time (USA)

#define D0 16
#define D1 5 // I2C Bus SCL (clock)
#define D2 4 // I2C Bus SDA (data)
#define D3 0
#define D4 2 // Same as "LED_BUILTIN", but inverted logic
#define D5 14 // SPI Bus SCK (clock)
#define D6 12 // SPI Bus MISO
#define D7 13 // SPI Bus MOSI
#define D8 15 // SPI Bus SS (CS)
#define D9 3 // RX0 (Serial console)
#define D10 1 // TX0 (Serial console)
const int pinout[11] = {D0, D1, D2, D3, D4, D5, D6, D7, D8, D9, D10};

//address to read value from output pin
int* GPIO_OUT = (int*)0x60000300;

//websocket
boolean websocketConnectionStatus = false;
int timeToTryReconnectWebSocket = 1 * 60 * 100; //seconds
//last timestamp to connect websocket
int lastAttemptConnectionWebSocket = 0;
// attepts to connect to server, ariable to indicate how many time it tried connect to websocket
int attemptToConnectServer = 0;
void connectWebSocket() {
  char adressWebSocket[20];
  strcpy(adressWebSocket, "/dev_con/");
  strcat(adressWebSocket, chipID);
  lastAttemptConnectionWebSocket = millis();
  Serial.println("Trying connect to websocket...");
  attemptToConnectServer++;
  webSocket.begin("138.68.19.117", 8025, adressWebSocket);
  //webSocket.begin(" 192.168.1.42", 8025, adressWebSocket);
  //webSocket.setAuthorization("user", "Password"); // HTTP Basic Authorization
}


/**
   Handle user request for pins status
*/
void handleStatusRequired(JsonObject& root) {
  const int BUFFER_SIZE = JSON_OBJECT_SIZE(3) + 11 * JSON_OBJECT_SIZE(2) + JSON_ARRAY_SIZE(11);
  Serial.println(BUFFER_SIZE);
  StaticJsonBuffer<BUFFER_SIZE> jsonBufferAux;
  JsonObject& statusJson = jsonBufferAux.createObject();
  statusJson["user_email"] = root["user_email"];
  statusJson["chipId"] = chipID;
  JsonArray& values = statusJson.createNestedArray("status");
  for (int i = 0; i < (int)( sizeof(pinout) / sizeof(pinout[0])); i++) {
    //int v = digitalRead(pinout[i]);
    int v = 0;
    if(*GPIO_OUT & (1<<pinout[i])){
      v=1;
    }
    JsonObject& value = jsonBufferAux.createObject();
    value["ioNum"] = i;
    value["value"] = v;
    String json1;
    value.printTo(json1);
    values.add(value);
  }
  String json;
  statusJson.printTo(json);
  Serial.println(json);
  webSocket.sendTXT(json);
}

void setPinoutValue(int ioNumber, int v) {
  switch (ioNumber) {
    case 0:
      digitalWrite(D0, v);
      break;
    case 1:
      digitalWrite(D7, v);
      digitalWrite(D1, v);
      delay(100);
      break;
  }
}
/**
   Handle user values to set in pins
   {"values":[{"value":"0","ioNumber":16}],"user_email":"a"}
*/
void handleValuesToSet(JsonObject& root) {
  JsonArray& nestedArray = root["values"].asArray();
  for (int i = 0; i < nestedArray.size(); i++) {
    int ioNumber = nestedArray[i]["ioNumber"];
    int v = nestedArray[i]["value"].as<int>();
    Serial.print("Pin: ");Serial.print(pinout[ioNumber]);Serial.print(" - Value: ");Serial.println(v);
    setPinoutValue(ioNumber, v);
  }
}

void webSocketEvent(WStype_t type, uint8_t * payload, size_t lenght) {
  switch (type) {
    case WStype_DISCONNECTED:
      Serial.printf("[WSc] Disconnected!\n");
      websocketConnectionStatus = false;
      break;
    case WStype_CONNECTED:
      Serial.printf("[WSc] Connected to url: %s\n",  payload);
      websocketConnectionStatus = true;
      attemptToConnectServer = 0;
      break;
    case WStype_TEXT:
      StaticJsonBuffer<200> jsonBuffer;
      String text = String((char *) &payload[0]);
      Serial.println(text);
      JsonObject& root = jsonBuffer.parseObject(text);
      // Test if parsing succeeds.
      if (!root.success()) {
        Serial.print("Failed to parse json: "); Serial.println(text);
        return;
      }
      if (root.containsKey("values") && root["values"].is<JsonArray&>())
      {
        Serial.println("User want set values on pins");
        handleValuesToSet(root);
      } else if (root.containsKey("status")) {
        Serial.println("User requires pins status");
        handleStatusRequired(root);
      } else {
        Serial.print("Json not recognized for IotPazeto Board: "); Serial.println(text);
      }
      break;
  }
}

void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("Entered AP mode");
  Serial.println(WiFi.softAPIP());
  Serial.println(myWiFiManager->getConfigPortalSSID());
}
boolean configMode = false; //simulate the input pin D5
void saveConfigCallback () {
  Serial.println("Should save config");
  WiFi.mode(WIFI_STA);
  Serial.println("WiFi.mode(WIFI_STA);");
  configMode = false;
}

void setup() {
  Serial.begin(115200);
  sprintf(chipID, "%d", ESP.getChipId());
  Serial.setDebugOutput(true);

  for (uint8_t t = 3; t > 0; t--) {
    Serial.printf("[SETUP] BOOT WAIT %d...\n", t);
    Serial.printf(" ESP8266 Chip id = %s\n", chipID);
    Serial.flush();
    delay(250);
  }
  Serial.print(" ESP.getFreeHeap(): "); Serial.println( ESP.getFreeHeap());

  //pinMode(D0, OUTPUT);
  pinMode(D1, OUTPUT);
  //pinMode(D3, INPUT);//digitalWrite(D3, LOW); //config pin
  
  pinMode(D7, OUTPUT); // led
  //pinMode(D8, OUTPUT); // led
  
  //WIFI settings
  wifiManager.setDebugOutput(true);
  wifiManager.setAPCallback(configModeCallback);
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  wifiManager.setConnectTimeout(20);
  wifiManager.setConfigPortalTimeout(60 * 3);

  //websocket
  connectWebSocket();
  webSocket.onEvent(webSocketEvent);
}

int CHANCES_CONNECT_WEB_SOCKET = 10;

void loop() {

  if (/*digitalRead(D3) == LOW ||*/ configMode) {
    configMode = false;
	  digitalWrite(D7, HIGH);
    wifiManager.resetSettings();
    if (!wifiManager.startConfigPortal(chipID, chipID)) {
      //reset and try again, or maybe put it to deep sleep
     // ESP.deepSleep(0);
      delay(5000);
      return;
    }
  }else if (WiFi.status() == WL_CONNECTED) {
	  digitalWrite(D7, LOW);
    if (attemptToConnectServer >= CHANCES_CONNECT_WEB_SOCKET) {
      Serial.println("attemptToConnectServer >= CHANCES_CONNECT_WEB_SOCKET --->>> something worng when tried connect to websocket.. will enter in config mode again ");
      configMode = true;
      return;
	}
    //Wifi connected
    if (!websocketConnectionStatus && (lastAttemptConnectionWebSocket + timeToTryReconnectWebSocket) < millis()) {
      connectWebSocket();
      Serial.println("connectWebSocket();");
    }
    webSocket.loop();

    //to keep time sycronized
    if ((timeStatus() == timeNotSet || timeStatus() == timeNeedsSync) && now() < 946684800 /*YEAR 1/1/2000 0:0:0*/) {
      setSyncInterval(1);
    } else {
      setSyncInterval(1 * 60 * 30);
    }

  } else {
	digitalWrite(D7, HIGH);
    Serial.println("WiFi.status() == WL_CONNECTED FALSE");
    attemptToConnectServer = 0;
    if (!wifiManager.autoConnect(chipID, chipID)) {
      Serial.println("Failed to connect and hit timeout in AUTOCONNECT");
      delay(3000);
      //reset and try again, or maybe put it to deep sleep
      ESP.reset();
      delay(5000);
    }else{
      Serial.println("Connected in wifi via autoConnect!!!");  
    }
  }
}



