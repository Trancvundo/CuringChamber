
//Libraries
#include <DHT.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#define UINT32_MAX uint32_t(-1)
LiquidCrystal_I2C lcd(0x27, 16, 2);

/*
 If you change pins, change in constants
 
// 8  - DHT22 (input pin)
// 9  - Refrigerator
// 10 - Heater
// 11 - Humidifier
// 12 - Dehumidifier
 */

//Select Tempature and Humidity
int Chamber = 1; //Chamber number

float Temp_Target = 14.0;
float Temp_LT = 1.0;       // Low Tolerance (-)
float Temp_HT = 1.5;       // High Tolerance (+)

float Humi_Target = 60.0;
float Humi_LT = 5.0;       // Low Tolerance (-)
float Humi_HT = 7.5;       // High Tolerance (+)

uint32_t MAX_SWITCH_RATE = 30; // Seconds (five minutes)




//Constants
#define DHTPIN 8     // what pin we're connected to
#define DHTTYPE DHT22   // DHT 22  (AM2302)
DHT dht(DHTPIN, DHTTYPE); //// Initialize DHT sensor for normal 16mhz Arduino
int r_Frid = 9;
int r_Heat = 10;
int r_Humi = 11;
int r_Fan = 12;


//Variables
int chk;
float hum;  //Stores humidity value
float temp; //Stores temperature value

uint32_t LastStateChangeTime;
uint32_t LastCheckTime;
unsigned long NumWrapArounds; // How many times has the clock wrapped
unsigned long RuntimeInSeconds;

enum State {
  NOT_C,
  C_UP,
  C_DOWN
};


//Dict van controlledvalue, zorgt dat maar een void hoeft te maken 

struct ControlledValue {
  float Current;
  float Target;
  float Low_Tolerance;
  float High_Tolerance;
  int PinToIncrease;
  int PinToDecrease;
  int PinHigh; //switch 0-1 when relay is inverted
  int PinLow;
  uint32_t LastStateChangeTime; // In **seconds**, not milliseconds!
  State state;
};

ControlledValue Temperature = {0, Temp_Target, Temp_LT, Temp_HT, r_Heat, r_Frid, 0,1,0, NOT_C };
ControlledValue Humidity = {0, Humi_Target, Humi_LT,Humi_HT, r_Humi, r_Fan,1,0, 0, NOT_C };


unsigned long LastAverageUpdateSeconds;
unsigned long HourOfLastAverageUpdate;
unsigned long DayOfLastAverageUpdate;
float TempsLastHour[60];
float TempsLastDay[24];
float TempsLastMonth[30];
float HumiditiesLastHour[60];
float HumiditiesLastDay[24];
float HumiditiesLastMonth[30];
float AverageTempLastHour;
float AverageTempLastDay;
float AverageTempLastMonth;
float AverageHumidityLastHour;
float AverageHumidityLastDay;
float AverageHumidityLastMonth;
unsigned int LCDLine;

// waaarom moet dit?
void updateState (ControlledValue &variable);
void updateAverages (float temp, float hum);
void getCurrentRuntime (int &days, int &hours, int &minutes, int &seconds);
void writeDataToLCD (int column, int row, float temp, float hum);


void setup()
{
  Serial.begin(115200);
  dht.begin();
  NumWrapArounds = 0;
  RuntimeInSeconds = 0;
  LastCheckTime = 0;
  
  // This is needed when relay is inverted
  pinMode(r_Frid,OUTPUT);
  pinMode(r_Heat,OUTPUT);
  pinMode(r_Humi,OUTPUT);
  pinMode(r_Fan,OUTPUT);  
  digitalWrite(r_Frid, Temperature.PinLow);
  digitalWrite(r_Heat, Temperature.PinLow);
  digitalWrite(r_Humi, Humidity.PinLow);
  digitalWrite(r_Fan, Humidity.PinLow);

  
    for (int i = 0; i < 60; ++i) {
      TempsLastHour[i] = 0;
      HumiditiesLastHour[i] = 0;
    }
    for (int i = 0; i < 24; ++i) {
      TempsLastDay[i] = 0;
      HumiditiesLastDay[i] = 0;
    }
    for (int i = 0; i < 30; ++i) {
      TempsLastMonth[i] = 0;
      HumiditiesLastMonth[i] = 0;
    }
  LastAverageUpdateSeconds = 0;
  HourOfLastAverageUpdate = 10000; // Make sure it hits the first time by initializing to nonsense
  DayOfLastAverageUpdate = 10000; // Make sure it hits the first time by initializing to nonsense
  
  
  AverageTempLastHour = 0;
  AverageTempLastDay = 0;
  AverageTempLastMonth = 0;
  AverageHumidityLastHour = 0;
  AverageHumidityLastDay = 0;
  AverageHumidityLastMonth = 0;
  AverageCoolingOvershoot = 0.0;
  LCDLine = 0;
  lcd.init();
  lcd.backlight();
}

void loop()
{
  uint32_t currentTime = millis();
  if (currentTime < LastCheckTime) {
    // Happens once every 50 days or so
    NumWrapArounds++;
  } 
    LastCheckTime = currentTime;
    RuntimeInSeconds = NumWrapArounds * (UINT32_MAX/1000) + (currentTime/1000);
    const uint32_t pollRate = 15000; // Defaults to every fifteen seconds in the normal case
  
    //Read data and store it to variables hum and temp
    hum = dht.readHumidity();
    temp= dht.readTemperature();
    
    Temperature.Current = temp;
    Humidity.Current = hum;
    
    updateState (Temperature);
    updateState (Humidity);
    updateAverages (Temperature.Current, Humidity.Current);  

    lcd.clear();

    // LCD Display (16x2)
    //   0123456789012345
    // 0 Now:  15.0°C 75%    <- Current data, always displayed
    // 1 Hour: 15.0°C 75%    <- Alt 1, daily average
    // 1 Day:  15.0°C 75%    <- Alt 2, weekly average
    // 1 Month:15.0°C 75%    <- Alt 3, monthly average
    
    lcd.setCursor(0,0);
    lcd.print ("Now:");
    writeDataToLCD (4, 0, Temperature.Current, Humidity.Current);
    
    lcd.setCursor(0,1);
    switch (LCDLine) {
      case 0:
        lcd.print("Time:");
        lcd.setCursor(5,1);
        lcd.print(RuntimeInSeconds/3600);
        lcd.setCursor(15,1);
        lcd.print("h");
        LCDLine++;
        break;
      case 1:
        lcd.print("Tar:");
        writeDataToLCD (4, 1, Temp_Target, Humi_Target);        
        if (RuntimeInSeconds >= 3600) {
          LCDLine++;
        } else {
          LCDLine = 0;
        }
        break;
      case 2:
        lcd.print("Hrs:");
        writeDataToLCD (4, 1, AverageTempLastHour, AverageHumidityLastHour);
        if (RuntimeInSeconds >= 86400) {
          LCDLine++;
        } else {
          LCDLine = 0;
        }
        break;
      case 3:
        lcd.print("Day:");
        writeDataToLCD (4, 1, AverageTempLastDay, AverageHumidityLastDay);
        if (RuntimeInSeconds >= 604800) {
          LCDLine++;
        } else {
          LCDLine = 0;
        }
        break;
      case 4:
        lcd.print("Mth:");
        writeDataToLCD (4, 1, AverageTempLastMonth, AverageHumidityLastMonth);
        LCDLine = 0;
        break;    
    }
  

    delay(pollRate); //Delay 15 sec.
}
// An utterly naive bang-bang controller with a timeout to prevent too-frequent activations 
void updateState (ControlledValue &variable)
{
  switch (variable.state) {
    case NOT_C:
      if (RuntimeInSeconds > variable.LastStateChangeTime+MAX_SWITCH_RATE) {
        if (variable.Current > variable.Target + variable.High_Tolerance) {
          variable.LastStateChangeTime = RuntimeInSeconds;
          variable.state = C_DOWN;
          digitalWrite (variable.PinToDecrease, variable.PinHigh);
        } else if (variable.Current < variable.Target - variable.Low_Tolerance) {
          variable.LastStateChangeTime = RuntimeInSeconds;
          variable.state = C_UP;
          digitalWrite (variable.PinToIncrease, variable.PinHigh);
        }
      }
      break;
    case C_UP:
      if (variable.Current > variable.Target) {
        variable.LastStateChangeTime = RuntimeInSeconds;
        variable.state = NOT_C;
        digitalWrite (variable.PinToIncrease, variable.PinLow);
      }
      break;
    case C_DOWN:
      if (variable.Current < variable.Target) {
        variable.LastStateChangeTime = RuntimeInSeconds;
        variable.state = NOT_C;
        digitalWrite (variable.PinToDecrease, variable.PinLow);
      }
      break;
  }
}

void getCurrentRuntime (unsigned long &days, unsigned long &hours, unsigned long &minutes, unsigned long &seconds)
{
  const unsigned long secondsPerMinute = 60;
  const unsigned long secondsPerHour = 3600;
  const unsigned long secondsPerDay = 86400;
  
  unsigned long computationSeconds = RuntimeInSeconds;
  days = computationSeconds / secondsPerDay;
  computationSeconds = computationSeconds % secondsPerDay;
  hours = computationSeconds / secondsPerHour;
  computationSeconds = computationSeconds % secondsPerHour;
  minutes = computationSeconds / secondsPerMinute;
  computationSeconds = computationSeconds % secondsPerMinute;
  seconds = computationSeconds;
}


void updateAverages (float temp, float hum)
{
  unsigned long days, hours, minutes, seconds;
  getCurrentRuntime (days, hours, minutes, seconds);
  if (RuntimeInSeconds - LastAverageUpdateSeconds  >= 60) {
    
    // We just assume it's been about a minute (our usual polling rate)
    LastAverageUpdateSeconds = RuntimeInSeconds; 
    
    // We are just going to do the easiest (and slowest) method here, shifting ALL the data, instead
    // of keeping a pointer to the oldest.
    float humidityIntegrator = 0.0;
    float tempIntegrator = 0.0;
    unsigned int count = 1;
    for (int i = 0; i < 59 /*The last slot gets the current data*/; ++i) {
      if (TempsLastHour[i+1] > 0.0) { // Has it been set yet?
        count++;
        TempsLastHour[i] = TempsLastHour[i+1];
        tempIntegrator += TempsLastHour[i];
        HumiditiesLastHour[i] = HumiditiesLastHour[i+1];
        humidityIntegrator += HumiditiesLastHour[i];
      }
    }
    TempsLastHour[59] = temp;
    tempIntegrator += TempsLastHour[59];
    HumiditiesLastHour[59] = hum;
    humidityIntegrator += HumiditiesLastHour[59];
    
    AverageTempLastHour = tempIntegrator / count;
    AverageHumidityLastHour = humidityIntegrator / count;
    
    if (minutes == 0 && HourOfLastAverageUpdate != hours) {
      HourOfLastAverageUpdate = hours; // Make sure it only happens once per hour
    
      tempIntegrator = 0;
      humidityIntegrator = 0;  
      // Do the daily average the first minute of every hour:
      count = 1;
      for (int i = 0; i < 23; ++i) {
        if (TempsLastDay[i+1] > 0.0) { // Has it been set yet?
          count++;
          TempsLastDay[i] = TempsLastDay[i+1];
          tempIntegrator += TempsLastDay[i];
          HumiditiesLastDay[i] = HumiditiesLastDay[i+1];  
          humidityIntegrator += HumiditiesLastDay[i];
        }
      }
      TempsLastDay[23] = AverageTempLastHour;
      tempIntegrator += TempsLastDay[23];
      HumiditiesLastDay[23] = AverageHumidityLastHour;
      humidityIntegrator += HumiditiesLastDay[23];
        
      AverageTempLastDay = tempIntegrator / count;
      AverageHumidityLastDay = humidityIntegrator / count;
        
      if (hours == 0 && DayOfLastAverageUpdate != days) {
        DayOfLastAverageUpdate = days; // Make sure it only happens once per day
        tempIntegrator = 0;
        humidityIntegrator = 0;
        count = 1;
        for (int i = 0; i < 29; ++i) {
          if (TempsLastMonth[i+1] > 0.0) {
            count++;
            TempsLastMonth[i] = TempsLastMonth[i+1];
            tempIntegrator += TempsLastMonth[i];
            HumiditiesLastMonth[i] = HumiditiesLastMonth[i+1];  
            humidityIntegrator += HumiditiesLastMonth[i];
          }
        }
        TempsLastMonth[29] = AverageTempLastDay;
        tempIntegrator += TempsLastMonth[29];
        HumiditiesLastMonth[29] = AverageHumidityLastDay;
        humidityIntegrator += HumiditiesLastMonth[29];
            
        AverageTempLastMonth = tempIntegrator / count;
        AverageHumidityLastMonth = humidityIntegrator / count;   
      }
    }
  }
}

void writeDataToLCD (int column, int row, float temp1, float hum1)
{
  // The format is "##.#°C ##%" and takes 10 columns total
  lcd.setCursor (column,row);
  lcd.print (temp1);
  lcd.setCursor (column+4,row);
  lcd.print ((char)223); // The degree symbol
  lcd.setCursor(column+5,row);
  lcd.print ("C ");
  lcd.setCursor (column+7,row);
  lcd.print (hum1);
  lcd.setCursor(column+11,row);
  lcd.print("%");
}
