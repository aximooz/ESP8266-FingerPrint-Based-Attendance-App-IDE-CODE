#include <SPI.h>
#include <Wire.h>
#include <WiFiClient.h>
#include <ESP8266WiFi.h>
#include <SoftwareSerial.h>
#include <ESP8266HTTPClient.h>
#include <Adafruit_Fingerprint.h>

#define Finger_Rx 0  // D3
#define Finger_Tx 2  // D4

SoftwareSerial mySerial(Finger_Rx, Finger_Tx);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);

const char *ssid = "ASIM";      // Change this
const char *password = "123123123";   // Change this
String serverURL = "http://192.168.205.234:3001/attendance";  // Backend URL
String enrollURL = "http://192.168.205.234:3001/enroll";      // Enrollment URL
String checkEnrollmentURL = "http://192.168.205.234:3001/check-enrollment-requests"; // Check pending requests

void setup() {
  Serial.begin(115200);
  connectToWiFi();
  finger.begin(57600);

  Serial.println("\n\nFingerprint Scanner Initialization");
  if (finger.verifyPassword()) {
    Serial.println("Fingerprint sensor detected!");
  } else {
    Serial.println("Did not find fingerprint sensor :(");
    while (1) { delay(1); }
  }

  Serial.println("Waiting for a fingerprint...");
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connectToWiFi();
  }

  // Check for pending enrollment requests
  checkEnrollmentRequests();

  // Regular fingerprint scanning
  int FingerID = getFingerprintID();

  if (FingerID > 0) {  // Existing Finger Detected (Registered)
    Serial.print("Registered Finger Detected! ID: ");
    Serial.println(FingerID);
    sendToServer(FingerID);  // Send attendance data
  } else if (FingerID == -1) {  // Unregistered Finger Detected
    Serial.println("Unregistered Finger Detected! Ignoring...");
  } else {
    Serial.println("Place your finger on the scanner...");
  }

  delay(200);  // Delay to avoid overwhelming the scanner
}

// ✅ Connect to WiFi
void connectToWiFi() {
  Serial.println("Connecting to WiFi...");
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected!");
}

// ✅ Send Data to Backend
void sendToServer(int fingerID) {
  WiFiClient client;
  HTTPClient http;
  http.begin(client, serverURL);
  http.addHeader("Content-Type", "application/json");

  String payload = "{\"fingerprintID\":" + String(fingerID) + "}";
  int httpCode = http.POST(payload);

  Serial.print("HTTP Response Code: ");
  Serial.println(httpCode);

  String response = http.getString();
  Serial.println("Server Response: " + response);

  http.end();
}

// ✅ Check for pending enrollment requests
void checkEnrollmentRequests() {
  WiFiClient client;
  HTTPClient http;
  http.begin(client, checkEnrollmentURL);
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    int fingerprintID = parseFingerprintID(payload);
    if (fingerprintID >= 1 && fingerprintID <= 127) {
      Serial.print("Enrolling Fingerprint ID: ");
      Serial.println(fingerprintID);
      enrollFingerprint(fingerprintID);
    }
  }

  http.end();
}

// ✅ Enroll New Fingerprint
void enrollFingerprint(int fingerID) {
  Serial.print("Enrolling Fingerprint ID: ");
  Serial.println(fingerID);

  int p = -1;
  Serial.println("Waiting for valid finger...");

  while (p != FINGERPRINT_OK) {
    p = finger.getImage();
    if (p == FINGERPRINT_NOFINGER) {
      // No finger detected, keep waiting
    } else if (p == FINGERPRINT_PACKETRECIEVEERR) {
      Serial.println("Communication error");
    } else if (p == FINGERPRINT_IMAGEFAIL) {
      Serial.println("Imaging error");
    }
  }

  p = finger.image2Tz(1);
  if (p != FINGERPRINT_OK) {
    Serial.println("Error converting fingerprint image");
    return;
  }

  Serial.println("Remove finger and place again...");
  delay(2000);

  p = -1;
  while (p != FINGERPRINT_OK) {
    p = finger.getImage();
    if (p == FINGERPRINT_NOFINGER) {
      Serial.print(".");
    }
  }

  p = finger.image2Tz(2);
  if (p != FINGERPRINT_OK) {
    Serial.println("Error converting fingerprint image");
    return;
  }

  p = finger.createModel();
  if (p != FINGERPRINT_OK) {
    Serial.println("Error creating fingerprint model");
    return;
  }

  p = finger.storeModel(fingerID);
  if (p == FINGERPRINT_OK) {
    Serial.println("Fingerprint enrolled successfully!");

    // Notify backend about successful enrollment
    sendEnrollmentSuccess(fingerID);
  } else {
    Serial.println("Failed to enroll fingerprint");
  }
}

// ✅ Notify Backend After Enrollment
void sendEnrollmentSuccess(int fingerID) {
  WiFiClient client;
  HTTPClient http;
  http.begin(client, enrollURL);
  http.addHeader("Content-Type", "application/json");

  String payload = "{\"fingerprintID\":" + String(fingerID) + ", \"status\":\"enrolled\"}";
  int httpCode = http.POST(payload);

  Serial.print("Enrollment HTTP Response Code: ");
  Serial.println(httpCode);

  String response = http.getString();
  Serial.println("Enrollment Server Response: " + response);

  http.end();
}

// ✅ Read Fingerprint ID
int getFingerprintID() {
  uint8_t p = finger.getImage();
  if (p != FINGERPRINT_OK) {
    return 0;  // No finger detected
  }

  p = finger.image2Tz();
  if (p != FINGERPRINT_OK) return 0;  // Error in image conversion

  p = finger.fingerFastSearch();
  if (p == FINGERPRINT_OK) {
    return finger.fingerID;  // Recognized fingerprint ID
  } else if (p == FINGERPRINT_NOTFOUND) {
    return -1;  // Finger detected but not stored
  } else {
    return 0;  // Other errors (e.g., communication error)
  }
}

// ✅ Parse Fingerprint ID from JSON response
int parseFingerprintID(String payload) {
  // Example payload: {"fingerprintID":5}
  int startIndex = payload.indexOf(":") + 1;
  int endIndex = payload.indexOf("}");
  String idStr = payload.substring(startIndex, endIndex);
  return idStr.toInt();
}