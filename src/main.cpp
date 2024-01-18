#include <Arduino.h>
#include <avr/dtostrf.h>
#include <Wire.h>
#include <WiFiNINA.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <PubSubClient.h>
#include <SPI.h>
#include <TFT_ILI9163C.h> // Achtung! In der TFT_IL9163C_settings.h muss >> #define __144_BLACK_PCB__ << aktiv sein!. Offenbar ist mein Board nicht von dem Bug betroffen, von dem andere rote Boards betroffen sind. Siehe Readme der TFT_IL9163 Lib.

// *************** Konfig

// Einstellungen
boolean wifiActive = false;
boolean mqttActive = false;

// Display
                      // Rot   LED   +3.3V
#define TFT_CLK  13   // Gelb  SCK   D13
#define TFT_MOSI 11   // Braun SDA   D11
#define TFT_DC   4    // Grün  A0    D5
#define TFT_RST  3    // Lila  RES   D2
#define TFT_CS   5    // Blau  CS    D3
                      // Schw. GND   GND
                      // Rot   VCC   +3.3V                     
#define TFT_MISO 12   //   - D12

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
const int PinOneWireBus = 10; // = D10

// Frequenz in Sekunden, in der die WLAN-Verbindung versucht wird
const int wifiCheckInterval = 10;
// Frequenz in Sekunden, in der die Temperaturen abgefragt werden
const int tempCheckInterval = 10;
// Frequenz in Sekunden, in der die Temperaturen an MQTT gesendet werden
const int sendInterval = 10;
// Frequenz in Sekunden, in der die Temperaturen angezeigt werden
const int displayInterval = 1;
// Frequenz, in der die orange LED bei Fehlern blinkt in ms
const int blinkInterval = 500; 

// ***************  Globale Variablen
unsigned long tempCheckLast = 0;
unsigned long sendLast = 0;
unsigned long displayLast = 0;
unsigned long wifiCheckLast = 0;
unsigned long blinkLast = 0;
boolean blinking = false;

// Arbeitsdaten
struct SensorData {
  char address[17];
  char type[11];
  char name[21];
  float value;
};
SensorData* sensorList = nullptr;  // Zeiger auf das Array von SensorData
int numberOfSensors = 0; // Aktuelle Anzahl von Sensoren


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

// Display
TFT_ILI9163C tft = TFT_ILI9163C(TFT_CS, TFT_DC, TFT_RST);

// *************** Deklaration der Funktionen
boolean sensorsFine();
boolean wifiFine();
boolean checkWiFi();
boolean connectToMQTT();

void addSensor(const char address[17], const char name[21], const char type[11], const float value);
void removeSensor(char address[17]);
void clearSensorList();

void getTemperatures();
void displayTemperatures(); 
String deviceAddrToStr(DeviceAddress addr);
void deviceAddrToStrNew(const DeviceAddress addr, String out);
const char* deviceAddrToChar(DeviceAddress addr); 
void getSensorName(const char address[17], char name[21]); 
String getTemperatureAsHtml();
void sendTemperaturesToMQTT();
void printSensorAddresses();
void printWiFiStatus();
void setup1Wire(); 
void setupDisplay();
void setup();

// ***************  Funktionen
void clearSensorList() {
  // Befreie den Speicher von sensorList
  free(sensorList);

  // Setze die Anzahl der Sensoren auf 0
  numberOfSensors = 0;

  // Setze den Zeiger auf null, um sicherzustellen, dass er nicht auf ungültigen Speicher zeigt
  sensorList = nullptr;
}

void addSensor(const char address[16], const char name[20], const char type[10], const float value) {
  Serial.println("addSensor(): begin");

  Serial.print("Adresse: ");
  Serial.print(address);
  Serial.print(" Name: ");
  Serial.print(name);
  Serial.print(" Typ: ");
  Serial.print(type);
  Serial.print(" Wert: ");
  Serial.println(value);

  Serial.println("addSensor(): Erzeuge SensorData tempArray");
  SensorData* tempArray = (SensorData*)malloc((numberOfSensors + 1) * sizeof(SensorData));

  // Übertrage vorhandene Daten in das temporäre Array
  Serial.println("addSensor(): Übertrage vorhandene Daten in das temporäre Array");
  for (int i = 0; i < numberOfSensors; i++) {
    tempArray[i] = sensorList[i];
  }

  // Füge das neue Sensorobjekt hinzu
  Serial.println("addSensor(): Füge das neue Sensorobjekt hinzu");
  tempArray[numberOfSensors] = {*address, *name, *type, value};

  // Erhöhe die Anzahl der Sensoren
  Serial.println("addSensor(): Erhöhe die Anzahl der Sensoren");
  numberOfSensors++;

  // Befreie den alten Speicher
  Serial.println("addSensor(): Befreie den alten Speicher");
  free(sensorList);

  // Weise den neuen Speicher zu
  Serial.println("addSensor(): Weise den neuen Speicher zu");
  sensorList = tempArray;

  Serial.println("addSensor(): end");
}

void getSensorName(const char address[17], char name[21]) {
  // TODO: Dummy Funktion
  strcpy(name, address);
  strcat(name, ".");
}

void removeSensor(char address[17]) {
  for (int i = 0; i < numberOfSensors; i++) {
    if (sensorList[i].address == address) {
      // Sensor gefunden, löschen, indem die nachfolgenden Elemente verschoben werden
      for (int j = i; j < numberOfSensors - 1; j++) {
        sensorList[j] = sensorList[j + 1];
      }

      // Verringere die Anzahl der Sensoren
      numberOfSensors--;

      // Reduziere die Speichergröße
      SensorData* tempArray = (SensorData*)malloc(numberOfSensors * sizeof(SensorData));

      // Übertrage Daten in das temporäre Array
      for (int k = 0; k < numberOfSensors; k++) {
        tempArray[k] = sensorList[k];
      }

      // Befreie den alten Speicher
      free(sensorList);

      // Weise den neuen Speicher zu
      sensorList = tempArray;

      break;
    }
  }
}


void setupDisplay() {
  Serial.println("setup() Initialisiere Display");

  tft.begin();
  tft.setBitrate(24000000);
  tft.clearScreen();
  tft.defineScrollArea(128, 128);
  tft.setCursor(0, 10);
  tft.println("Start");
}

boolean wifiFine() {
  return WiFi.status() == WL_CONNECTED;
}

boolean sensorsFine() {
 return sensors.getDeviceCount() > 0;
}

String deviceAddrToStr(DeviceAddress addr) {
  String returnString = "";
    for (uint8_t j = 0; j < 8; j++) {
      //if (addr[j] < 16) returnString = "0";  // War im ursprünglichen Vorschlag einer Konvertierung, scheint aber keinen Sinn zu machen, da HEX 00 legitim ist.
      returnString = returnString + String(addr[j], HEX);
    }
  returnString.toUpperCase();
  return returnString;
}

void deviceAddrToStrNew(const DeviceAddress addr, String out) {
  out = "";
    for (uint8_t j = 0; j < 8; j++) {
      if (addr[j] < 16) out = "0";
      out = out + String(addr[j], HEX);
      Serial.println("deviceAddrToStrNew() Durchlauf " + String(j) + ": out = " + out);
    }
  out.toUpperCase();
  Serial.println("deviceAddrToStrNew() ende, out = " + out);
}

const char* deviceAddrToChar(DeviceAddress addr) {
  static char result[17];
  String returnString = "";
    for (uint8_t j = 0; j < 8; j++) {
      if (addr[j] < 16) returnString = "0";
      returnString = returnString + String(addr[j], HEX);
    }
  returnString.toUpperCase();
  strcpy(result, returnString.c_str());
  return result;
}


void blink() {
  if (wifiFine() && sensorsFine()) {
    digitalWrite(LED_BUILTIN, HIGH);
    return;
  } else {
    // Brich ab, wenn unser Inverall noch nicht erreicht ist
    if (millis() < blinkLast + (blinkInterval)) {
      return;
    }
    blinkLast = millis();
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
  }
}

void getTemperatures() {
float value;
String address;
char addressC[17];
char nameC[21];
DeviceAddress addr;

  // Brich ab, wenn unser Inverall noch nicht erreicht ist
  if (millis() < tempCheckLast + (tempCheckInterval * 1000)) {
    return;
  }
  Serial.println("getTemperatures() begin");
  tempCheckLast = millis();

  // Leere die Liste
  clearSensorList();

  // Aktualisiere die Temperaturdaten
  Serial.println("getTemperatures() Aktualisiere Temperaturen");
  sensors.requestTemperatures();

  // Iteriere durch alle Sensoren
  for (int i = 0; i < sensors.getDeviceCount(); i++) {
    // Ermittle die Temperatur
    Serial.println("Ermittle Temperatur Sensor " + String(i));
    value = sensors.getTempCByIndex(i);
    Serial.print("Temperatur Sensor " + String(i) + ": ");
    Serial.println(value);

    // Ermittle die Adresse
    Serial.println("Ermittle Adresse Sensor " + String(i));
    sensors.getAddress(addr, i); 
    address = deviceAddrToStr(addr);
    strcpy (addressC, address.c_str());
    Serial.println("address: " + address);
    Serial.print("addressC: ");
    Serial.println(addressC);

    // Ermittle den Namen
    Serial.println("Ermittle Name Sensor " + String(i));
    getSensorName(addressC, nameC);
    Serial.print("Name Sensor " + String(i) + ": ");
    Serial.println(nameC);

    addSensor(addressC, nameC, "unknown", value);
  }
Serial.println("getTemperatures() end");
}

void setup1Wire() {
    // Initialisiere die OneWire- und DallasTemperature-Bibliotheken
  if (oneWire.search(addrArray)) {
  } else {
    tft.println("Keine Geräte gefunden");
    Serial.println("Keine Geräte gefunden");
  }


  // Suche nach angeschlossenen Sensoren
  sensors.begin();

  Serial.println("Gefundene 1-Wire-Sensoren:");
  tft.println("Gefundene 1-Wire-Sensoren:");
  printSensorAddresses();
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
  delay(1000);
  Serial.println("setup() begin");
  pinMode(LED_BUILTIN, OUTPUT);


  // Display
  setupDisplay();

  // Verbinde mit dem WLAN
  checkWiFi();

  // Verbinde mit dem MQTT-Server
  connectToMQTT();

  // Öffne den 1-Wire Bus
  setup1Wire();
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
  // Prüfe, ob WiFi überhaupt aktivier tist
  if (!wifiActive) {
    return true;
  }
  // Ermittle den Zeitpunkt, bis zu dem der Verbindungsversuch dauern darf
  unsigned int deadline = millis() + (wifiTimeout * 1000);

  // Brich ab, wenn unser Inverall noch nicht erreicht ist
  if (millis() < wifiCheckLast + (wifiCheckInterval * 1000)) {
    return false;
  }
  wifiCheckLast = millis();
 
  // Wenn WLAN nicht verbunden ist,
  if (!wifiFine()) {
    // versuch, die Verbindung aufzubauen
    Serial.println("Verbinde mit WLAN...");
    while (WiFi.begin(ssid, pass) != WL_CONNECTED) {
      delay(1000);
      Serial.println("Verbindung wird hergestellt...");
      if (millis() > deadline) {
        break;
      }
    }
  } else {
    // Wenn die Verbindung besteht, steig aus
    return true;
  } 

  // Prüfe, ob nun eine Verbindung besteht
  if (wifiFine()) {
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
  // Prüfe, ob MQTT überhaupt aktivier tist
  if (!mqttActive) {
    return true;
  }

  if (!wifiFine()) {
    Serial.println("connectToMQTT(): Keine WLAN-Verbindung, breche Verbindung mit MQTT-Server ab");
    return false;
  }
  Serial.println("connectToMQTT(): Verbinde mit MQTT-Server...");
  mqttClient.setServer(mqttServer, mqttPort);
  while (!mqttClient.connected()) {
    if (mqttClient.connect(mqttName, mqttUser, mqttPassword)) {
      Serial.println("connectToMQTT(): Verbunden mit MQTT-Server");
      return true;
    } else {
      Serial.print("connectToMQTT(): Verbindung mit MQTT-Server fehlgeschlagen, rc=");
      Serial.println(mqttClient.state());
    }
  }
}

void displayTemperatures() {
  char nameC;

  // Brich ab, wenn unser Inverall noch nicht erreicht ist
  if (millis() < displayLast + (displayInterval * 1000)) {
    return;
  }
  displayLast = millis();

  // Fehlermeldung, wenn keine Sensoren gefunden wurden
  if (sensors.getDeviceCount() <= 0) {
    Serial.println("displayTemperatures(): Keine Sensoren gefunden, deren Daten angezeigt werden könnten"); 
  }

  // Iteriere durch alle Sensoren
  for (int i = 0; i < numberOfSensors; i++) {
    // Und bilde die MQTT-Nachricht
    if (sensorList[i].name == NULL) {
      strcpy(&nameC, sensorList[i].name);
    } else {
      strcpy(&nameC, sensorList[i].address);
    }

    tft.clearScreen();
    tft.setCursor(0, 0);
    tft.print(String(nameC) + ": " + sensorList[i].value);
  }
}



void sendTemperaturesToMQTT() {
  char topic[30] = "n/a";
  char payload[10];

  // Prüfe, ob WiFi überhaupt aktivier tist
  if (!mqttActive) {
    return;
  }

  // Brich ab, wenn unser Inverall noch nicht erreicht ist
  if (millis() < sendLast + (sendInterval * 1000)) {
    return;
  }
  sendLast = millis();

  // Wenn MQTT noch nicht verbunden ist
  if (!mqttClient.connected()) {
    Serial.println("sendTemperaturesToMQTT(): Keine Verbindung zum MQTT-Server, versuche Verbindungsaufbau");
    // Versuche einen Verbindungsaufbau
    if (!connectToMQTT()) {
      // Wenn das fehlgeschlagen ist, brich ab
      Serial.println("sendTemperaturesToMQTT(): Verbindungsaufbau fehlgeschlagen, breche ab");
      return;
    }
  }

  // Fehlermeldung, wenn keine Sensoren gefunden wurden
  if (sensors.getDeviceCount() <= 0) {
    Serial.println("sendTemperaturesToMQTT(): Keine Sensoren gefunden, deren Daten übermittelt werden könnten"); 
  }

  // Iteriere durch alle Sensoren
  for (int i = 0; i < numberOfSensors; i++) {
    // Ermittle die Temperatur
    Serial.println("Ermittle temperatur sensor " + String(i));
    // Und bilde die MQTT-Nachricht
    strcpy(topic, "sensor/");
    strcat(topic, sensorList[i].address);
    strcat(topic, "/temperature");
    dtostrf(sensorList[i].value, 3, 2, payload);
    
    Serial.print("topic: ");
    Serial.print(topic);
    Serial.print(" - payload: ");
    Serial.println(payload);
    mqttClient.publish(topic, payload);
  }
}

void printSensorAddresses() {
  DeviceAddress tempAddress;

  Serial.print("Anzahl: ");
  Serial.println(sensors.getDeviceCount());
  tft.print("Anzahl: ");
  tft.println(sensors.getDeviceCount());
  for (int i = 0; i < sensors.getDeviceCount(); i++) {   
    sensors.getAddress(tempAddress, i);

    Serial.print("Sensor ");
    Serial.print(i + 1);
    Serial.print(" Adresse: ");

    tft.print("Sensor ");
    tft.print(i + 1);
    tft.print(" Adresse: ");

    for (uint8_t j = 0; j < 8; j++) {
      if (tempAddress[j] < 16) Serial.print("0");
      Serial.print(tempAddress[j], HEX);
      tft.print(tempAddress[j], HEX);
    }

    Serial.println();
    tft.println();
  }
}

void loop() {
  blink();

  checkWiFi();

  getTemperatures();

  sendTemperaturesToMQTT();

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

}
