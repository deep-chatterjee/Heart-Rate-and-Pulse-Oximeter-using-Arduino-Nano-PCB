/*
 * Heart Rate & Pulse Oximeter - MEMORY OPTIMIZED for Arduino Nano
 * Hardware: Arduino Nano + MAX30102 + SSD1306 OLED + 3 Buttons + LED
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <MAX30105.h>
#include <heartRate.h>

// --- Pin Definitions ---
const int SW1_PIN = 3;
const int SW2_PIN = 4;
const int SW3_PIN = 5;
const int LED_PIN = 12;

// --- Display Setup ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- MAX30102 Setup ---
MAX30105 particleSensor;

// --- State Variables ---
bool measuring = false;
int displayMode = 0;
unsigned long lastBlink = 0;
bool ledState = false;

// --- Heart Rate Variables ---
const byte RATE_SIZE = 4;
byte rates[RATE_SIZE];
byte rateSpot = 0;
long lastBeat = 0;
float beatsPerMinute;
int beatAvg;

// --- Button Debounce ---
unsigned long lastDebounce[3] = {0, 0, 0};
const unsigned long DEBOUNCE_DELAY = 200;

void setup() {
  Serial.begin(115200);
  
  pinMode(SW1_PIN, INPUT_PULLUP);
  pinMode(SW2_PIN, INPUT_PULLUP);
  pinMode(SW3_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  
  // Initialize OLED
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.println(F("Pulse Oximeter"));
  display.println(F("Initializing..."));
  display.display();
  
  // Initialize MAX30102
  Wire.begin();
  Wire.setClock(400000);
  
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println(F("MAX30102 not found!"));
    display.println(F("Sensor Error!"));
    display.display();
    while (1);
  }
  
  Serial.println(F("MAX30102 found!"));
  display.println(F("Sensor OK"));
  
  // Configure sensor
  particleSensor.setup();
  particleSensor.setPulseAmplitudeRed(0x0A);
  particleSensor.setPulseAmplitudeIR(0x0A);
  
  display.display();
  delay(1000);
  display.clearDisplay();
}

void loop() {
  handleButtons();
  updateLED();
  
  if (measuring) {
    readSensor();
  } else {
    showIdleScreen();
  }
  
  delay(10);
}

void handleButtons() {
  if (digitalRead(SW1_PIN) == LOW && millis() - lastDebounce[0] > DEBOUNCE_DELAY) {
    lastDebounce[0] = millis();
    measuring = !measuring;
    if (!measuring) digitalWrite(LED_PIN, LOW);
  }
  
  if (digitalRead(SW2_PIN) == LOW && millis() - lastDebounce[1] > DEBOUNCE_DELAY) {
    lastDebounce[1] = millis();
    displayMode = (displayMode + 1) % 3;
  }
  
  if (digitalRead(SW3_PIN) == LOW && millis() - lastDebounce[2] > DEBOUNCE_DELAY) {
    lastDebounce[2] = millis();
    measuring = false;
    digitalWrite(LED_PIN, LOW);
    particleSensor.setup();
    display.clearDisplay();
    display.setCursor(0,0);
    display.println(F("Recalibrating..."));
    display.display();
    delay(1000);
  }
}

void updateLED() {
  if (measuring) {
    if (millis() - lastBlink > 500) {
      lastBlink = millis();
      ledState = !ledState;
      digitalWrite(LED_PIN, ledState);
    }
  } else {
    digitalWrite(LED_PIN, LOW);
  }
}

void readSensor() {
  uint32_t irValue = particleSensor.getIR();
  
  if (irValue > 50000) {  // Finger detected
    // Heart rate from beat detection
    if (checkForBeat(irValue) == true) {
      long delta = millis() - lastBeat;
      lastBeat = millis();
      beatsPerMinute = 60 / (delta / 1000.0);
      
      if (beatsPerMinute < 255 && beatsPerMinute > 20) {
        rates[rateSpot++] = (byte)beatsPerMinute;
        rateSpot %= RATE_SIZE;
        
        beatAvg = 0;
        for (byte x = 0; x < RATE_SIZE; x++)
          beatAvg += rates[x];
        beatAvg /= RATE_SIZE;
      }
    }
    
    // Simple SpO2 estimation from IR/Red ratio
    uint32_t redValue = particleSensor.getRed();
    int32_t spo2 = estimateSpO2(irValue, redValue);
    
    updateDisplay(beatAvg, spo2, irValue, redValue);
  }
  else {
    display.clearDisplay();
    display.setCursor(0,0);
    display.setTextSize(2);
    display.println(F("No"));
    display.println(F("Finger"));
    display.display();
  }
}

// Simple SpO2 estimation without large buffers
int32_t estimateSpO2(uint32_t ir, uint32_t red) {
  if (ir == 0 || red == 0) return 0;
  
  // Ratio of ratios approximation
  float ratio = (float)red / (float)ir;
  
  // Empirical formula (simplified)
  // Real SpO2 is calculated with AC/DC components over time
  // This is a rough estimate for demo purposes
  float spo2 = 110.0 - (25.0 * ratio);
  
  if (spo2 > 100) spo2 = 100;
  if (spo2 < 70) spo2 = 70;
  
  return (int32_t)spo2;
}

void updateDisplay(int bpm, int32_t spo2_val, uint32_t ir, uint32_t red) {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  
  switch(displayMode) {
    case 0:  // BPM + SpO2
      display.setTextSize(2);
      if (bpm > 0) display.print(bpm);
      else display.print(F("--"));
      display.setTextSize(1);
      display.println(F(" BPM"));
      display.println();
      display.setTextSize(2);
      if (spo2_val > 0) display.print(spo2_val);
      else display.print(F("--"));
      display.setTextSize(1);
      display.println(F(" % SpO2"));
      break;
      
    case 1:  // Graph view
      display.println(F("Heart Rate Graph"));
      display.drawRect(0, 20, 128, 40, SSD1306_WHITE);
      if (bpm > 0) {
        int barHeight = map(constrain(bpm, 40, 180), 40, 180, 0, 38);
        display.fillRect(2, 58 - barHeight, 124, barHeight, SSD1306_WHITE);
      }
      break;
      
    case 2:  // Raw values
      display.println(F("Raw Sensor Data"));
      display.print(F("IR: ")); display.println(ir);
      display.print(F("Red: ")); display.println(red);
      display.print(F("BPM: ")); display.println(bpm);
      display.print(F("SpO2: ")); display.println(spo2_val);
      break;
  }
  
  display.display();
}

void showIdleScreen() {
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate > 500) {
    lastUpdate = millis();
    display.clearDisplay();
    display.setCursor(0, 0);
    display.setTextSize(2);
    display.println(F("Ready"));
    display.setTextSize(1);
    display.println();
    display.println(F("Press SW1 to start"));
    display.println();
    display.print(F("Mode: "));
    switch(displayMode) {
      case 0: display.print(F("BPM+SpO2")); break;
      case 1: display.print(F("Graph")); break;
      case 2: display.print(F("Raw Data")); break;
    }
    display.display();
  }
}