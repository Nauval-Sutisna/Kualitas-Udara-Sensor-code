#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include "SDS011.h"
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <math.h>

#define WIFI_SSID "RTX 4070 TI"
#define WIFI_PASSWORD "Kelompok6"

#define API_KEY "AIzaSyAx3FuxpSC4UXxk7gxgMN3Aag-k4yr5ot4"
#define DATABASE_URL "https://dataset-kualitasudara-default-rtdb.firebaseio.com/"
#define USER_EMAIL "admin@gmail.com"
#define USER_PASSWORD "admin123"

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
int firebaseFail = 0;

// SENSOR
SDS011 sds;
Adafruit_BME280 bme;

// MQ-7
#define MQ7_PIN 34

const float VCC = 5.0;          // Tegangan sensor
const float ADC_VREF = 3.3;     // ADC ESP32
const int ADC_MAX = 4095;

const float RL = 10.0;          // kΩ
const float RO = 2.16;           // hasil kalibrasi final

float readMQ7()
{
    // Membaca tegangan sensor
    int adc = analogRead(MQ7_PIN);

    float voltage =
        (adc * ADC_VREF) / ADC_MAX;

    if (voltage < 0.01)
        voltage = 0.01;

    // Hitung resistansi sensor
    float Rs = ((VCC - voltage) / voltage) * RL;

    // Hitung rasio
    float ratio = Rs / RO;

    // Persamaan dari jurnal
    float ppmCO = 97.175 * pow(ratio, -1.535);

    // Hindari nilai negatif atau NaN
    if (ppmCO < 0 || isnan(ppmCO) || isinf(ppmCO))
        ppmCO = 0;

    return ppmCO;
}

// BUZZER
#define BUZZER_PIN 25

unsigned long lastSend = 0;
bool firebaseConnected = false;

void beep(int jumlah)
{
  for (int i = 0; i < jumlah; i++)
  {
    tone(BUZZER_PIN, 2000);   // Bunyi 2000 Hz
    delay(200);

    noTone(BUZZER_PIN);       // Diam
    delay(200);
  }
}

void reconnectWiFi()
{
  if (WiFi.status() == WL_CONNECTED)
    return;

  Serial.println("Reconnect WiFi...");

  WiFi.disconnect(true);
  delay(1000);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long start = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000)
  {
    Serial.print(".");
    delay(500);
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println();
    Serial.println("WiFi Connected Again");
  }
  else
  {
    Serial.println();
    Serial.println("Reconnect Failed");
  }
}

void setup()
{
  Serial.begin(115200);

  pinMode(BUZZER_PIN, OUTPUT);
  noTone(BUZZER_PIN);   

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);

  Serial.print("Connecting WiFi");

  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print(".");
    delay(500);
  }

  Serial.println();
  Serial.println("WiFi Connected");

  sds.begin(16, 17);

  // BME280
  Wire.begin(21, 22);

  if (!bme.begin(0x76))
  {
    Serial.println("BME280 NOT FOUND!");
  }
  else
  {
    Serial.println("BME280 OK");
  }

  // warmup sensor CO
  Serial.println("Warming MQ7...");
  delay(300000);

  // Firebase
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  config.timeout.serverResponse = 10000;

  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;

  Firebase.begin(&config, &auth);

  Firebase.reconnectNetwork(true);
  Firebase.reconnectWiFi(true);

  fbdo.setBSSLBufferSize(4096, 1024);
  fbdo.setResponseSize(2048);

  Serial.print("Connecting Firebase");

  unsigned long start = millis();

  while (!Firebase.ready() && millis() - start < 20000)
  {
      Serial.print(".");
      delay(300);
  }

  Serial.println();

  if (Firebase.ready())
  {
      Serial.println("Firebase Ready");
  }
  else
  {
      Serial.println("Firebase Login Timeout");
  }
}

void loop()
{
  if (millis() - lastSend >= 3000)
  {
    lastSend = millis();

    float pm25 = 0;
    float pm10 = 0;

    sds.read(&pm25, &pm10);

    float temp = bme.readTemperature();
    float hum = bme.readHumidity();
    float press = bme.readPressure() / 100.0F;

    float ppmCO = readMQ7();

    // HILANGKAN NaN
    if (isnan(temp)) temp = 0;
    if (isnan(hum)) hum = 0;
    if (isnan(press)) press = 0;
    if (isnan(ppmCO)) ppmCO = 0;

    // FIREBASE JSON
    FirebaseJson json;

    json.set("pm25", pm25);
    json.set("pm10", pm10);

    json.set("co", ppmCO);

    json.set("temperature", temp);
    json.set("humidity", hum);
    json.set("pressure", press);

    reconnectWiFi();

    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("Skip Send, WiFi Lost");
        return;
    }

    if (WiFi.status() == WL_CONNECTED && !Firebase.ready())
    {
        Serial.println("Reconnect Firebase...");
        Firebase.begin(&config, &auth);
        return;
    }

    if (!Firebase.ready())
    {
        Serial.println("Firebase Token Not Ready");
        return;
    }

    // KIRIM KE FIREBASE
    if (Firebase.RTDB.setJSON(&fbdo, "/sensor", &json))
    {
      firebaseFail = 0;
      
      if (!firebaseConnected)
      {
        firebaseConnected = true;
        beep(1); 
        Serial.println("Firebase Connected");
      }

      Serial.println("Firebase Update Success");

      Serial.print("PM2.5 : ");
      Serial.println(pm25);

      Serial.print("PM10  : ");
      Serial.println(pm10);

      Serial.print("CO    : ");
      Serial.println(ppmCO);

      Serial.print("Temp  : ");
      Serial.println(temp);

      Serial.print("Hum   : ");
      Serial.println(hum);

      Serial.print("Press : ");
      Serial.println(press);

      Serial.println("--------------------");
    }
    else
    {
      firebaseFail++;
      // Jika sebelumnya terhubung lalu sekarang gagal
      if (firebaseConnected)
      {
        firebaseConnected = false;
        beep(3);   // bunyi 3 kali
      }

      Serial.println("Firebase Disconnected");
      Serial.println(fbdo.errorReason());

      Serial.println("==========");

      Serial.print("WiFi Status : ");
      Serial.println(WiFi.status());

      Serial.print("Firebase Ready : ");
      Serial.println(Firebase.ready());

      Serial.print("RSSI : ");
      Serial.println(WiFi.RSSI());

      Serial.print("IP : ");
      Serial.println(WiFi.localIP());

      Serial.println("==================");

      if (firebaseFail >= 5)
      {
          Serial.println("Restart Firebase...");

          Firebase.begin(&config, &auth);

          firebaseFail = 0;
      }
    }
  }
}
