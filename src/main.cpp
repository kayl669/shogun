#include <Shogun.h>

#define BUTTON_BOOT_PIN 0
#define WAVESHARE_RS485_RX 18
#define WAVESHARE_RS485_TX 17

ShogunAC::ShogunAC shogunAC = ShogunAC::ShogunAC(
    WAVESHARE_RS485_RX,
    WAVESHARE_RS485_TX,
    BUTTON_BOOT_PIN
);

void setup() {
    Serial.begin(115200);
    shogunAC.setup();
}

void loop() {
    shogunAC.loop();
}
