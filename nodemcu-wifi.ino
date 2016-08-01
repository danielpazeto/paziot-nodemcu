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
#include <FS.h>

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
//Event struc
struct Event {
  time_t unixTime;
  int code;
  int portNumber;
  String value;
};
struct Event eventsBuffer[100];
typedef enum EventCode {CHANGE_PINTOUT_VALUE, CONNECTED, DISCONNECTED
                       };
//Access point struc
struct AccessPoint {
  String pwd;
  String ssid;
};
struct AccessPoint ac_points[3]; //access points to try connect

//websocket
boolean websocketConnectionStatus = false;
int timeToTryReconnectWebSocket = 1 * 60 * 100; //seconds
//last timestamp to connect websocket
int lastAttemptConnectionWebSocket = 0;
// attepts to connect to server
int attemptToConnectServer = 0;
void connectWebSocket() {
  char adressWebSocket[20];
  strcpy(adressWebSocket, "/dev_con/");
  strcat(adressWebSocket, chipID);
  lastAttemptConnectionWebSocket = millis();
  Serial.println("Trying connect to websocket...");
    attemptToConnectServer++;
  webSocket.begin("192.168.25.203", 8025, adressWebSocket);
//    webSocket.begin("52.67.51.187", 8025, adressWebSocket);
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
    int v = digitalRead(pinout[i]);
    //          Serial.printf("%d - %d  - v: %d ", pinout[i], i, v);
    JsonObject& value = jsonBufferAux.createObject();
    value["ioNum"] = i;
    value["value"] = v;
    String json1;
    value.printTo(json1);
    values.add(value);
    //    Serial.println(json1);
  }
  String json;
  statusJson.printTo(json);
  Serial.println(json);
  webSocket.sendTXT(json);
}

void saveEvent(int ioNumber, int v, int code) {
  struct Event ev;
  ev.unixTime = now();
  ev.code = code;
  ev.portNumber = ioNumber;
  ev.value = v;

  int lenEvents = (sizeof eventsBuffer / sizeof * eventsBuffer);
  eventsBuffer[0] = ev; //TODO verificar quando estourar buffer
  Serial.print("Tamanho eventos: "); Serial.println(lenEvents);
  if (WiFi.status() == WL_CONNECTED && lenEvents > 50) {
    const int BUFFER_SIZE = JSON_OBJECT_SIZE(4) + JSON_ARRAY_SIZE(1);
    Serial.println(BUFFER_SIZE);
    StaticJsonBuffer<BUFFER_SIZE> jsonBufferAux;
    JsonObject& rootEvents = jsonBufferAux.createObject();
    rootEvents["chipId"] = chipID;
    JsonArray& events = rootEvents.createNestedArray("events");
    for (int i = 0; i < (int)( (sizeof eventsBuffer / sizeof * eventsBuffer) ); i++) {
      Event event = eventsBuffer[i];
      //          Serial.printf("%d - %d  - v: %d ", pinout[i], i, v);
      JsonObject& jsonEvent = jsonBufferAux.createObject();
      jsonEvent["time"] = event.unixTime;
      jsonEvent["code"] = event.code;
      jsonEvent["ioNumber"] = event.portNumber;
      jsonEvent["value"] = event.value;
      //      String json1;
      //      jsonEvent.printTo(json1);
      events.add(jsonEvent);
      //      Serial.println(json1);
    }
    String json;
    rootEvents.printTo(json);
    Serial.println(json);
    webSocket.sendTXT(json);
    memset(eventsBuffer, 0, sizeof(eventsBuffer) * 100);
  }



}

void setPinoutValue(int ioNumber, int v) {
  switch (ioNumber) {
    case 4:
      digitalWrite(D4, v);
      break;
    case 0:
      digitalWrite(D0, v);
      break;
    case 1:
      digitalWrite(D1, v);
      break;
    default:
      digitalWrite(D4, LOW);
      break;
  }
//  saveEvent(ioNumber, v, CHANGE_PINTOUT_VALUE);
}
/**
   Handler user values to set in pins
*/
void handleValuesToSet(JsonObject& root) {
  // {"values":[{"value":"HIGH","ioNumber":16}],"user_email":"a"}
  JsonArray& nestedArray = root["values"].asArray();
  for (int i = 0; i < nestedArray.size(); i++) {
    int ioNumber = nestedArray[i]["ioNumber"];
    int v = nestedArray[i]["value"].as<int>();
    Serial.println(pinout[ioNumber]);
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
        Serial.println("parseObject() failed");
        return;
      }
      if (root.containsKey("values") && root["values"].is<JsonArray&>())
      {
        Serial.println("Values to set");
        handleValuesToSet(root);
      } else if (root.containsKey("status")) {
        Serial.println("Status required");
        handleStatusRequired(root);
      } else {
        Serial.println("Invalid JsonObject!!");
      }
      break;
  }

}

const char* configFilePath = "/ap_conf.txt";
//Method to read ap config file and refresh apArray with available access points
boolean refreshApArrayList() {
  Serial.print(" ESP.getFreeHeap() 1: "); Serial.println( ESP.getFreeHeap());
  File file = SPIFFS.open(configFilePath, "r");
  if (file) {
    int k = 0;
    while (k < 3 && file.available()) {
      //Lets read line by line from the file
      char val[50] = "\0";
      int j = 0;
      while (val[j - 1] != '\r') {
        val[j] = file.read();
        j++;
      }
      Serial.println(j);
      String line = val;
      Serial.print("linha: "); Serial.println(line);
      int i = line.lastIndexOf(',');
      Serial.print("index of , "); Serial.println(i);
      Serial.print("abtes da virgula: "); Serial.println(line.substring(0, i));
      Serial.print("depois da virgula: "); Serial.println(line.substring(i + 1));
      //      apArray[k][0] = line.substring(0, i); //ap name
      //      apArray[k][1] = line.substring(i+1);     //ap pwd
      Serial.print(" ESP.getFreeHeap() 2: "); Serial.println( ESP.getFreeHeap());
      Serial.print("k "); Serial.println(k);
      ac_points[k].ssid = line.substring(0, i);
      ac_points[k].pwd = line.substring(i + 1);
      Serial.print("ac_points[k].ssid: "); Serial.println(ac_points[k].ssid);
      //      strcpy(ac_points[k].ssid, line.substring(0, i));
      Serial.print("pqp k"); Serial.println(k);
      //      strcpy(ac_points[k].pwd, line.substring(i + 1));
      Serial.print("vamo veio, "); Serial.println(i);
      k++;
    }
    Serial.println("antes do close");
    file.close();
    return k;
  }
}

//Open the config screen when user enter in http://192.168.4.1
void handleRoot() {
  refreshApArrayList();
  char temp[700];
  snprintf(temp, 700, "<html>\<head>\
<title>Iot Pazeto</title>\
</head>\
<body>\
<h1>Iot Pazeto</h1>\
<h1>Configure available Wifi Hotspots</h1>\
<form action='http://192.168.4.1/submit' method='POST'>\
SSID1:<input type='text'name='ap1'value=%s>Pass1:<input type='text'name='pwd1'value=%s><br>\
SSID2:<input type='text'name='ap2'value=%s>Pass2:<input type='text'name='pwd2'value=%s><br>\
SSID3:<input type='text'name='ap3'value=%s>Pass3:<input type='text'name='pwd3'value=%s><br>\
<input type=submit value='Submit'>\
</form>\
</body>\
</html>", ac_points[0].ssid.c_str(), ac_points[0].pwd.c_str(), ac_points[1].ssid.c_str(), ac_points[1].pwd.c_str(), ac_points[2].ssid.c_str(), ac_points[2].pwd.c_str());
  server.send(200, "text/html", temp);
}

//Handle access pointes sent from user in config mode
void handleSubmit() {
  Serial.println("SUBMIT enviado e reconhecido");
  if (server.args() > 0 ) {
    Serial.println(server.argName(0));
    Serial.println(server.arg(0));
    Serial.println(server.argName(0).startsWith("ap"));
    Serial.println(server.argName(1).startsWith("pwd"));
    File f = SPIFFS.open("/ap_conf.txt", "w+");
    if (f) {
      for ( uint8_t i = 0; i < server.args(); i = i + 2 ) {
        if (server.argName(i).startsWith("ap") && server.argName(i + 1).startsWith("pwd")) {
          Serial.println(server.arg(i));
          Serial.println(server.arg(i + 1));
          if (f) {
            char buffer [100];
            snprintf (buffer, 100, "%s,%s", server.arg(i).c_str(), server.arg(i + 1).c_str());
            f.println(buffer);
          }
        }
      }
      f.close();
      server.send(200, "text/html", "<h1>Iot Pazeto</h1><br>Access Points saved<br>Exit config mode and wait until connection in one of them!<br><a href='http://192.168.4.1/'>Back</a>");
      return;
    } else {
      Serial.printf("Error on open AP config file!");
      server.send(200, "text/html", "<h1>Iot Pazeto</h1>\nError on save configuration!");
      return;
    }
  }
  server.send(200, "text/html", "<h1>Iot Pazeto</h1>\nSomething wrong with submit!");
}

void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());
  Serial.println(myWiFiManager->getConfigPortalSSID());
}
/*-------- NTP code ----------*/

const int NTP_PACKET_SIZE = 48; // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets
// send an NTP request to the time server at the given address
void sendNTPpacket(IPAddress &address)
{
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;
  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  Udp.beginPacket(address, 123); //NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}
time_t getNtpTime()
{
  while (Udp.parsePacket() > 0) ; // discard any previously received packets
  Serial.println("Transmit NTP Request");
  sendNTPpacket(timeServer);
  uint32_t beginWait = millis();
  while (millis() - beginWait < 1500) {
    int size = Udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      Serial.println("Receive NTP Response");
      Udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
      unsigned long secsSince1900;
      // convert four bytes starting at location 40 to a long integer
      secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      return secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR;
    }
  }
  Serial.println("No NTP Response :-(");
  return 0; // return 0 if unable to get the time
}

boolean configMode = true; //simulate the input pin D3

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
  wifiManager.setDebugOutput(true);
  for (uint8_t t = 3; t > 0; t--) {
    Serial.printf("[SETUP] BOOT WAIT %d...\n", t);
    Serial.printf(" ESP8266 Chip id = %s\n", chipID);
    Serial.flush();
    delay(250);
  }
  Serial.print(" ESP.getFreeHeap(): "); Serial.println( ESP.getFreeHeap());
  // SPIFFS.begin();
  pinMode(D0, OUTPUT);
  pinMode(D1, OUTPUT);
  pinMode(D3, INPUT); //config pin
  pinMode(D4, OUTPUT);

  digitalWrite(D4, HIGH); //D4 has inverted logic
  wifiManager.setAPCallback(configModeCallback);
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  WiFi.mode(WIFI_AP);
  Udp.begin(localPort);
  setSyncProvider(getNtpTime);
  connectWebSocket();
  webSocket.onEvent(webSocketEvent);
}
boolean serverConfStarted = false;

void loop() {
  if (configMode) {
    Serial.println("configMode true");
    if (!serverConfStarted) {
      Serial.println("!serverConfStarted)");
      wifiManager.setConfigPortalTimeout(30);
      wifiManager.startConfigPortal(chipID, chipID);
        //FOCO AQUI aparentemnte não conecta e não sai da config tbm
        
      serverConfStarted = true;
      digitalWrite(D4, !digitalRead(D4));
    }
    return;
  }
  if (WiFi.status() == WL_CONNECTED) {
     Serial.println("WiFi.status() == WL_CONNECTED");
    if(attemptToConnectServer >= 3){
      Serial.println("attemptToConnectServer >= 3");
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

    if (digitalRead(D4) == HIGH) {
      digitalWrite(D4, LOW); //D4 has inverted logic
    }

  } else {
     Serial.println("WiFi.status() == WL_CONNECTED FALSEEEEE");
    if (digitalRead(D4) == LOW) {
      digitalWrite(D4, HIGH); //D4 has inverted logic
    }
    attemptToConnectServer = 0;
    wifiManager.autoConnect(chipID,chipID);
  }
}



