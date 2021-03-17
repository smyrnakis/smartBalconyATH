
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include "Wire.h"
#include "BH1750.h"
#include "Adafruit_SHT31.h"
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <NTPClient.h>
#include <ArduinoOTA.h>
#include "secrets.h"

// INPUTS
//   PIR_IN      --> D4  (!! used as BUILTIN_LED too !!)
//   BUTTONS     --> A0  (analog IN)
//   I2C sensors
//      SDA      --> D1
//      SCL      --> D2

// OUTPUTS
//   RELAY_W      --> D5  (RELAY green lamp)
//   RELAY_G      --> D6  (RELAY white lamp)
//   RELAY_WT     --> D7  (RELAY water valve)
//   BUILTIN_LED  --> D4  (!! used as PIR_IN too !!)

#define BTN_IN A0
// #define BUILTIN_LED D4 (used for PIR_IN & BUILTIN_LED)

#define RELAY_W D5
#define RELAY_G D6
#define RELAY_WT D7

char defaultSSID[] = WIFI_DEFAULT_SSID;
char defaultPASS[] = WIFI_DEFAULT_PASS;

char apiKey[] = THINGSP_WR_APIKEY;
char otaAuthPin[] = OTA_AUTH_PIN;

String httpHeader;
String formatedTime;
String macAddr;
String wifiSSID;
String wifiSignal;
String localIPaddress;

bool wifiAvailable = false;
bool sht3xAvailable = false;
bool bh1750Available = false;

int analogValue;
float humidity;
float temperature;
bool movementFlag;
uint16_t luminosity;

unsigned long lastAnalogTime = 0;
unsigned long lastNTPtime = 0;
unsigned long lastUploadTime = 0;
unsigned long lastSensorsTime = 0;
unsigned long lastWiFiLostTime = 0;
unsigned long lastWiFiCheckTime = 0;
unsigned long lastLEDblinkTime = 0;

bool autoGreen = true;
bool manualGreen = false;
bool autoWhite = true;
bool manualWhite = false;
bool autoWater = true;
bool manualWater = false;

const int sensorsInterval = 15000;        //  15 seconds
const int analogReadInterval = 250;       //  250 ms
unsigned int ledBlinkInterval = 3000;     //  3 seconds

const int ntpInterval = 2000;             //  2 seconds
const int connectionKeepAlive = 2000;     //  2 seconds
const long thingSpeakInterval = 300000;   //  5 minutes

const char* thinkSpeakAPI = "api.thingspeak.com";       // "184.106.153.149" or api.thingspeak.com

const long utcOffsetSec = 7200;     // GR: 2H (7200) for winter time / 3H (10800) for summer time
char daysOfTheWeek[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};


WiFiServer server(80);
WiFiClient client;
WiFiClient clientThSp;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", utcOffsetSec);

BH1750 lightMeter;
Adafruit_SHT31 sht31 = Adafruit_SHT31();


void setup() {

  Serial.begin(115200);
  Serial.println();
  delay(200);

  Wire.begin();
  // Wire.begin(I2C_SDA, I2C_SCL);
  Serial.println();
  delay(200);

  if (! sht31.begin(0x44)) {                             // Alternative address: 0x45
    Serial.println("[ERROR] initialising SHT31");
  }
  else {
    sht3xAvailable = true;
    Serial.println("[SUCCESS] SHT31 initialised");
  }
  delay(200);

  // lightMeter.begin();
  if (lightMeter.begin(BH1750::CONTINUOUS_LOW_RES_MODE)) {
    bh1750Available = true;
    Serial.println("[SUCCESS] BH1750 initialised");
  }
  else {
    Serial.println("[ERROR] initialising BH1750");
  }
  Serial.println();
  delay(200);

  WiFiManager wifiManager;
  //wifiManager.resetSettings();
  wifiManager.setConfigPortalTimeout(120);              // 2 min timeout for WiFi configuration
  wifiManager.autoConnect(defaultSSID, defaultPASS);
  
  server.begin();
  Serial.println("[SUCCESS] HTTP server started on port 80");
  delay(200);

  timeClient.begin();
  Serial.println("[SUCCESS] NTP client started");
  delay(200);

  handleOTA();
  delay(200);

  if (WiFi.waitForConnectResult() != WL_CONNECTED) {
    wifiAvailable = false;
    Serial.println("[ERROR] connecting on WiFi - operating 'OFFLINE'");
  }
  else {
    wifiAvailable = true;
    localIPaddress = (WiFi.localIP()).toString();
    wifiSignal = String(WiFi.RSSI());
    wifiSSID = String(WiFi.SSID());
    macAddr = String(WiFi.macAddress());

    Serial.print("[SUCCESS] Connected on WiFi: ");
    Serial.println(wifiSSID);
    Serial.print("Local IP address: ");
    Serial.println(localIPaddress);
  }
  delay(200);

  pinMode(BTN_IN, INPUT);
  pinMode(BUILTIN_LED, INPUT);

  pinMode(RELAY_G, OUTPUT);
  pinMode(RELAY_W, OUTPUT);
  pinMode(RELAY_WT, OUTPUT);
  // pinMode(BUILTIN_LED, OUTPUT);

  digitalWrite(RELAY_G, HIGH);
  digitalWrite(RELAY_W, HIGH);
  digitalWrite(RELAY_WT, HIGH);
  // digitalWrite(BUILTIN_LED, HIGH);
}

void handleOTA() {
  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname("SmartBalcony");

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
  clientThSp.stop();
  if (clientThSp.connect(thinkSpeakAPI,80)) {
    String postStr = apiKey;
    if (temperature != NULL) {
      postStr +="&field1=";
      postStr += String(temperature);
    }
    if (humidity != NULL) {
      postStr +="&field2=";
      postStr += String(humidity);
    }
    if (luminosity != NULL) {
      postStr +="&field3=";
      postStr += String(luminosity);
    }
    // if (movementFlag != NULL) {
    //   postStr +="&field4=";
    //   postStr += String(movementFlag);
    // }
    postStr += "\r\n\r\n";

    clientThSp.print("POST /update HTTP/1.1\n");
    clientThSp.print("Host: api.thingspeak.com\n");
    clientThSp.print("Connection: close\n");
    clientThSp.print("X-THINGSPEAKAPIKEY: " + (String)apiKey + "\n");
    clientThSp.print("Content-Type: application/x-www-form-urlencoded\n");
    clientThSp.print("Content-Length: ");
    clientThSp.print(postStr.length());
    clientThSp.print("\n\n");
    clientThSp.print(postStr);
    clientThSp.stop();
    Serial.println("[SUCCESS] data uploaded to thingspeak");

    lastUploadTime = millis();
  }
  else {
    Serial.println("[ERROR] could not connect to thingspeak");
  }
}

void refreshToRoot() {
  client.print("<HEAD>");
  client.print("<meta http-equiv=\"refresh\" content=\"0;url=/\">");
  client.print("</head>");
}

void handleClientConnection() {

  unsigned long currentTime;
  unsigned long previousTime;
  currentTime = millis();
  previousTime = currentTime;

  String currentLine = "";                      // incoming HTML request
  bool mobileDevice = false;

  // loop while the client's connected
  while (client.connected() && currentTime - previousTime <= connectionKeepAlive) {
    currentTime = millis();

    if (client.available()) {                   // if there's bytes to read from the client,
      char c = client.read();                   // read a byte
      // Serial.write(c);                       // print it to the serial monitor
      httpHeader += c;
      if (c == '\n') {                          // if the byte is a newline character
        // if the current line is blank, you got two newline characters in a row.
        // that's the end of the client HTTP request, so send a response:

        if (currentLine.length() == 0) {
          // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
          // and a content-type so the client knows what's coming, then a blank line:
          client.println("HTTP/1.1 200 OK");
          client.println("Content-type:text/html");
          client.println("Connection: close");
          client.println();

          // Handle requests
          if (httpHeader.indexOf("GET /autoWhite") >= 0) {
            Serial.println("autoWhite");
            autoWhite = !autoWhite;
            refreshToRoot();
          }
          else if (httpHeader.indexOf("GET /manualWhite") >= 0) {
            Serial.println("manualWhite");
            manualWhite = !manualWhite;
            refreshToRoot();
          }
          else if (httpHeader.indexOf("GET /autoGreen") >= 0) {
            Serial.println("autoGreen");
            autoGreen = !autoGreen;
            refreshToRoot();
          }
          else if (httpHeader.indexOf("GET /manualGreen") >= 0) {
            Serial.println("manualGreen");
            manualGreen = !manualGreen;
            refreshToRoot();
          }
          else if (httpHeader.indexOf("GET /autoWater") >= 0) {
            Serial.println("autoWater");
            autoWater = !autoWater;
            refreshToRoot();
          }
          else if (httpHeader.indexOf("GET /manualWater") >= 0) {
            Serial.println("manualWater");
            manualWater = !manualWater;
            refreshToRoot();
          }
          else if (httpHeader.indexOf("GET /settings") >= 0) {
            // Send HTML web page
            client.println("<!DOCTYPE html><html>");
            // client.println("<meta http-equiv=\"refresh\" content=\"15\" >\n");
            client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
            client.println("<link rel=\"icon\" href=\"data:,\">");
            client.println("<title>Smart Balcony</title>");
            // CSS styling
            client.println("<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}");
            // client.println("body{margin-top: 50px;} h1 {color: #B4F9F3;margin: 50px auto 30px;}");
            client.println("body {color: white; background: black;}");
            // client.println(".button { background-color: #195B6A; border: none; color: white; padding: 16px 50px;");
            client.println(".button { background-color: #195B6A; border: none; color: white; height: 50px; width: 130px;");
            client.println("text-decoration: none; text-align: center; font-size: 30px; margin: 2px; cursor: pointer;}");
            client.println(".button3 {background-color: #ff3300;}");
            client.println(".button2 {background-color: #77878A;}</style></head>");

            client.println("<body><h1>Smart Balcony</h1>");
            client.println("<h2>Settings page</h2>");
            client.println("<p></p>");
            // break;
          }
          else if (httpHeader.indexOf("GET /debug") >= 0) {
            // Send HTML web page
            client.println("<!DOCTYPE html><html>");
            client.println("<meta http-equiv=\"refresh\" content=\"5\" >\n");
            client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
            client.println("<link rel=\"icon\" href=\"data:,\">");
            client.println("<title>Smart Balcony</title>");
            // CSS styling
            client.println("<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}");
            // client.println("body{margin-top: 50px;} h1 {color: #B4F9F3;margin: 50px auto 30px;}");
            client.println("body {color: white; background: black;}");
            // client.println(".button { background-color: #195B6A; border: none; color: white; padding: 16px 50px;");
            client.println(".button { background-color: #195B6A; border: none; color: white; height: 50px; width: 130px;");
            client.println("text-decoration: none; text-align: center; font-size: 30px; margin: 2px; cursor: pointer;}");
            client.println(".button3 {background-color: #ff3300;}");
            client.println(".button2 {background-color: #77878A;}</style></head>");

            client.println("<body><h1>Smart Balcony</h1>");
            client.println("<body><h2>Debugging Page (auto refresh: 5\")</h2>");
            client.println("<p></p>");

            client.println("<table style=\"margin-left:auto;margin-right:auto;\">");

            client.println("<tr>");
            client.println("<td colspan=\"2\"> SENSORS </td>");
            client.println("</tr>");
            client.println("<tr>");
            client.println("<td>SHT3x OK:</td>");
            client.println("<td>" + String(sht3xAvailable) + "</td>");
            client.println("</tr>");
            client.println("<tr>");
            client.println("<td>BH1750 OK:</td>");
            client.println("<td>" + String(bh1750Available) + "</td>");
            client.println("</tr>");

            client.println("<tr>");
            client.println("<td colspan=\"2\"> </td>");
            client.println("</tr>");

            client.println("<tr>");
            client.println("<td colspan=\"2\"> INPUTS </td>");
            client.println("</tr>");
            client.println("<tr>");
            client.println("<td>Temperature:</td>");
            client.println("<td>" + String(temperature) + " &#176C</td>");
            client.println("</tr>");
            client.println("<tr>");
            client.println("<td>Humidity:</td>");
            client.println("<td>" + String(humidity) + " %</td>");
            client.println("</tr>");
            client.println("<tr>");
            client.println("<td>Luminosity:</td>");
            client.println("<td>" + String(luminosity) + " lux</td>");
            client.println("</tr>");
            client.println("<tr>");
            client.println("<td>Movement:</td>");
            String tempMove;
            if (movementFlag) {
              tempMove = "yes";
            }
            else {
              tempMove = "no";
            }
            client.println("<td>" + tempMove  + "</td>");
            client.println("</tr>");

            client.println("<tr>");
            client.println("<td colspan=\"2\"> </td>");
            client.println("</tr>");

            client.println("<tr>");
            client.println("<td colspan=\"2\"> NETWORK </td>");
            client.println("</tr>");
            client.println("<tr>");
            client.println("<td>SSID:</td>");
            client.println("<td>" + wifiSSID + "</td>");
            client.println("</tr>");
            client.println("<tr>");
            client.println("<td>Signal:</td>");
            client.println("<td>" + wifiSignal + "</td>");
            client.println("</tr>");
            client.println("<tr>");
            client.println("<td>Local IP:</td>");
            client.println("<td>" + localIPaddress + "</td>");
            client.println("</tr>");
            client.println("<tr>");
            client.println("<td>MAC:</td>");
            client.println("<td>" + macAddr + "</td>");
            client.println("</tr>");

            client.println("<tr>");
            client.println("<td colspan=\"2\"> </td>");
            client.println("</tr>");

            client.println("<tr>");
            client.println("<td colspan=\"2\"> RUNTIME </td>");
            client.println("</tr>");
            client.println("<tr>");
            client.println("<td>Time:</td>");
            client.println("<td>" + formatedTime + "</td>");
            client.println("</tr>");
            client.println("<tr>");
            client.println("<td>millis():</td>");
            client.println("<td>" + String(millis()) + "</td>");
            client.println("</tr>");
            client.println("<tr>");
            client.println("<td>Up time (millis):</td><td>");
            client.println(millisToTime(true));
            // if (millis() >= 86400000) {                           // more than one day
            //   client.println(millisToTime(true));
            // }
            // else {
            //   client.println(millisToTime(false));
            // }
            // client.println(tempUpMin,1);
            client.println("</td></tr>");
            client.println("<tr>");
            client.println("<td>PIR_IN:</td>");
            client.println("<td>" + String(digitalRead(BUILTIN_LED)) + "</td>");
            client.println("</tr>");
            client.println("<tr>");
            client.println("<td>BTN_IN:</td>");
            client.println("<td>" + String(analogValue) + "</td>");
            client.println("</tr>");
            client.println("<tr>");
            client.println("<td>RELAY_W:</td>");
            short tmpRL;
            tmpRL = map(digitalRead(RELAY_W), 0, 1, 1, 0);
            client.println("<td>" + String(tmpRL) + "</td>");
            client.println("</tr>");
            client.println("<tr>");
            client.println("<td>RELAY_G:</td>");
            tmpRL = map(digitalRead(RELAY_G), 0, 1, 1, 0);
            client.println("<td>" + String(tmpRL) + "</td>");
            client.println("</tr>");
            client.println("<tr>");
            client.println("<td>RELAY_WT:</td>");
            tmpRL = map(digitalRead(RELAY_WT), 0, 1, 1, 0);
            client.println("<td>" + String(tmpRL) + "</td>");
            client.println("</tr>");

            client.println("<tr>");
            client.println("<td colspan=\"2\"> </td>");
            client.println("</tr>");
            client.println("<tr>");

            client.println("<tr>");
            client.println("<td colspan=\"2\"> FLAGS </td>");
            client.println("</tr>");
            client.println("<tr>");
            client.println("<td>manualWhite:</td>");
            client.println("<td>" + String(manualWhite) + "</td>");
            client.println("</tr>");
            client.println("<tr>");
            client.println("<td>autoWhite:</td>");
            client.println("<td>" + String(autoWhite) + "</td>");
            client.println("</tr>");
            client.println("<tr>");
            client.println("<td>manualGreen:</td>");
            client.println("<td>" + String(manualGreen) + "</td>");
            client.println("</tr>");
            client.println("<tr>");
            client.println("<td>autoGreen:</td>");
            client.println("<td>" + String(autoGreen) + "</td>");
            client.println("</tr>");
            client.println("<tr>");
            client.println("<td>manualWater:</td>");
            client.println("<td>" + String(manualWater) + "</td>");
            client.println("</tr>");
            client.println("<tr>");
            client.println("<td>autoWater:</td>");
            client.println("<td>" + String(autoWater) + "</td>");
            client.println("</tr>");

            client.println("<tr>");
            client.println("<td colspan=\"2\"> </td>");
            client.println("</tr>");
            client.println("<tr>");

            client.println("</table>");
            client.println("<p></p>");
            client.println("<p></p>");
            client.println("<div><label for=\"debugging\">" + httpHeader + "</label></div>");
            client.println("<p></p>");
            client.println("<p></p>");

            client.println("<table style=\"margin-left:auto;margin-right:auto;\">");
            client.println("<tr>");
            client.println("<td><p><a href=\"/\"><button class=\"button\">back</button></a></p></td>");
            client.println("<td><p><a href=\"/restart\"><button class=\"button button3\">RESTART</button></a></p></td>");
            client.println("</tr>");
            client.println("</table>");

            client.println("<p></p>");
            client.println("</body></html>");

            // The HTTP response ends with another blank line
            client.println();
            break;
          }
          else if (httpHeader.indexOf("GET /restart") >= 0) {
            refreshToRoot();
            delay(1000);
            ESP.restart();
          }

          if (httpHeader.indexOf("Android") >= 0) {
            Serial.println("Mobile device detected");
            mobileDevice = true;
          }

          // Send HTML web page
          client.println("<!DOCTYPE html><html>");
          client.println("<meta http-equiv=\"refresh\" content=\"15\" >\n");
          client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
          client.println("<link rel=\"icon\" href=\"data:,\">");
          client.println("<title>Smart Balcony</title>");
          // CSS styling
          client.println("<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}");
          // client.println("body{margin-top: 50px;} h1 {color: #B4F9F3;margin: 50px auto 30px;}");
          client.println("body {color: white; background: black;}");
          // client.println(".button { background-color: #195B6A; border: none; color: white; padding: 16px 50px;");
          client.println(".button { background-color: #195B6A; border: none; color: white; height: 50px; width: 130px;");
          client.println("text-decoration: none; text-align: center; font-size: 30px; margin: 2px; cursor: pointer;}");
          client.println(".button3 {background-color: #ff3300;}");
          client.println(".button2 {background-color: #77878A;}</style></head>");

          client.println("<body><h1>Smart Balcony</h1>");
          client.println("<p></p>");

          client.println("<p><h3>" + formatedTime + "</h3></p>");
          client.println("<p></p>");

          client.println("<table style=\"margin-left:auto;margin-right:auto;\">");

          client.println("<tr>");
          client.println("<td>Temperature:</td>");
          client.println("<td colspan=\"2\">" + String(temperature) + " &#176C</td>");
          client.println("</tr>");

          client.println("<tr>");
          client.println("<td>Humidity:</td>");
          client.println("<td colspan=\"2\">" + String(humidity) + " %</td>");
          client.println("</tr>");

          client.println("<tr>");
          client.println("<td>Luminosity:</td>");
          client.println("<td colspan=\"2\">" + String(luminosity) + " lux</td>");
          client.println("</tr>");

          client.println("<tr>");
          client.println("<td>Movement:</td>");
          String tempMove;
          if (movementFlag) {
              tempMove = "yes";
            }
            else {
              tempMove = "no";
            }
          client.println("<td colspan=\"2\">" + tempMove  + "</td>");
          client.println("</tr>");

          client.println("<tr>");
          client.println("<td>");
          client.println("</td>");
          client.println("</tr>");

          client.println("<tr>");
          client.println("<td>White lamp</td>");
          if (autoWhite) {
            client.println("<td><p><a href=\"/autoWhite\"><button class=\"button\">Auto</button></a></p></td>");
          } else {
            client.println("<td><p><a href=\"/autoWhite\"><button class=\"button button2\">Auto</button></a></p></td>");
          }
          if (manualWhite) {
            client.println("<td><p><a href=\"/manualWhite\"><button class=\"button\">ON</button></a></p></td>");
          } else {
            client.println("<td><p><a href=\"/manualWhite\"><button class=\"button button2\">OFF</button></a></p></td>");
          }
          client.println("</tr>");

          client.println("<tr>");
          client.println("<td>Green lamp</td>");
          if (autoGreen) {
            client.println("<td><p><a href=\"/autoGreen\"><button class=\"button\">Auto</button></a></p></td>");
          } else {
            client.println("<td><p><a href=\"/autoGreen\"><button class=\"button button2\">Auto</button></a></p></td>");
          }
          if (manualGreen) {
            client.println("<td><p><a href=\"/manualGreen\"><button class=\"button\">ON</button></a></p></td>");
          } else {
            client.println("<td><p><a href=\"/manualGreen\"><button class=\"button button2\">OFF</button></a></p></td>");
          }
          client.println("</tr>");

          client.println("<tr>");
          client.println("<td>Watering system</td>");
          if (autoWater) {
            client.println("<td><p><a href=\"/autoWater\"><button class=\"button\">Auto</button></a></p></td>");
          } else {
            client.println("<td><p><a href=\"/autoWater\"><button class=\"button button2\">Auto</button></a></p></td>");
          }
          if (manualWater) {
            client.println("<td><p><a href=\"/manualWater\"><button class=\"button\">ON</button></a></p></td>");
          } else {
            client.println("<td><p><a href=\"/manualWater\"><button class=\"button button2\">OFF</button></a></p></td>");
          }
          client.println("</tr>");
          client.println("</table>");
          client.println("<p></p>");
          // client.println("<p><input type=\"checkbox\" name=\"autoRefresh\" value=\"AutoRefresh\" checked> Auto refresh ?</p>");
          // client.println("<p></p>");

          if (mobileDevice) {
            client.println("<p><iframe width=\"450\" height=\"250\" style=\"border: 0px solid #cccccc;\" src=\"https://thingspeak.com/channels/943716/charts/1?average=10&bgcolor=%232E2E2E&color=%23d62020&dynamic=true&results=288&round=1&title=Temperature+%2810%27+average%29&type=spline\"></iframe></p>");
            client.println("<p><iframe width=\"450\" height=\"250\" style=\"border: 0px solid #cccccc;\" src=\"https://thingspeak.com/channels/943716/charts/2?average=10&bgcolor=%232E2E2E&color=%232020d6&dynamic=true&results=288&round=1&title=Humidity+%2810%27+average%29&type=spline\"></iframe></p>");
            client.println("<p><iframe width=\"450\" height=\"250\" style=\"border: 0px solid #cccccc;\" src=\"https://thingspeak.com/channels/943716/charts/3?average=10&bgcolor=%232E2E2E&color=%23d6d620&dynamic=true&results=288&round=0&title=Luminosity+%2810%27+average%29&type=spline\"></iframe></p>");
          } else {
            client.println("<table style=\"margin-left:auto;margin-right:auto;\">");
            client.println("<tr>");
            client.println("<td>");
            client.println("<p><iframe width=\"450\" height=\"250\" style=\"border: 0px solid #cccccc;\" src=\"https://thingspeak.com/channels/943716/charts/1?average=10&bgcolor=%232E2E2E&color=%23d62020&dynamic=true&results=288&round=1&title=Temperature+%2810%27+average%29&type=spline\"></iframe></p>");
            client.println("</td>");
            client.println("<td>");
            client.println("<p><iframe width=\"450\" height=\"250\" style=\"border: 0px solid #cccccc;\" src=\"https://thingspeak.com/channels/943716/charts/2?average=10&bgcolor=%232E2E2E&color=%232020d6&dynamic=true&results=288&round=1&title=Humidity+%2810%27+average%29&type=spline\"></iframe></p>");
            client.println("</td>");
            client.println("<td>");
            client.println("<p><iframe width=\"450\" height=\"250\" style=\"border: 0px solid #cccccc;\" src=\"https://thingspeak.com/channels/943716/charts/3?average=10&bgcolor=%232E2E2E&color=%23d6d620&dynamic=true&results=288&round=0&title=Luminosity+%2810%27+average%29&type=spline\"></iframe></p>");
            client.println("</td>");
            client.println("</tr>");
            client.println("</table>");
          }

          client.println("<p></p>");
          client.println("</body></html>");

          client.println();     // HTTP response ends with a blank line
          break;
        }
        else {                  // if new content --> clear 'currentLine'
          currentLine = "";
        }
      }
      else if (c != '\r') {     // any input BUT a '\r' --> append it to 'currentLine'
        currentLine += c;
      }
    }
  }
  httpHeader = "";
}

void getSensorData() {
  if (sht3xAvailable) {
    humidity = sht31.readHumidity();
    temperature = sht31.readTemperature();
  }
  else {
    humidity = NULL;
    temperature = NULL;
    // humidity = -1;
    // temperature = -1;
  }
  if (isnan(humidity) || isnan(temperature)) {
    humidity = NULL;
    temperature = NULL;
  }

  if (bh1750Available) {
    luminosity = lightMeter.readLightLevel();
  }
  else {
    luminosity = NULL;
    // luminosity = -1;
  }
}

void pullNTPtime(bool printData) {
  timeClient.update();
  formatedTime = timeClient.getFormattedTime();
  // dayToday = daysOfTheWeek[timeClient.getDay()];

  if (printData) {
    // Serial.print(daysOfTheWeek[timeClient.getDay()]);
    // Serial.print(", ");
    // Serial.print(timeClient.getHours());
    // Serial.print(":");
    // Serial.print(timeClient.getMinutes());
    // Serial.print(":");
    // Serial.println(timeClient.getSeconds());
    Serial.println(timeClient.getFormattedTime());        // format time HH:MM:SS (23:05:00)
  }
  lastNTPtime = millis();
}

void serialPrintAll() {
  Serial.println();
  Serial.println(timeClient.getFormattedTime());
  Serial.print("Temperature: ");
  Serial.print(String(temperature));
  Serial.println(" °C");
  Serial.print("Humidity: ");
  Serial.print(String(humidity));
  Serial.println(" %");
  Serial.print("Luminosity: ");
  Serial.print(luminosity);
  Serial.println(" lux");
  Serial.print("Movement: ");
  Serial.print(movementFlag);
  Serial.println(" [T/F]");
  Serial.println();
}

void ledBlinker(short blinks) {
  if ((millis() > lastLEDblinkTime + ledBlinkInterval) && blinks > 0) {
    pinMode(BUILTIN_LED, OUTPUT);

    short flashed = 0;
    do {
      delay(10);
      digitalWrite(BUILTIN_LED, LOW);
      delay(40);
      digitalWrite(BUILTIN_LED, HIGH);
      delay(40);
      flashed += 1;
    } while (flashed < blinks);

    pinMode(BUILTIN_LED, INPUT);
    lastLEDblinkTime = millis();
  }
}

String millisToTime(bool calcDays) {

  String outString;
  // String strUpTime;
  // String strUpDaysTime;

  short millisSeconds = millis() / 1000;

  short seconds = millisSeconds % 60;

  millisSeconds = (millisSeconds - seconds) / 60;
  short minutes = millisSeconds % 60;

  millisSeconds = (millisSeconds - minutes) / 60;
  short hours = millisSeconds % 24;

  short days = (millisSeconds - hours) / 24;

  if (calcDays) {
    // output:  1d 3h 42' 4"  (d H M S)
    outString = String(days) + "d " + String(hours) + "h " + String(minutes) + "' " + String(seconds) + "\"";
  }
  else {
    // output:  3:42:4        (H:M:S)
    outString = String(hours) + ":" + String(minutes) + ":" + String(seconds);
  }

  return outString;
}

void loop(){

  ArduinoOTA.handle();

  // ~~~~~~ REBOOT DEVICE IF NO WiFi FOR 15' (900000 ms) (1h : 3600000) ~~~~~~~
  // check connection every 30"
  if (millis() > lastWiFiCheckTime + 30000) {
    if (WiFi.status() != WL_CONNECTED) {
      wifiAvailable = false;
      lastWiFiLostTime = millis();
      Serial.println("[ERROR] WiFi connection lost");
    }
    else {
      wifiAvailable = true;
    }
    lastWiFiCheckTime = millis();
  }
  if ((!wifiAvailable) && (millis() > lastWiFiLostTime + 900000)) {
    Serial.println("[ERROR] no WiFi for 15 min. Reconnecting ...");
    delay(100);
    WiFi.disconnect();
    WiFi.reconnect();
    delay(5000);

    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[ERROR] WiFi reconnection failled. Rebooting ...");
      ESP.restart();
    }
  }
  // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~


  // ~~~~~~~~~~~~~~~~~~~~~~~~ READ SENSORS AND INPUTS ~~~~~~~~~~~~~~~~~~~~~~~~~
  if (millis() > lastSensorsTime + sensorsInterval) {
    // Serial.println("[INFO] reading sensor data...");
    getSensorData();
    lastSensorsTime = millis();
    serialPrintAll();
  }

  // used for PIR_IN
  if (digitalRead(BUILTIN_LED)) {
    movementFlag = false;
  }
  else {
    movementFlag = true;
  }

  if (millis() > lastAnalogTime + analogReadInterval) {
    // Serial.println("[INFO] reading analog input...");
    analogValue = analogRead(BTN_IN);
    analogValue = map(analogValue, 0, 1024, 1024, 0);
    lastAnalogTime = millis();

    if (analogValue <= 300) {         // btn_1  (214)
      // Serial.println("[DEBUG] BTN_1 pressed");
      manualWhite = !manualWhite;
    }
    else if (analogValue <= 550) {    // btn_2  (480)
      // Serial.println("[DEBUG] BTN_2 pressed");
      manualGreen = !manualGreen;
    }
    else if (analogValue <= 850) {    // btn_3  (747)
      // Serial.println("[DEBUG] BTN_3 pressed");
      manualWater = !manualWater;
    }
  }
  // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~


  // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ RELAY HANDLER ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  if (manualWhite || (autoWhite && movementFlag)) {
    digitalWrite(RELAY_W, LOW);
  }
  else {
    digitalWrite(RELAY_W, HIGH);
  }

  if (manualGreen) {
    digitalWrite(RELAY_G, LOW);
  }
  else {
    digitalWrite(RELAY_G, HIGH);
  }

  if (manualWater) {
    digitalWrite(RELAY_WT, LOW);
  }
  else {
    digitalWrite(RELAY_WT, HIGH);
  }
  // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~


  // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ LED HANDLER ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  if (!wifiAvailable) {
    ledBlinker(3);
    ledBlinkInterval = 1000;
  }
  else if (manualWater) {
    ledBlinker(3);
    ledBlinkInterval = 3000;
  }
  else {
    ledBlinker(1);
    ledBlinkInterval = 5000;
  }
  // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~


  // ~~~~~~~~~~~~~~~~~~~~~~~ NTP - THINGSPEAK - SERVER ~~~~~~~~~~~~~~~~~~~~~~~~
  if (wifiAvailable) {
    if (millis() > lastNTPtime + ntpInterval) {
      pullNTPtime(false);
    }

    if (millis() > lastUploadTime + thingSpeakInterval) {
      thingSpeakRequest();
    }

    client = server.available();

    if (client) {
      // clientsHandler();        // test on '.webpageHandler.cpp'
      handleClientConnection();
      // httpHeader = "";
      client.stop();
    }
  }
  // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
}