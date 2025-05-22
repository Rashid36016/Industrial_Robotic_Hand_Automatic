#include <esp_now.h>
#include <WiFi.h>
#include <ESP32Servo.h>

const int servoPins[6] = {26, 27, 19, 23, 25, 33};
Servo servos[6];
int initialAngles[6] = {90, 90, 90, 90, 90, 90};
int finalAngles[6] = {90, 90, 90, 90, 90, 90};
int currentAngles[6] = {90, 90, 90, 90, 90, 90};
bool servoRunning = false;
int currentServo = 0; // Tracks current servo
bool isInitialToFinalPhase = true; // Tracks phase (Initial-to-Final or Final-to-Initial)

const int normalDelay = 10;   // 10ms/deg for Initial-to-Final (slower)
const float fastDelay = 10.0 / 3; // 3.33ms/deg for Final-to-Initial (~3x faster)
const int movementDelay = 500; // 500ms delay between servo movements

typedef struct struct_message {
  int initialAngles[6];
  int finalAngles[6];
  bool servoRunning;
} struct_message;

struct_message servoData;

void OnDataRecv(const esp_now_recv_info *recvInfo, const uint8_t *incomingData, int len) {
  memcpy(&servoData, incomingData, sizeof(servoData));
  for (int i = 0; i < 6; i++) {
    initialAngles[i] = servoData.initialAngles[i];
    finalAngles[i] = servoData.finalAngles[i];
  }
  servoRunning = servoData.servoRunning;
  Serial.println("Received: servoRunning = " + String(servoRunning));
  for (int i = 0; i < 6; i++) {
    Serial.println("Servo-" + String(i + 1) + ": Initial = " + String(initialAngles[i]) + ", Final = " + String(finalAngles[i]));
  }
  if (!servoRunning) {
    currentServo = 0; // Reset to Servo-1
    isInitialToFinalPhase = true; // Reset to Initial-to-Final phase
  }
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("Receiver Starting...");

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW initialization failed");
    return;
  }
  esp_now_register_recv_cb(OnDataRecv);
  Serial.println("ESP-NOW initialized");

  for (int i = 0; i < 6; i++) {
    servos[i].setPeriodHertz(50);
    if (!servos[i].attach(servoPins[i], 500, 2400)) {
      Serial.println("Servo-" + String(i + 1) + " failed to attach");
    } else {
      servos[i].write(90); // Set to 90° at startup
      currentAngles[i] = 90;
      Serial.println("Servo-" + String(i + 1) + " set to 90 deg");
    }
  }
}

void loop() {
  if (servoRunning) {
    runServoLoop();
  }
}

void runServoLoop() {
  // Skip servo if Initial = Final
  if (initialAngles[currentServo] == finalAngles[currentServo]) {
    Serial.println("Servo-" + String(currentServo + 1) + " skipped (Initial = Final)");
    advanceServo();
    return;
  }

  // Move current servo
  if (isInitialToFinalPhase) {
    if (currentAngles[currentServo] < finalAngles[currentServo]) {
      currentAngles[currentServo]++;
      servos[currentServo].write(currentAngles[currentServo]);
      delay(normalDelay);
    } else if (currentAngles[currentServo] > finalAngles[currentServo]) {
      currentAngles[currentServo]--;
      servos[currentServo].write(currentAngles[currentServo]);
      delay(normalDelay);
    } else {
      Serial.println("Servo-" + String(currentServo + 1) + " reached Final");
      delay(movementDelay); // Delay after movement
      advanceServo();
    }
  } else {
    if (currentAngles[currentServo] < initialAngles[currentServo]) {
      currentAngles[currentServo]++;
      servos[currentServo].write(currentAngles[currentServo]);
      delay(fastDelay);
    } else if (currentAngles[currentServo] > initialAngles[currentServo]) {
      currentAngles[currentServo]--;
      servos[currentServo].write(currentAngles[currentServo]);
      delay(fastDelay);
    } else {
      Serial.println("Servo-" + String(currentServo + 1) + " reached Initial");
      delay(movementDelay); // Delay after movement
      advanceServo();
    }
  }
}

void advanceServo() {
  currentServo++;
  if (currentServo >= 6) {
    currentServo = 0;
    isInitialToFinalPhase = !isInitialToFinalPhase; // Switch phase
    Serial.println(isInitialToFinalPhase ? "Starting Initial-to-Final phase" : "Starting Final-to-Initial phase");
  }
}