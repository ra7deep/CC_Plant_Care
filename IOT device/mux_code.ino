//online_mode.ino
// Libraries to be installed in Arduino IDE : Adafruit Unified Sensor, ArduinoJson, DHT kxn, WifiManager by tzapu
#include <DHT.h>  // Include the library for DHT11 temperature and humidity sensor
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>  // Include the library for https requests
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <WiFiClientSecureBearSSL.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <Wire.h> 
#include <LiquidCrystal_I2C.h>

static const char* serverUrl = "https://plant-care-automation-backend.onrender.com"; // IP address of the server
static const int deviceIDbundle[8] = {109, 110, 111, 112, 113, 114, 115, 116};
static const unsigned long deviceId = deviceIDbundle[0];
static const int maxSoilReading = 720;
static const int minSoilReading = 150;


// Set the LCD address to 0x27 for a 16 chars and 2 line display
LiquidCrystal_I2C lcd(0x27, 16, 2);

#define soil_moisture_pin A0
#define DHTTYPE DHT11  // Define the sensor type (DHT11)
#define dht_dpin D5   // Pin where the DHT11 sensor is connected

const int RELAY_PIN_1 = D6; // Connect first relay to D6 pin
const int RELAY_PIN_2 = D7; // Connect second relay to D7 pin

//D1 D2 for led display
const int SELECT_1 = D0;
const int SELECT_2 = D3;
const int SELECT_3 = D4;



long checkDelayDuration = 60000 * 10; //cooldown time between each check
int pumpFlowDuration = 5000; 
int moisturethreshold=400;
int pumpDuration = pumpFlowDuration/1000;
int threshold;

unsigned long lastPumpActivationTime[8] = {0}; // Array to store the last activation time for each pump
bool pumpActivated[8] = {false}; // Array to track if each pump is activated

int sns1, sns2, sns3, sns4, sns5, sns6, sns7, sns8; // value read from the pot
int readingList[8][2]; // 2D array to store binary value and corresponding reading
int listSize = 0;

DHT dht(dht_dpin, DHTTYPE);
float humidity,temperature;
int soil_moisture;

//Function to read when mux ON
int readSensor(int select1, int select2, int select3) {
  digitalWrite(SELECT_1, select1);
  digitalWrite(SELECT_2, select2);
  digitalWrite(SELECT_3, select3);
  delay(20);
  int sensorValue = analogRead(A0);
  delay(20);
  return sensorValue;
}

int readAnalogMUX() {
  int listIndex = 0;
  // read the analog in value:
  sns1 = readSensor(0, 0, 0); //select = 0
  sns2 = readSensor(1, 0, 0); //select = 1
  sns3 = readSensor(0, 1, 0); //select = 2
  sns4 = readSensor(1, 1, 0); //select = 3
  sns5 = readSensor(0, 0, 1); //select = 4
  sns6 = readSensor(1, 0, 1); //select = 5
  sns7 = readSensor(0, 1, 1); //select = 6
  sns8 = readSensor(1, 1, 1); //select = 7

  // create a list with binary value and corresponding reading for sensors with readings greater than 100
  if(sns1 > 100) { readingList[listIndex][0] = 0; readingList[listIndex][1] = sns1; listIndex++; }
  if(sns2 > 100) { readingList[listIndex][0] = 1; readingList[listIndex][1] = sns2; listIndex++; }
  if(sns3 > 100) { readingList[listIndex][0] = 2; readingList[listIndex][1] = sns3; listIndex++; }
  if(sns4 > 100) { readingList[listIndex][0] = 3; readingList[listIndex][1] = sns4; listIndex++; }
  if(sns5 > 100) { readingList[listIndex][0] = 4; readingList[listIndex][1] = sns5; listIndex++; }
  if(sns6 > 100) { readingList[listIndex][0] = 5; readingList[listIndex][1] = sns6; listIndex++; }
  if(sns7 > 100) { readingList[listIndex][0] = 6; readingList[listIndex][1] = sns7; listIndex++; }
  if(sns8 > 100) { readingList[listIndex][0] = 7; readingList[listIndex][1] = sns8; listIndex++; }

  return listIndex;
}

void scrollTexts(String text1, String text2, int delayTime) {
    int text1Length = text1.length();
    int text2Length = text2.length();
    int displayWidth = 16; // Adjust this value based on your LCD display width

    // Determine the maximum length between the two texts
    int maxLength = max(text1Length, text2Length);

    for (int position = 0; position < maxLength; position++) {
        lcd.clear();

        // Print line 1 text
        lcd.setCursor(0, 0);
        lcd.print(text1.substring(position, position + displayWidth));

        // Print line 2 text
        lcd.setCursor(0, 1);
        lcd.print(text2.substring(position, position + displayWidth));

        delay(delayTime);
    }
}

// Function to update check delay and pump flow
void fetchDeviceSettings() {

  HTTPClient https;
  BearSSL::WiFiClientSecure client; 
  client.setInsecure();
  // Define the path for fetching device settings
  const char* path = "/api/user_devices/settings";

  // Combine the server IP, port, and path
  String url = String(serverUrl) + String(path);

  // Create the JSON payload with the device ID
  String requestBody = "{\"deviceId\":\"" + String(deviceId) + "\"}";

  https.begin(client, url);
  https.addHeader("Content-Type", "application/json");

  int httpsResponseCode = https.POST(requestBody);

  if (httpsResponseCode == 404) {
    String line1 = "Please add device in your dashboard";
    String line2 = "Go to Dashboard > + (Add Device)";
    scrollTexts(line1,line2,400);
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Put Device ID:");
    lcd.setCursor(0, 1);
    lcd.print(String(deviceId));
    delay(5000);
    scrollTexts(line1,line2,400);
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Put Device ID:");
    lcd.setCursor(0, 1);
    lcd.print(String(deviceId));
    delay(5000);
  }

  if (httpsResponseCode == 200) {
    //digitalWrite(server_led,LOW); // Connected to server turn off server error led
    String response = https.getString();
    Serial.println("Received device settings:");
    Serial.println(response);
    // Parse the JSON response
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, response);
    if (error) {
      Serial.print("Failed to parse JSON: ");
      Serial.println(error.c_str());
      return;
      }

    // Extract checkIntervals and pumpDuration from the JSON
    long checkIntervals = doc["data"]["checkIntervals"];
    pumpDuration = doc["data"]["pumpDuration"];
    threshold = doc["data"]["threshold"];

    delay(1000);

    // Update checkDelayDuration and pumpFlowDuration
    checkDelayDuration = (checkIntervals*60000);
    pumpFlowDuration = (pumpDuration*1000);
    moisturethreshold = (float(100-threshold)/100)*(maxSoilReading-minSoilReading)+minSoilReading;//70.703125% =300

    delay(1000);
    
    Serial.print("\nDELAY DURATION:");
    Serial.println(checkDelayDuration);
    Serial.print("\nPump flow DURATION:");
    Serial.println(pumpFlowDuration);
    Serial.print("\nMOISTURE threshold:");
    Serial.println(moisturethreshold);

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Fetched Device");
    lcd.setCursor(0, 1);
    lcd.print("Settings!");
  } else {
    //digitalWrite(server_led, HIGH); // Not connecting to server, turn on error light
    Serial.print("Error fetching device settings. https response code: ");
    Serial.println(httpsResponseCode);
  }

  https.end();
}

// Function to send sensor data
void sendDataToServer() {
    for (int i = 0; i < listSize; i++) {
        int deviceIdIndex = readingList[i][0];
        int soilMoisture = readingList[i][1];
        if (soilMoisture <= 750 && soilMoisture > 50) {
            // Create a WiFi Client
            HTTPClient https;
            BearSSL::WiFiClientSecure client;
            client.setInsecure();

            // Define the path for sensor readings
            const char* path = "/sensor_readings";

            // Combine the server IP, port, and path
            String url = String(serverUrl) + String(path);

            // Create a JSON payload with deviceId, soil moisture, temperature, and humidity
            String payload = "{\"deviceId\": \"" + String(deviceIDbundle[deviceIdIndex]) + "\"" +
                             ", \"soilMoisture\": " + String(soilMoisture) +
                             ", \"temperature\": " + String(temperature) +
                             ", \"humidity\": " + String(humidity) + "}";

            // Start the https connection
            https.begin(client, url);
            https.addHeader("Content-Type", "application/json");

            // Send the POST request with the JSON payload
            int httpsResponseCode = https.POST(payload);

            // Check for successful https response
            if (httpsResponseCode > 0) {
                Serial.print("https Response code: ");
                Serial.println(httpsResponseCode);
            } else {
                Serial.print("Error sending data to server. https Response code: ");
                Serial.println(httpsResponseCode);
            }
            Serial.print("Sent data to server.");

            // End the https connection
            https.end();
        } else {
            Serial.println("Invalid Reading, Sensor not in soil\nReading is:" + String(soilMoisture));
        }
    }
}

// Function to read soil moisture
int readSoilMoisture() {
  soil_moisture = analogRead(soil_moisture_pin);
  if(isnan(soil_moisture)){
    return 0;
  }
  return 1;
}

void printReadingList() {
    Serial.println("Reading List:");
    for (int i = 0; i < listSize; i++) {
        Serial.print("Index: ");
        Serial.print(readingList[i][0]);
        Serial.print(", Value: ");
        Serial.println(readingList[i][1]);
    }
}

// Function to control water pump
void controlWaterPump() {
  printReadingList();

  for (int i = 0; i < listSize; i++) {
    int soilMoisture = readingList[i][1]; // Read soil moisture from the current sensor
    int deviceIdIndex = readingList[i][0];
    Serial.println("Soil moisture value for sensor " + String(deviceIdIndex) + ": " + String(soilMoisture));

    if (!pumpActivated[deviceIdIndex] && soilMoisture > moisturethreshold && soilMoisture < 950) {
      // Control the water pump based on the current soil moisture sensor
      Serial.println("Water Level for sensor " + String(deviceIdIndex) + ": " + String(soilMoisture));
      lcd.clear();
      lcd.print("Water Level " + String(deviceIdIndex) + ": " + String(soilMoisture));

      pumpActivated[deviceIdIndex] = true; // Set the pump activation flag for the current pump
      lastPumpActivationTime[deviceIdIndex] = millis(); // Record the timestamp of pump activation for the current pump

      int pumpPin; // Declare the pumpPin variable
      switch (deviceIdIndex) {
        case 0:
          pumpPin = RELAY_PIN_1; // Assign the first pump pin for sensor index 0
          break;
        case 1:
          pumpPin = RELAY_PIN_2; // Assign the second pump pin for sensor index 1
          break;
        // Add more cases for additional sensors and pump pins
        default:
          pumpPin = RELAY_PIN_1; // Use the default pump pin if the sensor index is not recognized
          break;
      }

      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Water Level Low!");
      lcd.setCursor(0, 1);
      lcd.print("Pumping Water");
      
      digitalWrite(pumpPin, LOW); // Turn on the corresponding pump

      Serial.println("Water Level Low! Pumping Water");
      delay(pumpFlowDuration); // Set value at top of program
      digitalWrite(pumpPin, HIGH); // Turn off the corresponding pump

      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Water Pump " + String(deviceIdIndex));
      lcd.setCursor(0, 1);
      lcd.print("Turned Off!");
      Serial.println("Water Pump " + String(deviceIdIndex) + " turned off");

      // Send pump activation data to the server
      HTTPClient https;
      BearSSL::WiFiClientSecure client;
      client.setInsecure();
      String url = String(serverUrl) + "/pump"; // Adjust URL to match your server's endpoint

      // Start the https connection
      https.begin(client, url);
      https.addHeader("Content-Type", "application/json");
      String requestBody = "{\"deviceId\": " + String(deviceIDbundle[deviceIdIndex]) + ", \"pumpDuration\": " + String(pumpDuration) + ", \"threshold\": " + String(threshold) + "}";
      int httpsResponseCode = https.POST(requestBody);
      if (httpsResponseCode > 0) {
        Serial.print("Pump " + String(deviceIdIndex) + " activation data sent to server. https Response code: ");
        Serial.println(httpsResponseCode);
      } else {
        Serial.print("Error sending pump " + String(deviceIdIndex) + " activation data to server. https Response code: ");
        Serial.println(httpsResponseCode);
      }
      https.end();
    }
  }
}

// Function to read temperature and humidity
int readTemperatureHumidity() {
  humidity = dht.readHumidity();
  temperature = dht.readTemperature();

  // Check if read successfully
  if(isnan(temperature) || isnan(humidity)){
    Serial.println("Failed to read from DHT sensor!");
    return 0;
  }else{
    Serial.print("Current humidity = ");
    Serial.print(humidity);
    Serial.print("% ");
    Serial.print("Temperature = ");
    Serial.print(temperature);
    Serial.print("°C ");
    Serial.print("Current Soil Moisture = ");
    Serial.println(soil_moisture);
  }
  return 1;
}

void lcdDisplay(){
  lcd.clear();
  lcd.setCursor(0,0);
  String line1 = "T:" + String(temperature) +"C "+"Hum:"+ String(humidity)+"%";
  lcd.print(line1);

  lcd.setCursor(0, 1);
  String line2 = "Mois:" ;
  for (int i = 0; i < listSize; i++) {
    // line2 += ("[");
    // line2 += String(readingList[i][0]);
    // line2 += (",");
    // line2 += ((1.00 - readingList[i][1] / 1024.00) * 100);
    line2 += String(((1 - float(readingList[i][1] - minSoilReading) / (maxSoilReading - minSoilReading)) * 100));

    // line2 += ("]");
    line2 += (" ");
  }
  lcd.print(line2);
}

void setup() {
  dht.begin();

  //Selection Lines
  pinMode(SELECT_2, OUTPUT);
  pinMode(SELECT_3, OUTPUT);
  pinMode(SELECT_1, OUTPUT);

  // initialize the LCD
	lcd.begin();
	// Turn on the blacklight and print a message.
	lcd.backlight();

  // pinMode(wifi_led, OUTPUT);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Connecting");
  lcd.setCursor(0, 1);
  lcd.print("to WiFi...");
  // digitalWrite(wifi_led, LOW); // Turn off wifi led (blue)
  Serial.begin(9600);
  
  // Create an instance of WiFiManager
  WiFiManager wifiManager;

  // Start the configuration portal
  if (!wifiManager.autoConnect("BloomBuddy")) {
    // WiFi connection timed out
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Couldn't connect");
    lcd.setCursor(0, 1);
    lcd.print("to WiFi");
    delay(3000); // Wait for 3 seconds before trying again
  } else {
    // WiFi connected successfully
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Connected");
    lcd.setCursor(0, 1);
    lcd.print("to WiFi!");
  }
  

  //digitalWrite(wifi_led, HIGH); // Turn on wifi led (blue)
  Serial.println("Humidity, Temperature, and Soil Moisture Level\n\n");
  
  fetchDeviceSettings();

  pinMode(RELAY_PIN_1, OUTPUT);
  pinMode(RELAY_PIN_2, OUTPUT);
  digitalWrite(RELAY_PIN_1, HIGH); // Turn off first pump
  digitalWrite(RELAY_PIN_2, HIGH); // Turn off second pump
  
}

// Main code here, to run repeatedly:
void loop() {
  Serial.println("Humidity, Temperature, and Soil Moisture Level\n\n");
  fetchDeviceSettings();

  unsigned long currentTime = millis(); // Get the current time
  for (int i = 0; i < 8; i++) {

    if (currentTime - lastPumpActivationTime[i] >= 10800000) { // 10800000 = 3 hours in ms
      pumpActivated[i] = false; // Reset the pump activation flag for the current pump
    }
  }


  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Taking Moisture");
  lcd.setCursor(0, 1);
  lcd.print("Readings");
  delay(1000);
  listSize = readAnalogMUX();
  

  if(readTemperatureHumidity()){
    //Print on lcd
    lcdDisplay();
    sendDataToServer(); // Send data to server
    Serial.println("Before control water pump.");
    controlWaterPump();
    delay(1000);
    lcdDisplay();
    Serial.println("After control water pump");
    delay(checkDelayDuration); // Set value at top of programdelay
  }
}
