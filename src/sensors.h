#include <Arduino.h>
#include <DallasTemperature.h>

typedef char  SensorAddress    [17];
typedef char  SensorType;
typedef char  SensorName       [21];
typedef char  SensorValueFormat[11];
typedef int   SensorValuePrecision;
typedef float SensorValueMin;
typedef float SensorValueMax;
typedef float SensorValueFormatMin;
typedef float SensorValueFormatMax;

struct SensorData {
  SensorAddress         address    = "";    // Adresse des Sensors userfriendly
  DeviceAddress         deviceAddress;      // Adresse des Sensors als HEX
  SensorType            type       = '.';   // Typ, derzeit werden nur t, b und u unterstützt
  SensorName            name       = "";    // Name zur Anzeige
  SensorValueFormat     format     = "%s";  // Format-String zur Darstellung des Wertes
  SensorValueFormatMin  formatMin  = -1;    // Minimum des Anzeige-Wertes
  SensorValueFormatMax  formatMax  = -1;    // Maximum des Anzeige-Wertes
  SensorValuePrecision  precision  = 0;     // Dezimalstellen des Wertes
  SensorValueMin        min        = -1;    // Minimum des Messwertes 
  SensorValueMax        max        = -1;    // Minimum des Messwertes
  float                 value;
};

/* 
    Die Logik zur Anzeige ist wie folgt: 

    * Direkt Anzeige
    Der Wert des Sensors wird ermittelt und steht in => value (z.B.  1,5)
    Der Wert kann nun direkt ausgegeben werden (Standard).
    Beispiel
    format ="%s C" / precision = 1  (Implizit: formatMin = -1 / formatMax = -1 / min = -1 / max = -1)
    Bei einem Messwert von 1,5 wird 1,5 C angezeigt.

    * Prozent-Anzeige
    Der Wert kann aber auch in ein Verhältnis gesetzt werden, wenn sich z.B. der Messbereich von 0 bis 3 erstreckt, entspräche 1,5 50%.
    Beispiel:
    Wir messen 0 bis 3 und zeigen das als prozentualen Level an.
    min = 0 / max = 3 / format = "%s %%"   (Implizit: formatMin = -1 / formatMax = -1 / precision = 0)
    Bei value = 1,5 wird dann "50 %" angezeigt (Das doppelte "%%"" führt zur Darstellung von "%").
    
   * Anteilige Anzeige
   Haben wir nun z.B. einen Wassertank, der 120l fasst, können wir auch vom Prozent-Wert auf den anteiligen Wert umrechnen lassen.
   Beispiel:
   Wir messen 0 bis 3 und wollen das als Füllstand von 0 bis 120 l anzeigen.
   min = 0 / max = 3 / formatMin = 0 / fformatMax = 120 / format = "%s l"   (Implizit: precision = 0)
   Bei einem Messwert von 1,5 wird nun "60 l" angezeigt

*/


struct SensorConfig {
  SensorAddress         address   = "";
  SensorName            name      = "";
  SensorValueFormat     format    = "%s";
  SensorValueFormatMin  formatMin  = -1;
  SensorValueFormatMax  formatMax  = -1;
  SensorValuePrecision  precision = 0;
  SensorValueMin        min       = -1;
  SensorValueMax        max       = -1;
};

const int sensorConfigCount = 10;

// *************** Deklaration der Funktionen
byte convertHexCStringToByte(const char* hexString);
String deviceAddressToStr(DeviceAddress addr);
void deviceAddressToStrNew(const DeviceAddress addr, String out);
const char* deviceAddressToChar(DeviceAddress addr); 
bool strToDeviceAddress(const String &str, DeviceAddress &addr);
bool getSensorTypeByAddress(const SensorAddress manufacturerCode, char& sensorType);
void copyDeviceAddress(const DeviceAddress in, DeviceAddress out);
void sensorValueToDisplay(const float sensorValue, const SensorValueFormat formatString, const SensorValueFormatMin formatMin, const SensorValueFormatMax formatMax, const SensorValuePrecision precision, const SensorValueMin min, const SensorValueMax max, char displayValue[30]);

// ***************  Funktionen
void sensorValueToDisplay(const float sensorValue, const SensorValueFormat formatString, const SensorValueFormatMin formatMin, const SensorValueFormatMax formatMax, const SensorValuePrecision precision, const SensorValueMin min, const SensorValueMax max, char displayValue[30]) {
  char stringBuffer[30] = "";
  float calcedValue = -1;
  Serial.println("sensorValueToDisplay() begin");

  // Wenn min oder max nicht gesetzt sind
  if (min < 0 || max < 0) {
    // Erfolgt keine Umrechnung, sondern die Übernahme des float Wertes
    Serial.println("  Keine Umrechnung, direkte Anzeige");
    calcedValue = sensorValue;
  } else {
    // Plausi-Prüfung
    if (sensorValue >= min && sensorValue <= max && max > min) {
      if (formatMin < 0 || formatMax < 0) {
        Serial.println("  Umrechnung in Prozentwert");
        calcedValue = (sensorValue - min) / (max - min) * 100;
      } else {
        Serial.println("  Umrechnung in anteiligen Wert");
        calcedValue = ((formatMax - formatMin) * (sensorValue - min) / (max - min)) + formatMin;
      }
    } else {
      Serial.println("  Umrechnung nicht möglich");
      calcedValue = sensorValue;
    }
  }
  Serial.print("  dtostrf: calcedValue=");
  Serial.print(calcedValue);
  Serial.print(" minField=0 precision=");
  Serial.print(precision);
  dtostrf(calcedValue, 0, precision, stringBuffer);
  Serial.print("  stringBuffer: ");
  Serial.println(stringBuffer);
  sprintf(displayValue, formatString, stringBuffer);
  Serial.print("  displayValue: ");
  Serial.println(displayValue);
  Serial.println("sensorValueToDisplay() end");
}


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

void copyDeviceAddress(const DeviceAddress in, DeviceAddress out) {
  for (int i = 0; i < 8; i++) {
    out[i] = in[i];
  }
}

bool strToDeviceAddress(const String &str, DeviceAddress &addr) {
  // Überprüfen, ob die Zeichenkette die richtige Länge hat
  if (str.length() != 16) {
    return false; // Fehler, wenn die Länge nicht korrekt ist
  }

  for (uint8_t j = 0; j < 8; j++) {
    // Extrahiere zwei Zeichen von der Zeichenkette
    String hexStr = str.substring(j * 2, j * 2 + 2);
    
    // Konvertiere die extrahierten Zeichen in einen Hexadezimalwert
    addr[j] = strtol(hexStr.c_str(), nullptr, 16);
  }

  return true; // Konvertierung erfolgreich
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


String deviceAddressToStr(DeviceAddress addr) {
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

void deviceAddressToStrNew(const DeviceAddress addr, String out) {
  out = "";
    for (uint8_t j = 0; j < 8; j++) {
      if (addr[j] < 16) out = "0";
      out = out + String(addr[j], HEX);
      Serial.println("deviceAddrToStrNew() Durchlauf " + String(j) + ": out = " + out);
    }
  out.toUpperCase();
  Serial.println("deviceAddrToStrNew() ende, out = " + out);
}

const char* deviceAddressToChar(DeviceAddress addr) {
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
