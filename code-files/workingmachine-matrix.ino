//===========================================================================//
//                                                                           //
//  Desc:    Arduino Code to implement a fencing scoring apparatus           //
//  Dev:     Wnew                                                            //
//  Date:    Nov  2012                                                       //
//  Updated: Sept 2015                                                       //
//                                                                           //
//  Alisdair updates etc...                                                  //
//  Modified (2026): WS2812B 8x8 matrices for Red/Green hit indicators       //
//                   (2 matrices total, one data pin each)                   //
//===========================================================================//

#define DEBUG 1x
#define BUZZERTIME  1500  // ms
#define LIGHTTIME   3500  // ms
#define BAUDRATE   115200

//======================
// WS2812B MATRIX SETUP
//======================
#include <FastLED.h>

#define LED_TYPE     WS2812B
#define COLOR_ORDER  GRB
#define BRIGHTNESS   70

#define MATRIX_W 8
#define MATRIX_H 8
#define NUM_LEDS_MATRIX (MATRIX_W * MATRIX_H)  // 64

// Data pins (CONNECT MATRICES HERE)
const uint8_t GREEN_MATRIX_PIN = 9;   // Green fencer (A)
const uint8_t RED_MATRIX_PIN   = 12;  // Red fencer (B)

CRGB greenMatrix[NUM_LEDS_MATRIX];
CRGB redMatrix[NUM_LEDS_MATRIX];

//====================
// Pin Setup (rest)
//====================
const uint8_t shortLEDA  =  8;    // Short Circuit A small LED (kept)
const uint8_t shortLEDB  = 13;    // Short Circuit B small LED (kept)

// Analog pins
const uint8_t groundPinA = A0;
const uint8_t weaponPinA = A1;
const uint8_t lamePinA   = A2;
const uint8_t lamePinB   = A3;
const uint8_t weaponPinB = A4;
const uint8_t groundPinB = A5;

// Mode + buzzer + mode LEDs (small LEDs)
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

//=======================
// depress and timeouts
//=======================
long depressAtime = 0;
long depressBtime = 0;
bool lockedOut    = false;

//==========================
// Lockout & Depress Times
//==========================
//                         foil   epee   sabre
const long lockout [] = {300000,  45000, 170000};
const long depress [] = { 14000,   2000,   1000};

//=================
// mode constants
//=================
const uint8_t FOIL_MODE  = 0;
const uint8_t EPEE_MODE  = 1;
const uint8_t SABRE_MODE = 2;

uint8_t currentMode = EPEE_MODE;
bool modeJustChangedFlag = false;

//=========
// states
//=========
boolean depressedA  = false;
boolean depressedB  = false;
boolean hitOnTargA  = false;
boolean hitOffTargA = false;
boolean hitOnTargB  = false;
boolean hitOffTargB = false;

// Short circuit flags (so we can display without spamming FastLED.show in the fast loop)
bool shortAFlag = false;
bool shortBFlag = false;

//======================
// Matrix helper funcs
//======================
void matricesClear() {
  fill_solid(greenMatrix, NUM_LEDS_MATRIX, CRGB::Black);
  fill_solid(redMatrix,   NUM_LEDS_MATRIX, CRGB::Black);
}

// Very simple display:
// - On-target: solid team color
// - Off-target: solid white
// - Short: yellow (overrides)
void matricesRenderHits() {
  matricesClear();

  // Green/A
  if (hitOnTargA)  fill_solid(greenMatrix, NUM_LEDS_MATRIX, CRGB::Green);
  if (hitOffTargA) fill_solid(greenMatrix, NUM_LEDS_MATRIX, CRGB::White);
  if (shortAFlag)  fill_solid(greenMatrix, NUM_LEDS_MATRIX, CRGB::Yellow);

  // Red/B
  if (hitOnTargB)  fill_solid(redMatrix, NUM_LEDS_MATRIX, CRGB::Red);
  if (hitOffTargB) fill_solid(redMatrix, NUM_LEDS_MATRIX, CRGB::White);
  if (shortBFlag)  fill_solid(redMatrix, NUM_LEDS_MATRIX, CRGB::Yellow);

  FastLED.show();
}

// Quick flash test patterns
void matricesTest() {
  matricesClear(); FastLED.show();

  fill_solid(greenMatrix, NUM_LEDS_MATRIX, CRGB::Green);
  FastLED.show(); delay(150);

  matricesClear(); FastLED.show(); delay(80);

  fill_solid(redMatrix, NUM_LEDS_MATRIX, CRGB::Red);
  FastLED.show(); delay(150);

  matricesClear(); FastLED.show(); delay(80);

  fill_solid(greenMatrix, NUM_LEDS_MATRIX, CRGB::White);
  fill_solid(redMatrix,   NUM_LEDS_MATRIX, CRGB::White);
  FastLED.show(); delay(250);

  matricesClear(); FastLED.show();
}

//================
// Configuration
//================
void setup() {
  Serial.begin(BAUDRATE);

  pinMode(modePin, INPUT_PULLUP);
  attachInterrupt(modePin - 2, changeMode, FALLING);

  pinMode(modeLeds[0], OUTPUT);
  pinMode(modeLeds[1], OUTPUT);
  pinMode(modeLeds[2], OUTPUT);

  pinMode(shortLEDA, OUTPUT);
  pinMode(shortLEDB, OUTPUT);
  pinMode(buzzerPin, OUTPUT);

  // Init matrices
  FastLED.addLeds<LED_TYPE, GREEN_MATRIX_PIN, COLOR_ORDER>(greenMatrix, NUM_LEDS_MATRIX);
  FastLED.addLeds<LED_TYPE, RED_MATRIX_PIN,   COLOR_ORDER>(redMatrix,   NUM_LEDS_MATRIX);
  FastLED.setBrightness(BRIGHTNESS);
  matricesClear();
  FastLED.show();

  testLights();  // now tests matrices + mode LEDs

  digitalWrite(modeLeds[currentMode], HIGH);

  Serial.println("# WLFC 3 Weapon Scoring Box");
  Serial.println("# =========================");
  Serial.println();
  Serial.println("version: 9 March 2021 (modified for WS2812B matrices)");
  Serial.println();

  Serial.print("# Mode : ");
  Serial.println(currentMode);

  resetValues();

  // Choose mode before starting (5 seconds)
  unsigned long startloop = millis();
  uint8_t startMode = currentMode;

  digitalWrite(modeLeds[0], LOW);
  digitalWrite(modeLeds[1], LOW);
  digitalWrite(modeLeds[2], LOW);
  digitalWrite(modeLeds[currentMode], HIGH);

  while (millis() - startloop <= 5000) {
    if (modeJustChangedFlag) {
      if (currentMode == 2) currentMode = 0;
      else currentMode++;

      setModeLeds();
      startloop = millis();

      Serial.print("# Mode changed to: ");
      switch (currentMode) {
        case 0: Serial.println("Foil");  break;
        case 1: Serial.println("Epee");  break;
        case 2: Serial.println("Sabre"); break;
      }

      modeJustChangedFlag = false;
      unsigned long currentMillis = millis();
      while ((millis() - currentMillis) < 1000) { }
    }

    if (currentMode != startMode) {
      digitalWrite(modeLeds[0], LOW);
      digitalWrite(modeLeds[1], LOW);
      digitalWrite(modeLeds[2], LOW);
      digitalWrite(modeLeds[currentMode], HIGH);
      buzz();
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

    signalHits();

    if      (currentMode == FOIL_MODE)  foil();
    else if (currentMode == EPEE_MODE)  epee();
    else if (currentMode == SABRE_MODE) sabre();
  }
}

//=====================
// Mode pin interrupt
//=====================
void changeMode() {
  modeJustChangedFlag = true;
}

//============================
// Sets the correct mode led
//============================
void setModeLeds() {
  digitalWrite(modeLeds[0], LOW);
  digitalWrite(modeLeds[1], LOW);
  digitalWrite(modeLeds[2], LOW);

  digitalWrite(modeLeds[currentMode], HIGH);
  buzz();
  delay(500);
}

//========================
// Run when mode changed
//========================
void checkIfModeChanged() {
  if (modeJustChangedFlag) {
    if (currentMode == 2) currentMode = 0;
    else currentMode++;

    setModeLeds();

#if DEBUG
    Serial.print("# Mode changed to: ");
    switch (currentMode) {
      case 0: Serial.println("Foil");  break;
      case 1: Serial.println("Epee");  break;
      case 2: Serial.println("Sabre"); break;
    }
#endif

    modeJustChangedFlag = false;
    unsigned long currentMillis = millis();
    while ((millis() - currentMillis) < 500) { }
  }
}

//===================
// Main foil method
//===================
void foil() {
  long now = micros();
  if (((hitOnTargA || hitOffTargA) && (depressAtime + lockout[0] < now)) ||
      ((hitOnTargB || hitOffTargB) && (depressBtime + lockout[0] < now))) {
    lockedOut = true;
  }

  // weapon A
  if (!hitOnTargA && !hitOffTargA) {
    // off target
    if (900 < weaponA && lameB < 100) {
      if (!depressedA) {
        depressAtime = micros();
        depressedA   = true;
      } else if (depressAtime + depress[0] <= micros()) {
        hitOffTargA = true;
      }
    } else {
      // on target
      if (400 < weaponA && weaponA < 600 && 400 < lameB && lameB < 600) {
        if (!depressedA) {
          depressAtime = micros();
          depressedA   = true;
        } else if (depressAtime + depress[0] <= micros()) {
          hitOnTargA = true;
        }
      } else {
        depressAtime = 0;
        depressedA   = 0;
      }
    }
  }

  // weapon B
  if (!hitOnTargB && !hitOffTargB) {
    // off target
    if (900 < weaponB && lameA < 100) {
      if (!depressedB) {
        depressBtime = micros();
        depressedB   = true;
      } else if (depressBtime + depress[0] <= micros()) {
        hitOffTargB = true;
      }
    } else {
      // on target
      if (400 < weaponB && weaponB < 600 && 400 < lameA && lameA < 600) {
        if (!depressedB) {
          depressBtime = micros();
          depressedB   = true;
        } else if (depressBtime + depress[0] <= micros()) {
          hitOnTargB = true;
        }
      } else {
        depressBtime = 0;
        depressedB   = 0;
      }
    }
  }
}

//===================
// Main epee method
//===================
void epee() {
  long now = micros();
  if ((hitOnTargA && (depressAtime + lockout[1] < now)) ||
      (hitOnTargB && (depressBtime + lockout[1] < now))) {
    lockedOut = true;
  }

  // weapon A
  if (!hitOnTargA) {
    if (400 < weaponA && weaponA < 600 && 400 < lameA && lameA < 600) {
      if (!depressedA) {
        depressAtime = micros();
        depressedA   = true;
      } else if (depressAtime + depress[1] <= micros()) {
        hitOnTargA = true;
      }
      shortAFlag = false;
    } else {
      // short-circuit detection (flag + small LED)
      shortAFlag = (abs(weaponA - lameA) < 40 && (weaponA < 400 || weaponA > 600));
      digitalWrite(shortLEDA, shortAFlag ? HIGH : LOW);

      if (depressedA) {
        depressAtime = 0;
        depressedA   = 0;
      }
    }
  }

  // weapon B
  if (!hitOnTargB) {
    if (400 < weaponB && weaponB < 600 && 400 < lameB && lameB < 600) {
      if (!depressedB) {
        depressBtime = micros();
        depressedB   = true;
      } else if (depressBtime + depress[1] <= micros()) {
        hitOnTargB = true;
      }
      shortBFlag = false;
    } else {
      shortBFlag = (abs(weaponB - lameB) < 40 && (weaponB < 400 || weaponB > 600));
      digitalWrite(shortLEDB, shortBFlag ? HIGH : LOW);

      if (depressedB) {
        depressBtime = 0;
        depressedB   = 0;
      }
    }
  }
}

//===================
// Main sabre method
//===================
void sabre() {
  long now = micros();
  if (((hitOnTargA || hitOffTargA) && (depressAtime + lockout[2] < now)) ||
      ((hitOnTargB || hitOffTargB) && (depressBtime + lockout[2] < now))) {
    lockedOut = true;
  }

  // weapon A (on target only)
  if (!hitOnTargA && !hitOffTargA) {
    if (315 < weaponA && weaponA < 600 && 300 < lameB && lameB < 600) {
      if (!depressedA) {
        depressAtime = micros();
        depressedA   = true;
      } else if (depressAtime + depress[2] <= micros()) {
        hitOnTargA = true;
      }
    } else {
      depressAtime = 0;
      depressedA   = 0;
    }
  }

  // weapon B (on target only)
  if (!hitOnTargB && !hitOffTargB) {
    if (315 < weaponB && weaponB < 600 && 300 < lameA && lameA < 600) {
      if (!depressedB) {
        depressBtime = micros();
        depressedB   = true;
      } else if (depressBtime + depress[2] <= micros()) {
        hitOnTargB = true;
      }
    } else {
      depressBtime = 0;
      depressedB   = 0;
    }
  }
}

//==============
// Signal Hits
//==============
void signalHits() {
  if (lockedOut) {
    writeDisplay();

    // Show the result on the matrices (single update)
    matricesRenderHits();

    digitalWrite(buzzerPin, HIGH);

    resetValues();
  }
}

//======================
// Reset all variables
//======================
void resetValues() {
  delay(BUZZERTIME);
  digitalWrite(buzzerPin, LOW);

  delay(LIGHTTIME - BUZZERTIME);

  // Clear matrices
  matricesClear();
  FastLED.show();

  // Turn off short LEDs
  digitalWrite(shortLEDA, LOW);
  digitalWrite(shortLEDB, LOW);
  shortAFlag = false;
  shortBFlag = false;

  lockedOut    = false;
  depressAtime = 0;
  depressedA   = false;
  depressBtime = 0;
  depressedB   = false;

  hitOnTargA  = false;
  hitOffTargA = false;
  hitOnTargB  = false;
  hitOffTargB = false;

  delay(100);
}

//==============
// Test lights
//==============
void testLights() {
  // Test matrices
  matricesTest();

  // Test mode LEDs
  digitalWrite(modeLeds[1], HIGH); delay(100); digitalWrite(modeLeds[1], LOW);
  digitalWrite(modeLeds[0], HIGH); delay(100); digitalWrite(modeLeds[0], LOW);
  digitalWrite(modeLeds[2], HIGH); delay(100); digitalWrite(modeLeds[2], LOW);

  buzz();
}

void buzz() {
  tone(buzzerPin, 500, 100);
}
void beep() {
  tone(buzzerPin, 1000, 500);
}

// Writes two-character hit codes to serial for wireless display
void writeDisplay() {
  if (hitOnTargA)  Serial.println("GH");
  if (hitOffTargA) Serial.println("GM");
  if (hitOffTargB) Serial.println("RM");
  if (hitOnTargB)  Serial.println("RH");
}

// Optional troubleshooting status dump
void status() {
  Serial.println("======================================");
  Serial.print(" hitOnTargA :"); Serial.println(hitOnTargA);
  Serial.print("hitOffTargA :"); Serial.println(hitOffTargA);
  Serial.print(" hitOnTargB :"); Serial.println(hitOnTargB);
  Serial.print("hitOffTargB :"); Serial.println(hitOffTargB);
  Serial.print("    weaponA :"); Serial.println(weaponA);
  Serial.print("      lameB :"); Serial.println(lameB);
  Serial.print("    weaponB :"); Serial.println(weaponB);
  Serial.print("      lameA :"); Serial.println(lameA);
  Serial.println("======================================");
  delay(1000);
}
