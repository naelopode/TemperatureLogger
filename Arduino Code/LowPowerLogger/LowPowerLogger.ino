/*
 Low Power Temperature Logger for Feather M0 Adalogger
 Uses internal RTC and interrupts to put M0 into deep sleep, sync with external RTC.
 Uses 5 PT1000 with MAX31865, 1 DS18B20 with 4.7k Pull-Up Resistor, 1 on the DS3231 (inside the box)
 Output logged to uSD at intervals set
 Created in august 2022 for ISTE - UNIL - RISK GROUP
 Based on  cavemoa's code
*/

////////////////////////////////////////////////////////////
//#define ECHO_TO_SERIAL // Uncomment for serial output
////////////////////////////////////////////////////////////

#include <SPI.h>
#include <SD.h>
#include <RTCZero.h> 
#include <DS3231.h>
#include <Wire.h>
#include <Adafruit_MAX31865.h> 
#include <OneWire.h>
#include <DallasTemperature.h>


bool century = false;
bool h12Flag;
bool pmFlag;


#define cardSelect 4  // Set the pin used for uSD

//////////////// Key Settings ///////////////////

#define SampleIntSec 60 // RTC - Sample interval in seconds
#define SamplesPerCycle 10  // Number of samples to buffer before uSD card flush is called


// The value of the Rref resistor. Use optimisation prog to calculate
#define NUM_MAX31865 5    //Number of MAX31865 sensor
#define RREF1      4300.0
#define RREF2      4300.0 
#define RREF3      4300.0 
#define RREF4      4300.0 
#define RREF5      4300.0 
// The 'nominal' 0-degrees-C resistance of the sensor
#define RNOMINAL   1000.0

#define AVERAGE_MEASURES     10 //number of mesure take and averaged to take 1 better mesure

#define ONE_WIRE_BUS A0

// Setup a oneWire instance to communicate with any OneWire devices
OneWire oneWire(ONE_WIRE_BUS);

// Pass our oneWire reference to Dallas Temperature sensor 
DallasTemperature sensors(&oneWire);

//const int SampleIntSeconds = 2000;   //Simple Delay used for testing, ms i.e. 1000 = 1 sec DELETE

Adafruit_MAX31865 thermo5 = Adafruit_MAX31865(10, 11, 12, 13);
Adafruit_MAX31865 thermo3 = Adafruit_MAX31865(9, 11, 12, 13);
Adafruit_MAX31865 thermo4 = Adafruit_MAX31865(6, 11, 12, 13);
Adafruit_MAX31865 thermo1 = Adafruit_MAX31865(5, 11, 12, 13);
Adafruit_MAX31865 thermo2 = Adafruit_MAX31865(1, 11, 12, 13);



/////////////// Global Objects ////////////////////
RTCZero rtc;    // Create RTC object
File logfile;   // Create file object
float measuredvbat;   // Variable for battery voltage
int NextAlarmSec; // Variable to hold next alarm time in seconds
unsigned int CurrentCycleCount;  // Num of smaples in current cycle, before uSD flush call
DS3231 myRTC;   //Create external RTC object

//////////////    Setup   ///////////////////
void setup() {
  Wire.begin();   //Start 1Wire communication
  rtc.begin();    // Start the RTC in 24hr mode
  rtc.setTime(myRTC.getHour(h12Flag, pmFlag), myRTC.getMinute(), myRTC.getSecond());   // Set the time from external RTC
  rtc.setDate(myRTC.getDate(), myRTC.getMonth(century), myRTC.getYear());    // Set the date from external RTC
  WireStartup();  //Set MAX31865 as 3WIRE
  sensors.begin(); //Start for DS18B20
  #ifdef ECHO_TO_SERIAL
    while (! Serial); // Wait until Serial is ready
    Serial.begin(115200);
    Serial.println("\r\nFeather M0 Analog logger");
  #endif  

  // see if the card is present and can be initialized:
  if (!SD.begin(cardSelect)) {
    Serial.println("Card init. failed! or Card not present");
  }
  char filename[15];
  strcpy(filename, "ANALOG00.CSV");
  for (uint8_t i = 0; i < 100; i++) {
    filename[6] = '0' + i/10;
    filename[7] = '0' + i%10;
    // create if does not exist, do not open existing, write, sync after write
    if (! SD.exists(filename)) {
      break;
    }
  }

  logfile = SD.open(filename, FILE_WRITE);
  if( ! logfile ) {
    Serial.print("Couldnt create "); 
    Serial.println(filename);
  }
  Serial.print("Writing to "); 
  Serial.println(filename);

  Serial.println("Logging ....");
  writeHeader();
}

/////////////////////   Loop    //////////////////////
void loop() {

  CurrentCycleCount += 1;     //  Increment samples in current uSD flush cycle

  #ifdef ECHO_TO_SERIAL
    SerialOutput();           // Only logs to serial if ECHO_TO_SERIAL is uncommented at start of code
  #endif
  
  SdOutput();                 // Output to uSD card

  // Code to limit the number of power hungry writes to the uSD
  if( CurrentCycleCount >= SamplesPerCycle ) {
    logfile.flush();
    CurrentCycleCount = 0;
    #ifdef ECHO_TO_SERIAL
      Serial.println("logfile.flush() called");
    #endif
  }

  
  ///////// Interval Timing and Sleep Code ////////////////
  //delay(SampleIntSeconds);   // Simple delay for testing only interval set by const in header

  NextAlarmSec = (NextAlarmSec + SampleIntSec) % 60;   // i.e. 65 becomes 5
  rtc.setAlarmSeconds(NextAlarmSec); // RTC time to wake, currently seconds only
  rtc.enableAlarm(rtc.MATCH_SS); // Match seconds only
  delay(2); // Brief delay prior to sleeping not really sure its required
  
  rtc.standbyMode();    // Sleep until next alarm match
  
  // Code re-starts here after sleep !

}

///////////////   Functions   //////////////////

// Debbugging output of time/date and battery voltage
void SerialOutput() {
  sensors.requestTemperatures(); //request temps for DS18B20
  Serial.print(CurrentCycleCount);
  Serial.print(",");
  Serial.print(rtc.getDay());
  Serial.print("/");
  Serial.print(rtc.getMonth());
  Serial.print("/");
  Serial.print(rtc.getYear()+2000);
  Serial.print(" ");
  Serial.print(rtc.getHours());
  Serial.print(":");
  if(rtc.getMinutes() < 10)
    Serial.print('0');      // Trick to add leading zero for formatting
  Serial.print(rtc.getMinutes());
  Serial.print(":");
  if(rtc.getSeconds() < 10)
    Serial.print('0');      // Trick to add leading zero for formatting
  Serial.print(rtc.getSeconds());
  for (int i(0); i<NUM_MAX31865; ++i){
    Serial.print(",");
    Serial.print(get_temp_moy(i, AVERAGE_MEASURES));
  }
  Serial.print(",");
  Serial.println(myRTC.getTemperature());
  Serial.print(",");
  Serial.println(sensors.getTempCByIndex(0));
}

// Print data and time followed by battery voltage to SD card
void SdOutput() {
  sensors.requestTemperatures(); //request temps for DS18B20
  // Formatting for file output CycleCount, dd/mm/yyyy hh:mm:ss, [sensors output] x7
  logfile.print(CurrentCycleCount);
  logfile.print(",");
  logfile.print(rtc.getDay());
  logfile.print("/");
  logfile.print(rtc.getMonth());
  logfile.print("/");
  logfile.print(rtc.getYear()+2000);
  logfile.print(" ");
  logfile.print(rtc.getHours());
  logfile.print(":");
  if(rtc.getMinutes() < 10)
    logfile.print('0');      // Trick to add leading zero for formatting
  logfile.print(rtc.getMinutes());
  logfile.print(":");
  if(rtc.getSeconds() < 10)
    logfile.print('0');      // Trick to add leading zero for formatting
  logfile.print(rtc.getSeconds());
  for (int i(0); i<NUM_MAX31865; ++i){
    logfile.print(",");
    logfile.print(get_temp_moy(i, AVERAGE_MEASURES));
  }
  logfile.print(",");
  logfile.print(myRTC.getTemperature());
  logfile.print(",");
  logfile.print(sensors.getTempCByIndex(0));
  logfile.println();
}

// Write data header.
void writeHeader() {
  logfile.println("Cycle count,DD:MM:YYYY hh:mm:ss,T_SENSOR1,T_SENSOR2,T_SENSOR3,T_SENSOR4,T_SENSOR5,T_SENSOR_BOX,T_SENSOR_SURFACE");
}

void WireStartup() {
  thermo1.begin(MAX31865_3WIRE);  // set to 2WIRE or 4WIRE as necessary
  thermo2.begin(MAX31865_3WIRE);
  thermo3.begin(MAX31865_3WIRE);
  thermo4.begin(MAX31865_3WIRE);
  thermo5.begin(MAX31865_3WIRE);
}

double get_temp(int sensor) {
  switch (sensor) {
    case 0: return thermo1.temperature(RNOMINAL, RREF1);
    case 1: return thermo2.temperature(RNOMINAL, RREF2);
    case 2: return thermo3.temperature(RNOMINAL, RREF3);
    case 3: return thermo4.temperature(RNOMINAL, RREF4);
    case 4: return thermo5.temperature(RNOMINAL, RREF5); //You can add MAX31865 sensors here
  }
  return 0;
}

double get_temp_moy(int sensor, int nb) {
  double moyenne = 0;
  for (int i(0); i < nb; ++i) {
    moyenne += get_temp(sensor);
  }
  return (moyenne / nb);
}