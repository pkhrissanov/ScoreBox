#include <Arduino.h>
#include <FastLED.h>

// ============================================================
// SCOREBOX LITE - ESP32-WROOM + FreeRTOS + WS2812B 8x32 MATRIX
// ============================================================
// Design goal:
// - Core 0: weapon reading / hit timing, always fast
// - Core 1: matrix, buzzer, buttons, menu screens
// - Inputs per side: CLOSE, MID, GND only
// - No lame pin, no separate weapon pin
// ============================================================

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
#define BUZZERTIME 1500UL   // ms
#define LIGHTTIME 3500UL    // ms after lockout ends

// ================= BUTTONS =================
#define BTN_VOLUME 13
#define BTN_REARM  14
#define BTN_MODE   16

// ================= WEAPON INPUTS =================
#define redClosePin   39
#define redMidPin     36
#define redGNDPin     34

#define greenClosePin 35
#define greenGNDPin   33
#define greenMidPin   32

// ================= MODES =================
#define FOIL_MODE  0
#define EPEE_MODE  1
#define SABRE_MODE 2

volatile uint8_t currentMode = EPEE_MODE;

// microseconds: foil, epee, sabre
const unsigned long lockout[] = {300000UL, 45000UL, 170000UL};
const unsigned long depress[] = { 14000UL,  2000UL,   1000UL};

// ================= SETTINGS =================
volatile uint8_t volumeLevel = 3; // 0-5
volatile uint8_t rearmIndex = 1;  // 0=>1s, 1=>3s, 2=>5s
const uint8_t rearmValues[] = {1, 3, 5};

// ================= THRESHOLDS =================
// These are starter values. Tune with serial readings later.
#define HIT_LOW   400
#define HIT_HIGH  600
#define GND_LOW   400
#define GND_HIGH  600

// ================= DISPLAY PATTERNS =================
const uint16_t ReArmBase[] = {
  1, 2, 5, 6, 7, 8, 10, 11, 12, 14, 15, 19, 21,
  61, 59, 57, 52, 50, 48, 46, 44, 42, 40,
  65, 67, 69, 74, 76, 78, 80, 82, 84, 86, 88,
  125, 124, 121, 120, 119, 116, 115, 114, 112, 111, 108, 106, 104,
  129, 132, 133, 138, 140, 142, 144, 146, 148, 150,
  189, 187, 185, 180, 178, 176, 174, 172, 170, 168, 166,
  193, 195, 197, 202, 204, 206, 208, 210, 212, 214,
  253, 251, 249, 248, 247, 246, 244, 242, 240, 238, 236, 234, 232
};

const uint16_t VolBase[] = {
  5, 8, 11, 12, 13, 14, 17,
  57, 54, 51, 48, 45,
  69, 72, 75, 78, 81,
  121, 118, 115, 112, 109,
  133, 136, 139, 142, 145,
  185, 182, 179, 176, 173,
  197, 200, 203, 206, 209,
  249, 248, 244, 243, 242, 241, 238, 237, 236, 235
};

const uint16_t epeeMode[] = {
  5,6,7,8,11,12,13,17,18,19,20,23,24,25,26,
  57,51,48,45,39,69,75,78,81,87,
  121,120,119,115,114,113,109,108,107,103,102,101,
  133,139,145,151,185,179,173,167,197,203,209,215,
  250,249,248,247,244,238,237,236,235,232,231,230,229
};

const uint16_t foilMode[] = {
  4,5,6,7,10,11,12,13,16,17,18,19,20,23,
  58,52,49,44,39,68,74,77,82,87,122,121,120,116,113,108,103,
  132,138,141,146,151,186,180,177,172,167,196,202,205,210,215,
  251,245,244,243,242,239,238,237,236,235,232,231,230,229,228
};

const uint16_t saberMode[] = {
  5,6,7,8,10,11,12,14,15,18,19,20,21,23,24,25,
  57,52,50,48,46,44,39,36,69,74,76,78,80,82,87,90,
  121,120,119,118,116,115,114,112,111,108,107,106,103,102,101,
  136,138,140,142,143,144,146,151,154,184,180,178,176,174,172,167,164,
  200,202,204,206,208,210,215,218,250,249,248,247,244,242,240,239,238,236,235,234,233,232,228
};

// ================= SMALL NUMBER FONT =================
// 3x5 digits, drawn into matrix for rearm/volume values.
const uint8_t digitFont[10][5] = {
  {0b111, 0b101, 0b101, 0b101, 0b111}, // 0
  {0b010, 0b110, 0b010, 0b010, 0b111}, // 1
  {0b111, 0b001, 0b111, 0b100, 0b111}, // 2
  {0b111, 0b001, 0b111, 0b001, 0b111}, // 3
  {0b101, 0b101, 0b111, 0b001, 0b001}, // 4
  {0b111, 0b100, 0b111, 0b001, 0b111}, // 5
  {0b111, 0b100, 0b111, 0b101, 0b111}, // 6
  {0b111, 0b001, 0b001, 0b010, 0b010}, // 7
  {0b111, 0b101, 0b111, 0b101, 0b111}, // 8
  {0b111, 0b101, 0b111, 0b001, 0b111}  // 9
};

// ================= EVENTS =================
enum OutputEventType : uint8_t {
  EVT_CLEAR,
  EVT_RED_HIT,
  EVT_GREEN_HIT,
  EVT_SHOW_MODE,
  EVT_SHOW_REARM,
  EVT_SHOW_VOLUME,
  EVT_RESTORE
};

struct OutputEvent {
  OutputEventType type;
  uint8_t value;
};

QueueHandle_t outputQueue;
portMUX_TYPE scoreMux = portMUX_INITIALIZER_UNLOCKED;

// ================= SCORING STATE =================
volatile bool redHit = false;
volatile bool greenHit = false;
volatile bool redOffTarget = false;
volatile bool greenOffTarget = false;
volatile bool redGroundedNow = false;
volatile bool greenGroundedNow = false;
volatile bool scoringActive = false;
volatile bool displayHoldActive = false;

bool redDepressed = false;
bool greenDepressed = false;

unsigned long redDepressStartUs = 0;
unsigned long greenDepressStartUs = 0;
unsigned long lockoutStartUs = 0;
unsigned long displayStartMs = 0;
unsigned long buzzerStartMs = 0;
volatile bool buzzerActive = false;

// ================= MENU STATE =================
volatile bool menuActive = false;
unsigned long menuUntilMs = 0;

// ================= LED HELPERS =================
uint16_t XY(uint8_t x, uint8_t y) {
  // Serpentine mapping for 32x8 WS2812 matrix.
  // If your matrix is straight-line, replace this with: return y * MATRIX_W + x;
  if (y % 2 == 0) {
    return y * MATRIX_W + x;
  }
  return y * MATRIX_W + (MATRIX_W - 1 - x);
}

void clearMatrixNoShow() {
  fill_solid(leds, NUM_LEDS, CRGB::Black);
}

void clearMatrix() {
  clearMatrixNoShow();
  FastLED.show();
}

void drawPatternNoShow(const uint16_t *pattern, size_t count, const CRGB &color) {
  for (size_t i = 0; i < count; i++) {
    if (pattern[i] < NUM_LEDS) {
      leds[pattern[i]] = color;
    }
  }
}

void drawDigitNoShow(uint8_t digit, uint8_t x0, uint8_t y0, const CRGB &color) {
  if (digit > 9) return;

  for (uint8_t row = 0; row < 5; row++) {
    for (uint8_t col = 0; col < 3; col++) {
      if (digitFont[digit][row] & (1 << (2 - col))) {
        uint8_t x = x0 + col;
        uint8_t y = y0 + row;
        if (x < MATRIX_W && y < MATRIX_H) {
          leds[XY(x, y)] = color;
        }
      }
    }
  }
}

void drawNumberNoShow(uint8_t number, uint8_t x0, uint8_t y0, const CRGB &color) {
  if (number < 10) {
    drawDigitNoShow(number, x0, y0, color);
  } else {
    drawDigitNoShow(number / 10, x0, y0, color);
    drawDigitNoShow(number % 10, x0 + 4, y0, color);
  }
}

void drawModeScreen() {
  clearMatrixNoShow();

  if (currentMode == FOIL_MODE) {
    drawPatternNoShow(foilMode, sizeof(foilMode) / sizeof(foilMode[0]), CRGB::White);
  } else if (currentMode == EPEE_MODE) {
    drawPatternNoShow(epeeMode, sizeof(epeeMode) / sizeof(epeeMode[0]), CRGB::White);
  } else {
    drawPatternNoShow(saberMode, sizeof(saberMode) / sizeof(saberMode[0]), CRGB::White);
  }

  FastLED.show();
}

void drawRearmScreen() {
  clearMatrixNoShow();
  drawPatternNoShow(ReArmBase, sizeof(ReArmBase) / sizeof(ReArmBase[0]), CRGB::White);

  // Draw actual 1 / 3 / 5 value on right side.
  drawNumberNoShow(rearmValues[rearmIndex], 27, 2, CRGB::Yellow);
  FastLED.show();
}

void drawVolumeScreen() {
  clearMatrixNoShow();
  drawPatternNoShow(VolBase, sizeof(VolBase) / sizeof(VolBase[0]), CRGB::White);

  // Draw actual 0-5 value on right side.
  drawNumberNoShow(volumeLevel, 27, 2, CRGB::Yellow);
  FastLED.show();
}

void drawHits() {
  clearMatrixNoShow();

  bool r, g;
  bool ro, go, rg, gg;

  portENTER_CRITICAL(&scoreMux);
  r = redHit;
  g = greenHit;
  ro = redOffTarget;
  go = greenOffTarget;
  rg = redGroundedNow;
  gg = greenGroundedNow;
  portEXIT_CRITICAL(&scoreMux);

  // Layout across 32x8:
  // x 0-7   = red on-target, red grounding indicator area
  // x 8-15  = red off-target white
  // x 16-23 = green off-target white
  // x 24-31 = green on-target, green grounding indicator area

  if (r) {
    for (uint8_t y = 0; y < MATRIX_H; y++) {
      for (uint8_t x = 0; x < 8; x++) {
        leds[XY(x, y)] = CRGB::Red;
      }
    }
  }

  if (ro) {
    for (uint8_t y = 0; y < MATRIX_H; y++) {
      for (uint8_t x = 8; x < 16; x++) {
        leds[XY(x, y)] = CRGB::White;
      }
    }
  }

  if (go) {
    for (uint8_t y = 0; y < MATRIX_H; y++) {
      for (uint8_t x = 16; x < 24; x++) {
        leds[XY(x, y)] = CRGB::White;
      }
    }
  }

  if (g) {
    for (uint8_t y = 0; y < MATRIX_H; y++) {
      for (uint8_t x = 24; x < 32; x++) {
        leds[XY(x, y)] = CRGB::Green;
      }
    }
  }

  // Grounding indicators: 4x4 yellow square centered inside the two main 8x8 light areas.
  // Only show grounding if that side has no active hit/off-target display.
  if (rg && !r && !ro) {
    for (uint8_t y = 2; y < 6; y++) {
      for (uint8_t x = 2; x < 6; x++) {
        leds[XY(x, y)] = CRGB::Yellow;
      }
    }
  }

  if (gg && !g && !go) {
    for (uint8_t y = 2; y < 6; y++) {
      for (uint8_t x = 26; x < 30; x++) {
        leds[XY(x, y)] = CRGB::Yellow;
      }
    }
  }

  FastLED.show();
}

void restoreNormalDisplay() {
  bool active;
  portENTER_CRITICAL(&scoreMux);
  active = scoringActive;
  portEXIT_CRITICAL(&scoreMux);

  if (active) {
    drawHits();
  } else {
    clearMatrix();
  }
}

// ================= QUEUE HELPER =================
void sendEvent(OutputEventType type, uint8_t value = 0) {
  if (!outputQueue) return;
  OutputEvent ev{type, value};
  xQueueSend(outputQueue, &ev, 0);
}

// ================= INPUT HELPERS =================
bool inHitBand(int v) {
  return v > HIT_LOW && v < HIT_HIGH;
}

bool inGroundBand(int v) {
  return v > GND_LOW && v < GND_HIGH;
}

// This is intentionally simple for the Lite prototype.
// Tune this once you collect real close/mid/gnd readings.
bool redOnTargetCandidate(int redClose, int redMid, int redGND) {
  return inHitBand(redClose) && inHitBand(redMid) && !inGroundBand(redGND);
}

bool greenOnTargetCandidate(int greenClose, int greenMid, int greenGND) {
  return inHitBand(greenClose) && inHitBand(greenMid) && !inGroundBand(greenGND);
}

// Starter off-target logic for the Lite prototype.
// With only CLOSE / MID / GND, this is intentionally simple and will need tuning.
// Current assumption: CLOSE shows a tip/contact event, MID decides on-target path.
bool redOffTargetCandidate(int redClose, int redMid, int redGND) {
  return inHitBand(redClose) && !inHitBand(redMid) && !inGroundBand(redGND);
}

bool greenOffTargetCandidate(int greenClose, int greenMid, int greenGND) {
  return inHitBand(greenClose) && !inHitBand(greenMid) && !inGroundBand(greenGND);
}

// ================= BUZZER HELPERS =================
void startBuzzerFromWeaponTask() {
  if (volumeLevel == 0) return;

  digitalWrite(BUZZER_PIN, HIGH);
  buzzerStartMs = millis();
  buzzerActive = true;
}

void stopBuzzerFromWeaponTask() {
  digitalWrite(BUZZER_PIN, LOW);
  buzzerActive = false;
}

void updateBuzzerFromWeaponTask() {
  if (buzzerActive && millis() - buzzerStartMs >= BUZZERTIME) {
    stopBuzzerFromWeaponTask();
  }
}

// ================= SCORING HELPERS =================
bool lockoutOpen() {
  if (!scoringActive) return true;
  return micros() - lockoutStartUs < lockout[currentMode];
}

void resetScoringFromWeaponTask() {
  portENTER_CRITICAL(&scoreMux);
  redHit = false;
  greenHit = false;
  redOffTarget = false;
  greenOffTarget = false;
  scoringActive = false;
  displayHoldActive = false;
  portEXIT_CRITICAL(&scoreMux);

  redDepressed = false;
  greenDepressed = false;
  redDepressStartUs = 0;
  greenDepressStartUs = 0;
  lockoutStartUs = 0;
  displayStartMs = 0;

  stopBuzzerFromWeaponTask();
  sendEvent(EVT_CLEAR);
}

void registerRedHit(bool offTarget = false) {
  bool shouldSend = false;

  portENTER_CRITICAL(&scoreMux);
  if (!redHit && !redOffTarget) {
    if (offTarget) redOffTarget = true;
    else redHit = true;
    shouldSend = true;

    if (!scoringActive) {
      scoringActive = true;
      displayHoldActive = false;
      lockoutStartUs = micros();
    }
  }
  portEXIT_CRITICAL(&scoreMux);

  if (shouldSend) {
    startBuzzerFromWeaponTask();
    sendEvent(EVT_RED_HIT);
#if DEBUG
    Serial.println(offTarget ? "RED OFF TARGET" : "RED HIT");
#endif
  }
}

void registerGreenHit(bool offTarget = false) {
  bool shouldSend = false;

  portENTER_CRITICAL(&scoreMux);
  if (!greenHit && !greenOffTarget) {
    if (offTarget) greenOffTarget = true;
    else greenHit = true;
    shouldSend = true;

    if (!scoringActive) {
      scoringActive = true;
      displayHoldActive = false;
      lockoutStartUs = micros();
    }
  }
  portEXIT_CRITICAL(&scoreMux);

  if (shouldSend) {
    startBuzzerFromWeaponTask();
    sendEvent(EVT_GREEN_HIT);
#if DEBUG
    Serial.println(offTarget ? "GREEN OFF TARGET" : "GREEN HIT");
#endif
  }
}

void updateScoringCycleFromWeaponTask() {
  updateBuzzerFromWeaponTask();

  if (!scoringActive) return;

  if (!displayHoldActive && !lockoutOpen()) {
    portENTER_CRITICAL(&scoreMux);
    displayHoldActive = true;
    portEXIT_CRITICAL(&scoreMux);

    displayStartMs = millis();
  }

  if (displayHoldActive && millis() - displayStartMs >= LIGHTTIME) {
    resetScoringFromWeaponTask();
  }
}

// ================= WEAPON TASK =================
void WeaponTask(void *pvParameters) {
  uint32_t loopCounter = 0;
  unsigned long lastDebugMs = 0;

  while (true) {
    int redClose = analogRead(redClosePin);
    int redMid   = analogRead(redMidPin);
    int redGND   = analogRead(redGNDPin);

    int greenClose = analogRead(greenClosePin);
    int greenMid   = analogRead(greenMidPin);
    int greenGND   = analogRead(greenGNDPin);

    bool redGround = inGroundBand(redGND);
    bool greenGround = inGroundBand(greenGND);

    portENTER_CRITICAL(&scoreMux);
    redGroundedNow = redGround;
    greenGroundedNow = greenGround;
    portEXIT_CRITICAL(&scoreMux);

    bool redOnCandidate = redOnTargetCandidate(redClose, redMid, redGND);
    bool greenOnCandidate = greenOnTargetCandidate(greenClose, greenMid, greenGND);

    bool redOffCandidate = redOffTargetCandidate(redClose, redMid, redGND);
    bool greenOffCandidate = greenOffTargetCandidate(greenClose, greenMid, greenGND);

    // Refresh grounding indicators while idle, but do not spam during a menu screen.
    if (!scoringActive && (redGround || greenGround)) {
      sendEvent(EVT_RESTORE);
    }

    if (!scoringActive || lockoutOpen()) {
      if (!redHit && !redOffTarget) {
        if (redOnCandidate || redOffCandidate) {
          if (!redDepressed) {
            redDepressed = true;
            redDepressStartUs = micros();
          } else if (micros() - redDepressStartUs >= depress[currentMode]) {
            registerRedHit(redOffCandidate && !redOnCandidate);
          }
        } else {
          redDepressed = false;
          redDepressStartUs = 0;
        }
      }

      if (!greenHit && !greenOffTarget) {
        if (greenOnCandidate || greenOffCandidate) {
          if (!greenDepressed) {
            greenDepressed = true;
            greenDepressStartUs = micros();
          } else if (micros() - greenDepressStartUs >= depress[currentMode]) {
            registerGreenHit(greenOffCandidate && !greenOnCandidate);
          }
        } else {
          greenDepressed = false;
          greenDepressStartUs = 0;
        }
      }
    }

    updateScoringCycleFromWeaponTask();

#if DEBUG
    // Very light debug only. Do not spam Serial every loop.
    if (millis() - lastDebugMs >= 1000) {
      lastDebugMs = millis();
      Serial.print("R close/mid/gnd: ");
      Serial.print(redClose); Serial.print(" / ");
      Serial.print(redMid); Serial.print(" / ");
      Serial.print(redGND);
      Serial.print("   G close/mid/gnd: ");
      Serial.print(greenClose); Serial.print(" / ");
      Serial.print(greenMid); Serial.print(" / ");
      Serial.println(greenGND);
    }
#endif

    // Yield occasionally so the watchdog is happy, but do not add delay().
    loopCounter++;
    if ((loopCounter & 0xFF) == 0) {
      taskYIELD();
    }
  }
}

// ================= OUTPUT TASK =================
void OutputTask(void *pvParameters) {
  OutputEvent ev;

  while (true) {
    if (xQueueReceive(outputQueue, &ev, 20 / portTICK_PERIOD_MS)) {
      switch (ev.type) {
        case EVT_CLEAR:
          if (!menuActive) clearMatrix();
          break;

        case EVT_RED_HIT:
        case EVT_GREEN_HIT:
          if (!menuActive) drawHits();
          break;

        case EVT_SHOW_MODE:
          menuActive = true;
          menuUntilMs = millis() + 3000UL;
          drawModeScreen();
          break;

        case EVT_SHOW_REARM:
          menuActive = true;
          menuUntilMs = millis() + 3000UL;
          drawRearmScreen();
          break;

        case EVT_SHOW_VOLUME:
          menuActive = true;
          menuUntilMs = millis() + 3000UL;
          drawVolumeScreen();
          break;

        case EVT_RESTORE:
          menuActive = false;
          restoreNormalDisplay();
          break;
      }
    }

    if (menuActive && (long)(millis() - menuUntilMs) >= 0) {
      menuActive = false;
      restoreNormalDisplay();
    }
  }
}

// ================= BUTTON TASK =================
void ButtonTask(void *pvParameters) {
  bool lastMode = HIGH;
  bool lastRearm = HIGH;
  bool lastVolume = HIGH;

  unsigned long lastModeMs = 0;
  unsigned long lastRearmMs = 0;
  unsigned long lastVolumeMs = 0;

  const unsigned long debounceMs = 180;

  while (true) {
    bool modeNow = digitalRead(BTN_MODE);
    bool rearmNow = digitalRead(BTN_REARM);
    bool volumeNow = digitalRead(BTN_VOLUME);
    unsigned long now = millis();

    if (lastMode == HIGH && modeNow == LOW && now - lastModeMs >= debounceMs) {
      lastModeMs = now;

      currentMode++;
      if (currentMode > SABRE_MODE) currentMode = FOIL_MODE;

      // Changing weapon mode clears current hit state.
      resetScoringFromWeaponTask();
      sendEvent(EVT_SHOW_MODE);

#if DEBUG
      Serial.print("Mode changed to: ");
      if (currentMode == FOIL_MODE) Serial.println("FOIL");
      else if (currentMode == EPEE_MODE) Serial.println("EPEE");
      else Serial.println("SABRE");
#endif
    }

    if (lastRearm == HIGH && rearmNow == LOW && now - lastRearmMs >= debounceMs) {
      lastRearmMs = now;

      rearmIndex++;
      if (rearmIndex >= 3) rearmIndex = 0;

      sendEvent(EVT_SHOW_REARM);

#if DEBUG
      Serial.print("REARM: ");
      Serial.println(rearmValues[rearmIndex]);
#endif
    }

    if (lastVolume == HIGH && volumeNow == LOW && now - lastVolumeMs >= debounceMs) {
      lastVolumeMs = now;

      volumeLevel++;
      if (volumeLevel > 5) volumeLevel = 0;

      sendEvent(EVT_SHOW_VOLUME);

#if DEBUG
      Serial.print("VOLUME: ");
      Serial.println(volumeLevel);
#endif
    }

    lastMode = modeNow;
    lastRearm = rearmNow;
    lastVolume = volumeNow;

    vTaskDelay(20 / portTICK_PERIOD_MS);
  }
}

// ================= TEST SCREEN =================
void startupTest() {
  clearMatrixNoShow();

  for (uint8_t y = 0; y < MATRIX_H; y++) {
    for (uint8_t x = 0; x < 8; x++) leds[XY(x, y)] = CRGB::Red;
    for (uint8_t x = 24; x < 32; x++) leds[XY(x, y)] = CRGB::Green;
  }

  FastLED.show();
  delay(700);
  clearMatrix();

  digitalWrite(BUZZER_PIN, HIGH);
  delay(80);
  digitalWrite(BUZZER_PIN, LOW);
}

// ================= SETUP =================
void setup() {
  Serial.begin(BAUDRATE);
  delay(300);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  pinMode(BTN_MODE, INPUT_PULLUP);
  pinMode(BTN_REARM, INPUT_PULLUP);
  pinMode(BTN_VOLUME, INPUT_PULLUP);

  // ADC pins are input-only already, but this is fine.
  pinMode(redClosePin, INPUT);
  pinMode(redMidPin, INPUT);
  pinMode(redGNDPin, INPUT);
  pinMode(greenClosePin, INPUT);
  pinMode(greenMidPin, INPUT);
  pinMode(greenGNDPin, INPUT);

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  FastLED.addLeds<LED_TYPE, MATRIX_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
  clearMatrix();

  startupTest();

  outputQueue = xQueueCreate(16, sizeof(OutputEvent));
  if (!outputQueue) {
    Serial.println("ERROR: Could not create output queue");
    while (true) delay(1000);
  }

  xTaskCreatePinnedToCore(
    WeaponTask,
    "WeaponTask",
    4096,
    NULL,
    10,
    NULL,
    0
  );

  xTaskCreatePinnedToCore(
    OutputTask,
    "OutputTask",
    4096,
    NULL,
    3,
    NULL,
    1
  );

  xTaskCreatePinnedToCore(
    ButtonTask,
    "ButtonTask",
    3072,
    NULL,
    2,
    NULL,
    1
  );

  sendEvent(EVT_SHOW_MODE);

  Serial.println("ScoreBox Lite FreeRTOS started");
}

// ================= LOOP =================
void loop() {
  // Everything important runs in FreeRTOS tasks.
  vTaskDelay(1000 / portTICK_PERIOD_MS);
}
