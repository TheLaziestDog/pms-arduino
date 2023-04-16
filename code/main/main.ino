//WITHOUT BUFFERS	

/*
  @owner TEAM NATECH
  @license GNU GENERAL PUBLIC LICENSE
  @github TheLaziestDog/pms-iot

  This is the prototyping codebase for the NATECH PMS-IOT Arduino Project
  *THIS CODEBASE IS ONLY PERMITTED TO BE USED FOR PROTOTYPING PURPOSES*
*/

/*
  PMS-IOT Features : 
    Monitoring
    - Monitor plant & incubator
    - Shows the sensors data on a LCD display

    Control
    - Controls incubator temperature and humidity.
    - Regulates how much sunlight the plant will get.
    - Protects the plant from potential threats such as insects, pests, and diseases.

    Automation
    - Provide the plant with sufficient and appropriate nutrition at the right time based on the time and NPK sensor data.
    - Water the plant automatically based on the time, soil moisture, temperature, and humidity data that the sensor gather..

    IoT
    - Sends data sensors to multiple IoT platforms such as a custom spreadsheet database and Blynk IoT.
    - Regularly collect all of the (conditions) data automatically and systematically to provide the users with ready to use (matured) data for further analysis of the plant condition.
    - Can integrate with any existing system management.
*/

/*
  TODO :
  Make the solution of connecting the arduino uno to blynk and google sheets using a seperate esp8266 esp-01 microcontroller
*/

/*
  *IMPORTANT*
  All of the operations that relies on millis() are not 100% will have the same expected numbers of millisecond as in real life, as this sketch had some functions required delay() in order to work
  So, the trade-off is the number of milliseconds in millis() will have an accuracy tolerance of ~30ms
*/

// Importing Libraries

#include <SoftwareSerial.h>
#include <Wire.h> // I2C (Wire) Communication Library
#include <LiquidCrystal_I2C.h> // LCD I2C Library
#include "DHT.h" // DHT Sensor Library
#include "uRTCLib.h" // RTC Library
#include <SPI.h>

// DFRobot Libraries
#include "DFRobot_OxygenSensor.h" // O2 Sensor Library

// Variables

// LCD
LiquidCrystal_I2C lcd(0x27, 20, 4);
const unsigned long updateDisplayInterval = 1000;

int lcdSwitchTimer = 0;
int timerIncrement = 15;

// RTC
uRTCLib rtcData(0x68); // The fixed i2c address of rtc is 0x68

// RELAY
const byte solenoid = 8;
const byte fan = 9;
const byte ledStrip = 10;

// CAPACITIVE SOIL MOISTURE
const byte soilMoisturePin = A1;
const int wetRange = 2;   //needs more calibartion
const int dryRange = 19;  //needs more calibartion

// SOIL NPK SENSOR
// RE and DE Pins set the RS485 module
// to Receiver or Transmitter mode
#define RE 8
#define DE 7

// Modbus RTU requests for reading NPK values
const byte nitro[] = {0x01,0x03, 0x00, 0x1e, 0x00, 0x01, 0xe4, 0x0c};
const byte phos[] = {0x01,0x03, 0x00, 0x1f, 0x00, 0x01, 0xb5, 0xcc};
const byte pota[] = {0x01,0x03, 0x00, 0x20, 0x00, 0x01, 0x85, 0xc0};

byte values[11]; // to store NPK values

// Sets up a new SoftwareSerial object
SoftwareSerial mod(2, 3); // 2 = RO pin, 3 = DI pin

const unsigned long npkSensorInterval = 250;
byte npkState = 0;

// O2 Sensor 
#define COLLECT_NUMBER    10             
#define Oxygen_IICAddress ADDRESS_3
/*   I2C slave Address, The default is ADDRESS_3.
       ADDRESS_0               0x70      // iic device address.
       ADDRESS_1               0x71
       ADDRESS_2               0x72
       ADDRESS_3               0x73
*/
DFRobot_OxygenSensor Oxygen;

// Co2 Sensor
const byte co2SensorPin = A3;

// Other Sensors
const byte dhtPin = 5;
const byte ldrPin = A2;
DHT dht(dhtPin, DHT11);

// Millis
unsigned long previousTime_0 = 0;
unsigned long previousTime_1 = 0;

void setup(){
  Serial.begin(115200);
  mod.begin(115200);

  // Relay
  pinMode(solenoid, OUTPUT);
  pinMode(fan, OUTPUT);
  pinMode(ledStrip, OUTPUT);

  // SOIL NPK SENSOR
  pinMode(RE, OUTPUT);
  pinMode(DE, OUTPUT);

  // Lcd
  lcd.init();
  lcd.backlight();

  // Rtc
  URTCLIB_WIRE.begin();
  rtcData.set(0, 56, 12, 1, 2, 4, 23); // 0:12:56 PM 2/4/23
  // index starts from 1 not 0
  // rtcData.set(second, minute, hour, dayOfWeek, dayOfMonth, month, year)
  // set day of week (1=Sunday, 7=Saturday)

  // Dht
  dht.begin();

  // Wait for everything to start on, the lcd, and especially for the dht11 because it's a really slow sensor
  delay(3000); 
}

void loop(){
  // Millis
  unsigned long currentTime = millis();
  
  // Dht
  float temperatureData = dht.readTemperature();
  float humidityData = dht.readHumidity();

  // O2 Sensor
  float oxygenData = Oxygen.getOxygenData(COLLECT_NUMBER);

  // CO2 Sensor
  int co2SVal = analogRead(co2SensorPin);
  float co2SVoltage = co2SVal*(5000/1024.0);
  float co2Data = getCo2Data(co2SVoltage);

  // Soil Moisture Sensor
  float soilMoistureValue = analogRead(soilMoisturePin);
  float soilMoisturePercent = map(soilMoistureValue, wetRange, dryRange, 0, 100);

  // Soil NPK Sensor
  byte nitroVal,phosVal,potaVal;
  if(currentTime - previousTime_1 >= npkSensorInterval){
    switch (npkState) {
    case 0:
      nitroVal = nitrogen();
      npkState++;
      break;
    case 1:
      phosVal = phosphorous();
      npkState++;
      break;
    case 2:
      potaVal = potassium();
      npkState = 0;
      break;
    }
    previousTime_1 = currentTime;
  }
  char npkRatBuffer[8];
  sprintf(npkRatBuffer, "%d-%d-%d", nitroVal, phosVal, potaVal);

  // Ldr
  int brightnessData = analogRead(ldrPin);

  // Rtc
  rtcData.refresh();
  char dateTimeBuffer[20];
  sprintf(dateTimeBuffer, "%d/%d/%d %d:%d:%d", rtcData.year(), rtcData.month(), rtcData.day(), rtcData.hour(), rtcData.minute(), rtcData.second());

  // Error checking 
  if(isnan(temperatureData) || isnan(humidityData)){
    Serial.println(F("Fail to read data from DHT11 sensor"));
  }
  if(isnan(oxygenData)){
    Serial.println(F("Fail to read data from O2 sensor"));
  }
  if(isnan(co2Data)){
    Serial.println(F("Fail to read data from Co2 sensor"));
  }
  if(isnan(soilMoisturePercent)){
    Serial.println(F("Fail to read data from Soil Moisture sensor"));
  }

  // LCD

  lcd.setCursor(0, 2);
  lcd.print(dateTimeBuffer);

  lcd.setCursor(0, 3);
  lcd.print(F("DS.Flava ---- 80mdpl"));

  // Displaying Sensors Data
  if(lcdSwitchTimer < timerIncrement){
    lcd.setCursor(0, 0);
    lcd.print(F("Temp Humd Brightness"));

    lcd.setCursor(0, 1);
    lcd.print(temperatureData, 1);

    lcd.setCursor(5, 1);
    lcd.print(humidityData, 1);
    
    lcd.setCursor(10, 1);
    lcd.print(brightnessData);
  } 
  else if(lcdSwitchTimer >= timerIncrement){
    lcd.setCursor(0, 0);
    lcd.print(F("   Oxygen     Co2   "));

    lcd.setCursor(3, 1);
    lcd.print(oxygenData, 2);
    lcd.print(F("%"));
    
    lcd.setCursor(14, 1);
    lcd.print(co2Data, 2);
    lcd.print(F("%"));  
  } 
  else if(lcdSwitchTimer >= timerIncrement*2){
    lcd.setCursor(0, 0);
    lcd.print(F("Soil Moist NPK Ratio"));
    
    lcd.setCursor(0, 1);
    lcd.print(soilMoisturePercent, 2);
    lcd.print(F("%"));
    
    lcd.setCursor(11, 1);
    lcd.print(npkRatBuffer);
  } 
  else if(lcdSwitchTimer >= timerIncrement*3){
    lcdSwitchTimer = 0;
  }
  
  if(currentTime - previousTime_0 >= updateDisplayInterval){
    clearDisplay();
    lcdSwitchTimer++;
    previousTime_0 = currentTime;
  }
}

float getCo2Data(float voltage){
  float val = 0;
  
  if(voltage == 0)
  {
    Serial.print(F("CO2 Sensor Condition : "));
    Serial.println(F("Fault"));
  }
  else if(voltage < 400)
  {
    Serial.print(F("CO2 Sensor Condition : "));
    Serial.println(F("preheating"));
    val = 0;
  }
  else
  {
    int voltage_diference=voltage-400;
    val = voltage_diference*50.0/16.0;
  }
  return val;
}

void clearDisplay(){
  for(int i = 0; i <= 3; i++){
    lcd.setCursor(0, i);
    lcd.print(F("                    "));
  }
}

byte nitrogen(){
  digitalWrite(DE,HIGH);
  digitalWrite(RE,HIGH);
  delay(10);
  if(mod.write(nitro,sizeof(nitro))==8){
    digitalWrite(DE,LOW);
    digitalWrite(RE,LOW);
    for(byte i=0;i<7;i++){
    //Serial.print(mod.read(),HEX);
    values[i] = mod.read();
    Serial.print(values[i],HEX);
    }
    Serial.println();
  }
  return values[4];
}
 
byte phosphorous(){
  digitalWrite(DE,HIGH);
  digitalWrite(RE,HIGH);
  delay(10);
  if(mod.write(phos,sizeof(phos))==8){
    digitalWrite(DE,LOW);
    digitalWrite(RE,LOW);
    for(byte i=0;i<7;i++){
    //Serial.print(mod.read(),HEX);
    values[i] = mod.read();
    Serial.print(values[i],HEX);
    }
    Serial.println();
  }
  return values[4];
}
 
byte potassium(){
  digitalWrite(DE,HIGH);
  digitalWrite(RE,HIGH);
  delay(10);
  if(mod.write(pota,sizeof(pota))==8){
    digitalWrite(DE,LOW);
    digitalWrite(RE,LOW);
    for(byte i=0;i<7;i++){
    //Serial.print(mod.read(),HEX);
    values[i] = mod.read();
    Serial.print(values[i],HEX);
    }
    Serial.println();
  }
  return values[4];
}
