#include <Arduino.h>
#include "Servo.h"
#include <string.h>

// PIN SETUP
#define button_pin D6
#define ldr2_pin D5
#define ldr1_pin A0
#define servo_pin D7

// OLED LIBRARY
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Fonts/FreeMonoBold12pt7b.h>
Adafruit_SSD1306 display(128, 64);

// FIREBASE LIBRARY
#include <ESP8266WiFi.h>
#include <FirebaseESP8266.h>
#include <addons/TokenHelper.h>
#include <addons/RTDBHelper.h>

/* REALTIME CLOCK */
#include <WiFiUdp.h>
#include <NTPClient.h>

// NTP server settings
const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 25200; // GMT+7 (WIB)
const int daylightOffset_sec = 0;

// NTP client and UDP
WiFiUDP udp;
NTPClient ntpClient(udp, ntpServer, gmtOffset_sec, daylightOffset_sec);

/* 1. Define the WiFi credentials */
#define WIFI_SSID "myssid"
#define WIFI_PASSWORD "mypass"
/* 2. Define the API Key */
#define API_KEY "myapi"
/* 3. Define the RTDB URL */
#define DATABASE_URL "myurl"
/* 4. Define the user Email and password that alreadey registerd or added in your project */
#define USER_EMAIL "myemail.com"
#define USER_PASSWORD "mypassword"

// Define Firebase Data object
FirebaseData fbdo;
FirebaseData stream, alarm_stream;

FirebaseAuth auth;
FirebaseConfig config;

unsigned long sendDataPrevMillis = 0;

String parentPath = "/test/stream/data";
String alarmParentPath = "/test/stream/app";
String childPath[8] = {"/esp", "/level", "/jam1", "/jam2", "/jam3", "/menit1", "/menit2", "/menit3"};
String statusPakanPath = "/esp/statusPakan";
String statusKatupPath = "/esp/statusKatupPakan";

int count = 0, resetCounter = 0, manual_feed = 0, feed = 0;
int level, jam[3], menit[3];
int current_time = 0;
Servo myservo;
int angle = 0;

void streamCallback(MultiPathStreamData stream);
void streamTimeoutCallback(bool timeout);

void setup()
{
  Serial.begin(9600);

  // OLED SETUP
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C))
  { // Address 0x3D for 128x64
    Serial.println(F("SSD1306 allocation failed"));
    for (;;)
      ;
  }
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C); // Initialize display with the I2C address of 0x3C.
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setRotation(0);
  display.setTextWrap(false);

  display.dim(0);
  display.setCursor(0, 25);
  display.println("AUTOMATIC FISH FEEDER");
  display.display();
  delay(1500);
  display.clearDisplay();
  delay(1000);

  // WIFI SETUP
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  String connectString = "Connecting";
  display.println(connectString);
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print(".");
    if (resetCounter < 3)
    {
      display.clearDisplay();
      display.setCursor(0, 25);
      connectString = connectString + ".";
      display.println(connectString);
      resetCounter++;
    }
    else
    {
      display.clearDisplay();
      display.setCursor(0, 25);
      connectString = "Connecting";
      display.println(connectString);
      resetCounter = 0;
    }

    display.display();
    delay(300);
  }
  delay(700);
  display.clearDisplay();
  delay(500);
  display.setCursor(0, 10);
  display.println("Connected ^_^");
  Serial.println();
  Serial.print("Connected with IP: ");
  Serial.println(WiFi.localIP());
  Serial.println();

  Serial.printf("Firebase Client v%s\n\n", FIREBASE_CLIENT_VERSION);

  /* Assign the api key (required) */
  config.api_key = API_KEY;

  /* Assign the user sign in credentials */
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;

  /* Assign the RTDB URL (required) */
  config.database_url = DATABASE_URL;

  /* Assign the callback function for the long running token generation task */
  config.token_status_callback = tokenStatusCallback; 

  Firebase.begin(&config, &auth);

  Firebase.reconnectWiFi(true);

  // We use ESP8266 so set the BSSLBuffer
#if defined(ESP8266)
  stream.setBSSLBufferSize(16384 /* Rx in bytes, 512 - 16384 */, 16384 /* Tx in bytes, 512 - 16384 */);
#endif

  if (!Firebase.beginMultiPathStream(stream, parentPath))
    Serial.printf("stream begin error, %s\n\n", stream.errorReason().c_str());

  if (!Firebase.beginMultiPathStream(alarm_stream, alarmParentPath))
    Serial.printf("alarm stream begin error, %s\n\n", alarm_stream.errorReason().c_str());

  Firebase.setMultiPathStreamCallback(stream, streamCallback, streamTimeoutCallback);
  Firebase.setMultiPathStreamCallback(alarm_stream, streamCallback, streamTimeoutCallback);

  myservo.attach(servo_pin, 500, 2400);
  pinMode(button_pin, INPUT);
  pinMode(ldr1_pin, INPUT);
  pinMode(ldr2_pin, INPUT);
  myservo.write(135);
  // Initialize NTP client
  ntpClient.begin();
}

void loop()
{
  // Update NTP time
  ntpClient.update();

  // Get current time and display on OLED display
  int hour = ntpClient.getHours();
  int minute = ntpClient.getMinutes();

  String timeStr = String(hour) + ":" + String(minute);
  Serial.println(timeStr);

  int ldr1 = analogRead(ldr1_pin);
  int ldr2 = digitalRead(ldr2_pin);
  ldr1 > 255 ? ldr1 = 1 : ldr1 = 0; // 1 == Light not detected
  int button = digitalRead(button_pin);
  Serial.println(ldr1);
  Serial.println(ldr2);
  Serial.print(button);

  // OUPUT to OLED display
  display.clearDisplay();
  display.setFont(&FreeMonoBold12pt7b);
  display.setTextSize(0);
  display.setCursor(0, 25);
  display.print(timeStr);
  display.display();

  if (Firebase.ready())
  {

    Serial.print("\nSet json...");

    FirebaseJson json;

    switch (level)
    {
    case 1:
      display.println("|1"); // Little
      break;
    case 2:
      display.println("|2"); // Medium
      break;
    case 3:
      display.println("|3"); // Lots
      break;
    default:
      break;
    }

    if (ldr1 == 1 && ldr2 == 1)
    {
      json.set(statusPakanPath, "BANYAK");
      display.println("BANYAK");
      display.display();
    }
    else if (ldr1 == 0 && ldr2 == 1)
    {
      json.set(statusPakanPath, "SEDANG");
      display.println("SEDANG");
      display.display();
    }
    else
    {
      display.println("SEDIKIT");
      display.display();
      json.set(statusPakanPath, "SEDIKIT");
    }

    if (button == 0)
    {
      manual_feed = 1;
    }

    if (current_time != minute)
      feed = 0;

    if (manual_feed == 1 || (((hour == jam[0] && minute == menit[0]) || (hour == jam[1] && minute == menit[1]) || (hour == jam[2] && minute == menit[2])) && feed != 1))
    {
      if (manual_feed != 1)
      {
        feed = 1;
        current_time = minute;
      }
      json.set(statusKatupPath, feed);
      display.println("MENGISI");
      myservo.write(45);
      switch (level)
      {
      case 1: // Sedikit
        delay(1000);
        break;
      case 2: // Sedang
        delay(1500);
        break;
      case 3: // Banyak
        delay(2000);
        break;
      default:
        break;
      }
      myservo.write(135);
      manual_feed = 0;
    }

    display.display(); // Print everything we set previously
    Firebase.setJSONAsync(fbdo, parentPath, json);

    Serial.println("ok\n");
  }
  delay(100);
}

void streamCallback(MultiPathStreamData stream)
{
  size_t numChild = sizeof(childPath);

  for (size_t i = 0; i < numChild; i++)
  {
    if (stream.get(childPath[i]))
    {
      Serial.printf("path: %s, event: %s, type: %s, value: %s%s", stream.dataPath.c_str(), stream.eventType.c_str(), stream.type.c_str(), stream.value.c_str(), i < numChild - 1 ? "\n" : "");

      if (strcmp(stream.dataPath.c_str(), "/level") == 0)
      {
        level = stream.value.toInt();
        Serial.println(level);
      }
      else if (strcmp(stream.dataPath.c_str(), "/jam1") == 0)
      {
        char convertToChar[10];
        String stringValue = stream.value.c_str();

        // CONVERT STRING TO CHAR ARRAY
        stringValue.toCharArray(convertToChar, stringValue.length() - 1);

        convertToChar[0] = '0';
        convertToChar[1] = '0';

        jam[0] = atoi(convertToChar);

        Serial.println(jam[0]);
      }
      else if (strcmp(stream.dataPath.c_str(), "/jam2") == 0)
      {
        char convertToChar[10];
        String stringValue = stream.value.c_str();

        // CONVERT STRING TO CHAR ARRAY
        stringValue.toCharArray(convertToChar, stringValue.length() - 1);

        convertToChar[0] = '0';
        convertToChar[1] = '0';

        jam[1] = atoi(convertToChar);

        Serial.println(jam[1]);
      }
      else if (strcmp(stream.dataPath.c_str(), "/jam3") == 0)
      {
        char convertToChar[10];
        String stringValue = stream.value.c_str();

        // CONVERT STRING TO CHAR ARRAY
        stringValue.toCharArray(convertToChar, stringValue.length() - 1);

        convertToChar[0] = '0';
        convertToChar[1] = '0';

        jam[2] = atoi(convertToChar);

        Serial.println(jam[2]);
      }
      else if (strcmp(stream.dataPath.c_str(), "/menit1") == 0)
      {
        char convertToChar[10];
        String stringValue = stream.value.c_str();

        // CONVERT STRING TO CHAR ARRAY
        stringValue.toCharArray(convertToChar, stringValue.length() - 1);

        convertToChar[0] = '0';
        convertToChar[1] = '0';

        menit[0] = atoi(convertToChar);

        Serial.println(menit[0]);
      }
      else if (strcmp(stream.dataPath.c_str(), "/menit2") == 0)
      {
        char convertToChar[10];
        String stringValue = stream.value.c_str();

        // CONVERT STRING TO CHAR ARRAY
        stringValue.toCharArray(convertToChar, stringValue.length() - 1);

        convertToChar[0] = '0';
        convertToChar[1] = '0';

        menit[1] = atoi(convertToChar);

        Serial.println(menit[1]);
      }
      else if (strcmp(stream.dataPath.c_str(), "/menit3") == 0)
      {
        char convertToChar[10];
        String stringValue = stream.value.c_str();

        // CONVERT STRING TO CHAR ARRAY
        stringValue.toCharArray(convertToChar, stringValue.length() - 1);

        convertToChar[0] = '0';
        convertToChar[1] = '0';

        menit[2] = atoi(convertToChar);

        Serial.println(menit[2]);
      }
    }
  }

  Serial.println();
  Serial.printf("Received stream payload size: %d (Max. %d)\n\n", stream.payloadLength(), stream.maxPayloadLength());
}

void streamTimeoutCallback(bool timeout)
{
  if (timeout)
    Serial.println("stream timed out, resuming...\n");

  if (!stream.httpConnected())
    Serial.printf("error code: %d, reason: %s\n\n", stream.httpCode(), stream.errorReason().c_str());
}