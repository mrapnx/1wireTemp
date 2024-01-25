#include <Arduino.h>
#include <DallasTemperature.h>

typedef char SensorAddress[17];
typedef char SensorType;
typedef char SensorName[21];
typedef char SensorValueFormat[11];
struct SensorData {
  SensorAddress     address = "";
  SensorType        type    = '.';
  SensorName        name    = "";
  SensorValueFormat format  = "";
  float value;
};

struct SensorConfig {
  SensorAddress     address = "";
  SensorName        name    ="";
  SensorValueFormat format  = "";
};

const int sensorConfigCount = 10;

byte convertHexCStringToByte(const char* hexString);
String deviceAddrToStr(DeviceAddress addr);
void deviceAddrToStrNew(const DeviceAddress addr, String out);
const char* deviceAddrToChar(DeviceAddress addr); 
bool getSensorTypeByAddress(const SensorAddress manufacturerCode, char& sensorType);


byte convertHexCStringToByte(const char* hexString) {
  // Erstelle einen temporären C-String mit den ersten beiden Zeichen des Eingabe-C-Strings
  char tempString[3];
  strncpy(tempString, hexString, 2);
  // Stelle sicher, dass der temporäre C-String mit einem Null-Byte abgeschlossen ist
  tempString[2] = '\0';

  // Verwende strtol, um den hexadezimalen C-String in eine Ganzzahl (long) zu konvertieren
  long value = strtol(tempString, nullptr, 16);
  
  // Konvertiere die Ganzzahl auf einen byte-Wert (0-255)
  byte result = static_cast<byte>(value);

  return result;
}

bool getSensorTypeByAddress(const SensorAddress manufacturerCode, char& sensorType) {
  char code[17];
  byte firstByte;
  // Überprüfe nur das erste Byte des char-Arrays
  strcpy(code, manufacturerCode);
  firstByte = convertHexCStringToByte(code);

  switch (firstByte) {
    case 0x28:
      sensorType = 't'; // DS18B20 Temperatursensor
      return true;
    case 0x10:
      sensorType = 't'; // DS18S20 Temperatursensor
      return true;
    case 0x22:
      sensorType = 't'; // DS1822 Temperatursensor
      return true;
    case 0x26:
      sensorType = 'b'; // DS2438 (Smart Battery Monitor)
      return true;
    default:
      sensorType = 'u';
      return false;
    }
}


String deviceAddrToStr(DeviceAddress addr) {
  String returnString = "";
    for (uint8_t j = 0; j < 8; j++) {
      // if (addr[j] < 16) returnString = "0";  // War im ursprünglichen Vorschlag einer Konvertierung, scheint aber keinen Sinn zu machen, da HEX 00 legitim ist.
      if (addr[j] < 16) {
        returnString = returnString + "00";
      } else {
      returnString = returnString + String(addr[j], HEX);
      }
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
  static SensorAddress result;
  String returnString = "";
    for (uint8_t j = 0; j < 8; j++) {
      if (addr[j] < 16) returnString = "0";
      returnString = returnString + String(addr[j], HEX);
    }
  returnString.toUpperCase();
  strcpy(result, returnString.c_str());
  return result;
}
