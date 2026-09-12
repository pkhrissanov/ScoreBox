#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

#define PANEL_RES_X 64
#define PANEL_RES_Y 32
#define PANEL_CHAIN 1

HUB75_I2S_CFG::i2s_pins _pins = {
    23, 22, 21,   // R1 G1 B1
    19, 18, 5,    // R2 G2 B2
    17, 16, 4, 2, 15, // A B C D E
    27, 25, 26    // LAT OE CLK
};

HUB75_I2S_CFG mxconfig(PANEL_RES_X, PANEL_RES_Y, PANEL_CHAIN, _pins);
MatrixPanel_I2S_DMA *display = nullptr;

uint16_t RED, YELLOW, WHITE, CYAN, BG, MAGENTA;

void drawDigit(int x, int y, int n, uint16_t color) {
    const int w = 7;
    const int h = 13;
    const int t = 2;

    bool seg[10][7] = {
        {1,1,1,1,1,1,0}, // 0
        {0,1,1,0,0,0,0}, // 1
        {1,1,0,1,1,0,1}, // 2
        {1,1,1,1,0,0,1}, // 3
        {0,1,1,0,0,1,1}, // 4
        {1,0,1,1,0,1,1}, // 5
        {1,0,1,1,1,1,1}, // 6
        {1,1,1,0,0,0,0}, // 7
        {1,1,1,1,1,1,1}, // 8
        {1,1,1,1,0,1,1}  // 9
    };

    if (seg[n][0]) display->fillRect(x + t, y, w - 2*t, t, color);
    if (seg[n][1]) display->fillRect(x + w - t, y + t, t, h/2 - t, color);
    if (seg[n][2]) display->fillRect(x + w - t, y + h/2 + t, t, h/2 - t, color);
    if (seg[n][3]) display->fillRect(x + t, y + h - t, w - 2*t, t, color);
    if (seg[n][4]) display->fillRect(x, y + h/2 + t, t, h/2 - t, color);
    if (seg[n][5]) display->fillRect(x, y + t, t, h/2 - t, color);
    if (seg[n][6]) display->fillRect(x + t, y + h/2, w - 2*t, t, color);
}

void drawNumber2(int x, int y, int value, uint16_t color) {
    drawDigit(x, y, value / 10, color);
    drawDigit(x + 9, y, value % 10, color);
}

void drawScoreboard() {
    display->clearScreen();

    uint16_t white = display->color565(255, 255, 255);
    uint16_t red   = display->color565(255, 0, 0);
    uint16_t green = display->color565(0, 255, 0);
    uint16_t blue  = display->color565(0, 0, 255);

    display->drawRect(0, 0, 64, 32, white);

    display->fillRect(0, 0, 16, 8, red);
    display->fillRect(16, 8, 16, 8, green);
    display->fillRect(32, 16, 16, 8, blue);
    display->fillRect(48, 24, 16, 8, white);

    display->setTextSize(1);
    display->setTextColor(white);
    display->setCursor(2, 12);
    display->print("45");

    display->setCursor(23, 12);
    display->print("3:20");

    display->setCursor(52, 12);
    display->print("18");
}

void setup()
{
    Serial.begin(115200);

    mxconfig.driver = HUB75_I2S_CFG::FM6126A;
    mxconfig.gpio.e = 15;
    mxconfig.clkphase = false;   // try true if this fails

    display = new MatrixPanel_I2S_DMA(mxconfig);

    if (!display->begin()) {
        Serial.println("Matrix begin failed");
        while (true) delay(1000);
    }

    delay(500);
    display->setBrightness8(5);

    display->clearScreen();
    delay(500);
}


void loop() {
    delay(1000);
}

