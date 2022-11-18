/********************************************************************************
    Simple_Solar_MQTT_Watch.ino

    Demo application using authenticated MQTT access to receive Solar.org blockchain events.
    This project is designed to run on the Lilygo T-Watch2020 
    Blockchain event packets are sent to the serial port as ASCII text for display on terminal program running on PC
    LED is toggled at every blockchain event    

    Required Libraries:
      TTGO_TWatch_Library  https://github.com/Xinyuan-LilyGO/TTGO_TWatch_Library
        Use TTGO T-Watch board
      EspMQTTClient by Patrick Lapointe Version 1.13.3
      ArduinoJson by Benoit Blanchon Version 6.19.4
      PubSubClient by Nick O'Leary Version 2.8
        The MQTT JSON packets are larger then the default setting in the libary.
        You need to increase max packet size from 128 to 4096 via this line in \Arduino\libraries\PubSubClient\src\PubSubClient.h
            #define MQTT_MAX_PACKET_SIZE 4096  //default 128
      
    MQTT Server Access
      I operate a special MQTT broker that receives realtime Solar.org blockchain events from both Testnet and Mainnet networks.
      Special thanks to @deadlock for creating a plugin to enable this. https://github.com/deadlock-delegate/mqtt
      MQTT connections are able to subscribe to these blockchain events.
      MQTT server access is provided by a unique authentication method.  
        Authentication credentials are generated by signing a message using your blockchain private keys and by voting for my delegate with your wallet.
        Currently the authentication method uses the Solar Testnet. Future support for Solar Mainnet will be added

********************************************************************************/

#include "secrets.h"  // credentials for WiFi and MQTT

#include "time.h"  // required for internal clock to syncronize with NTP server

#include <ArduinoJson.h>

#include "bitmap.h"  //Soloar Logo bitmap stored in program memory

// RGB565 Color Definitions
// This is a good tool for color conversions into RGB565 format
// http://www.barth-dev.de/online/rgb565-color-picker/
#define SolarOrange 0xE222  // 0xEB06

//Frequency at which the battery level is updated on the screen
uint32_t UpdateInterval_Battery = 10000;  // 10 seconds
uint32_t previousUpdateTime_Battery = millis();

/********************************************************************************
    Watch 2020 V1 Configuration
********************************************************************************/
#define LILYGO_WATCH_2020_V1  //Use T-Watch2020

// => Function select
#define LILYGO_WATCH_LVGL       //To use LVGL, you need to enable the macro LVGL
#define LILYGO_WATCH_HAS_MOTOR  //Use Motor module
#include <LilyGoWatch.h>

TTGOClass* ttgo;
TFT_eSPI* tft;
AXP20X_Class* power;

void clearMainScreen() {
  tft->fillRect(0, 0, 240, 240, TFT_BLACK);  //clear the screen except for the status bar
}

void DisplaySolarBitmap() {
  tft->drawBitmap(0, 0, SolarLogo_240x240, 240, 240, SolarOrange);  // Display Solar bitmap on middle portion of screen
}


/********************************************************************************
    EspMQTTClient Library by @plapointe6 Version 1.13.3
    WiFi and MQTT connection handler for ESP32
    This library does a nice job of encapsulating the handling of WiFi and MQTT connections.
    You just need to provide your credentials and it will manage the connection and reconnections to the Wifi and MQTT networks.
    EspMQTTClient is a wrapper around the MQTT PubSubClient Library by @knolleary
********************************************************************************/
#include "EspMQTTClient.h"

EspMQTTClient WiFiMQTTclient(
  WIFI_SSID,
  WIFI_PASS,
  MQTT_SERVER_IP,
  MQTT_USERNAME,
  MQTT_PASSWORD,
  MQTT_CLIENT_NAME,
  MQTT_SERVER_PORT);


const int LED_PIN = 25;                          //LED integrated on Heltec WiFi Kit
bool initialConnectionEstablished_Flag = false;  //used to detect first run after power up

StaticJsonDocument<4000> doc;  //allocate memory on stack for the received MQTT JSON packets

/********************************************************************************
  Handler for receiving Block Applied MQTT message
********************************************************************************/
void Block_Applied_Handler(const String& payload) {
  //Serial.println(payload);    //print the received JSON packet

  // Deserialize the JSON packet
  DeserializationError error = deserializeJson(doc, payload);
  // Test if parsing succeeds.
  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
    return;
  }

  JsonObject data = doc["data"];
  long data_height = data["height"];                             //extract block height
  int data_numberOfTransactions = data["numberOfTransactions"];  //extract number of transactions in block

  Serial.print("Block generated with Height ");
  Serial.print(data_height);
  Serial.print(" and ");
  Serial.print(data_numberOfTransactions);
  Serial.println(" Transactions");

  Serial.println();

  tft->setCursor(5, 50);
  tft->print("New Block with height ");
  tft->print(data_height);
}

/********************************************************************************
  Handler for receiving Transaction Applied MQTT message
********************************************************************************/
void Transaction_Applied_Handler(const String& payload) {
  //Serial.println(payload);    //print the received JSON packet

  // Deserialize the JSON packet
  DeserializationError error = deserializeJson(doc, payload);
  // Test if parsing succeeds.
  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
    return;
  }

  JsonObject data = doc["data"];
  const char* data_senderId = data["senderId"];  //extract sender's address

  Serial.print("Transaction sent from address ");
  Serial.println(data_senderId);

  Serial.println();

  tft->setCursor(5, 100);
  tft->println("New Transaction sent by address");
  tft->print(" ");
  tft->print(data_senderId);
}

/********************************************************************************
  Handler for receiving Round Applied MQTT message
********************************************************************************/
void Round_Applied_Handler(const String& payload) {
  Serial.println("New Round Started");
  //Serial.println(payload);    //print the received JSON packet
  Serial.println();
}

/********************************************************************************
  Handler for receiving Round Missed MQTT message
********************************************************************************/
void Round_Missed_Handler(const String& payload) {

  //Serial.println(payload);    //print the received JSON packet

  // Deserialize the JSON packet
  DeserializationError error = deserializeJson(doc, payload);
  // Test if parsing succeeds.
  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
    return;
  }

  JsonObject data_delegate = doc["data"]["delegate"];
  const char* data_delegate_address = data_delegate["address"];  //extract address of delegate that missed a block
  Serial.print("Delegate ");
  Serial.print(data_delegate_address);
  Serial.println(" missed a block");

  Serial.println();

  tft->setCursor(5, 150);
  tft->print("Delegate ");
  tft->print(data_delegate_address);
  tft->print(" missed a block! ");
}

/********************************************************************************
  This function is called once WiFi and MQTT connections are complete
  This must be implemented for EspMQTTClient to function
********************************************************************************/
void onConnectionEstablished() {

  if (!initialConnectionEstablished_Flag) {  //execute this the first time we have established a WiFi and MQTT connection after powerup
    initialConnectionEstablished_Flag = true;

    // sync local time to NTP server
    configTime(TIME_ZONE * 3600, DST, "pool.ntp.org", "time.nist.gov");

    // subscribe to Solar Mainnet Events
    WiFiMQTTclient.subscribe("solar_mainnet/events/block_applied", Block_Applied_Handler);
    WiFiMQTTclient.subscribe("solar_mainnet/events/transaction_applied", Transaction_Applied_Handler);
    WiFiMQTTclient.subscribe("solar_mainnet/events/round_applied", Round_Applied_Handler);
    WiFiMQTTclient.subscribe("solar_mainnet/events/round_missed", Round_Missed_Handler);

    //wait for time to sync from NTP server
    while (time(nullptr) <= 100000) {
      delay(20);
    }

  }

  else {
    // subscribe to Solar Mainnet Events
    WiFiMQTTclient.subscribe("solar_mainnet/events/block_applied", Block_Applied_Handler);
    WiFiMQTTclient.subscribe("solar_mainnet/events/transaction_applied", Transaction_Applied_Handler);
    WiFiMQTTclient.subscribe("solar_mainnet/events/round_applied", Round_Applied_Handler);
    WiFiMQTTclient.subscribe("solar_mainnet/events/round_missed", Round_Missed_Handler);
  }
}


//--------------------------------------------
// Mealy Finite State Machine
// The state machine logic is executed once each cycle of the "main" loop.
// For this simple application it just helps manage the connection states of WiFi and MQTT
//--------------------------------------------

enum State_enum { STATE_0,
                  STATE_1,
                  STATE_2 };  //The possible states of the state machine
State_enum state = STATE_0;   //initialize the starting state.

void StateMachine() {
  switch (state) {

    //--------------------------------------------
    // State 0
    // Initial state after ESP32 powers up and initializes the various peripherals
    // Transitions to State 1 once WiFi is connected
    case STATE_0:
      {
        if (WiFiMQTTclient.isWifiConnected()) {  //wait for WiFi to connect
          state = STATE_1;

          Serial.print("\nState: ");
          Serial.print(state);
          Serial.print("  WiFi Connected to IP address: ");
          Serial.println(WiFi.localIP());
        } else {
          state = STATE_0;
        }
        break;
      }

    //--------------------------------------------
    // State 1
    // Transitions to state 2 once connected to MQTT broker
    // Return to state 0 if WiFi disconnects
    case STATE_1:
      {
        if (!WiFiMQTTclient.isWifiConnected()) {  //check for WiFi disconnect
          state = STATE_0;
          Serial.print("\nState: ");
          Serial.print(state);
          Serial.println("  WiFi Disconnected");
        } else if (WiFiMQTTclient.isMqttConnected()) {  //wait for MQTT connect
          state = STATE_2;
          Serial.print("\nState: ");
          Serial.print(state);
          Serial.print("  MQTT Connected at local time: ");

          time_t now = time(nullptr);  //get current time
          struct tm* timeinfo;
          //    time(&now);
          timeinfo = localtime(&now);
          char formattedTime[30];
          strftime(formattedTime, 30, "%r", timeinfo);
          Serial.println(formattedTime);

        } else {
          state = STATE_1;
        }
        break;
      }

    //--------------------------------------------
    // State 2
    // Return to state 0 if WiFi disconnects
    // Returns to state 1 if MQTT disconnects
    case STATE_2:
      {
        if (!WiFiMQTTclient.isWifiConnected()) {  //check for WiFi disconnect
          state = STATE_0;
          Serial.print("\nState: ");
          Serial.print(state);
          Serial.println("  WiFi Disconnected");
        } else if (!WiFiMQTTclient.isMqttConnected()) {  //check for MQTT disconnect
          state = STATE_1;
          Serial.print("\nState: ");
          Serial.print(state);
          Serial.println("  MQTT Disconnected");
        } else {
          state = STATE_2;
        }
        break;
      }
  }
}

void UpdateBatteryStatus() {
  if (millis() - previousUpdateTime_Battery > UpdateInterval_Battery) {
    previousUpdateTime_Battery += UpdateInterval_Battery;
    tft->setCursor(3, 3);
    tft->setTextFont(2);
    tft->print(power->getBattPercentage());
    tft->print(" %       ");

    tft->print(WiFi.localIP());
  }
}


/********************************************************************************
  Configure peripherals and system
********************************************************************************/
void setup() {

  Serial.begin(115200);  // Initialize Serial Connection for debug
  while (!Serial && millis() < 20);  // wait 20ms for serial port adapter to power up

  Serial.println("\n\nStarting Simple_Solar_MQTT");

  ttgo = TTGOClass::getWatch();
  ttgo->begin();
  ttgo->openBL();  // turn on back light

  previousUpdateTime_Battery = millis() - UpdateInterval_Battery;  //turn back time so battery level updates the first time running through the main loop

  power = ttgo->power;
  tft = ttgo->tft;
  ttgo->motor_begin();
  tft->setCursor(3, 3);
  power->adc1Enable(AXP202_VBUS_VOL_ADC1 | AXP202_VBUS_CUR_ADC1 | AXP202_BATT_CUR_ADC1 | AXP202_BATT_VOL_ADC1, true);
  tft->setTextFont(2);

  // Optional Features of EspMQTTClient
  WiFiMQTTclient.enableDebuggingMessages();  // Enable MQTT debugging messages

  // Display Splash Screen
  DisplaySolarBitmap();
  delay(1500);

  //clearMainScreen();
  tft->fillScreen(TFT_BLACK);
  tft->setTextColor(SolarOrange, TFT_BLACK);
}


/********************************************************************************
  MAIN LOOP
********************************************************************************/
void loop() {

  //--------------------------------------------
  // Process state machine
  StateMachine();

  //--------------------------------------------
  // Handle the WiFi and MQTT connections
  WiFiMQTTclient.loop();

  lv_task_handler();
  UpdateBatteryStatus();  //update battery status bar
}