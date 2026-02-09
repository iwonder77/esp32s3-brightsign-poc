/**
 *  Interactive: NA
 *  File: esp32-button-udp.ino
 *  Description: send UDP commands from ESP32 to Mac upon button press
 *
 *  Author: Isai Sanchez
 *  Date: 2-6-2026
 *  Hardware:
 *    - ESP32 DevKitC v4
 *    - Adafruit WZ5500 Ethernet Breakout 
 *  Notes: 
 *    - Commands for testing from Mac terminal
 *
 * (c) Thanksgiving Point Exhibits Electronics Team — 2025
*/

#include <SPI.h>
#include <Ethernet.h>
#include <EthernetUdp.h>

// ===== NETWORK CONFIG =====
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01 };  // esp32 MAC address
IPAddress localIP(192, 168, 50, 2);                   // esp32 static IP address
IPAddress targetIP(192, 168, 50, 10);                 // BrightSign or Macbook for testing
const unsigned int UDP_PORT = 5000;

// ===== HARDWARE CONFIG =====
// ESP32S3-ETH module wiki specifies the following pinout for onboard WZ5500 use
// wiki link: https://www.waveshare.com/wiki/ESP32-S3-ETH#ETH_DHCP
const unsigned int W5500_CS = 14;    // Chip Select (CS)
const unsigned int W5500_RST = 9;    // Reset (RST)
const unsigned int W5500_INT = 10;   // Interrupt (INT) - optional
const unsigned int W5500_MISO = 12;  // MISO
const unsigned int W5500_MOSI = 11;  // MOSI
const unsigned int W5500_SCK = 13;   // SPI Clock (SCK)

// ===== TIMING =====
const unsigned long BUTTON_DEBOUNCE_MS = 250;
const unsigned long LINK_TIMEOUT = 10000;
const unsigned long STATUS_INTERVAL_MS = 10000;

// ===== BUTTON STRUCT =====
struct Button {
  const int pin;
  const char* command;  // UDP command to send when pressed
  volatile bool pressed;
  volatile unsigned long lastPressTime;
};

Button buttons[] = {
  { 1, "var1:video1mp4", false, 0 },
  { 2, "var2:video2mp4", false, 0 },
  { 3, "var3:video3mp4", false, 0 },
};
const uint8_t NUM_BUTTONS = 3;

// ===== GLOBAL STATE =====
EthernetUDP udp;

// simple state tracking
bool ethernetInitialized = false;
bool linkUp = false;
unsigned long packetsSent = 0;

// ===== INTERRUPT FUNCTIONS =====
void IRAM_ATTR buttonISR_0() {
  unsigned long now = millis();
  if (now - buttons[0].lastPressTime > BUTTON_DEBOUNCE_MS) {
    buttons[0].pressed = true;
    buttons[0].lastPressTime = now;
  }
}
void IRAM_ATTR buttonISR_1() {
  unsigned long now = millis();
  if (now - buttons[1].lastPressTime > BUTTON_DEBOUNCE_MS) {
    buttons[1].pressed = true;
    buttons[1].lastPressTime = now;
  }
}
void IRAM_ATTR buttonISR_2() {
  unsigned long now = millis();
  if (now - buttons[2].lastPressTime > BUTTON_DEBOUNCE_MS) {
    buttons[2].pressed = true;
    buttons[2].lastPressTime = now;
  }
}

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println();
  Serial.println("==========================================");
  Serial.println("       W5500 + ESP32 ETHERNET TEST        ");
  Serial.println("==========================================");
  Serial.println();

  for (uint8_t i = 0; i < NUM_BUTTONS; i++) {
    pinMode(buttons[i].pin, INPUT);
    switch (i) {
      case 0:
        attachInterrupt(digitalPinToInterrupt(buttons[i].pin), buttonISR_0, FALLING);
        break;
      case 1:
        attachInterrupt(digitalPinToInterrupt(buttons[i].pin), buttonISR_1, FALLING);
        break;
      case 2:
        attachInterrupt(digitalPinToInterrupt(buttons[i].pin), buttonISR_2, FALLING);
        break;
      default:
        break;
    }
  }

  // initialize SPI with custom pins
  SPI.begin(W5500_SCK, W5500_MISO, W5500_MOSI, W5500_CS);

  // hardware reset
  Serial.print("Step 1: Hardware reset on RST pin (GPIO ");
  Serial.print(W5500_RST);
  Serial.print(")");
  pinMode(W5500_RST, OUTPUT);
  digitalWrite(W5500_RST, LOW);
  delay(100);
  digitalWrite(W5500_RST, HIGH);
  delay(500);
  Serial.println("  Done.\n");

  // initialize ethernet
  Serial.println("Step 2: Initialize Ethernet library...");
  Ethernet.init(W5500_CS);
  Ethernet.begin(mac, localIP);
  Serial.println("  Done.\n");

  // check hardware status
  Serial.println("Step 3: Hardware detection...");
  switch (Ethernet.hardwareStatus()) {
    case EthernetNoHardware:
      Serial.println("  FAIL: No hardware found");
      ethernetInitialized = false;
      break;
    case EthernetW5100:
      Serial.println("  Found: W5100 (unexpected)");
      ethernetInitialized = false;
      break;
    case EthernetW5200:
      Serial.println("  Found: W5200 (unexpected)");
      ethernetInitialized = false;
      break;
    case EthernetW5500:
      Serial.println("  Found: W5500 (correct!)");
      ethernetInitialized = true;
      break;
    default:
      Serial.println("  Unknown hardware");
      ethernetInitialized = false;
  }

  // wait for link to come up
  Serial.println("\nStep 4: Waiting for ethernet link...");
  unsigned long startTime = millis();
  while (Ethernet.linkStatus() != LinkON) {
    if (millis() - startTime > LINK_TIMEOUT) {
      Serial.println("  TIMEOUT: link did not come up after 10s timeout");
      Serial.println("  Continuing anyway - link may come up later");
      break;
    }
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  switch (Ethernet.linkStatus()) {
    case Unknown:
      Serial.println("  UNKNOWN");
      linkUp = false;
      break;
    case LinkON:
      Serial.println("  UP! ← Success!");
      linkUp = true;
      break;
    case LinkOFF:
      Serial.println("  DOWN");
      linkUp = false;
      break;
    default:
      linkUp = false;
      break;
  }

  // start UDP server
  Serial.print("Step 5: Starting UDP server, binding to port: ");
  Serial.println(UDP_PORT);
  udp.begin(UDP_PORT);
}

void loop() {
  // Update link status
  updateLinkStatus();

  // check for button press and send UDP command accordingly
  if (ethernetInitialized && linkUp) {
    for (uint8_t i = 0; i < NUM_BUTTONS; i++) {
      if (buttons[i].pressed) {
        buttons[i].pressed = false;
        sendUdp(i, buttons[i].command);
      }
    }
  }

  // Periodic status update
  printPeriodicStatus();

  // Maintain Ethernet (DHCP renewal, etc.)
  Ethernet.maintain();

  delay(10);
}

void updateLinkStatus() {
  static EthernetLinkStatus lastStatus = Unknown;
  static unsigned long lastChange = 0;

  EthernetLinkStatus currentStatus = Ethernet.linkStatus();

  if (currentStatus != lastStatus) {
    lastStatus = currentStatus;
    lastChange = millis();

    Serial.print("[");
    printTimestamp();
    Serial.print("] LINK STATUS: ");

    switch (currentStatus) {
      case LinkON:
        Serial.println("UP ✓ ←── Cable connected!");
        linkUp = true;
        break;
      case LinkOFF:
        Serial.println("DOWN ✗ ←── Check cable");
        linkUp = false;
        break;
      case Unknown:
        Serial.println("UNKNOWN");
        linkUp = false;
        break;
    }
  }
}

void sendUdp(size_t buttonIndex, const char* command) {

  udp.beginPacket(targetIP, UDP_PORT);  // address and port to send data to
  udp.print(command);                   // build data
  udp.endPacket();                      // send it NOW

  Serial.print("Button {");
  Serial.print(buttonIndex);
  Serial.print("} pressed! Sending: ");
  Serial.println(command);

  // update
  packetsSent++;
}

void printPeriodicStatus() {
  static unsigned long lastStatusTime = 0;

  if (millis() - lastStatusTime >= STATUS_INTERVAL_MS) {
    lastStatusTime = millis();

    Serial.print("[");
    printTimestamp();
    Serial.print("] Status: Link=");
    Serial.print(linkUp ? "UP" : "DOWN");
    Serial.print(", IP=");
    Serial.print(Ethernet.localIP());
    Serial.print(", TX=");
    Serial.println(packetsSent);
  }
}
void printTimestamp() {
  unsigned long seconds = millis() / 1000;
  unsigned long minutes = seconds / 60;
  seconds = seconds % 60;

  if (minutes < 10) Serial.print("0");
  Serial.print(minutes);
  Serial.print(":");
  if (seconds < 10) Serial.print("0");
  Serial.print(seconds);
}
