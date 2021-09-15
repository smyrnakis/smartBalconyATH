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

char otaAuthPin[] = OTA_AUTH_PIN;

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
const long connectionKeepAlive = 2000;    // 2 seconds


const char* thinkSpeakAPI = "api.thingspeak.com"; // "184.106.153.149" or api.thingspeak.com

// Network Time Protocol
const long utcOffsetInSeconds = 7200; // 2H (7200) for winter time / 3H (10800) for summer time
char daysOfTheWeek[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

// ESP8266WebServer server(80);
WiFiServer server(80);
WiFiClient client;
WiFiClient clientThSp;

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

  // server.on("/", handle_OnConnect);
  // server.on("/help", handle_OnConnectHelp);
  // server.onNotFound(handle_NotFound);
  
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

void thingSpeakRequest() {
  clientThSp.stop();
  if (clientThSp.connect(thinkSpeakAPI,80)) {
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
    if (debuggingMode) { Serial.println("Data uploaded to thingspeak!"); }

    lastUploadTime = millis();
  }
  else {
    if (debuggingMode) { Serial.println("ERROR: could not upload data to thingspeak!"); }
  }
}

void refreshToRoot() {
  client.print("<HEAD>");
  client.print("<meta http-equiv=\"refresh\" content=\"0;url=/\">");
  client.print("</head>");
}

// <?><?><?><?><?><?><?><?><?><?><?><?><?><?><?><?>
void handleClientConnection() {
    String currentLine = "";                    // make a String to hold incoming data from the client
    unsigned long currentTime;
    currentTime = millis();
    previousTime = currentTime;
    while (client.connected() && currentTime - previousTime <= connectionKeepAlive) { // loop while the client's connected
        currentTime = millis();         
        if (client.available()) {                   // if there's bytes to read from the client,
            char c = client.read();                 // read a byte, then
            Serial.write(c);                        // print it out the serial monitor
            httpHeader += c;
            if (c == '\n') {                        // if the byte is a newline character
                // if the current line is blank, you got two newline characters in a row.
                // that's the end of the client HTTP request, so send a response:
                if (currentLine.length() == 0) {
                    // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
                    // and a content-type so the client knows what's coming, then a blank line:
                    client.println("HTTP/1.1 200 OK");
                    client.println("Content-type:text/html");
                    client.println("Connection: close");
                    client.println();

                    if (httpHeader.indexOf("GET /on") >= 0) {
                        Serial.println("LEDs on");
                        digitalWrite(USB_1, HIGH);
                        digitalWrite(USB_2, HIGH);
                        manuallyOn = true;
                        manuallyOff = false;
                        refreshToRoot();
                    }
                    else if (httpHeader.indexOf("GET /off") >= 0) {
                        Serial.println("LEDs off");
                        digitalWrite(USB_1, LOW);
                        digitalWrite(USB_2, LOW);
                        manuallyOn = false;
                        manuallyOff = true;
                        refreshToRoot();
                    }
                    else if (httpHeader.indexOf("GET /lumUp") >= 0) {
                        if (luminosity < 1024) {
                            luminosity++;
                            Serial.print("Luminosity up (");
                            Serial.print(luminosity);
                            Serial.println(")");
                        }
                        refreshToRoot();
                    }
                    else if (httpHeader.indexOf("GET /lumDow") >= 0) {
                        if (luminosity > 0) {
                            luminosity--; 
                            Serial.print("Luminosity down (");
                            Serial.print(luminosity);
                            Serial.println(")");
                        }
                        refreshToRoot();
                    }
                    else if (httpHeader.indexOf("GET /auto") >= 0) {
                        autoMode = !autoMode;
                        refreshToRoot();
                    }

                    // Display the HTML web page
                    client.println("<!DOCTYPE html><html>");
                    client.println("<meta http-equiv=\"refresh\" content=\"10\" >\n");
                    client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
                    client.println("<link rel=\"icon\" href=\"data:,\">");
                    // CSS to style the on/off buttons 
                    // Feel free to change the background-color and font-size attributes to fit your preferences
                    client.println("<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}");
                    client.println(".button { background-color: #195B6A; border: none; color: white; padding: 16px 50px;");
                    client.println("text-decoration: none; font-size: 30px; margin: 2px; cursor: pointer;}");
                    client.println(".button3 {background-color: #ff3300;}");
                    client.println(".button2 {background-color: #77878A;}</style></head>");


                    // Web Page Heading
                    client.println("<body><h1>XmasLEDs configuration</h1>");
                    client.println("<p><h3>Time: " + formatedTime + "</h3></p>");

                    client.println("<table style=\"margin-left:auto;margin-right:auto;\">");
                    client.println("<tr>");
                    if (digitalRead(USB_1) || digitalRead(USB_2)) {
                        client.println("<th colspan=\"2\"><p><a href=\"/off\"><button class=\"button\">ON</button></a></p></th>");
                    } else {
                        client.println("<th colspan=\"2>\"<p><a href=\"/on\"><button class=\"button button2\">OFF</button></a></p></th>");
                    }
                    client.println("</tr>");
                    client.println("<tr>");
                    if (autoMode) {
                        client.println("<td colspan=\"2\"><p><a href=\"/auto\"><button class=\"button\">Auto Mode</button></a></p></td>");
                    } else {
                        client.println("<td colspan=\"2\"><p><a href=\"/auto\"><button class=\"button button2\">Auto Mode</button></a></p></td>");
                    }
                    client.println("</tr>");
                    // client.println("</table>");

                    // client.println("<table style=\"margin-left:auto;margin-right:auto;\">");
                    client.println("<tr>");
                    client.println("<td>");
                    client.println("<p><a href=\"/lumUp\"><button class=\"button\">+</button></a></p>");
                    client.println("</td>");
                    client.println("<td>");
                    client.println("<p><a href=\"/lumDow\"><button class=\"button\">-</button></a></p>");
                    client.println("</td>");
                    client.println("</tr>");
                    client.println("</table>");

                    client.println("<p></p>");
                    client.println("<p>autoMode: " + String(autoMode) + "</p>");
                    client.println("<p>manuallyOn: " + String(manuallyOn) + "</p>");
                    client.println("<p>manuallyOff: " + String(manuallyOff) + "</p>");
                    client.println("<p>allowNtp: " + String(allowNtp) + "</p>");
                    client.println("<p>allowPing: " + String(allowPing) + "</p>");
                    client.println("<p>allowThSp: " + String(allowThSp) + "</p>");

                    client.println("</body></html>");

                    // The HTTP response ends with another blank line
                    client.println();
                    break;
                } else { // if you got a newline, then clear currentLine
                    currentLine = "";
                }
            } else if (c != '\r') {  // if you got anything else but a carriage return character,
                currentLine += c;      // add it to the end of the currentLine
            }
        }
    }
}

void getSensorData() {
  temperature = dht.readTemperature();
  humidity = dht.readHumidity();
  luminosity = analogRead(ANLG_IN);
  luminosity = map(luminosity, 0, 1024, 1024, 0);
}

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

// <?><?><?><?><?><?><?><?><?><?><?><?><?><?><?><?>
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

  client = server.available();

  if (client) {
      Serial.println("New Client.");
      
      handleClientConnection();

      httpHeader = "";
      client.stop();
      Serial.println("Client disconnected.");
      Serial.println("");
  }
}