#include <Arduino.h>

#define BAUDRATE 115200

// =====================
// RED weapon inputs
// =====================
#define RED_MID_PIN    36   // SENSOR_VP
#define RED_CLOSE_PIN  39   // SENSOR_VN
#define RED_GND_PIN    35

// =====================
// GREEN weapon inputs
// =====================
#define GREEN_MID_PIN    32
#define GREEN_GND_PIN    33
#define GREEN_CLOSE_PIN  34

// Optional: print rate
#define PRINT_INTERVAL_MS 100

unsigned long lastPrint = 0;

int to10bit(int raw12) {
  return map(raw12, 0, 4095, 0, 1023);
}

const char* classify10bit(int v) {
  if (v > 400 && v < 600) return "MID";
  if (v > 900) return "HIGH";
  if (v < 100) return "LOW";
  return "FLOAT";
}

void printReading(const char* label, int raw12) {
  int old10 = to10bit(raw12);

  Serial.print(label);
  Serial.print(" raw12=");
  Serial.print(raw12);



  Serial.print(" state=");
  Serial.print(classify10bit(old10));
  Serial.print("   ");
}

void setup() {
  Serial.begin(BAUDRATE);
  delay(1000);

  analogReadResolution(12);

  // ADC range setting
  analogSetPinAttenuation(RED_MID_PIN, ADC_11db);
  analogSetPinAttenuation(RED_CLOSE_PIN, ADC_11db);
  analogSetPinAttenuation(RED_GND_PIN, ADC_11db);

  analogSetPinAttenuation(GREEN_MID_PIN, ADC_11db);
  analogSetPinAttenuation(GREEN_GND_PIN, ADC_11db);
  analogSetPinAttenuation(GREEN_CLOSE_PIN, ADC_11db);

  pinMode(RED_MID_PIN, INPUT);
  pinMode(RED_CLOSE_PIN, INPUT);
  pinMode(RED_GND_PIN, INPUT);

  pinMode(GREEN_MID_PIN, INPUT);
  pinMode(GREEN_GND_PIN, INPUT);
  pinMode(GREEN_CLOSE_PIN, INPUT);

  Serial.println();
  Serial.println("# ScoreBox weapon input calibration");
  Serial.println("# Baud: 115200");
  Serial.println("# raw12 = ESP32 0-4095 reading");
  Serial.println("# old10 = converted 0-1023 reading to compare with old machine thresholds");
  Serial.println("# Old threshold: MID roughly 400-600");
  Serial.println();
}

void loop() {
  // Read as close together as possible
  int redMid      = analogRead(RED_MID_PIN);
  int redClose    = analogRead(RED_CLOSE_PIN);
  int redGnd      = analogRead(RED_GND_PIN);

  int greenMid    = analogRead(GREEN_MID_PIN);
  int greenGnd    = analogRead(GREEN_GND_PIN);
  int greenClose  = analogRead(GREEN_CLOSE_PIN);

  if (millis() - lastPrint >= PRINT_INTERVAL_MS) {
    lastPrint = millis();

    Serial.println("================================================================");

    Serial.print("RED:   ");
    printReading("MID", redMid);
    printReading("CLOSE", redClose);
    printReading("GND", redGnd);
    Serial.println();

    Serial.print("GREEN: ");
    printReading("MID", greenMid);
    printReading("CLOSE", greenClose);
    printReading("GND", greenGnd);
    Serial.println();
  }
}