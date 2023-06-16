/*
 * ADVANCED example for: how to use up to N SinricPro Switch devices on one ESP module
 *                       to control N relays and N flipSwitches for manually control:
 * - setup N SinricPro switch devices
 * - setup N relays
 * - setup N flipSwitches to control relays manually
 *   (flipSwitch can be a tactile button or a toggle switch and is setup in line #52)
 *
 * - handle request using just one callback to switch relay
 * - handle flipSwitches to switch relays manually and send event to sinricPro server
 *
 * - SinricPro deviceId and PIN configuration for relays and buttins is done in std::map<String, deviceConfig_t> devices
 *
 * If you encounter any issues:
 * - check the readme.md at https://github.com/sinricpro/esp8266-esp32-sdk/blob/master/README.md
 * - ensure all dependent libraries are installed
 *   - see https://github.com/sinricpro/esp8266-esp32-sdk/blob/master/README.md#arduinoide
 *   - see https://github.com/sinricpro/esp8266-esp32-sdk/blob/master/README.md#dependencies
 * - open serial monitor and check whats happening
 * - check full user documentation at https://sinricpro.github.io/esp8266-esp32-sdk
 * - visit https://github.com/sinricpro/esp8266-esp32-sdk/issues and check for existing issues or open a new one
 */

// Uncomment the following line to enable serial debug output
// #define ENABLE_DEBUG

#ifdef ENABLE_DEBUG
#define DEBUG_ESP_PORT Serial
#define NODEBUG_WEBSOCKETS
#define NDEBUG
#endif

#include <Arduino.h>
#ifdef ESP8266
#include <ESP8266WiFi.h>
#endif
#ifdef ESP32
#include <WiFi.h>
#endif

// Google Nest Libraries
#include "SinricPro.h"
#include "SinricProSwitch.h"
#include <esp8266-google-home-notifier.h>

//#define TOUCH_CS 3
//#include <TFT_eSPI.h>

//General libraries
#include <string.h>
#include <time.h> // For alarm
#include <map>
#include <list> // For alarm list 

//Heart rate sensor libraries
#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h"

//OLED display libraries
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

//Temperature and Humidity sensor libraries
#include <Adafruit_Sensor.h>
#include <DHT.h>

//Database libraries
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h" // provide the token generation process info
#include "addons/RTDBHelper.h" // provide the RTDB payload printing info and other helpers functions

//SQl libraries
// #include <MySQL_Connection.h>
// #include <MySQL_Cursor.h>



/*<-------------------------->
  <---- GLOBAL VARIABLES ---->
  <-------------------------->*/

// ----------------------------------- Wifi credentials -----------------------------------------------------
// #define WIFI_SSID "Minkmates"
// #define WIFI_PASS "Minkmaatstraat50"
//#define WIFI_SSID "Definitely Not A Wifi"
//#define WIFI_PASS "jkoy3240"
#define WIFI_SSID "D.E-CAFE-GAST"
#define WIFI_PASS ""
// #define WIFI_SSID "iPhone de Ines"
// #define WIFI_PASS "inesparletropbeaucoup"
// #define WIFI_SSID "NESNOS"
// #define WIFI_PASS "princessenesnos"
// #define WIFI_SSID "AXEL_DESKTOP"
// #define WIFI_PASS "azulejo38!"


//SinricPro credentials
#define APP_KEY "75f4059f-33e0-4266-86f2-e646618364f8"										   
#define APP_SECRET "671516bc-99cf-4b1b-8e4a-4f13ef2353a6-14f8dad9-ec0d-4b67-be39-206bee47554c" 

// comment the following line if you use a toggle switches instead of tactile buttons
#define TACTILE_BUTTON 1

#define BAUD_RATE 115200

#define DEBOUNCE_TIME 250

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels


#define DHTPIN 15         // Pin connected to the DHT11 sensor
#define DHTTYPE DHT11    // DHT sensor type


// WiFiClient client;
// MySQL_Connection conn((Client *)&client);
// IPAddress server_addr(20,74,46,0);  // IP of the MySQL *server* here
// char SQLuser[] = "nesnos";              // MySQL user login username
// char password[] = "Axeltalksalot3!";        // MySQL user login password


#define EMERGENCY_PIN 32

GoogleHomeNotifier ghn;

// TFT_eSPI tft = TFT_eSPI();

bool alarm_set = false;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

DHT dht(DHTPIN, DHTTYPE); // Initialize the DHT sensor

MAX30105 particleSensor;

const byte RATE_SIZE = 4; //Increase this for more averaging. 4 is good.
byte rates[RATE_SIZE]; //Array of heart rates
byte rateSpot = 0;
long lastBeat = 0; //Time at which the last beat occurred
float beatsPerMinute;
int beatAvg;
bool isEmergency = false;
unsigned long emergencyTime = 0;
const unsigned long emergencyLimit =  20000; // 20 seconds

// Firebase project API key 
#define API_KEY "AIzaSyD3DMkvWPlYfkCQbmw0vLnxymwSbmVFccw"

//RTDB URL definition 
#define DATABASE_URL "https://medical-watch-6e163-default-rtdb.europe-west1.firebasedatabase.app/"

//Database objects declaration
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
bool signupOK = false;

//Database credentials
#define Email_database "giaani022@gmail.com"
#define Password_database "Adelinaisperfect!"

typedef struct
{ // struct for the std::map below
	int relayPIN;
	int flipSwitchPIN;
} deviceConfig_t;

// this is the main configuration
// please put in your deviceId, the PIN for Relay and PIN for flipSwitch
// this can be up to N devices...depending on how much pin's available on your device ;)
// right now we have 4 devicesIds going to 4 relays and 4 flip switches to switch the relay manually
std::map<String, deviceConfig_t> devices = {
	//{deviceId, {relayPIN,  flipSwitchPIN}}
	{"645e4cd6929949c1da656545", {EMERGENCY_PIN, 17}}};

typedef struct
{ // struct for the std::map below
	String deviceId;
	bool lastFlipSwitchState;
	unsigned long lastFlipSwitchChange;
} flipSwitchConfig_t;

std::map<int, flipSwitchConfig_t> flipSwitches; // this map is used to map flipSwitch PINs to deviceId and handling debounce and last flipSwitch state checks
												// it will be setup in "setupFlipSwitches" function, using informations from devices map

std::list<String> alarmList;

/*<--------------------------->
  <------ USE FUNCTIONS ------>
  <--------------------------->*/

void GoogleHomeMessage(const char* message){
	if (ghn.notify(message) != true) {
    Serial.println(ghn.getLastError());
    return;
  }
  Serial.println("Google Home Notifier meesage sent sccessfully.");
}
 
void emergency() {
	isEmergency = true;
	//TODO emergency sound (buzzer)
	emergencyTime = millis();
	GoogleHomeMessage("Emergency detected, notifying nurse");
}

bool onPowerState(String deviceId, bool &state)
{
	Serial.printf("%s: %s\r\n", deviceId.c_str(), state ? "on" : "off");
	int relayPIN = devices[deviceId].relayPIN; // get the relay pin for corresponding device
	digitalWrite(relayPIN, !state);			   // set the new relay state

	if (relayPIN == EMERGENCY_PIN && state == true) {
		emergency();
	}
	
	return true;
}


/*<---------------------------->
  <----- SET UP FUNCTIONS ----->
  <---------------------------->*/

void setupWiFi()
{
	Serial.printf("\r\n[Wifi]: Connecting");
	WiFi.begin(WIFI_SSID, WIFI_PASS);
	
	while (WiFi.status() != WL_CONNECTED)
	{
		Serial.printf(".");
		delay(250);
	}
	//digitalWrite(LED_BUILTIN, HIGH);
	Serial.printf("connected!\r\n[WiFi]: IP-Address is %s\r\n", WiFi.localIP().toString().c_str());
}

void setupSinricPro()
{
	for (auto &device : devices)
	{
		const char *deviceId = device.first.c_str();
		SinricProSwitch &mySwitch = SinricPro[deviceId];
		mySwitch.onPowerState(onPowerState);
	}

	SinricPro.begin(APP_KEY, APP_SECRET);
	SinricPro.restoreDeviceStates(true);
}

void setupRelays()
{
	for (auto &device : devices)
	{										   // for each device (relay, flipSwitch combination)
		int relayPIN = device.second.relayPIN; // get the relay pin
		pinMode(relayPIN, OUTPUT);			   // set relay pin to OUTPUT
	}
}

void setupFlipSwitches()
{
	for (auto &device : devices)
	{										 // for each device (relay / flipSwitch combination)
		flipSwitchConfig_t flipSwitchConfig; // create a new flipSwitch configuration

		flipSwitchConfig.deviceId = device.first;	  // set the deviceId
		flipSwitchConfig.lastFlipSwitchChange = 0;	  // set debounce time
		flipSwitchConfig.lastFlipSwitchState = false; // set lastFlipSwitchState to false (LOW)

		int flipSwitchPIN = device.second.flipSwitchPIN; // get the flipSwitchPIN

		flipSwitches[flipSwitchPIN] = flipSwitchConfig; // save the flipSwitch config to flipSwitches map
		pinMode(flipSwitchPIN, INPUT);					// set the flipSwitch pin to INPUT
	}
}

// void setup_tft(){
// 	tft.init();
// 	tft.fillScreen(TFT_RED);
// 	tft.setRotation(1);
// 	tft.setTextWrap(true,true);
// 	Serial.println("TFT initialized");
// }

void setupDisplay(){
	if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3D for 128x64
		Serial.println(F("SSD1306 allocation failed"));
		for(;;);
  	}
	delay(2000);
	display.clearDisplay();

	display.setTextSize(1);
	display.setTextColor(WHITE);
	display.setCursor(0, 0);
	// Display static text
	display.println("I work 2");
	display.display(); 
	delay(100);
}

void setupTime(){
	const char* ntpServer = "pool.ntp.org";
	const long  gmtOffset_sec = 3600;
	const int   daylightOffset_sec = 3600;
	// Init and get the time
	configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
	//printLocalTime();

}
void setupGoogleHomeNotifier() {
  const char deviceName[] = "Office";

  Serial.println("connecting to Google Home...");
  if (ghn.device(deviceName, "en") != true) {
	Serial.println("Google Home connection failed");
    Serial.println(ghn.getLastError());
    return;
  }

  Serial.print("found Google Home(");
  Serial.print(ghn.getIPAddress());
  Serial.print(":");
  Serial.print(ghn.getPort());
  Serial.println(")");

}

void setupHeartbeatSensor(){
	// Initialize sensor
	if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) //Use default I2C port, 400kHz speed
	{
		Serial.println("MAX30105 was not found. Please check wiring/power. ");
		while (1);
	}
	Serial.println("Place your index finger on the sensor with steady pressure.");

	particleSensor.setup(); //Configure sensor with default settings
	particleSensor.setPulseAmplitudeRed(0x0A); //Turn Red LED to low to indicate sensor is running
	particleSensor.setPulseAmplitudeGreen(0); //Turn off Green LED
}

void setup_firebase(){
	 /* Assign the api key (required) */
  config.api_key = API_KEY;

  /* Assign the RTDB URL (required) */
  config.database_url = DATABASE_URL;

  /* Sign up */
  if (Firebase.signUp(&config, &auth, "", "")){
  //if (Firebase.signUp(&config, &auth, Email_database, Password_database)){
    Serial.println("ok");
    signupOK = true;
  }
  else{
    Serial.printf("%s\n", config.signer.signupError.message.c_str());
  }

  /* Assign the callback function for the long running token generation task */
  config.token_status_callback = tokenStatusCallback; //see addons/TokenHelper.h
  
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  Serial.println("Firebase setup done!");
  //delay(10000);
}

// void setupSQL()
// {
// 	// if (!WiFi.hostByName("giaani-mysql.mysql.database.azure.com", server_addr)) {
//   	// 	Serial.println("Failed to resolve server name");
//   	// return;
// 	// }
// 	Serial.println("Connecting to SQL Server...");
// 	if (conn.connect(server_addr, 3306, SQLuser, password)) {
// 		Serial.println("Connected to SQL Server successfully.");
// 	} else {
// 		Serial.println("Connection to SQL Server failed.");
// 	}
// }

/*<-------------------------->
  <----- LOOP FUNCTIONS ----->
  <-------------------------->*/

void handleFlipSwitches()
{
	unsigned long actualMillis = millis(); // get actual millis
	for (auto &flipSwitch : flipSwitches)
	{																				 // for each flipSwitch in flipSwitches map
		unsigned long lastFlipSwitchChange = flipSwitch.second.lastFlipSwitchChange; // get the timestamp when flipSwitch was pressed last time (used to debounce / limit events)

		if (actualMillis - lastFlipSwitchChange > DEBOUNCE_TIME)
		{ // if time is > debounce time...

			int flipSwitchPIN = flipSwitch.first;							  // get the flipSwitch pin from configuration
			bool lastFlipSwitchState = flipSwitch.second.lastFlipSwitchState; // get the lastFlipSwitchState
			bool flipSwitchState = digitalRead(flipSwitchPIN);				  // read the current flipSwitch state
			if (flipSwitchState != lastFlipSwitchState)
			{ // if the flipSwitchState has changed...
#ifdef TACTILE_BUTTON
				if (flipSwitchState)
				{ // if the tactile button is pressed
#endif
					flipSwitch.second.lastFlipSwitchChange = actualMillis; // update lastFlipSwitchChange time
					String deviceId = flipSwitch.second.deviceId;		   // get the deviceId from config
					int relayPIN = devices[deviceId].relayPIN;			   // get the relayPIN from config
					bool newRelayState = !digitalRead(relayPIN);		   // set the new relay State
					digitalWrite(relayPIN, newRelayState);				   // set the trelay to the new state

					SinricProSwitch &mySwitch = SinricPro[deviceId]; // get Switch device from SinricPro
					mySwitch.sendPowerStateEvent(!newRelayState);	 // send the event
#ifdef TACTILE_BUTTON
				}
#endif
				flipSwitch.second.lastFlipSwitchState = flipSwitchState; // update lastFlipSwitchState
			}
		}
	}
}



String printLocalTime() {
	struct tm timeinfo;
	if (!getLocalTime(&timeinfo))
	{
		Serial.println("Failed to obtain time");
		return "Failed to obtain time";
	}
	Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
	String daytime = asctime(&timeinfo);
	//Serial.println("The needed string:" + daytime);
	String day = daytime.substring(0, 11) += daytime.substring(20, 24);
	String time = daytime.substring(11, 19);
	//TODO change TFT to other display type
	// tft.drawString(day, 15, 10, 4);
	// tft.drawString(time, 20, 45, 7);
	Serial.print("Day of week: ");
	Serial.println(&timeinfo, "%A");
	Serial.print("Month: ");
	Serial.println(&timeinfo, "%B");
	Serial.print("Day of Month: ");
	Serial.println(&timeinfo, "%d");
	Serial.print("Year: ");
	Serial.println(&timeinfo, "%Y");
	Serial.print("Hour: ");
	Serial.println(&timeinfo, "%H");
	Serial.print("Hour (12 hour format): ");
	Serial.println(&timeinfo, "%I");
	Serial.print("Minute: ");
	Serial.println(&timeinfo, "%M");
	Serial.print("Second: ");
	Serial.println(&timeinfo, "%S");
	char timeWeekDay[10];
	strftime(timeWeekDay, 10, "%A", &timeinfo);
	Serial.println(timeWeekDay);
	Serial.println();

	// if (!alarm_set)
	// {

	// 	// // Set the alarm with the received time

	// 	// Serial.print("Setting alarm with time: ");
	// 	// Serial.println(alarmTime);
	// 	String alarmTime_c = Serial.readStringUntil('\n');
	// 	alarmTime_c += "\n";
	// 	Serial.println("Wake up alarm set to:" + alarmTime_c);
	// 	if (daytime == alarmTime_c)
	// 	{
	// 		//tft.drawString("WAAAAKEEEE UP", 30, 100, 4);
	// 		Serial.println("WAAAAKEEEE UP");
	// 		alarm_set = true;
	// 	}
	// }
	return daytime;
}

void checkAlarms() {
	struct tm timeinfo;
	if (!getLocalTime(&timeinfo)) {
		Serial.println("Failed to obtain time");
		return;
	}
	String daytime = asctime(&timeinfo);

	if (!alarmList.empty()) {
		for(auto it = alarmList.begin(); alarmList.end() != it;) {
			if (*it == daytime) {
				//TODO: play alarm sound
				//TODO: Display alarm on screen
				//TODO: Send alarm to Google Home with message taken from json file (title of alarm)
				it = alarmList.erase(it);
			} else {
				++it;
			}
		}
	}
}

void getHeartbeat(){
	
	long irValue = particleSensor.getIR();

	if (checkForBeat(irValue) == true){
		//We sensed a beat!
		long delta = millis() - lastBeat;
		lastBeat = millis();

		beatsPerMinute = 60 / (delta / 1000.0);

		if (beatsPerMinute < 255 && beatsPerMinute > 20) {
			rates[rateSpot++] = (byte)beatsPerMinute; //Store this reading in the array
			rateSpot %= RATE_SIZE; //Wrap variable

			//Take average of readings
			beatAvg = 0;
			for (byte x = 0 ; x < RATE_SIZE ; x++)
				beatAvg += rates[x];
			beatAvg /= RATE_SIZE;
		}
  	}

	//TODO add info to json file

	Serial.print("IR=");
	Serial.print(irValue);
	Serial.print(", BPM=");
	Serial.print(beatsPerMinute);
	Serial.print(", Avg BPM=");
	Serial.print(beatAvg);

	if (irValue < 50000)
		Serial.print(" No finger?");

	Serial.println();
}

float getTempHumidity(){
	
	float temperature = dht.readTemperature();    // Read temperature in Celsius
	float humidity = dht.readHumidity();          // Read humidity

	if (isnan(temperature) || isnan(humidity)) {  // Check if any reading failed
		Serial.println("Failed to read from DHT sensor!");
		return 0;
	}

	Serial.print("Temperature: ");
	Serial.print(temperature);
	Serial.print(" °C\t");
	Serial.print("Humidity: ");
	Serial.print(humidity);
	Serial.println(" %");

	return temperature;
}

void sendToFirebase(String date_time, float temperature){
	if (Firebase.ready() && signupOK ){
		Firebase.RTDB.setString(&fbdo, "Clients/client1/name", "Ines");
		Firebase.RTDB.setString(&fbdo, "Clients/client1/lastname", "Barnous");
		Firebase.RTDB.setString(&fbdo, "Clients/client1/allergies", "Nothing");
		Firebase.RTDB.setString(&fbdo, "Clients/client1/room_number", "8");
		Firebase.RTDB.setString(&fbdo, "Clients/client1/age", "22");
		Firebase.RTDB.setString(&fbdo, "Clients/client1/diseases", "Very hot as hell");
		Firebase.RTDB.setString(&fbdo, "Clients/client1/gender", "princess");
		Firebase.RTDB.setFloat(&fbdo, "Clients/client1/temperature", temperature);
		Firebase.RTDB.setFloat(&fbdo, "Clients/client1/heartbeat", beatsPerMinute);
		Firebase.RTDB.setString(&fbdo, "Clients/client1/fallen", "no");
		Firebase.RTDB.setString(&fbdo, "Clients/client1/emergency", isEmergency);
		if (!date_time.length() > 0) {
  			date_time = date_time.substring(0,date_time.length()-1); // Remove the last character
		}

		String date = date_time.substring(0, 24);

		if (!Firebase.RTDB.setString(&fbdo, "Clients/client1/date", date)) {
			Serial.println("Failed to set data.");
			Serial.println(fbdo.errorReason());
			Serial.println("------------------------------------");
			Serial.println();
		}
		Firebase.RTDB.setString(&fbdo, "Clients/client1/reminder/title", "idk");
		Firebase.RTDB.setString(&fbdo, "Clients/client1/reminder/type", "idk");
		Firebase.RTDB.setString(&fbdo, "Clients/client1/reminder/details", "too hot to handle");
		Firebase.RTDB.setString(&fbdo, "Clients/client1/reminder/date", "idk bro");
		Firebase.RTDB.setString(&fbdo, "Clients/client1/reminder/time", "time to get a watch");	
	}
	else {
		Serial.println("Cannot connect and write to the database");
		delay(10000);
	}
}


/*<-------------------------->
  <----- MAIN FUNCTIONS ----->
  <-------------------------->*/

void setup() {
	Serial.begin(BAUD_RATE);
	Serial.printf("Starting...\r\n");

	//Set up functions
	setupRelays();
	setupFlipSwitches();
	setupWiFi();
	//setupDisplay();
  	//setupGoogleHomeNotifier();
	//GoogleHomeMessage("Office is online");
	setupSinricPro();
	setupTime();
	setupHeartbeatSensor();
	dht.begin();
	setup_firebase();

	//--Delete later--
	//setupSQL();
	//setup_tft();

	Serial.printf("Setup done.\r\n");
}

void loop()
{
	//SinricPro.handle();
	//handleFlipSwitches();
	String daytime = printLocalTime();
	getHeartbeat();
	float temperature = getTempHumidity();
	sendToFirebase(daytime,temperature);
	//checkAlarms();
	//TODO retrive json file to set up alarms
	if(emergency  && ((millis() - emergencyTime) >= emergencyLimit )) { //20 seconds
		isEmergency = false; 
	}
}
