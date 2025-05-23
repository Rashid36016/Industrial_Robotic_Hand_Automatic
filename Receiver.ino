#include <esp_now.h>
#include <WiFi.h>
#include <ESP32Servo.h>
const int servoPins[6] = {26, 27, 19, 23, 25, 33};
Servo servos[6];
float initialAngles[6] = {90, 90, 90, 90, 90, 90}; // Changed to float
float finalAngles[6] = {90, 90, 90, 90, 90, 90};   // Changed to float
float currentAngles[6] = {90, 90, 90, 90, 90, 90}; // Changed to float
bool servoRunning = false;
int currentServo = 0; // Tracks current servo
bool isInitialToFinalPhase = true; // Tracks phase (Initial-to-Final or Final-to-Initial)
bool restartRequested = false; // Tracks if restart is requested
float newInitialAngles[6] = {90, 90, 90, 90, 90, 90}; // Changed to float
float newFinalAngles[6] = {90, 90, 90, 90, 90, 90};   // Changed to float
int restartPhase = 0; // 0: Normal, 1: Old Final, 2: New Initial
const float stepSize = 0.1; // 0.1 degree steps for smooth movement
const float normalDelay = 30.0 / 10; // 3ms per 0.1 deg (30ms/deg)
const float fastDelay = (10.0 / 3) / 10; // 0.333ms per 0.1 deg (~3.33ms/deg)
const int movementDelay = 500; // 500ms delay between servo movements
typedef struct struct_message {
    int initialAngles[6]; // Sender still uses int
    int finalAngles[6];   // Sender still uses int
    bool servoRunning;
    bool restartRequested; // Flag for restart
} struct_message;
struct_message servoData;
struct_message lastSentData; // Store last sent data
void OnDataRecv(const esp_now_recv_info *recvInfo, const uint8_t *incomingData, int len) {
    memcpy(&servoData, incomingData, sizeof(servoData));
    if (!servoRunning) {
        // If not running, apply angles immediately
        for (int i = 0; i < 6; i++) {
            initialAngles[i] = (float)servoData.initialAngles[i]; // Cast to float
            finalAngles[i] = (float)servoData.finalAngles[i];     // Cast to float
            currentAngles[i] = initialAngles[i]; // Align currentAngles
        }
        servoRunning = servoData.servoRunning;
        restartRequested = servoData.restartRequested;
        lastSentData = servoData; // Store the last sent data
        Serial.println("Received: servoRunning = " + String(servoRunning) + ", restartRequested = " + String(restartRequested));
        for (int i = 0; i < 6; i++) {
            Serial.println("Servo-" + String(i + 1) + ": Initial = " + String(initialAngles[i]) + ", Final = " + String(finalAngles[i]));
        }
        if (!servoRunning) {
            currentServo = 0; // Reset to Servo-1
            isInitialToFinalPhase = true; // Reset to Initial-to-Final phase
            restartPhase = 0; // Reset to normal operation
        }
    } else {
        // If running, store data for later
        lastSentData = servoData;
        if (servoData.restartRequested) {
            restartRequested = true;
            for (int i = 0; i < 6; i++) {
                newInitialAngles[i] = (float)servoData.initialAngles[i]; // Cast to float
                newFinalAngles[i] = (float)servoData.finalAngles[i];     // Cast to float
            }
            Serial.println("Received restart request, stored new angles");
        } else {
            Serial.println("Received new data but servos are running. Stored for next cycle.");
        }
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
            servos[i].write(90); // Set to 90 deg at startup
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
    if (restartPhase == 1) { // Old Final phase
        if (initialAngles[currentServo] == finalAngles[currentServo]) {
            Serial.println("Servo-" + String(currentServo + 1) + " skipped (Initial = Final)");
            advanceServo();
            return;
        }
        if (currentAngles[currentServo] < finalAngles[currentServo] - stepSize) {
            currentAngles[currentServo] += stepSize;
            servos[currentServo].write(currentAngles[currentServo]);
            delayMicroseconds((int)(normalDelay * 1000)); // Convert ms to us
        } else if (currentAngles[currentServo] > finalAngles[currentServo] + stepSize) {
            currentAngles[currentServo] -= stepSize;
            servos[currentServo].write(currentAngles[currentServo]);
            delayMicroseconds((int)(normalDelay * 1000)); // Convert ms to us
        } else {
            currentAngles[currentServo] = finalAngles[currentServo]; // Snap to target
            servos[currentServo].write(currentAngles[currentServo]);
            Serial.println("Servo-" + String(currentServo + 1) + " reached old Final");
            delay(movementDelay); // Delay after movement
            advanceServo();
        }
    } else if (restartPhase == 2) { // New Initial phase
        if (abs(currentAngles[currentServo] - newInitialAngles[currentServo]) < stepSize) {
            currentAngles[currentServo] = newInitialAngles[currentServo]; // Snap to target
            Serial.println("Servo-" + String(currentServo + 1) + " skipped (current = new Initial)");
            advanceServo();
            return;
        }
        if (currentAngles[currentServo] < newInitialAngles[currentServo] - stepSize) {
            currentAngles[currentServo] += stepSize;
            servos[currentServo].write(currentAngles[currentServo]);
            delayMicroseconds((int)(normalDelay * 1000)); // Convert ms to us
        } else if (currentAngles[currentServo] > newInitialAngles[currentServo] + stepSize) {
            currentAngles[currentServo] -= stepSize;
            servos[currentServo].write(currentAngles[currentServo]);
            delayMicroseconds((int)(normalDelay * 1000)); // Convert ms to us
        } else {
            currentAngles[currentServo] = newInitialAngles[currentServo]; // Snap to target
            servos[currentServo].write(currentAngles[currentServo]);
            Serial.println("Servo-" + String(currentServo + 1) + " reached new Initial");
            delay(movementDelay); // Delay after movement
            advanceServo();
        }
    } else { // Normal operation
        if (initialAngles[currentServo] == finalAngles[currentServo]) {
            Serial.println("Servo-" + String(currentServo + 1) + " skipped (Initial = Final)");
            advanceServo(); // No delay for skipped servos
            return;
        }
        if (isInitialToFinalPhase) {
            if (currentAngles[currentServo] < finalAngles[currentServo] - stepSize) {
                currentAngles[currentServo] += stepSize;
                servos[currentServo].write(currentAngles[currentServo]);
                delayMicroseconds((int)(normalDelay * 1000)); // Convert ms to us
            } else if (currentAngles[currentServo] > finalAngles[currentServo] + stepSize) {
                currentAngles[currentServo] -= stepSize;
                servos[currentServo].write(currentAngles[currentServo]);
                delayMicroseconds((int)(normalDelay * 1000)); // Convert ms to us
            } else {
                currentAngles[currentServo] = finalAngles[currentServo]; // Snap to target
                servos[currentServo].write(currentAngles[currentServo]);
                Serial.println("Servo-" + String(currentServo + 1) + " reached Final");
                delay(movementDelay); // Delay after movement
                advanceServo();
            }
        } else {
            if (currentAngles[currentServo] < initialAngles[currentServo] - stepSize) {
                currentAngles[currentServo] += stepSize;
                servos[currentServo].write(currentAngles[currentServo]);
                delayMicroseconds((int)(fastDelay * 1000)); // Convert ms to us
            } else if (currentAngles[currentServo] > initialAngles[currentServo] + stepSize) {
                currentAngles[currentServo] -= stepSize;
                servos[currentServo].write(currentAngles[currentServo]);
                delayMicroseconds((int)(fastDelay * 1000)); // Convert ms to us
            } else {
                currentAngles[currentServo] = initialAngles[currentServo]; // Snap to target
                servos[currentServo].write(currentAngles[currentServo]);
                Serial.println("Servo-" + String(currentServo + 1) + " reached Initial");
                delay(movementDelay); // Delay after movement
                advanceServo();
            }
        }
    }
}
void advanceServo() {
    currentServo++;
    if (currentServo >= 6) {
        currentServo = 0;
        if (restartRequested && restartPhase == 0) {
            // Start Old Final phase
            restartPhase = 1;
            Serial.println("Restart: Starting old final positions phase");
        } else if (restartPhase == 1) {
            // Move to New Initial phase
            restartPhase = 2;
            for (int i = 0; i < 6; i++) {
                initialAngles[i] = newInitialAngles[i];
                finalAngles[i] = newFinalAngles[i];
                currentAngles[i] = initialAngles[i]; // Align with new initial
            }
            Serial.println("Restart: All servos reached old final positions, transitioning to new initial positions");
        } else if (restartPhase == 2) {
            // Resume normal operation
            restartPhase = 0;
            restartRequested = false;
            isInitialToFinalPhase = true; // Start new cycle with Initial-to-Final
            for (int i = 0; i < 6; i++) {
                currentAngles[i] = initialAngles[i]; // Ensure alignment
            }
            Serial.println("Restart: All servos reached new initial positions, resuming normal operation");
        } else {
            // Normal cycle: Switch phase
            isInitialToFinalPhase = !isInitialToFinalPhase;
            Serial.println(isInitialToFinalPhase ? "Starting Initial-to-Final phase" : "Starting Final-to-Initial phase");
        }
        // Apply any new data received during running
        if (lastSentData.servoRunning != servoRunning || lastSentData.restartRequested) {
            servoRunning = lastSentData.servoRunning;
            restartRequested = lastSentData.restartRequested;
            for (int i = 0; i < 6; i++) {
                newInitialAngles[i] = (float)lastSentData.initialAngles[i]; // Cast to float
                newFinalAngles[i] = (float)lastSentData.finalAngles[i];     // Cast to float
            }
            if (restartPhase == 0) {
                for (int i = 0; i < 6; i++) {
                    initialAngles[i] = newInitialAngles[i];
                    finalAngles[i] = newFinalAngles[i];
                    currentAngles[i] = isInitialToFinalPhase ? initialAngles[i] : finalAngles[i]; // Align with phase
                }
            }
            Serial.println("Applied new data after cycle completion");
        }
    }
}