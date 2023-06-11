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

#include "SinricPro.h"
#include "SinricProSwitch.h"
#include <esp8266-google-home-notifier.h>

#define TOUCH_CS 3
#include <time.h>
#include <TFT_eSPI.h>
#include <string.h>

#include <list>

// #include <MySQL_Connection.h>
// #include <MySQL_Cursor.h>

#include <map>

#define EMERGENCY_PIN 32

// Wifi credentials 
// #define WIFI_SSID "Minkmates"
// #define WIFI_PASS "Minkmaatstraat50"
//#define WIFI_SSID "Definitely Not A Wifi"
//#define WIFI_PASS "jkoy3240"
#define WIFI_SSID         "D.E-CAFE-GAST"
#define WIFI_PASS         ""

//SinricPro credentials
#define APP_KEY "75f4059f-33e0-4266-86f2-e646618364f8"										   
#define APP_SECRET "671516bc-99cf-4b1b-8e4a-4f13ef2353a6-14f8dad9-ec0d-4b67-be39-206bee47554c" 

// comment the following line if you use a toggle switches instead of tactile buttons
 #define TACTILE_BUTTON 1

#define BAUD_RATE 115200

#define DEBOUNCE_TIME 250

GoogleHomeNotifier ghn;

TFT_eSPI tft = TFT_eSPI();

bool alarm_set = false;

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 3600;
const int   daylightOffset_sec = 3600;


// WiFiClient client;
// MySQL_Connection conn((Client *)&client);
// IPAddress server_addr(20,74,46,0);  // IP of the MySQL *server* here
// char SQLuser[] = "nesnos";              // MySQL user login username
// char password[] = "Axeltalksalot3!";        // MySQL user login password

/*<------------------------->
  <----GLOBAL VARIABLES ---->
  <------------------------->*/


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

void setup_tft(){
	tft.init();
	tft.fillScreen(TFT_RED);
	tft.setRotation(1);
	tft.setTextWrap(true,true);
}

void time_setup(){
	// Init and get the time
	configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
	printLocalTime();

}
void setupGoogleHomeNotifier() {
  const char deviceName[] = "Office";

  Serial.println("connecting to Google Home...");
  if (ghn.device(deviceName, "en") != true) {
    Serial.println(ghn.getLastError());
    return;
  }

  Serial.print("found Google Home(");
  Serial.print(ghn.getIPAddress());
  Serial.print(":");
  Serial.print(ghn.getPort());
  Serial.println(")");

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
	//send to database emergency
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

void printLocalTime() {
	struct tm timeinfo;
	if (!getLocalTime(&timeinfo))
	{
		Serial.println("Failed to obtain time");
		return;
	}
	Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
	String daytime = asctime(&timeinfo);
	Serial.println("The needed string:" + daytime);
	String day = daytime.substring(0, 11) += daytime.substring(20, 24);
	String time = daytime.substring(11, 19);
	tft.drawString(day, 15, 10, 4);
	tft.drawString(time, 20, 45, 7);
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

	if (!alarm_set)
	{

		// // Set the alarm with the received time

		// Serial.print("Setting alarm with time: ");
		// Serial.println(alarmTime);
		String alarmTime_c = Serial.readStringUntil('\n');
		alarmTime_c += "\n";
		Serial.println("Wake up alarm set to:" + alarmTime_c);
		if (daytime == alarmTime_c)
		{
			tft.drawString("WAAAAKEEEE UP", 30, 100, 4);
			Serial.println("WAAAAKEEEE UP");
			alarm_set = true;
		}
	}
}

void setup()
{
	Serial.begin(BAUD_RATE);
	Serial.printf("Starting...\r\n");
	setupRelays();
	setupFlipSwitches();
	setupWiFi();
  	//setupGoogleHomeNotifier();
	//GoogleHomeMessage("Office is online");
	//delay(1000);
	//GoogleHomeMessage("Hello Eric");
	//setupSQL();
	setupSinricPro();
	setup_tft();
	time_setup();
}

void loop()
{
	SinricPro.handle();
	handleFlipSwitches();
	printLocalTime();
}
