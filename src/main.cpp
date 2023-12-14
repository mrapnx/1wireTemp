#include <Arduino.h>
#include <Wire.h>
#include <WiFiNINA.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <PubSubClient.h>

// *************** Deklaration der Funktionen
boolean checkWiFi();
boolean connectToMQTT();
void sendTemperaturesToMQTT();
void printSensorAddresses();
void setup();
void printWiFiStatus();
void getTemperatures();
String getTemperatureAsHtml();
String deviceAddrToStr(DeviceAddress addr);

// *************** Konfig

// WLAN-Zugangsdaten
char ssid[] = "Muspelheim";
char pass[] = "dRN4wlan5309GTD9fR";
const int wifiTimeout = 10; // Timeout zur Verbindung mit dem WLAN in Sek

// MQTT-Zugangsdaten
const char *mqttServer = "192.168.66.21";
const int mqttPort = 1883;
const char *mqttName = "ArduinoClient";
const char *mqttUser = "ArudinoNano";
const char *mqttPassword = "DEIN_MQTT_PASSWORT";

// Pin, an dem der 1-Wire-Bus angeschlossen ist
const int PinOneWireBus = 2; // = D2

// Frequenz in Sekunden, in der die WLAN-Verbindung versucht wird
const int wifiCheckInterval = 10;
// Frequenz in Sekunden, in der die Temperaturen abgefragt werden
const int tempCheckInterval = 10;
// Frequenz in Sekunden, in der die Temperaturen an MQTT gesendet werden
const int sendInterval = 10;
// Frequenz, in der die orange LED bei Fehlern blinkt
const int blinkInterval = 500; 

// ***************  Globale Variablen
unsigned long tempCheckLast = 0;
unsigned long sendLast = 0;
unsigned long wifiCheckLast = 0;
unsigned long blinkLast = 0;
boolean blinking = false;

// 1-Wire
byte addrArray[8];
OneWire oneWire(PinOneWireBus);
DallasTemperature sensors(&oneWire);

// Webserver
IPAddress   ip; 
WiFiServer server(80);

// MQTT-Client
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);


// ***************  Funktionen


String deviceAddrToStr(DeviceAddress addr) {
  String returnString = "";
    for (uint8_t j = 0; j < 8; j++) {
      if (addr[j] < 16) returnString = "0";
      returnString = returnString + String(addr[j], HEX);
    }
  returnString.toUpperCase();
  return returnString;
}

void blink() {
  if ((WiFi.status() == WL_CONNECTED) && (sensors.getDeviceCount() > 0)) {
    digitalWrite(LED_BUILTIN, HIGH);
    return;
  } else {
    // Brich ab, wenn unser Inverall noch nicht erreicht ist
    if (millis() < blinkLast + (blinkInterval * 1000)) {
      return;
    }
    blinkLast = millis();
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
  }
}

void getTemperatures() {
  // Brich ab, wenn unser Inverall noch nicht erreicht ist
  if (millis() < tempCheckLast + (tempCheckInterval * 1000)) {
    return;
  }
  tempCheckLast = millis();
  // Aktualisiere die Temperaturdaten
  sensors.requestTemperatures();
}

String getTemperatureAsHtml() {
  Serial.println("getTemperatureAsHtml() begin");
  String address;
  String temp;
  DeviceAddress addr;
  String returnString = "";
  for (int i = 0; i < sensors.getDeviceCount(); i++) {
    float tempC = sensors.getTempCByIndex(i);
    sensors.getAddress(addr, i);
    address = deviceAddrToStr(addr);
    temp = String(tempC);
    returnString = returnString + address.substring(0, address.length()-2) + "-<b>" + address.substring(address.length()-2) + "</b>: " + temp + " &#8451;</br>";
    Serial.println("Ermittle Inhalt für Webserver: " + returnString);
  }
  return returnString;
  Serial.println("getTemperatureAsHtml() end");
}

void setup() {
  // Starte die serielle Kommunikation
  Serial.begin(9600);
  Serial.println("setup() begin");
  pinMode(LED_BUILTIN, OUTPUT);

  // Verbinde mit dem WLAN
  checkWiFi();

  // Verbinde mit dem MQTT-Server
  connectToMQTT();

  // Initialisiere die OneWire- und DallasTemperature-Bibliotheken
  if (oneWire.search(addrArray)) {
    Serial.print("Geräte gefunden: ");
    // Serial.println(addrArray);
  } else {
    Serial.println("Keine Geräte gefunden");
  }


  // Suche nach angeschlossenen Sensoren
  sensors.begin();

  Serial.println("Gefundene 1-Wire-Sensoren:");
  printSensorAddresses();
  Serial.println("setup() end");
}

void printWiFiStatus() {
  Serial.println("printWiFiStatus() begin");
  // print the SSID of the network you're attached to:
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  // print your WiFi shield's IP address:
  ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);

  // print where to go in a browser:
  Serial.print("To see this page in action, open a browser to http://");
  Serial.println(ip);
  Serial.println("printWiFiStatus() end");
}

boolean checkWiFi() {
  // Ermittle den Zeitpunkt, bis zu dem der Verbindungsversuch dauern darf
  unsigned int deadline = millis() + (wifiTimeout * 1000);

  // Brich ab, wenn unser Inverall noch nicht erreicht ist
  if (millis() < wifiCheckLast + (wifiCheckInterval * 1000)) {
    return false;
  }
  wifiCheckLast = millis();

  Serial.println("Verbinde mit WLAN...");
  while (WiFi.begin(ssid, pass) != WL_CONNECTED) {
    delay(1000);
    Serial.println("Verbindung wird hergestellt...");
    if (millis() > deadline) {
      break;
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Verbunden mit WLAN");
    // Starte den Webserver
    server.begin();
    printWiFiStatus();
    return true;
  } else  {
    Serial.println("Verbindung mit WLAN fehlgeschlagen!");
    return false;
  } 
}

boolean connectToMQTT() {
  Serial.println("Verbinde mit MQTT-Server...");
  mqttClient.setServer(mqttServer, mqttPort);
  while (!mqttClient.connected()) {
    if (mqttClient.connect(mqttName, mqttUser, mqttPassword)) {
      Serial.println("Verbunden mit MQTT-Server");
    } else {
      Serial.print("Fehlgeschlagen, rc=");
      Serial.print(mqttClient.state());
    }
  }
}

void sendTemperaturesToMQTT() {
  String topic = "n/a";
  String payload;
  DeviceAddress addr;

  // Brich ab, wenn unser Inverall noch nicht erreicht ist
  if (millis() < sendLast + (sendInterval * 1000)) {
    return;
  }
  sendLast = millis();

  // Wenn MQTT noch nicht verbunden ist
  if (!mqttClient.connected()) {
    // Versuche einen Verbindungsaufbau
    if (!connectToMQTT()) {
      // Wenn das fehlgeschlagen ist, brich ab
      return;
    }
  }

  // Fehlermeldung, wenn keine Sensoren gefunden wurden
  if (sensors.getDeviceCount() <= 0) {
    Serial.println("Keine Sensoren gefunden, deren Daten übermittelt werden könnten"); 
  }

  // Iteriere durch alle Sensoren
  for (int i = 0; i < sensors.getDeviceCount(); i++) {
    // Ermittle die Temperatur
    Serial.println("Ermittle temperatur sensor " + String(i));
    float tempC = sensors.getTempCByIndex(i);

    // Ermittle die Adresse
    Serial.println("Ermittle adresse sensor " + String(i));
    sensors.getAddress(addr, i); 

    // Und bilde die MQTT-Nachricht
    topic = "sensor/" + deviceAddrToStr(addr) + "/temperature";
    payload = String(tempC);
    Serial.println("topic: " + topic + " - payload: " + payload);
    mqttClient.publish(topic.c_str(), payload.c_str());
  }
}

void printSensorAddresses() {
  Serial.print("Anzahl: ");
  Serial.println(sensors.getDeviceCount());
  for (int i = 0; i < sensors.getDeviceCount(); i++) {
    DeviceAddress tempAddress;
    sensors.getAddress(tempAddress, i);

    Serial.print("Sensor ");
    Serial.print(i + 1);
    Serial.print(" Adresse: ");

    for (uint8_t j = 0; j < 8; j++) {
      if (tempAddress[j] < 16) Serial.print("0");
      Serial.print(tempAddress[j], HEX);
    }

    Serial.println();
  }
}

void loop() {
  // Verarbeite HTTP-Anfragen
  WiFiClient client = server.available();
  if (client) {                             // if you get a client,
    Serial.println("new client");           // print a message out the serial port
    String currentLine = "";                // make a String to hold incoming data from the client
    while (client.connected()) {            // loop while the client's connected
      if (client.available()) {             // if there's bytes to read from the client,
        char c = client.read();             // read a byte, then
        Serial.write(c);                    // print it out the serial monitor
        if (c == '\n') {                    // if the byte is a newline character

          // if the current line is blank, you got two newline characters in a row.
          // that's the end of the client HTTP request, so send a response:
          if (currentLine.length() == 0) {

            // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
            // and a content-type so the client knows what's coming, then a blank line:
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type:text/html");
            client.println();

            // the content of the HTTP response follows the header:
            client.print("<html>");
            client.print("<head>");
            client.print("<meta http-equiv=\"refresh\" content=\"1; url=http://");
            client.print(ip);
            client.print("/\"/>");
            client.print("</head>");
            client.print("<p style=\"font-size:80px; font-family: monospace\">"); 
            client.print("Sensoren: </br>");
            client.print(getTemperatureAsHtml());
            //client.print("s <b><a href=\"/m\">+</a>&nbsp;&nbsp;<a href=\"/l\">-</a></b>");
            client.print("<br/>");
            //client.print("<a href=\"/1\">Start</a><br/>");
            //client.print("<a href=\"/0\">Stop</a><br/>");
            client.print("</p>");
            client.print("</html>");

            // The HTTP response ends with another blank line:
            client.println();

            // break out of the while loop:
            break;

          }
          else {      // if you got a newline, then clear currentLine:
            currentLine = "";
          }
        }

        else if (c != '\r') {    // if you got anything else but a carriage return character,
          currentLine += c;      // add it to the end of the currentLine
        }

        // "On"
        if (currentLine.endsWith("GET /1")) {
        }

        // "Off"
        if (currentLine.endsWith("GET /0")) {
        }

        // Zeit erhöhen, hiernach wird die Seite mit dem neuen Wert ausgeliefert
        if (currentLine.endsWith("GET /m")) {
        }

        // Zeit verringern, hiernach wird die Seite mit dem neuen Wert ausgeliefert
        if (currentLine.endsWith("GET /l")) {
        }
      }
    }

    // close the connection:
    client.stop();
    Serial.println("client disconnected");
  }

  checkWiFi();

  getTemperatures();

  sendTemperaturesToMQTT();

}
