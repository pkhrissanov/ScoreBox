#include <Arduino.h>

// Red weapon lines
#define RED_MID_PIN    36   // VP / GPIO36
#define RED_CLOSE_PIN  39   // VN / GPIO39
#define RED_FAR_PIN    34   // GPIO34

#define BAUDRATE 115200
#define SAMPLES  32

int readAvg(int pin) {
  long total = 0;
  for (int i = 0; i < SAMPLES; i++) {
    total += analogRead(pin);
    delayMicroseconds(150);
  }
  return total / SAMPLES;
}

int to10bit(int raw12) {
  return map(raw12, 0, 4095, 0, 1023);
}

String classify10bit(int v) {
  if (v > 400 && v < 600) return "MID / likely contact range";
  if (v > 900) return "HIGH";
  if (v < 100) return "LOW";
  return "FLOAT / unknown";
}

void printLine(const char* name, int raw12) {
  int v10 = to10bit(raw12);

  Serial.print(name);
  Serial.print(" raw12=");
  Serial.print(raw12);

  Serial.print("  old10=");
  Serial.print(v10);

  Serial.print("  ");
  Serial.println(classify10bit(v10));
}

void setup() {
  Serial.begin(BAUDRATE);
  delay(1000);

  analogReadResolution(12);

  analogSetPinAttenuation(RED_MID_PIN, ADC_11db);
  analogSetPinAttenuation(RED_CLOSE_PIN, ADC_11db);
  analogSetPinAttenuation(RED_FAR_PIN, ADC_11db);

  pinMode(RED_MID_PIN, INPUT);
  pinMode(RED_CLOSE_PIN, INPUT);
  pinMode(RED_FAR_PIN, INPUT);

  Serial.println("# ScoreBox red-line ADC calibration");
  Serial.println("# MID=VP/GPIO36, CLOSE=VN/GPIO39, FAR=GPIO34");
  Serial.println("# Compare old10 values to old machine thresholds: mid ~= 400-600");
}

void loop() {
  int midRaw   = readAvg(RED_MID_PIN);
  int closeRaw = readAvg(RED_CLOSE_PIN);
  int farRaw   = readAvg(RED_FAR_PIN);

  Serial.println("================================");

  printLine("MID   VP/GPIO36: ", midRaw);
  printLine("CLOSE VN/GPIO39: ", closeRaw);
  printLine("FAR   GPIO34:    ", farRaw);

  delay(250);
}