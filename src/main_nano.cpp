#include <Arduino.h>
#include <FastLED.h>

#define DEBUG 1
#define BUZZERTIME  1500  // ms
#define LIGHTTIME   3500  // ms (starts AFTER lockout ends)
#define BAUDRATE   115200

//======================
// WS2812B MATRIX SETUP
//======================
#define LED_TYPE     WS2812B
#define COLOR_ORDER  GRB
#define BRIGHTNESS   70

#define MATRIX_W 8
#define MATRIX_H 8
#define NUM_LEDS_MATRIX (MATRIX_W * MATRIX_H)

// Data pins
const uint8_t GREEN_MATRIX_PIN = 9;   // Green fencer (A)
const uint8_t RED_MATRIX_PIN   = 12;  // Red fencer (B)

CRGB greenMatrix[NUM_LEDS_MATRIX];
CRGB redMatrix[NUM_LEDS_MATRIX];

//====================
// Pin Setup
//====================
const uint8_t shortLEDA  =  8;
const uint8_t shortLEDB  = 13;

// Analog pins
const uint8_t groundPinA = A0;
const uint8_t weaponPinA = A1;
const uint8_t lamePinA   = A2;
const uint8_t lamePinB   = A3;
const uint8_t weaponPinB = A4;
const uint8_t groundPinB = A5;

// Mode + buzzer + mode LEDs
const uint8_t modePin    = 2;
const uint8_t buzzerPin  = 3;
const uint8_t modeLeds[] = {4, 5, 6}; // {foil, epee, sabre}

//=========================
// values of analog reads
//=========================
int weaponA = 0;
int weaponB = 0;
int lameA   = 0;
int lameB   = 0;
int groundA = 0;
int groundB = 0;

bool stripGroundA = false;
bool stripGroundB = false;

//==========================
// Lockout & Depress Times
//==========================
//                         foil   epee   sabre
const unsigned long lockout[] = {300000UL, 45000UL, 170000UL}; // microseconds
const unsigned long depress[] = { 14000UL,  2000UL,   1000UL}; // microseconds

//=================
// mode constants
//=================
const uint8_t FOIL_MODE  = 0;
const uint8_t EPEE_MODE  = 1;
const uint8_t SABRE_MODE = 2;

uint8_t currentMode = EPEE_MODE;
volatile bool modeJustChangedFlag = false;

//=========
// states
//=========
bool depressedA  = false;
bool depressedB  = false;
bool hitOnTargA  = false;
bool hitOffTargA = false;
bool hitOnTargB  = false;
bool hitOffTargB = false;

// Short circuit flags
bool shortAFlag = false;
bool shortBFlag = false;

// Candidate hit timing
unsigned long depressAtime = 0;
unsigned long depressBtime = 0;

//=============================
// Scoring cycle state
//=============================
bool scoringActive      = false;
bool displayHoldActive  = false;
unsigned long lockoutStartUs = 0;
unsigned long displayStartMs = 0;
unsigned long buzzerStartMs  = 0;
bool buzzerActive = false;

//======================
// Matrix helper funcs
//======================
uint16_t XY(uint8_t x, uint8_t y) {
  if (y % 2 == 0) {
    return y * MATRIX_W + x;
  } else {
    return y * MATRIX_W + (MATRIX_W - 1 - x);
  }
}

void matricesClear() {
  fill_solid(greenMatrix, NUM_LEDS_MATRIX, CRGB::Black);
  fill_solid(redMatrix,   NUM_LEDS_MATRIX, CRGB::Black);
}

void matricesRenderHits() {
  matricesClear();

  if (hitOnTargA)  fill_solid(greenMatrix, NUM_LEDS_MATRIX, CRGB::Green);
  if (hitOffTargA) fill_solid(greenMatrix, NUM_LEDS_MATRIX, CRGB::White);

  if (hitOnTargB)  fill_solid(redMatrix, NUM_LEDS_MATRIX, CRGB::Red);
  if (hitOffTargB) fill_solid(redMatrix, NUM_LEDS_MATRIX, CRGB::White);

  uint16_t c1 = XY(3, 3);
  uint16_t c2 = XY(4, 3);
  uint16_t c3 = XY(3, 4);
  uint16_t c4 = XY(4, 4);

  if (!hitOnTargA && !hitOffTargA && (shortAFlag || stripGroundA)) {
    greenMatrix[c1] = CRGB::Yellow;
    greenMatrix[c2] = CRGB::Yellow;
    greenMatrix[c3] = CRGB::Yellow;
    greenMatrix[c4] = CRGB::Yellow;
  }

  if (!hitOnTargB && !hitOffTargB && (shortBFlag || stripGroundB)) {
    redMatrix[c1] = CRGB::Yellow;
    redMatrix[c2] = CRGB::Yellow;
    redMatrix[c3] = CRGB::Yellow;
    redMatrix[c4] = CRGB::Yellow;
  }

  FastLED.show();
}

void matricesTest() {
  matricesClear();
  FastLED.show();

  fill_solid(greenMatrix, NUM_LEDS_MATRIX, CRGB::Green);
  FastLED.show();
  delay(150);

  matricesClear();
  FastLED.show();
  delay(80);

  fill_solid(redMatrix, NUM_LEDS_MATRIX, CRGB::Red);
  FastLED.show();
  delay(150);

  matricesClear();
  FastLED.show();
  delay(80);

  fill_solid(greenMatrix, NUM_LEDS_MATRIX, CRGB::White);
  fill_solid(redMatrix,   NUM_LEDS_MATRIX, CRGB::White);
  FastLED.show();
  delay(250);

  matricesClear();
  FastLED.show();
}

//=====================
// Mode pin interrupt
//=====================
void changeMode() {
  modeJustChangedFlag = true;
}

void buzzShort() {
  digitalWrite(buzzerPin, HIGH);
  delay(100);
  digitalWrite(buzzerPin, LOW);
}

void beep() {
  digitalWrite(buzzerPin, HIGH);
  delay(500);
  digitalWrite(buzzerPin, LOW);
}

//============================
// Helper threshold checks
//============================
bool isMidFoilEpee(int v) {
  return (v > 400 && v < 600);
}

bool isMidSabreWeapon(int v) {
  return (v > 315 && v < 600);
}

bool isMidSabreLame(int v) {
  return (v > 300 && v < 600);
}

// foil/sabre self-short: blade touching own lame
bool foilShortA() {
  return isMidFoilEpee(weaponA) && isMidFoilEpee(lameA);
}

bool foilShortB() {
  return isMidFoilEpee(weaponB) && isMidFoilEpee(lameB);
}

bool sabreShortA() {
  return isMidSabreWeapon(weaponA) && isMidSabreLame(lameA);
}

bool sabreShortB() {
  return isMidSabreWeapon(weaponB) && isMidSabreLame(lameB);
}

void updateShortIndicators() {
  switch (currentMode) {
    case FOIL_MODE:
      shortAFlag = foilShortA();
      shortBFlag = foilShortB();
      break;

    case EPEE_MODE:
      // epee short flags are handled in epee()
      break;

    case SABRE_MODE:
      shortAFlag = sabreShortA();
      shortBFlag = sabreShortB();
      break;
  }

  digitalWrite(shortLEDA, shortAFlag ? HIGH : LOW);
  digitalWrite(shortLEDB, shortBFlag ? HIGH : LOW);
}

//============================
// Sets the correct mode led
//============================
void setModeLeds() {
  digitalWrite(modeLeds[0], LOW);
  digitalWrite(modeLeds[1], LOW);
  digitalWrite(modeLeds[2], LOW);

  digitalWrite(modeLeds[currentMode], HIGH);
  buzzShort();
  delay(500);
}

void sendHitMessageA(bool onTarget) {
  Serial.println(onTarget ? "GH" : "GM");
}

void sendHitMessageB(bool onTarget) {
  Serial.println(onTarget ? "RH" : "RM");
}

unsigned long currentLockoutTime() {
  return lockout[currentMode];
}

bool lockoutWindowOpen() {
  if (!scoringActive) return false;
  return (micros() - lockoutStartUs) < currentLockoutTime();
}

void startBuzzer() {
  digitalWrite(buzzerPin, HIGH);
  buzzerStartMs = millis();
  buzzerActive = true;
}

void stopBuzzerIfNeeded() {
  if (buzzerActive && (millis() - buzzerStartMs >= BUZZERTIME)) {
    digitalWrite(buzzerPin, LOW);
    buzzerActive = false;
  }
}

void resetHitStateOnly() {
  hitOnTargA  = false;
  hitOffTargA = false;
  hitOnTargB  = false;
  hitOffTargB = false;

  depressedA = false;
  depressedB = false;
  depressAtime = 0;
  depressBtime = 0;

  scoringActive = false;
  displayHoldActive = false;
  lockoutStartUs = 0;
  displayStartMs = 0;

  digitalWrite(buzzerPin, LOW);
  buzzerActive = false;
}

void fullReset() {
  resetHitStateOnly();

  digitalWrite(shortLEDA, LOW);
  digitalWrite(shortLEDB, LOW);
  shortAFlag = false;
  shortBFlag = false;

  matricesClear();
  FastLED.show();
}

void registerHitA(bool onTarget) {
  if (onTarget) {
    if (hitOnTargA) return;
    hitOnTargA = true;
  } else {
    if (hitOffTargA) return;
    hitOffTargA = true;
  }

  if (!scoringActive) {
    scoringActive = true;
    displayHoldActive = false;
    lockoutStartUs = micros();
    startBuzzer();
  }

  sendHitMessageA(onTarget);
  matricesRenderHits();
}

void registerHitB(bool onTarget) {
  if (onTarget) {
    if (hitOnTargB) return;
    hitOnTargB = true;
  } else {
    if (hitOffTargB) return;
    hitOffTargB = true;
  }

  if (!scoringActive) {
    scoringActive = true;
    displayHoldActive = false;
    lockoutStartUs = micros();
    startBuzzer();
  }

  sendHitMessageB(onTarget);
  matricesRenderHits();
}

void updateScoringCycle() {
  stopBuzzerIfNeeded();

  if (!scoringActive) return;

  if (!displayHoldActive && !lockoutWindowOpen()) {
    displayHoldActive = true;
    displayStartMs = millis();
  }

  if (displayHoldActive && (millis() - displayStartMs >= LIGHTTIME)) {
    fullReset();
  }
}

//========================
// Run when mode changed
//========================
void checkIfModeChanged() {
  if (modeJustChangedFlag) {
    if (currentMode == SABRE_MODE) currentMode = FOIL_MODE;
    else currentMode++;

    setModeLeds();

#if DEBUG
    Serial.print("# Mode changed to: ");
    switch (currentMode) {
      case FOIL_MODE:  Serial.println("Foil");  break;
      case EPEE_MODE:  Serial.println("Epee");  break;
      case SABRE_MODE: Serial.println("Sabre"); break;
    }
#endif

    modeJustChangedFlag = false;
    fullReset();

    unsigned long currentMillis = millis();
    while ((millis() - currentMillis) < 300) { }
  }
}

//===================
// Main foil method
//===================
void foil() {
  // foil short = own weapon touching own lame
  shortAFlag = foilShortA();
  shortBFlag = foilShortB();
  digitalWrite(shortLEDA, shortAFlag ? HIGH : LOW);
  digitalWrite(shortLEDB, shortBFlag ? HIGH : LOW);

  if (!hitOnTargA && !hitOffTargA) {
    if (weaponA > 900 && lameB < 100 && !stripGroundA) {
      if (!depressedA) {
        depressAtime = micros();
        depressedA = true;
      } else if (micros() - depressAtime >= depress[FOIL_MODE]) {
        registerHitA(false);
      }
    }
    else if (weaponA > 400 && weaponA < 600 &&
             lameB   > 400 && lameB   < 600 &&
             !stripGroundA) {
      if (!depressedA) {
        depressAtime = micros();
        depressedA = true;
      } else if (micros() - depressAtime >= depress[FOIL_MODE]) {
        registerHitA(true);
      }
    } else {
      depressAtime = 0;
      depressedA = false;
    }
  }

  if (!hitOnTargB && !hitOffTargB) {
    if (weaponB > 900 && lameA < 100 && !stripGroundB) {
      if (!depressedB) {
        depressBtime = micros();
        depressedB = true;
      } else if (micros() - depressBtime >= depress[FOIL_MODE]) {
        registerHitB(false);
      }
    }
    else if (weaponB > 400 && weaponB < 600 &&
             lameA   > 400 && lameA   < 600 &&
             !stripGroundB) {
      if (!depressedB) {
        depressBtime = micros();
        depressedB = true;
      } else if (micros() - depressBtime >= depress[FOIL_MODE]) {
        registerHitB(true);
      }
    } else {
      depressBtime = 0;
      depressedB = false;
    }
  }
}

//===================
// Main epee method
//===================
void epee() {
  if (!hitOnTargA) {
    if (weaponA > 400 && weaponA < 600 &&
        lameA   > 400 && lameA   < 600 &&
        !stripGroundA) {
      if (!depressedA) {
        depressAtime = micros();
        depressedA = true;
      } else if (micros() - depressAtime >= depress[EPEE_MODE]) {
        registerHitA(true);
      }
      shortAFlag = false;
    } else {
      shortAFlag = (abs(weaponA - lameA) < 40 && (weaponA < 400 || weaponA > 600));
      digitalWrite(shortLEDA, shortAFlag ? HIGH : LOW);

      depressAtime = 0;
      depressedA = false;
    }
  } else {
    digitalWrite(shortLEDA, LOW);
  }

  if (!hitOnTargB) {
    if (weaponB > 400 && weaponB < 600 &&
        lameB   > 400 && lameB   < 600 &&
        !stripGroundB) {
      if (!depressedB) {
        depressBtime = micros();
        depressedB = true;
      } else if (micros() - depressBtime >= depress[EPEE_MODE]) {
        registerHitB(true);
      }
      shortBFlag = false;
    } else {
      shortBFlag = (abs(weaponB - lameB) < 40 && (weaponB < 400 || weaponB > 600));
      digitalWrite(shortLEDB, shortBFlag ? HIGH : LOW);

      depressBtime = 0;
      depressedB = false;
    }
  } else {
    digitalWrite(shortLEDB, LOW);
  }
}

//===================
// Main sabre method
//===================
void sabre() {
  // sabre short = own weapon touching own lame
  shortAFlag = sabreShortA();
  shortBFlag = sabreShortB();
  digitalWrite(shortLEDA, shortAFlag ? HIGH : LOW);
  digitalWrite(shortLEDB, shortBFlag ? HIGH : LOW);

  if (!hitOnTargA && !hitOffTargA) {
    if (weaponA > 315 && weaponA < 600 &&
        lameB   > 300 && lameB   < 600 &&
        !stripGroundA) {
      if (!depressedA) {
        depressAtime = micros();
        depressedA = true;
      } else if (micros() - depressAtime >= depress[SABRE_MODE]) {
        registerHitA(true);
      }
    } else {
      depressAtime = 0;
      depressedA = false;
    }
  }

  if (!hitOnTargB && !hitOffTargB) {
    if (weaponB > 315 && weaponB < 600 &&
        lameA   > 300 && lameA   < 600 &&
        !stripGroundB) {
      if (!depressedB) {
        depressBtime = micros();
        depressedB = true;
      } else if (micros() - depressBtime >= depress[SABRE_MODE]) {
        registerHitB(true);
      }
    } else {
      depressBtime = 0;
      depressedB = false;
    }
  }
}

//==============
// Test lights
//==============
void testLights() {
  matricesTest();

  digitalWrite(modeLeds[1], HIGH);
  delay(100);
  digitalWrite(modeLeds[1], LOW);

  digitalWrite(modeLeds[0], HIGH);
  delay(100);
  digitalWrite(modeLeds[0], LOW);

  digitalWrite(modeLeds[2], HIGH);
  delay(100);
  digitalWrite(modeLeds[2], LOW);

  buzzShort();
}

// Optional troubleshooting
void status() {
  Serial.println("======================================");
  Serial.print(" hitOnTargA :"); Serial.println(hitOnTargA);
  Serial.print("hitOffTargA :"); Serial.println(hitOffTargA);
  Serial.print(" hitOnTargB :"); Serial.println(hitOnTargB);
  Serial.print("hitOffTargB :"); Serial.println(hitOffTargB);
  Serial.print(" shortAFlag :"); Serial.println(shortAFlag);
  Serial.print(" shortBFlag :"); Serial.println(shortBFlag);
  Serial.print(" scoringAct :"); Serial.println(scoringActive);
  Serial.print(" lockoutOpen :"); Serial.println(lockoutWindowOpen());
  Serial.print("    weaponA  :"); Serial.println(weaponA);
  Serial.print("    weaponB  :"); Serial.println(weaponB);
  Serial.print("    lameA    :"); Serial.println(lameA);
  Serial.print("    lameB    :"); Serial.println(lameB);
  Serial.print("    groundA  :"); Serial.println(groundA);
  Serial.print("    groundB  :"); Serial.println(groundB);
  Serial.println("======================================");
  delay(1000);
}

//================
// Configuration
//================
void setup() {
  Serial.begin(BAUDRATE);

  pinMode(modePin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(modePin), changeMode, FALLING);

  pinMode(modeLeds[0], OUTPUT);
  pinMode(modeLeds[1], OUTPUT);
  pinMode(modeLeds[2], OUTPUT);

  pinMode(shortLEDA, OUTPUT);
  pinMode(shortLEDB, OUTPUT);
  pinMode(buzzerPin, OUTPUT);
  digitalWrite(buzzerPin, LOW);

  FastLED.addLeds<LED_TYPE, GREEN_MATRIX_PIN, COLOR_ORDER>(greenMatrix, NUM_LEDS_MATRIX);
  FastLED.addLeds<LED_TYPE, RED_MATRIX_PIN,   COLOR_ORDER>(redMatrix,   NUM_LEDS_MATRIX);
  FastLED.setBrightness(BRIGHTNESS);

  matricesClear();
  FastLED.show();

  testLights();

  digitalWrite(modeLeds[currentMode], HIGH);

  Serial.println("#ScoreBox");
  Serial.print("# Mode : ");
  Serial.println(currentMode);

  fullReset();

  unsigned long startloop = millis();
  uint8_t startMode = currentMode;

  digitalWrite(modeLeds[0], LOW);
  digitalWrite(modeLeds[1], LOW);
  digitalWrite(modeLeds[2], LOW);
  digitalWrite(modeLeds[currentMode], HIGH);

  while (millis() - startloop <= 5000) {
    if (modeJustChangedFlag) {
      if (currentMode == SABRE_MODE) currentMode = FOIL_MODE;
      else currentMode++;

      setModeLeds();
      startloop = millis();

      Serial.print("# Mode changed to: ");
      switch (currentMode) {
        case FOIL_MODE:  Serial.println("Foil");  break;
        case EPEE_MODE:  Serial.println("Epee");  break;
        case SABRE_MODE: Serial.println("Sabre"); break;
      }

      modeJustChangedFlag = false;

      unsigned long currentMillis = millis();
      while ((millis() - currentMillis) < 500) { }
    }

    if (currentMode != startMode) {
      digitalWrite(modeLeds[0], LOW);
      digitalWrite(modeLeds[1], LOW);
      digitalWrite(modeLeds[2], LOW);
      digitalWrite(modeLeds[currentMode], HIGH);
      buzzShort();
    }
    startMode = currentMode;
  }
}

//============
// Main Loop
//============
void loop() {
  beep();
  digitalWrite(modeLeds[currentMode], HIGH);

  Serial.println("# Starting.........");

  while (1) {
    checkIfModeChanged();

    weaponA = analogRead(weaponPinA);
    weaponB = analogRead(weaponPinB);
    lameA   = analogRead(lamePinA);
    lameB   = analogRead(lamePinB);
    groundA = analogRead(groundPinA);
    groundB = analogRead(groundPinB);

    stripGroundA = (groundA > 400 && groundA < 600);
    stripGroundB = (groundB > 400 && groundB < 600);

    if (!scoringActive || lockoutWindowOpen()) {
      if (currentMode == FOIL_MODE) {
        foil();
      } else if (currentMode == EPEE_MODE) {
        epee();
      } else if (currentMode == SABRE_MODE) {
        sabre();
      }
    } else {
      // keep foil/sabre short indicators alive even while not re-running hit logic
      if (currentMode == FOIL_MODE || currentMode == SABRE_MODE) {
        updateShortIndicators();
      }
    }

    matricesRenderHits();
    updateScoringCycle();
  }
}