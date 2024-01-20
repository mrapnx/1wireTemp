#include <Arduino.h>
#include <avr/dtostrf.h>
#include <Wire.h>
#include <WiFiNINA.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <PubSubClient.h>
#include <SPI.h>
#include <FlashStorage_SAMD.h>
#include <TFT_ILI9163C.h> // Achtung! In der TFT_IL9163C_settings.h muss >> #define __144_BLACK_PCB__ << aktiv sein!. Offenbar ist mein Board nicht von dem Bug betroffen, von dem andere rote Boards betroffen sind. Siehe Readme der TFT_IL9163 Lib.
#include <DS2438.h>
#include "sensors.h"

// *************** Konfig

typedef struct {
  // WLAN
  boolean wifiActive = true;
  char ssid[20] = "Arduino AP";
  char pass[20] = "Arduino AP";
  char mode = 'a'; // a = Access Point / c = Client
  int wifiTimeout = 10;

  // MQTT-Zugangsdaten
  boolean mqttActive = true;
  char *mqttServer = "127.0.0.1";
  int mqttPort = 1883;
  char *mqttName = "ArduinoClient";
  char *mqttUser = "ArudinoNano";
  char *mqttPassword = "DEIN_MQTT_PASSWORT";
  
} Config;

// Einstellungen
int     xBegin     = 0;
int     yBegin     = 10;
const int WRITTEN_SIGNATURE = 0xBEEFDEED;

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


// 1-Wire
const int PinOneWireBus = 10; // Pin, an dem der 1-Wire-Bus angeschlossen ist // = D10
uint8_t DS2438_address[] = { 0x26, 0x2c, 0xc4, 0x2c, 0x00, 0x00, 0x00, 0xf5 }; // Adresse des DS2438 

// Frequenz in Sekunden, in der die WLAN-Verbindung versucht wird
const int wifiCheckInterval = 10;
// Frequenz in Sekunden, in der die Temperaturen abgefragt werden
const int tempCheckInterval = 1;
// Frequenz in Sekunden, in der die Füllstände abgefragt werden
const int levelCheckInterval = 1;
// Frequenz in Sekunden, in der die Temperaturen an MQTT gesendet werden
const int sendInterval = 10;
// Frequenz in Sekunden, in der die Temperaturen angezeigt werden
const int displayInterval = 1;
// Frequenz, in der die orange LED bei Fehlern blinkt in ms
const int blinkInterval = 500; 

  // WLAN
  const int wifiPort = 80; // Port, auf den der HTTP-Server lauscht

// ***************  Globale Variablen
unsigned long tempCheckLast = 0;
unsigned long levelCheckLast = 0;
unsigned long sendLast = 0;
unsigned long displayLast = 0;
unsigned long wifiCheckLast = 0;
unsigned long blinkLast = 0;
boolean blinking = false;

// 1-Wire
SensorData* sensorList = nullptr;  // Zeiger auf das Array von SensorData
int numberOfSensors = 0; // Aktuelle Anzahl von Sensoren
byte addrArray[8];
OneWire oneWire(PinOneWireBus);
DallasTemperature sensors(&oneWire);
DS2438 ds2438(&oneWire, DS2438_address);


// Webserver
IPAddress   ip; 
WiFiServer  server(wifiPort);

// MQTT-Client
WiFiClient   wifiClient;
PubSubClient mqttClient(wifiClient);

// Display
TFT_ILI9163C tft = TFT_ILI9163C(TFT_CS, TFT_DC, TFT_RST);

// Speicher
FlashStorage(configStorage, Config);
Config config;

// *************** Deklaration der Funktionen
boolean sensorsFine();
boolean wifiFine();
boolean checkWiFi();
boolean connectToMQTT();
void loadConfig();
void saveConfig();

// Sensorlisten-Methoden
void addSensor(const SensorAddress address, const SensorName name, const SensorType type, const float value);
void removeSensor(SensorAddress address);
boolean updateSensorValue(const SensorAddress address, const float value);
void clearSensorList();
void getSensorName(const SensorAddress address, SensorName); 
boolean getSensorType(const SensorAddress address, SensorType& type);

// Ein- & Ausgabe-Methoden
void outputSensors();
void getTemperatures();
void getLevels();
void displayValues(); 
String getTemperatureAsHtml();
void sendTemperaturesToMQTT();
void printSensorAddresses();
void printWiFiStatus();

//  Setup-Methoden
void setup1Wire(); 
void setupDisplay();
void setupMemory();
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

void saveConfig() {
  configStorage.write(config);
}


void loadConfig() {
   configStorage.read(config);
}


void outputSensors() {
  Serial.println("Anzahl Sensoren im Array: " + String(numberOfSensors));
  for (int i = 0; i < numberOfSensors; i++) {
    Serial.print("Sensor " + String(i) + " Adresse: ");
    Serial.print(sensorList[i].address);
    Serial.print(" Name: ");
    Serial.print(sensorList[i].name);
    Serial.print(" Typ: ");
    Serial.print(sensorList[i].type);
    Serial.print(" Wert: ");
    Serial.println(sensorList[i].value);
  }
}

void addSensor(const SensorAddress address, const SensorName name, const SensorType type, const float value) {
  Serial.println("addSensor(): begin");

  SensorData* tempArray = (SensorData*)malloc((numberOfSensors + 1) * sizeof(SensorData));

  // Übertrage vorhandene Daten in das temporäre Array
  for (int i = 0; i < numberOfSensors; i++) {
    tempArray[i] = sensorList[i];
  }

  // Füge das neue Sensorobjekt hinzu
  //tempArray[numberOfSensors] = {*address, *name, *type, value};
  strcpy(tempArray[numberOfSensors].address, address);
  strcpy(tempArray[numberOfSensors].name, name);
  tempArray[numberOfSensors].type = type;
  tempArray[numberOfSensors].value = value;

  // Erhöhe die Anzahl der Sensoren
  numberOfSensors++;

  // Befreie den alten Speicher
  free(sensorList);

  // Weise den neuen Speicher zu
  sensorList = tempArray;

  Serial.println("addSensor(): end");
}

boolean updateSensorValue(const SensorAddress address, const float value) {
   for (int i = 0; i < numberOfSensors; i++) {
     if (strcmp(sensorList[i].address, address) == 0) {
       sensorList[i].value = value;
       return true; 
     }
   }
   return false;
}

boolean getSensorType(const SensorAddress address, SensorType& type) {
   for (int i = 0; i < numberOfSensors; i++) {
     if (strcmp(sensorList[i].address, address) == 0) {
       type = sensorList[i].type;
       return true; 
     }
   }
   return false;
}

void getSensorName(const SensorAddress address, SensorName name) {
  // TODO: Dummy Funktion
  strcpy(name, address);
  strcat(name, ".");
}

void removeSensor(SensorAddress address) {
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
  tft.setCursor(xBegin, yBegin);
  tft.println("Start");
}

boolean wifiFine() {
  return WiFi.status() == WL_CONNECTED;
}

boolean sensorsFine() {
 return sensors.getDeviceCount() > 0;
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

void getLevels() {
String address;
SensorAddress addressC;

  // Brich ab, wenn unser Inverall noch nicht erreicht ist
  if (millis() < levelCheckLast + (levelCheckInterval * 1000)) {
    return;
  }
  Serial.println("getLevels() begin");
  levelCheckLast = millis();

  ds2438.update();
  if (ds2438.isError()) {
      Serial.println("Error reading from DS2438 device");
  } else {
      Serial.print("Temperature = ");
      Serial.print(ds2438.getTemperature(), 1);
      Serial.print("C, Channel A = ");
      Serial.print(ds2438.getVoltage(DS2438_CHA), 1); // Pin 1
      Serial.print("v, Channel B = ");
      Serial.print(ds2438.getVoltage(DS2438_CHB), 1);
      Serial.println("v.");
      address = deviceAddrToStr(DS2438_address);
      strcpy (addressC, address.c_str());
      updateSensorValue(addressC, ds2438.getVoltage(DS2438_CHA));
  }

  Serial.println("getLevels() end");
}

void getTemperatures() {
float value;
String address;
SensorAddress addressC;
DeviceAddress addr;
SensorType type;

  // Brich ab, wenn unser Inverall noch nicht erreicht ist
  if (millis() < tempCheckLast + (tempCheckInterval * 1000)) {
    return;
  }
  Serial.println("getTemperatures() begin");
  tempCheckLast = millis();

  // Aktualisiere die Temperaturdaten
  Serial.println("getTemperatures() Aktualisiere Temperaturen");
  sensors.requestTemperatures();

  // Iteriere durch alle Sensoren
  for (int i = 0; i < sensors.getDeviceCount(); i++) {
    // Ermittle die Adresse
    sensors.getAddress(addr, i); 
    address = deviceAddrToStr(addr);
    strcpy (addressC, address.c_str());

    // Ermittle die Temperatur
    value = sensors.getTempCByIndex(i);

    // Prüfe, ob der Typ (="t") passt
    if (getSensorType(addressC, type)) {
      if (type == 't') {
        // Aktualisiere die Liste
        updateSensorValue(addressC, value);
      }
    }
  }  

  Serial.println("getTemperatures() end");
}

void setup1Wire() {
float value;
String address;
SensorAddress addressC;
SensorName nameC;
SensorType typeC;
DeviceAddress addr;

    // Initialisiere die OneWire- und DallasTemperature-Bibliotheken
  if (oneWire.search(addrArray)) {
  } else {
    tft.println("Keine Geräte gefunden");
    Serial.println("Keine Geräte gefunden");
  }


  // Starte Objekt für Temperatur-Sensoren
  sensors.begin();

  // Starte Objekt für DS2438
  ds2438.begin();

  // Leere die Liste
  clearSensorList();

  Serial.println("Gefundene 1-Wire-Sensoren:");
  tft.println("Gefundene 1-Wire-Sensoren:");
  printSensorAddresses();

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

    // Ermittle den Typ
    Serial.println("Ermittle Typ Sensor " + String(i));
    if (getSensorTypeByAddress(addressC, typeC) == true) {
      Serial.println("Typ erfolgreich ermittelt");
    } else {
      Serial.println("Typ nicht erfolgreich ermittelt");
    }
    Serial.print("Typ Sensor " + String(i) + ": ");
    Serial.println(typeC);

    addSensor(addressC, nameC, typeC, value);
  }  
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
  while (!SERIAL_PORT_MONITOR) { }
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

  outputSensors();

  // Debug
  strcpy(sensorList[0].name, "Temp");
  strcpy(sensorList[1].name, "Fuellstand");

  tft.clearScreen();
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
  if (!config.wifiActive) {
    return true;
  }
  // Ermittle den Zeitpunkt, bis zu dem der Verbindungsversuch dauern darf
  unsigned int deadline = millis() + (config.wifiTimeout * 1000);

  // Brich ab, wenn unser Inverall noch nicht erreicht ist
  if (millis() < wifiCheckLast + (wifiCheckInterval * 1000)) {
    return false;
  }
  wifiCheckLast = millis();
 
  // Wenn WLAN nicht verbunden ist,
  if (!wifiFine()) {
    if (config.mode == 'c') {
      // versuch, die Verbindung aufzubauen
      Serial.println("Verbinde mit WLAN...");
      while (WiFi.begin(config.ssid, config.pass) != WL_CONNECTED) {
        delay(1000);
        Serial.println("Verbindung wird hergestellt...");
        if (millis() > deadline) {
          break;
        }
      }
    } else {
      // Versuch, den AP zu starten
      Serial.println("Starte WLAN AccessPoint...");
      while (WiFi.beginAP(config.ssid, config.pass) != WL_CONNECTED) {
        delay(1000);
        Serial.println("AccessPoint wird gestartet...");
        if (millis() > deadline) {
          break;
        }
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

void setupMemory() {

}

boolean connectToMQTT() {
  // Prüfe, ob MQTT überhaupt aktivier tist
  if (!config.mqttActive) {
    return true;
  }

  if (!wifiFine()) {
    Serial.println("connectToMQTT(): Keine WLAN-Verbindung, breche Verbindung mit MQTT-Server ab");
    return false;
  }
  Serial.println("connectToMQTT(): Verbinde mit MQTT-Server...");
  mqttClient.setServer(config.mqttServer, config.mqttPort);
  while (!mqttClient.connected()) {
    if (mqttClient.connect(config.mqttName, config.mqttUser, config.mqttPassword)) {
      Serial.println("connectToMQTT(): Verbunden mit MQTT-Server");
      return true;
    } else {
      Serial.print("connectToMQTT(): Verbindung mit MQTT-Server fehlgeschlagen, rc=");
      Serial.println(mqttClient.state());
    }
  }
}

void displayValues() {
  SensorName name;

  // Brich ab, wenn unser Inverall noch nicht erreicht ist
  if (millis() < displayLast + (displayInterval * 1000)) {
    return;
  }
  displayLast = millis();

  tft.fillRect(xBegin, yBegin, 128, 20, 0x0000);
  tft.setCursor(xBegin, yBegin);

  // Fehlermeldung, wenn keine Sensoren gefunden wurden
  if (numberOfSensors <= 0) {
    Serial.println("displayTemperatures(): Keine Sensoren gefunden, deren Daten angezeigt werden könnten"); 
  }

  // Iteriere durch alle Sensoren
  for (int i = 0; i < numberOfSensors; i++) {
    // und wenn ein Name gesetzt ist,
    if (!sensorList[i].name[0] == '\0') {
      // Nimm den
      strcpy(name, sensorList[i].name);
    } else {
      // Sonst die Adresse
      strcpy(name, sensorList[i].address);
    }
    tft.println(String(name) + ": " + sensorList[i].value);
    Serial.print("> ");
    Serial.print(name);
    Serial.print(": ");
    Serial.println(sensorList[i].value);
  }
}



void sendTemperaturesToMQTT() {
  char topic[30] = "n/a";
  char payload[10];

  // Prüfe, ob WiFi überhaupt aktivier tist
  if (!config.mqttActive) {
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
  getLevels();

  displayValues(); 

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
