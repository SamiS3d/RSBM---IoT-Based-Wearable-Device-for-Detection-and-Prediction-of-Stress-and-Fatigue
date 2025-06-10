#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <MAX30105.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP085.h>
#include "heartRate.h"
#include "BluetoothSerial.h"

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

MAX30105 max30102;
Adafruit_MPU6050 mpu;
Adafruit_BMP085 bmp;
const int GSR_PIN = 32;
BluetoothSerial SerialBT;
unsigned long lastUpdateTime = 0;
const long updateInterval = 12000;
unsigned long lastSampleTime = 0;
const long sampleInterval = 500; 
const byte RATE_SIZE = 4;
byte rates[RATE_SIZE];
byte rateSpot = 0;
long lastBeat = 0;
float beatsPerMinute = 0;
int beatAvg = 0;
int stepCount = 0;
float stepThreshold = 1.6;
float noiseThreshold = 0.3;
float prevMagnitude = 0;
unsigned long lastStepTime = 0;
const long stepDelay = 250;
float heartRateSum = 0;
float spo2Sum = 0;
float gsrSum = 0;
float temperatureSum = 0;
int sampleCount = 0;
void initializeSensors() {
  Serial.begin(115200);
  SerialBT.begin("ESP32_BT");
  Serial.println("Bluetooth Started");

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("❌ Failed to initialize OLED");
    while (1);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.display();
  pinMode(GSR_PIN, INPUT);
  if (!max30102.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("❌ Failed to initialize MAX30102");
    while (1);
  }
  max30102.setup();
  if (!mpu.begin()) {
    Serial.println("❌ Failed to initialize MPU6050");
    while (1);
  }
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  if (!bmp.begin()) {
    Serial.println("❌ Failed to initialize BMP180");
    while (1);
  }
  Serial.println("✅ All Sensors Initialized");
}
int readGSR() {
  long sum = 0;
  for (int i = 0; i < 10; i++) {
    int gsrRaw = analogRead(GSR_PIN);
    sum += gsrRaw;
    delay(5);
  }
  return sum / 10;
}
void readMAX30102() {
  long irValue = max30102.getIR();
  long redValue = max30102.getRed();
  int currentHR = map(irValue, 50000, 100000, 60, 100);
  int currentSpO2 = map(redValue, 50000, 100000, 90, 100);
  heartRateSum += currentHR;
  spo2Sum += currentSpO2;
}
float getTemperature() {
  return bmp.readTemperature();
}
float calculateMagnitude(float x, float y, float z) {
  return sqrt(x * x + y * y + z * z);
}
bool detectStep(float magnitude) {
  magnitude = (magnitude + prevMagnitude) / 2;
  if (abs(magnitude - prevMagnitude) > stepThreshold && abs(magnitude - prevMagnitude) > noiseThreshold) {
    long currentTime = millis();
    if (currentTime - lastStepTime > stepDelay) {
      lastStepTime = currentTime;
      prevMagnitude = magnitude;
      return true;
    }
  }
  prevMagnitude = magnitude;
  return false;
}
void readMPU6050() {
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  float acc_x = a.acceleration.x;
  float acc_y = a.acceleration.y;
  float acc_z = a.acceleration.z;
  float acc_magnitude = calculateMagnitude(acc_x, acc_y, acc_z);
  if (detectStep(acc_magnitude)) {
    stepCount++;
  }
}
void takeSample() {
  readMAX30102();
  gsrSum += readGSR();
  temperatureSum += getTemperature();
  sampleCount++;
}
void displayData(int avgHR, int avgSpO2, int avgGSR, float avgTemp, int steps) {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.printf("HR:%3d bpm | SpO2:%3d%%", avgHR, avgSpO2);
  display.setCursor(0, 16);
  display.printf("Steps:%4d | Temp:%.1f C", steps, avgTemp);
  display.setCursor(0, 32);
  display.printf("GSR:%4d", avgGSR);
  display.display();
}
void sendDataViaBluetooth(int avgHR, int avgSpO2, int avgGSR, float avgTemp, int steps) {
  SerialBT.printf("HR:%d bpm, SpO2:%d%%, Steps:%d, Temp:%.1f C, GSR:%d\n", 
                 avgHR, avgSpO2, steps, avgTemp, avgGSR);
}
void setup() {
  initializeSensors();
}
void loop() {
  unsigned long currentTime = millis();
  readMPU6050();
  if (currentTime - lastSampleTime >= sampleInterval) {
    takeSample();
    lastSampleTime = currentTime;
  }
  if (currentTime - lastUpdateTime >= updateInterval) {
    int avgHeartRate = sampleCount > 0 ? heartRateSum / sampleCount : 0;
    int avgSpO2 = sampleCount > 0 ? spo2Sum / sampleCount : 0;
    int avgGSR = sampleCount > 0 ? gsrSum / sampleCount : 0;
    float avgTemperature = sampleCount > 0 ? temperatureSum / sampleCount : 0;
    displayData(avgHeartRate, avgSpO2, avgGSR, avgTemperature, stepCount);
    sendDataViaBluetooth(avgHeartRate, avgSpO2, avgGSR, avgTemperature, stepCount);
    heartRateSum = 0;
    spo2Sum = 0;
    gsrSum = 0;
    temperatureSum = 0;
    sampleCount = 0;
    stepCount = 0;

    lastUpdateTime = currentTime;
  }
}