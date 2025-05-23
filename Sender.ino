#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <esp_now.h>
#include <WiFi.h>
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
const int cursorUpPin = 14; // Cursor Button Up
const int cursorDownPin = 15; // Cursor Button Down
const int angleUpPin = 16; // Angle Button Up
const int angleDownPin = 17; // Angle Button Down
const int enterPin = 18; // Enter Button
// Receiver ESP32 MAC Address
uint8_t receiverMacAddress[] = {0x94, 0x54, 0xC5, 0x75, 0x89, 0xD4};
int initialAngles[6] = {90, 90, 90, 90, 90, 90};
int finalAngles[6] = {90, 90, 90, 90, 90, 90};
bool servoRunning = false;
bool isEspNowConnected = false;
bool isFirstRun = true; // Tracks if this is the first run after power-on
bool anglesChanged = false; // Tracks if angles were changed during running
bool restartPressed = false; // Tracks if Restart was pressed
// Antenna icon (8x8 pixels) for connection status
const unsigned char antennaIcon[] PROGMEM = {
    0b00011000,
    0b00011000,
    0b00111100,
    0b00111100,
    0b01111110,
    0b01111110,
    0b00011000,
    0b00011000
};
int currentMenu = 0; // 0: Servo Select, 1: Angle Set
int selectedServo = 1; // 1 to 6
int cursorPosition = 0; // Servo Select: 0-6 (Servo-1 to Servo-6, Start/Stop/Restart), Angle Set: 0-2 (Initial, Final, OK)
unsigned long lastButtonPress = 0;
unsigned long buttonHoldStart = 0;
bool isButtonHeld = false;
int debounceDelay = 300; // Slow cursor selection
const int fastDebounceDelay = 150; // Faster when held
const int holdThreshold = 500; // Hold time (ms)
typedef struct struct_message {
    int initialAngles[6];
    int finalAngles[6];
    bool servoRunning;
    bool restartRequested; // Flag for restart
} struct_message;
struct_message servoData;
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Data sent successfully" : "Data send failed");
}
void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("Sender Servo Control Starting...");
    // Initialize I2C and OLED
    Wire.begin(21, 22); // SDA = 21, SCL = 22
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println("OLED initialization failed");
        for (;;);
    }
    Serial.println("OLED initialized");
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Select Servo:");
    display.display();
    // Initialize Wi-Fi and ESP-NOW
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    if (esp_now_init() != ESP_OK) {
        Serial.println("ESP-NOW initialization failed");
        display.clearDisplay();
        display.setCursor(0, 0);
        display.println("ESP-NOW Failed");
        display.display();
        for (;;);
    }
    Serial.println("ESP-NOW initialized");
    // Register peer
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, receiverMacAddress, 6);
    peerInfo.channel = 1;
    peerInfo.encrypt = false;
    peerInfo.ifidx = WIFI_IF_STA;
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("Failed to add peer");
        display.clearDisplay();
        display.setCursor(0, 0);
        display.println("Peer Add Failed");
        display.display();
        for (;;);
    }
    Serial.println("Peer added successfully");
    isEspNowConnected = true;
    esp_now_register_send_cb(OnDataSent);
    // Setup buttons
    pinMode(cursorUpPin, INPUT_PULLUP);
    pinMode(cursorDownPin, INPUT_PULLUP);
    pinMode(angleUpPin, INPUT_PULLUP);
    pinMode(angleDownPin, INPUT_PULLUP);
    pinMode(enterPin, INPUT_PULLUP);
    Serial.println("Buttons configured");
}
void loop() {
    handleButtons();
    displayMenu();
}
void displayMenu() {
    Serial.println("displayMenu called, currentMenu: " + String(currentMenu)); // Debugging
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    // Antenna icon (connection status)
    if (isEspNowConnected) {
        display.drawBitmap(120, 0, antennaIcon, 8, 8, SSD1306_WHITE);
        Serial.println("Drawing antenna icon"); // Debugging
    }

    if (currentMenu == 0) { // Servo Select menu
        display.setCursor(0, 0);
        display.println("Select Servo:");
        Serial.println("Rendering Servo Select Menu"); // Debugging
        for (int i = 0; i < 6; i++) {
            display.setCursor(5, 10 + i * 8);
            display.print(cursorPosition == i ? "> Servo-" : " Servo-");
            display.print(i + 1);
            display.setCursor(80, 10 + i * 8);
            display.print(initialAngles[i]);
            display.setCursor(100, 10 + i * 8);
            display.print(finalAngles[i]);
            Serial.println("Servo-" + String(i + 1) + ": Initial=" + String(initialAngles[i]) + ", Final=" + String(finalAngles[i])); // Debugging
        }
        if (cursorPosition == 6) {
            display.drawRect(38, 56, 44, 16, SSD1306_WHITE); // Adjusted box
            display.drawRect(37, 55, 46, 18, SSD1306_WHITE);
            display.drawRect(36, 54, 48, 20, SSD1306_WHITE);
        }
        String buttonText = servoRunning ? (anglesChanged || restartPressed ? "Stop" : "Stop") : "Start";
        if (anglesChanged && servoRunning) buttonText = "Restart";
        int startStopTextX = 40 + (40 - (buttonText.length() * 6)) / 2;
        display.setCursor(startStopTextX, 60);
        display.print(buttonText);
        Serial.println("Button Text: " + buttonText); // Debugging
    } else { // Angle Set menu
        display.setCursor(0, 0);
        display.print("Servo-");
        display.print(selectedServo);
        display.println(":");
        display.drawRect(10, 16, 40, 12, SSD1306_WHITE);
        if (cursorPosition == 0) {
            display.drawRect(9, 15, 42, 14, SSD1306_WHITE);
            display.drawRect(8, 14, 44, 16, SSD1306_WHITE);
        }
        int initialTextX = 10 + (40 - (String(servoInitialAngle()).length() * 6)) / 2;
        display.setCursor(initialTextX, 18);
        display.print(servoInitialAngle());
        // Draw arrows conditionally for Initial angle
        if (servoInitialAngle() < 180) {
            display.drawTriangle(55, 20, 59, 20, 57, 18, SSD1306_WHITE); // Up arrow
        }
        if (servoInitialAngle() > 0) {
            display.drawTriangle(55, 24, 59, 24, 57, 26, SSD1306_WHITE); // Down arrow
        }
        display.drawRect(70, 16, 40, 12, SSD1306_WHITE);
        if (cursorPosition == 1) {
            display.drawRect(69, 15, 42, 14, SSD1306_WHITE);
            display.drawRect(68, 14, 44, 16, SSD1306_WHITE);
        }
        int finalTextX = 70 + (40 - (String(servoFinalAngle()).length() * 6)) / 2;
        display.setCursor(finalTextX, 18);
        display.print(servoFinalAngle());
        // Draw arrows conditionally for Final angle
        if (servoFinalAngle() < 180) {
            display.drawTriangle(115, 20, 119, 20, 117, 18, SSD1306_WHITE); // Up arrow
        }
        if (servoFinalAngle() > 0) {
            display.drawTriangle(115, 24, 119, 24, 117, 26, SSD1306_WHITE); // Down arrow
        }
        display.setCursor(10, 32);
        display.println("Initial");
        display.setCursor(70, 32);
        display.println("Final");
        int okTextX = 58;
        display.setCursor(okTextX, 52);
        display.print("OK");
        if (cursorPosition == 2) {
            display.drawRect(54, 50, 20, 12, SSD1306_WHITE);
            display.drawRect(53, 49, 22, 14, SSD1306_WHITE);
            display.drawRect(52, 48, 24, 16, SSD1306_WHITE);
        }
        Serial.println("Rendering Angle Set Menu for Servo-" + String(selectedServo)); // Debugging
    }
    display.display();
    Serial.println("Display updated"); // Debugging
}
void handleButtons() {
    unsigned long currentTime = millis();
    if (currentTime - lastButtonPress < debounceDelay) return;
    if (digitalRead(angleUpPin) == LOW || digitalRead(angleDownPin) == LOW) {
        if (!isButtonHeld) {
            buttonHoldStart = currentTime;
            isButtonHeld = true;
        }
        if (currentTime - buttonHoldStart > holdThreshold) {
            debounceDelay = fastDebounceDelay;
        }
    } else {
        isButtonHeld = false;
        debounceDelay = 300;
    }
    if (digitalRead(cursorUpPin) == LOW) {
        if (currentMenu == 0) {
            if (cursorPosition > 0) {
                cursorPosition--;
                Serial.println("Cursor Up: Position " + String(cursorPosition));
            }
        } else {
            if (cursorPosition > 0) {
                cursorPosition--;
                Serial.println("Cursor Up: Position " + String(cursorPosition));
            }
        }
        lastButtonPress = currentTime;
    }
    if (digitalRead(cursorDownPin) == LOW) {
        if (currentMenu == 0) {
            if (cursorPosition < 6) {
                cursorPosition++;
                Serial.println("Cursor Down: Position " + String(cursorPosition));
            }
        } else {
            if (cursorPosition < 2) {
                cursorPosition++;
                Serial.println("Cursor Down: Position " + String(cursorPosition));
            }
        }
        lastButtonPress = currentTime;
    }
    if (digitalRead(angleUpPin) == LOW && currentMenu == 1) {
        if (cursorPosition == 0 && servoInitialAngle() < 180) {
            setServoInitialAngle(servoInitialAngle() + 5);
            if (servoRunning) anglesChanged = true; // Track angle change during running
            Serial.println("Angle Up: Initial Angle " + String(servoInitialAngle()) + " for Servo-" + String(selectedServo));
        } else if (cursorPosition == 1 && servoFinalAngle() < 180) {
            setServoFinalAngle(servoFinalAngle() + 5);
            if (servoRunning) anglesChanged = true; // Track angle change during running
            Serial.println("Angle Up: Final Angle " + String(servoFinalAngle()) + " for Servo-" + String(selectedServo));
        }
        lastButtonPress = currentTime;
    }
    if (digitalRead(angleDownPin) == LOW && currentMenu == 1) {
        if (cursorPosition == 0 && servoInitialAngle() > 0) {
            setServoInitialAngle(servoInitialAngle() - 5);
            if (servoRunning) anglesChanged = true; // Track angle change during running
            Serial.println("Angle Down: Initial Angle " + String(servoInitialAngle()) + " for Servo-" + String(selectedServo));
        } else if (cursorPosition == 1 && servoFinalAngle() > 0) {
            setServoFinalAngle(servoFinalAngle() - 5);
            if (servoRunning) anglesChanged = true; // Track angle change during running
            Serial.println("Angle Down: Final Angle " + String(servoFinalAngle()) + " for Servo-" + String(selectedServo));
        }
        lastButtonPress = currentTime;
    }
    if (digitalRead(enterPin) == LOW) {
        if (currentMenu == 0) {
            if (cursorPosition < 6) {
                selectedServo = cursorPosition + 1;
                currentMenu = 1;
                cursorPosition = 0;
                Serial.println("Enter: Angle Setting Menu for Servo-" + String(selectedServo));
            } else if (cursorPosition == 6) {
                if (servoRunning && anglesChanged) {
                    // Restart: Send new angles with restart flag
                    restartPressed = true;
                    anglesChanged = false; // Reset anglesChanged
                    servoData.restartRequested = true; // Indicate restart
                    sendServoData();
                    Serial.println("Enter: Restart pressed, sending new angles");
                } else {
                    // Start or Stop
                    servoRunning = !servoRunning;
                    isFirstRun = false; // No longer first run after pressing Start/Restart
                    if (!servoRunning) {
                        anglesChanged = false; // Reset anglesChanged when stopping
                        restartPressed = false; // Reset restartPressed when stopping
                    }
                    servoData.restartRequested = false; // No restart for Start/Stop
                    Serial.println("Enter: All Servos " + String(servoRunning ? "started" : "stopped"));
                    sendServoData();
                }
            }
        } else if (cursorPosition == 2) {
            currentMenu = 0;
            cursorPosition = selectedServo - 1;
            Serial.println("Enter: Servo-" + String(selectedServo) + " angles saved");
            servoData.restartRequested = false; // No restart when saving angles
            sendServoData(); // Send data when returning to Servo Select menu
        }
        lastButtonPress = currentTime;
    }
}
void sendServoData() {
    for (int i = 0; i < 6; i++) {
        servoData.initialAngles[i] = initialAngles[i];
        servoData.finalAngles[i] = finalAngles[i];
    }
    servoData.servoRunning = servoRunning;
    esp_err_t result = esp_now_send(receiverMacAddress, (uint8_t *) &servoData, sizeof(servoData));
    Serial.println(result == ESP_OK ? "Data sending..." : "Error sending data");
}
int servoInitialAngle() {
    return initialAngles[selectedServo - 1];
}
int servoFinalAngle() {
    return finalAngles[selectedServo - 1];
}
void setServoInitialAngle(int angle) {
    angle = constrain(angle, 0, 180);
    initialAngles[selectedServo - 1] = angle;
}
void setServoFinalAngle(int angle) {
    angle = constrain(angle, 0, 180);
    finalAngles[selectedServo - 1] = angle;
}