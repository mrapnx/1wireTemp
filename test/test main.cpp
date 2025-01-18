#include <Arduino.h>
#include <unity.h>
#include <avr/dtostrf.h>
#include <Wire.h>
#include <WiFiNINA.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <PubSubClient.h>
#include <SPI.h>
#include <FlashStorage_SAMD.h>
#include <TFT_ILI9163C.h> // Achtung! In der TFT_IL9163C_settings.h muss >> #define __MRA_PCB__ << aktiv sein!. Offenbar ist mein Board nicht von dem Bug betroffen, von dem andere rote Boards betroffen sind. Siehe Readme der TFT_IL9163 Lib.
#include <DS2438.h>
#include "sensors.h"

void setUp(void) {
    // set stuff up here

}

void tearDown(void) {
    // clean stuff up here

}

void test_parseValuePairs(void) {
    Bonds pairs;
    pairs.count = 0;
    parseValuePairs("111.111=111.222;222.222=222.111;333.333=333.111", pairs);
    
    TEST_ASSERT_EQUAL_FLOAT(111.111, pairs.bond[0].sensorValue);
    TEST_ASSERT_EQUAL_FLOAT(111.222, pairs.bond[0].displayValue);
}


void test_dummy(void) {
    TEST_ASSERT_EQUAL(0,0);
} 

void setup()
{
    delay(2000); // service delay
    UNITY_BEGIN();

    RUN_TEST(test_dummy);

    UNITY_END(); // stop unit testing
}

void loop()
{
}