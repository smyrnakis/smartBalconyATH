#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <NTPClient.h>
#include "DHT.h"
#include <ArduinoOTA.h>
#include <ESP8266Ping.h>
#include "secrets.h"

#define DHT_IN D1
#define PIR_IN D2
#define ANLG_IN A0
#define PCBLED D0 // 16 , LED_BUILTIN
#define ESPLED D4 // 2
#define WATER_RELAY D3
#define WHITE_RELAY D5
#define GREEN_RELAY D6

#define DHTTYPE DHT11

char defaultSSID[] = WIFI_DEFAULT_SSID;
char defaultPASS[] = WIFI_DEFAULT_PASS;

char apiKey[] = THINGSP_WR_APIKEY;

// ~~~~ Constants and variables
String httpHeader;
String localIPaddress;
String formatedTime;

bool debuggingMode = true;
bool pingResult = true;
bool wifiAvailable = false;
bool connectionLost = false;

bool movementFlag;
float humidity;
float temperature;
unsigned int luminosity;

unsigned long lastNTPtime = 0;
unsigned long lastPingTime = 0;
unsigned long connectionLostTime = 0;
unsigned long lastUploadTime = 0;
unsigned long lastPCBledTime = 0;
unsigned long lastESPledTime = 0;
unsigned long lastSensorsTime = 0;

const int ntpInterval = 2000;             // 2 seconds
const int pingInteval = 60000;            // 1 minute
const int sensorsInterval = 15000;        // 15 seconds
const long thingSpeakInterval = 300000;   // 5 minutes

const char* thinkSpeakAPI = "api.thingspeak.com"; // "184.106.153.149" or api.thingspeak.com

// Network Time Protocol
const long utcOffsetInSeconds = 7200; // 2H (7200) for winter time / 3H (10800) for summer time
char daysOfTheWeek[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

ESP8266WebServer server(80);
WiFiClient client;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", utcOffsetInSeconds);
DHT dht(DHT_IN, DHTTYPE);

void setup() {
  pinMode(DHT_IN, INPUT);
  pinMode(PIR_IN, INPUT);
  pinMode(ANLG_IN, INPUT);

  pinMode(PCBLED, OUTPUT);
  pinMode(ESPLED, OUTPUT);
  pinMode(WATER_RELAY, OUTPUT);
  pinMode(WHITE_RELAY, OUTPUT);
  pinMode(GREEN_RELAY, OUTPUT);

  digitalWrite(PCBLED, HIGH);
  digitalWrite(ESPLED, HIGH);
  digitalWrite(WATER_RELAY, LOW);
  digitalWrite(WHITE_RELAY, LOW);
  digitalWrite(GREEN_RELAY, LOW);

  Serial.begin(115200);

  WiFiManager wifiManager;
  //wifiManager.resetSettings();
  wifiManager.setConfigPortalTimeout(180);  // 180 sec timeout for WiFi configuration
  wifiManager.autoConnect(defaultSSID, defaultPASS);

  Serial.println("Connected to WiFi.");
  Serial.print("IP: ");
  localIPaddress = (WiFi.localIP()).toString();
  Serial.println(localIPaddress);

  server.on("/", handle_OnConnect);
  server.on("/help", handle_OnConnectHelp);
  server.onNotFound(handle_NotFound);
  
  server.begin();
  Serial.println("HTTP server starter on port 80.");

  timeClient.begin();

  dht.begin();

  // while (WiFi.waitForConnectResult() != WL_CONNECTED) {
  //   Serial.println("Connection Failed! Rebooting...");
  //   delay(5000);
  //   ESP.restart();
  // }

  // handle OTA updates
  handleOTA();
  delay(100);

  if (WiFi.waitForConnectResult() != WL_CONNECTED) {
    wifiAvailable = false;
    Serial.println("Failed to connect on WiFi network!");
    Serial.println("Operating offline.");
  }
  else {
    wifiAvailable = true;
    Serial.println("Connected to WiFi.");
    Serial.print("IP: ");
    localIPaddress = (WiFi.localIP()).toString();
    Serial.println(localIPaddress);
  }

  delay(5000);
}

bool pingStatus() {
  IPAddress ipThingSpeak (184, 106, 153, 149);
  IPAddress ipGoogle (8, 8, 8, 8);

  bool pingRet;    
  pingRet = Ping.ping(ipThingSpeak);

  if (!pingRet) {
      pingRet = Ping.ping(ipGoogle);
  }

  lastPingTime = millis();
  return pingRet;
}

void handleOTA() {
  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname("SmyESP-1");

  ArduinoOTA.setPassword((const char *)otaAuthPin);

  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
}

void thingSpeakRequest() {
  client.stop();
  if (client.connect(thinkSpeakAPI,80)) {
    String postStr = apiKey;
    postStr +="&field1=";
    postStr += String(temperature);
    postStr +="&field2=";
    postStr += String(humidity);
    postStr +="&field3=";
    postStr += String(luminosity);
    postStr +="&field4=";
    postStr += String(movementFlag);
    postStr += "\r\n\r\n";

    client.print("POST /update HTTP/1.1\n");
    client.print("Host: api.thingspeak.com\n");
    client.print("Connection: close\n");
    client.print("X-THINGSPEAKAPIKEY: " + (String)apiKey + "\n");
    client.print("Content-Type: application/x-www-form-urlencoded\n");
    client.print("Content-Length: ");
    client.print(postStr.length());
    client.print("\n\n");
    client.print(postStr);
    client.stop();
    if (debuggingMode) { Serial.println("Data uploaded to thingspeak!"); }

    lastUploadTime = millis();
  }
  else {
    if (debuggingMode) { Serial.println("ERROR: could not upload data to thingspeak!"); }
  }
}

// Handle HTML page calls
void handle_OnConnect() {
  digitalWrite(ESPLED, LOW);
  getSensorData();
  server.send(200, "text/html", HTMLpresentData());
  digitalWrite(ESPLED, HIGH);
}

void handle_OnConnectHelp() {

}

void handle_NotFound() {
  server.send(404, "text/html", HTMLnotFound());
}

// HTML page structure
String HTMLpresentData() {
  String ptr = "<!DOCTYPE html> <html>\n";
  ptr +="<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n";
  ptr +="<title>RJD Monitor</title>\n";
  ptr +="<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}\n";
  ptr +="body{margin-top: 50px;} h1 {color: #444444;margin: 50px auto 30px;}\n";
  ptr +="p {font-size: 24px;color: #444444;margin-bottom: 10px;}\n";
  ptr +="</style>\n";
  ptr +="</head>\n";
  ptr +="<body>\n";
  ptr +="<div id=\"webpage\">\n";
  ptr +="<h1>RJD Monitor</h1>\n";
  
  ptr +="<p>Local IP: ";
  ptr += (String)localIPaddress;
  ptr +="</p>";

  ptr +="<p>Temperature: ";
  ptr +=(String)temperature;
  ptr +="&#176C</p>"; // '°' is '&#176' in HTML
  ptr +="<p>Humidity: ";
  ptr +=(String)humidity;
  ptr +="%</p>";
  ptr +="<p>IR sensor: ";
  ptr +=(String)luminosity;
  ptr +="%</p>";
  ptr += "<p>Timestamp: ";
  ptr +=(String)formatedTime;
  ptr += "</p>";

  // ptr +="<p>Last recorder temp: ";
  // ptr +=(String)lastRecorderTemp;
  // ptr +="&#176C</p>";
  
  ptr +="</div>\n";
  ptr +="</body>\n";
  ptr +="</html>\n";
  return ptr;
}

String HTMLnotFound() {
  String ptr = "<!DOCTYPE html> <html>\n";
  ptr +="<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n";
  ptr +="<title>RJD Monitor</title>\n";
  ptr +="<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: left;}\n";
  ptr +="body{margin-top: 50px;} h1 {color: #444444;margin: 50px auto 30px;}\n";
  ptr +="p {font-size: 24px;color: #444444;margin-bottom: 10px;}\n";
  ptr +="</style>\n";
  ptr +="</head>\n";
  ptr +="<body>\n";
  ptr +="<div id=\"webpage\">\n";
  ptr +="<h1>You know this 404 thing ?</h1>\n";
  ptr +="<p>What you asked can not be found... :'( </p>";
  ptr +="</div>\n";
  ptr +="</body>\n";
  ptr +="</html>\n";
  return ptr;
}


// Read all sensors
void getSensorData() {
  temperature = dht.readTemperature();
  humidity = dht.readHumidity();
  luminosity = analogRead(ANLG_IN);
  luminosity = map(luminosity, 0, 1024, 1024, 0);
}

// Get the time
void pullNTPtime(bool printData) {
  timeClient.update();
  formatedTime = timeClient.getFormattedTime();

  if (printData) {
    // Serial.print(daysOfTheWeek[timeClient.getDay()]);
    // Serial.print(", ");
    // Serial.print(timeClient.getHours());
    // Serial.print(":");
    // Serial.print(timeClient.getMinutes());
    // Serial.print(":");
    // Serial.println(timeClient.getSeconds());
    Serial.println(timeClient.getFormattedTime()); // format time like 23:05:00
  }
}

// Serial print data      // <?><?><?><?><?><?><?><?>
void serialPrintAll() {
  Serial.println(timeClient.getFormattedTime());
  Serial.print("Temperature: ");
  Serial.print(String(temperature));
  Serial.println("°C");
  Serial.print("Humidity: ");
  Serial.print(String(humidity));
  Serial.println("%");
  Serial.println();
}


void loop(){

  if (millis() > lastSensorsTime + sensorsInterval) {
    if (debuggingMode) { Serial.println("Reading sensor data..."); }
    getSensorData();
    lastSensorsTime = millis();
  }

  if (millis() > lastNTPtime + ntpInterval) {
    if (debuggingMode) { Serial.println("Pulling NTP..."); }
    if (wifiAvailable) {
      pullNTPtime(false);
      lastNTPtime = millis();
    }
    else {
      if (debuggingMode) { Serial.println("No WiFi! Pulling NTP canceled!"); }
    }
  }

  if (digitalRead(PIR_IN)) {
    movementFlag = true;
  }

  if (millis() > lastUploadTime + thingSpeakInterval) {
    if (debuggingMode) { Serial.println("Uploading to thingspeak..."); }
    if (wifiAvailable) {
      thingSpeakRequest();
    }
    else {
      if (debuggingMode) { Serial.println("No WiFi! Uploading to thingspeak canceled!"); }
    }
  }

  // check Internet connectivity
  if (millis() > lastPingTime + pingInteval) {
      pingResult = pingStatus();
      Serial.print("\r\nPing status: ");
      Serial.println((String)pingResult);
      Serial.println("\r\n");

      connectionLost = !pingResult;

      if ((!pingResult) && (!connectionLost)) {
          Serial.println("\r\nWARNING: no Internet connectivity!\r\n");
          connectionLostTime = millis();
          connectionLost = true;
      }
  }

  // reboot if no Internet
  if ((millis() > connectionLostTime + 300000) && connectionLost) {
      if (!pingResult) {
          Serial.println("No Internet connection. Rebooting in 5 sec...");
          delay(5000);
          ESP.restart();
      }
  }

  // reboot device if no WiFi for 5 minutes (1h : 3600000)
  if ((millis() > 300000) && (!wifiAvailable)) {
      Serial.println("No WiFi connection. Rebooting in 5 sec...");
      delay(5000);
      ESP.restart();
  }

  server.handleClient();
}