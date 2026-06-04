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
#define DEBUG 0
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
constexpr uint8_t rearmValues[] = {1, 3, 5};

// ================= THRESHOLDS =================
// These are starter values. Tune with serial readings later.
#define HIT_LOW   400
#define HIT_HIGH  600
#define GND_LOW   400
#define GND_HIGH  600

// ================= DISPLAY PATTERNS =================
const uint16_t ReArmBase[] = {
  15,16,47,48,63,64,80,95,96,112,127,159,175,
  17,33,49,94,110,126,142,158,174,190,
  13,29,45,82,98,114,130,146,162,178,194,
  19,28,51,60,67,92,99,108,124,131,156,172,188,
  11,36,43,84,100,116,132,148,164,180,
  21,37,53,90,106,122,138,154,170,186,202,
  9,25,41,86,102,118,134,150,166,182,
  23,39,55,56,71,72,88,104,120,136,152,168,184
};

const uint16_t VolBase[] = {
  47,64,95,96,111,112,143,
  49,78,97,126,145,
  45,66,93,114,141,
  51,76,99,124,147,
  43,68,91,116,139,
  53,74,101,122,149,
  41,70,89,118,137,
  55,56,88,103,104,119,136,151,152,167
};

const uint16_t epeeMode[] = {
  47,48,63,64,95,96,111,143,144,159,160,191,192,207,208,
  49,97,126,145,193,45,93,114,141,189,
  51,60,67,99,108,115,147,156,163,195,204,211,
  43,91,139,187,53,101,149,197,41,89,137,185,
  40,55,56,71,88,136,151,152,167,184,199,200,215
};

const uint16_t foilMode[] = {
  32,47,48,63,80,95,96,111,128,143,144,159,160,191,
  46,94,113,158,193,34,82,109,146,189,44,51,60,92,115,156,195,
  36,84,107,148,187,42,90,117,154,197,38,86,105,150,185,
  39,87,88,103,104,135,136,151,152,167,184,199,200,215,216
};

const uint16_t saberMode[] = {
  47,48,63,64,80,95,96,112,127,144,159,160,175,191,192,207,
  49,94,110,126,142,158,193,222,45,82,98,114,130,146,189,210,
  51,60,67,76,92,99,108,124,131,156,163,172,195,204,211,
  68,84,100,116,123,132,148,187,212,58,90,106,122,138,154,197,218,
  70,86,102,118,134,150,185,214,40,55,56,71,88,104,120,135,136,152,167,168,183,184,216
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
// ================= LED HELPERS =================
uint16_t XY(uint8_t x, uint8_t y) {
  // Your 32x8 matrix is vertical-column serpentine.
  if (x >= MATRIX_W || y >= MATRIX_H) return 0;

  if (x % 2 == 0) {
    return x * MATRIX_H + y;
  } else {
    return x * MATRIX_H + (MATRIX_H - 1 - y);
  }
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

void drawDigitNoShow(uint8_t digit, int x0, int y0, CRGB color) {
  if (digit > 9) return;

  for (int y = 0; y < 5; y++) {
    for (int x = 0; x < 3; x++) {
      if (digitFont[digit][y] & (1 << (2 - x))) {
        int px = x0 + x;
        int py = y0 + y;

        if (px >= 0 && px < MATRIX_W &&
            py >= 0 && py < MATRIX_H) {
          leds[XY(px, py)] = color;
            }
      }
    }
  }
}

void drawNumberNoShow(uint8_t number, int x0, int y0, const CRGB &color) {
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
