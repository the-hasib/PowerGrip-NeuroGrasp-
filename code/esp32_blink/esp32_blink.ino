#include <WiFi.h>
#include <HTTPClient.h>
#include <ESP32Servo.h>

// ==========================================
// --- USER SETTINGS (FILL THESE IN!) ---
// ==========================================
const char* ssid     = "Areosol";      // <--- Enter WiFi Name
const char* password = "CFCFree*";  // <--- Enter WiFi Password
String apiKey        = "OK468N9TMHFXRHPZ";  // <--- Enter ThingSpeak Key

// ==========================================
// --- PIN DEFINITIONS ---
// ==========================================
const int POT_PIN    = 34; // Mirror Knob
const int FSR_PIN    = 35; // Pressure Sensor
const int BUTTON_PIN = 4;  // Manual Trigger
const int SERVO_PIN  = 13; // Motor

// ==========================================
// --- CALIBRATION & THRESHOLDS ---
// ==========================================
Servo myServo;

// Servo Limits
const int OPEN_POS   = 0;
const int CLOSED_POS = 180;
const int POT_MIN    = 0;
const int POT_MAX    = 850; 

// Safety
const int SAFETY_LIMIT = 3000; // Robot stops if pressure > 3000

// Rep Counting (Hybrid Logic)
const int REP_ANGLE_THRESHOLD    = 130;  // Count as closed if Angle > 130
const int REP_PRESSURE_THRESHOLD = 1000; // Count as closed if Pressure > 1000
const int REP_RESET_THRESHOLD    = 20;   // Count as open if Angle < 20

// Anti-Jitter
const int JITTER_THRESHOLD = 3;

// ==========================================
// --- VARIABLES ---
// ==========================================
int currentServoAngle = OPEN_POS;
int repsCount = 0;
int maxPressureSession = 0;
bool repStarted = false; 

// IoT Timer
unsigned long lastUploadTime = 0;
const long uploadInterval = 16000; // 16 Seconds (Safe for ThingSpeak)

void setup() {
  Serial.begin(115200);

  // 1. Connect to WiFi
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  // We do NOT wait forever here, so the motor works even without WiFi
  int timeout = 0;
  while (WiFi.status() != WL_CONNECTED && timeout < 20) {
    delay(500);
    Serial.print(".");
    timeout++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi Connected!");
  } else {
    Serial.println("\nWiFi Failed (Proceeding offline mode)");
  }

  // 2. Hardware Setup
  myServo.setPeriodHertz(50);
  myServo.attach(SERVO_PIN, 500, 2400);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  Serial.println("--- POWERGRIP: ALL SYSTEMS ONLINE ---");
}

// --- ADD THIS VARIABLE AT THE TOP WITH OTHER VARIABLES ---
unsigned long lastRepTime = 0; // Timer to prevent machine-gun counting

void loop() {
  // --- 1. READ SENSORS ---
  int fsrValue = analogRead(FSR_PIN);
  int potValue = analogRead(POT_PIN);
  int btnState = digitalRead(BUTTON_PIN);

  // --- 2. REP COUNTING LOGIC (WITH DEBOUNCE FIX) ---
  
  // A. Detect START of a Rep
  // Only allow a rep to start if we are NOT currently in a "cooldown" period
  if (!repStarted) {
    bool isGripping = (fsrValue > REP_PRESSURE_THRESHOLD);
    bool isMoving   = (currentServoAngle > REP_ANGLE_THRESHOLD);
    
    if (isGripping || isMoving) {
      repStarted = true;
    }
  }

  // B. Detect END of a Rep (Release)
  if (repStarted && currentServoAngle < REP_RESET_THRESHOLD) {
    // *** THE FIX: COOLDOWN CHECK ***
    // Only count if 2000ms (2 seconds) have passed since the last rep
    if (millis() - lastRepTime > 2000) { 
      repsCount++;
      lastRepTime = millis(); // Reset the timer
      repStarted = false;
      Serial.print(">>> VALID Rep Counted! Total: "); Serial.println(repsCount);
    } 
    // If it's too fast, we ignore it but reset the state
    else {
      repStarted = false; 
    }
  }

  // C. Track Strength (Fixing the reset issue)
  if (fsrValue > maxPressureSession) {
    maxPressureSession = fsrValue;
  }

  // --- 3. MOTOR CONTROL ---
  int targetAngle = OPEN_POS;
  int modeID = 0; 

  if (btnState == LOW) {
    targetAngle = CLOSED_POS; 
    modeID = 1;
  } else {
    targetAngle = map(potValue, POT_MIN, POT_MAX, OPEN_POS, CLOSED_POS);
    if (targetAngle < 0) targetAngle = 0;
    if (targetAngle > 180) targetAngle = 180;
    modeID = 0;
  }

  // --- 4. SAFETY STOP ---
  if (fsrValue > SAFETY_LIMIT && targetAngle > currentServoAngle) {
    // Safety Hold
  } else {
    if (abs(targetAngle - currentServoAngle) > JITTER_THRESHOLD) {
      myServo.write(targetAngle);
      currentServoAngle = targetAngle;
    }
  }

  // --- 5. CLOUD UPLOAD ---
  if (millis() - lastUploadTime > uploadInterval) {
    if(WiFi.status() == WL_CONNECTED){
      HTTPClient http;
      String url = "http://api.thingspeak.com/update?api_key=" + apiKey + 
                   "&field1=" + String(repsCount) + 
                   "&field2=" + String(maxPressureSession) + 
                   "&field3=" + String(modeID);
      http.begin(url);
      int httpCode = http.GET();
      http.end();
      
      if (httpCode > 0) Serial.println("Data sent to ThingSpeak!");
    }
    lastUploadTime = millis();
  }
  
  delay(15); 
}