#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <esp_now.h>
#include <WiFi.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

const int cursorUpPin = 14;   // Cursor Button Up
const int cursorDownPin = 15; // Cursor Button Down
const int angleUpPin = 16;    // Angle Button Up
const int angleDownPin = 17;  // Angle Button Down
const int enterPin = 18;      // Enter Button

// Receiver ESP32 MAC Address
uint8_t receiverMacAddress[] = {0x94, 0x54, 0xC5, 0x75, 0x89, 0xD4};

int initialAngles[6] = {90, 90, 90, 90, 90, 90};
int finalAngles[6] = {90, 90, 90, 90, 90, 90};
bool servoRunning = false;
bool isEspNowConnected = false;

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

int currentMenu = 0;          // 0: Servo Select, 1: Angle Set
int selectedServo = 1;        // 1 to 6
int cursorPosition = 0;       // Servo Select: 0-6 (Servo-1 to Servo-6, Start/Stop), Angle Set: 0-2 (Initial, Final, OK)
unsigned long lastButtonPress = 0;
unsigned long buttonHoldStart = 0;
bool isButtonHeld = false;
int debounceDelay = 300;      // Slow cursor selection
const int fastDebounceDelay = 150; // Faster when held
const int holdThreshold = 500;     // Hold time (ms)

typedef struct struct_message {
  int initialAngles[6];
  int finalAngles[6];
  bool servoRunning;
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
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(30, 25);
  display.println("Rashid");
  display.display();
  delay(2000);

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Servo Control");
  display.display();
  delay(1000);

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
  display.clearDisplay();

  // Draw connection icon at top-right if ESP-NOW is connected
  if (isEspNowConnected) {
    display.drawBitmap(120, 0, antennaIcon, 8, 8, SSD1306_WHITE);
  }

  if (currentMenu == 0) { // Servo Select Menu
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Select Servo:");

    for (int i = 0; i < 6; i++) {
      display.setCursor(5, 10 + i * 8);
      display.print(cursorPosition == i ? "> Servo-" : "  Servo-");
      display.print(i + 1);
      display.setCursor(80, 10 + i * 8);
      display.print(initialAngles[i]);
      display.setCursor(100, 10 + i * 8);
      display.print(finalAngles[i]);
    }

    if (cursorPosition == 6) {
      display.drawRect(40, 58, 40, 12, SSD1306_WHITE);
      display.drawRect(39, 57, 42, 14, SSD1306_WHITE);
      display.drawRect(38, 56, 44, 16, SSD1306_WHITE);
    }
    int startStopTextX = 40 + (40 - (String(servoRunning ? "Start" : "Stop").length() * 6)) / 2;
    display.setCursor(startStopTextX, 60);
    display.print(servoRunning ? "Start" : "Stop");

  } else { // Angle Set Menu
    display.setTextSize(1);
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
    display.drawTriangle(55, 20, 59, 20, 57, 18, SSD1306_WHITE);
    display.drawTriangle(55, 24, 59, 24, 57, 26, SSD1306_WHITE);

    display.drawRect(70, 16, 40, 12, SSD1306_WHITE);
    if (cursorPosition == 1) {
      display.drawRect(69, 15, 42, 14, SSD1306_WHITE);
      display.drawRect(68, 14, 44, 16, SSD1306_WHITE);
    }
    int finalTextX = 70 + (40 - (String(servoFinalAngle()).length() * 6)) / 2;
    display.setCursor(finalTextX, 18);
    display.print(servoFinalAngle());
    display.drawTriangle(115, 20, 119, 20, 117, 18, SSD1306_WHITE);
    display.drawTriangle(115, 24, 119, 24, 117, 26, SSD1306_WHITE);

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
  }
  display.display();
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
      Serial.println("Angle Up: Initial Angle " + String(servoInitialAngle()) + " for Servo-" + String(selectedServo));
      sendServoData();
    } else if (cursorPosition == 1 && servoFinalAngle() < 180) {
      setServoFinalAngle(servoFinalAngle() + 5);
      Serial.println("Angle Up: Final Angle " + String(servoFinalAngle()) + " for Servo-" + String(selectedServo));
      sendServoData();
    }
    lastButtonPress = currentTime;
  }
  if (digitalRead(angleDownPin) == LOW && currentMenu == 1) {
    if (cursorPosition == 0 && servoInitialAngle() > 0) {
      setServoInitialAngle(servoInitialAngle() - 5);
      Serial.println("Angle Down: Initial Angle " + String(servoInitialAngle()) + " for Servo-" + String(selectedServo));
      sendServoData();
    } else if (cursorPosition == 1 && servoFinalAngle() > 0) {
      setServoFinalAngle(servoFinalAngle() - 5);
      Serial.println("Angle Down: Final Angle " + String(servoFinalAngle()) + " for Servo-" + String(selectedServo));
      sendServoData();
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
        servoRunning = !servoRunning;
        Serial.println("Enter: All Servos " + String(servoRunning ? "started" : "stopped"));
        sendServoData();
      }
    } else if (cursorPosition == 2) {
      currentMenu = 0;
      cursorPosition = selectedServo - 1;
      Serial.println("Enter: Servo-" + String(selectedServo) + " angles saved");
      sendServoData();
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
  angle = constrain(angle, 0, 180); // Only constrain to 0-180
  initialAngles[selectedServo - 1] = angle;
}

void setServoFinalAngle(int angle) {
  angle = constrain(angle, 0, 180); // Only constrain to 0-180
  finalAngles[selectedServo - 1] = angle;
}