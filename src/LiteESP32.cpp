#include <Arduino.h>
#include <FastLED.h>

// ================= BASIC =================
#define DEBUG 1
#define BAUDRATE 115200

// ================= MATRIX =================
#define MATRIX_PIN 26
#define MATRIX_W 32
#define MATRIX_H 8
#define NUM_LEDS (MATRIX_W * MATRIX_H)

#define LED_TYPE WS2812B
#define COLOR_ORDER GRB
#define BRIGHTNESS 70

CRGB leds[NUM_LEDS];

// ================= BUZZER =================
#define BUZZER_PIN 27
#define BUZZER_CH 0
#define BUZZER_FREQ 3000
#define BUZZER_RES 8

#define BUZZERTIME 1500
#define LIGHTTIME 3500

// ================= BUTTONS =================
#define BTN_VOLUME 13
#define BTN_REARM  14
#define BTN_MODE   16

// ================= WEAPON INPUTS =================
#define redClosePin   39   // VN
#define redMidPin     36   // VP
#define redGNDPin     34

#define greenClosePin 35
#define greenGNDPin   33
#define greenMidPin   32

// ================= MODES =================
#define FOIL_MODE  0
#define EPEE_MODE  1
#define SABRE_MODE 2

volatile uint8_t currentMode = EPEE_MODE;

//                         foil      epee     sabre
const unsigned long lockout[] = {300000UL, 45000UL, 170000UL};
const unsigned long depress[] = { 14000UL,  2000UL,   1000UL};

// ================= THRESHOLDS =================
// Temporary 12-bit ESP32 values. Tune later.
#define MID_LOW          1600
#define MID_HIGH         2400
#define HIGH_HIT         3600
#define LOW_LAME         400
#define SABRE_WEAPON_LOW 1260
#define SABRE_LAME_LOW   1200
#define EPEE_SHORT_DIFF  160

// ================= RAW READINGS =================
volatile int redClose = 0;
volatile int redMid   = 0;
volatile int redGND   = 0;

volatile int greenClose = 0;
volatile int greenMid   = 0;
volatile int greenGND   = 0;

// Old scoring naming:
// A = green
// B = red
#define weaponA greenClose
#define lameA   greenMid
#define groundA greenGND

#define weaponB redClose
#define lameB   redMid
#define groundB redGND

// ================= STATE =================
volatile bool stripGroundA = false;
volatile bool stripGroundB = false;

volatile bool depressedA = false;
volatile bool depressedB = false;

volatile bool hitOnTargA  = false;
volatile bool hitOffTargA = false;
volatile bool hitOnTargB  = false;
volatile bool hitOffTargB = false;

volatile bool shortAFlag = false;
volatile bool shortBFlag = false;

volatile unsigned long depressAtime = 0;
volatile unsigned long depressBtime = 0;

volatile bool scoringActive = false;
volatile bool displayHoldActive = false;

volatile unsigned long lockoutStartUs = 0;
volatile unsigned long displayStartMs = 0;

volatile bool buzzerRequest = false;
volatile bool displayDirty = true;

bool buzzerActive = false;
unsigned long buzzerStartMs = 0;

uint8_t volumeLevel = 2;
unsigned long rearmTimeMs = LIGHTTIME;

// ================= MATRIX HELPERS =================
uint16_t XY(uint8_t x, uint8_t y) {
  if (x >= MATRIX_W || y >= MATRIX_H) return 0;

  // Column serpentine layout: each 8-pixel column is wired up/down
  if (x % 2 == 0) {
    return x * MATRIX_H + y;
  } else {
    return x * MATRIX_H + (MATRIX_H - 1 - y);
  }
}

void fillRect(uint8_t x0, uint8_t y0, uint8_t w, uint8_t h, CRGB color) {
  for (uint8_t x = x0; x < x0 + w; x++) {
    for (uint8_t y = y0; y < y0 + h; y++) {
      leds[XY(x, y)] = color;
    }
  }
}

void renderDisplay() {
  fill_solid(leds, NUM_LEDS, CRGB::Black);

  // Red/B hit light: x 0-7
  if (hitOnTargB) {
    fillRect(0, 0, 8, 8, CRGB::Red);
  } else if (hitOffTargB) {
    fillRect(0, 0, 8, 8, CRGB::White);
  } else if (shortBFlag || stripGroundB) {
    fillRect(8, 0, 2, 8, CRGB::Yellow);
  }

  // Green/A hit light: x 24-31
  if (hitOnTargA) {
    fillRect(24, 0, 8, 8, CRGB::Green);
  } else if (hitOffTargA) {
    fillRect(24, 0, 8, 8, CRGB::White);
  } else if (shortAFlag || stripGroundA) {
    fillRect(22, 0, 2, 8, CRGB::Yellow);
  }

  // Mode indicator in center
  if (currentMode == FOIL_MODE) {
    fillRect(14, 3, 4, 2, CRGB::Blue);
  } else if (currentMode == EPEE_MODE) {
    fillRect(14, 2, 4, 4, CRGB::Purple);
  } else {
    fillRect(14, 1, 4, 6, CRGB::Orange);
  }

  FastLED.show();
  displayDirty = false;
}

// ================= BUZZER =================
void buzzerOn() {
  uint8_t duty = 70 + volumeLevel * 35;
  if (duty > 220) duty = 220;

  ledcWriteTone(BUZZER_CH, BUZZER_FREQ);
  ledcWrite(BUZZER_CH, duty);

  buzzerStartMs = millis();
  buzzerActive = true;
}

void buzzerOff() {
  ledcWrite(BUZZER_CH, 0);
  ledcWriteTone(BUZZER_CH, 0);
  buzzerActive = false;
}

void updateBuzzer() {
  if (buzzerRequest) {
    buzzerRequest = false;
    buzzerOn();
  }

  if (buzzerActive && millis() - buzzerStartMs >= BUZZERTIME) {
    buzzerOff();
  }
}

// ================= THRESHOLDS =================
bool isMidFoilEpee(int v) {
  return v > MID_LOW && v < MID_HIGH;
}

bool isMidSabreWeapon(int v) {
  return v > SABRE_WEAPON_LOW && v < MID_HIGH;
}

bool isMidSabreLame(int v) {
  return v > SABRE_LAME_LOW && v < MID_HIGH;
}

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

bool lockoutWindowOpen() {
  if (!scoringActive) return false;
  return micros() - lockoutStartUs < lockout[currentMode];
}

// ================= HIT REGISTER =================
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
    buzzerRequest = true;
  }

  displayDirty = true;
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
    buzzerRequest = true;
  }

  displayDirty = true;
}

// ================= SCORING =================
void foil() {
  shortAFlag = foilShortA();
  shortBFlag = foilShortB();

  if (!hitOnTargA && !hitOffTargA) {
    if (weaponA > HIGH_HIT && lameB < LOW_LAME && !stripGroundA) {
      if (!depressedA) {
        depressedA = true;
        depressAtime = micros();
      } else if (micros() - depressAtime >= depress[FOIL_MODE]) {
        registerHitA(false);
      }
    } else if (isMidFoilEpee(weaponA) && isMidFoilEpee(lameB) && !stripGroundA) {
      if (!depressedA) {
        depressedA = true;
        depressAtime = micros();
      } else if (micros() - depressAtime >= depress[FOIL_MODE]) {
        registerHitA(true);
      }
    } else {
      depressedA = false;
      depressAtime = 0;
    }
  }

  if (!hitOnTargB && !hitOffTargB) {
    if (weaponB > HIGH_HIT && lameA < LOW_LAME && !stripGroundB) {
      if (!depressedB) {
        depressedB = true;
        depressBtime = micros();
      } else if (micros() - depressBtime >= depress[FOIL_MODE]) {
        registerHitB(false);
      }
    } else if (isMidFoilEpee(weaponB) && isMidFoilEpee(lameA) && !stripGroundB) {
      if (!depressedB) {
        depressedB = true;
        depressBtime = micros();
      } else if (micros() - depressBtime >= depress[FOIL_MODE]) {
        registerHitB(true);
      }
    } else {
      depressedB = false;
      depressBtime = 0;
    }
  }

  displayDirty = true;
}

void epee() {
  if (!hitOnTargA) {
    if (isMidFoilEpee(weaponA) && isMidFoilEpee(lameA) && !stripGroundA) {
      if (!depressedA) {
        depressedA = true;
        depressAtime = micros();
      } else if (micros() - depressAtime >= depress[EPEE_MODE]) {
        registerHitA(true);
      }

      shortAFlag = false;
    } else {
      shortAFlag = abs(weaponA - lameA) < EPEE_SHORT_DIFF &&
                   (weaponA < MID_LOW || weaponA > MID_HIGH);

      depressedA = false;
      depressAtime = 0;
    }
  } else {
    shortAFlag = false;
  }

  if (!hitOnTargB) {
    if (isMidFoilEpee(weaponB) && isMidFoilEpee(lameB) && !stripGroundB) {
      if (!depressedB) {
        depressedB = true;
        depressBtime = micros();
      } else if (micros() - depressBtime >= depress[EPEE_MODE]) {
        registerHitB(true);
      }

      shortBFlag = false;
    } else {
      shortBFlag = abs(weaponB - lameB) < EPEE_SHORT_DIFF &&
                   (weaponB < MID_LOW || weaponB > MID_HIGH);

      depressedB = false;
      depressBtime = 0;
    }
  } else {
    shortBFlag = false;
  }

  displayDirty = true;
}

void sabre() {
  shortAFlag = sabreShortA();
  shortBFlag = sabreShortB();

  if (!hitOnTargA && !hitOffTargA) {
    if (isMidSabreWeapon(weaponA) && isMidSabreLame(lameB) && !stripGroundA) {
      if (!depressedA) {
        depressedA = true;
        depressAtime = micros();
      } else if (micros() - depressAtime >= depress[SABRE_MODE]) {
        registerHitA(true);
      }
    } else {
      depressedA = false;
      depressAtime = 0;
    }
  }

  if (!hitOnTargB && !hitOffTargB) {
    if (isMidSabreWeapon(weaponB) && isMidSabreLame(lameA) && !stripGroundB) {
      if (!depressedB) {
        depressedB = true;
        depressBtime = micros();
      } else if (micros() - depressBtime >= depress[SABRE_MODE]) {
        registerHitB(true);
      }
    } else {
      depressedB = false;
      depressBtime = 0;
    }
  }

  displayDirty = true;
}

// ================= RESET =================
void fullReset() {
  depressedA = false;
  depressedB = false;

  hitOnTargA = false;
  hitOffTargA = false;
  hitOnTargB = false;
  hitOffTargB = false;

  shortAFlag = false;
  shortBFlag = false;

  depressAtime = 0;
  depressBtime = 0;

  scoringActive = false;
  displayHoldActive = false;
  lockoutStartUs = 0;
  displayStartMs = 0;

  buzzerRequest = false;
  displayDirty = true;

  buzzerOff();
}

// ================= SCORING CYCLE =================
void updateScoringCycle() {
  if (!scoringActive) return;

  if (!displayHoldActive && !lockoutWindowOpen()) {
    displayHoldActive = true;
    displayStartMs = millis();
  }

  if (displayHoldActive && millis() - displayStartMs >= rearmTimeMs) {
    fullReset();
  }
}

// ================= CORE 0 WEAPON TASK =================
void weaponTask(void *parameter) {
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  for (;;) {
    redClose = analogRead(redClosePin);
    redMid   = analogRead(redMidPin);
    redGND   = analogRead(redGNDPin);

    greenClose = analogRead(greenClosePin);
    greenMid   = analogRead(greenMidPin);
    greenGND   = analogRead(greenGNDPin);

    stripGroundA = isMidFoilEpee(groundA);
    stripGroundB = isMidFoilEpee(groundB);

    if (!scoringActive || lockoutWindowOpen()) {
      if (currentMode == FOIL_MODE) {
        foil();
      } else if (currentMode == EPEE_MODE) {
        epee();
      } else {
        sabre();
      }
    } else {
      if (currentMode == FOIL_MODE) {
        shortAFlag = foilShortA();
        shortBFlag = foilShortB();
        displayDirty = true;
      } else if (currentMode == SABRE_MODE) {
        shortAFlag = sabreShortA();
        shortBFlag = sabreShortB();
        displayDirty = true;
      }
    }

    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

// ================= BUTTONS =================
void handleButtons() {
  static bool lastVolume = HIGH;
  static bool lastRearm = HIGH;
  static bool lastMode = HIGH;

  static unsigned long lastVolumeMs = 0;
  static unsigned long lastRearmMs = 0;
  static unsigned long lastModeMs = 0;

  bool volumeNow = digitalRead(BTN_VOLUME);
  bool rearmNow = digitalRead(BTN_REARM);
  bool modeNow = digitalRead(BTN_MODE);

  unsigned long now = millis();

  if (lastVolume == HIGH && volumeNow == LOW && now - lastVolumeMs > 200) {
    lastVolumeMs = now;

    volumeLevel++;
    if (volumeLevel > 4) volumeLevel = 0;

    Serial.print("# Volume: ");
    Serial.println(volumeLevel);
  }

  if (lastRearm == HIGH && rearmNow == LOW && now - lastRearmMs > 200) {
    lastRearmMs = now;

    rearmTimeMs += 1000;
    if (rearmTimeMs > 5000) rearmTimeMs = 1000;

    Serial.print("# Rearm time: ");
    Serial.println(rearmTimeMs);
  }

  if (lastMode == HIGH && modeNow == LOW && now - lastModeMs > 250) {
    lastModeMs = now;

    currentMode++;
    if (currentMode > SABRE_MODE) currentMode = FOIL_MODE;

    fullReset();

    Serial.print("# Mode: ");
    if (currentMode == FOIL_MODE) Serial.println("Foil");
    else if (currentMode == EPEE_MODE) Serial.println("Epee");
    else Serial.println("Sabre");
  }

  lastVolume = volumeNow;
  lastRearm = rearmNow;
  lastMode = modeNow;
}

// ================= DEBUG =================
void debugInputs() {
#if DEBUG
  static unsigned long lastPrint = 0;

  if (millis() - lastPrint < 500) return;
  lastPrint = millis();

  Serial.print("R close=");
  Serial.print(redClose);
  Serial.print(" mid=");
  Serial.print(redMid);
  Serial.print(" gnd=");
  Serial.print(redGND);

  Serial.print(" | G close=");
  Serial.print(greenClose);
  Serial.print(" mid=");
  Serial.print(greenMid);
  Serial.print(" gnd=");
  Serial.print(greenGND);

  Serial.print(" | mode=");
  Serial.print(currentMode);

  Serial.print(" | score=");
  Serial.print(scoringActive);

  Serial.print(" | lockout=");
  Serial.println(lockoutWindowOpen());
#endif
}

// ================= TEST =================
void testLights() {
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();

  fillRect(0, 0, 8, 8, CRGB::Red);
  FastLED.show();
  delay(250);

  fill_solid(leds, NUM_LEDS, CRGB::Black);
  fillRect(24, 0, 8, 8, CRGB::Green);
  FastLED.show();
  delay(250);

  fill_solid(leds, NUM_LEDS, CRGB::Black);
  fillRect(8, 0, 2, 8, CRGB::Yellow);
  fillRect(22, 0, 2, 8, CRGB::Yellow);
  FastLED.show();
  delay(250);

  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();
}

// ================= SETUP =================
void setup() {
  Serial.begin(BAUDRATE);
  delay(500);

  Serial.println();
  Serial.println("# ScoreBox Lite booting");

  pinMode(BTN_VOLUME, INPUT_PULLUP);
  pinMode(BTN_REARM, INPUT_PULLUP);
  pinMode(BTN_MODE, INPUT_PULLUP);

  ledcSetup(BUZZER_CH, BUZZER_FREQ, BUZZER_RES);
  ledcAttachPin(BUZZER_PIN, BUZZER_CH);
  buzzerOff();

  FastLED.addLeds<LED_TYPE, MATRIX_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();

  testLights();
  fullReset();

  xTaskCreatePinnedToCore(
    weaponTask,
    "WeaponTask",
    4096,
    NULL,
    1,
    NULL,
    0
  );

  Serial.println("# ScoreBox Lite ready");
}

// ================= LOOP - CORE 1 =================
void loop() {
  handleButtons();
  updateBuzzer();
  updateScoringCycle();

  if (displayDirty) {
    renderDisplay();
  }

  debugInputs();

  delay(2);
}