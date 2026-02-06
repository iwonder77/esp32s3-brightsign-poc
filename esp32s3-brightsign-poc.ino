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
 *    - Wiring to WZ5500 is as follows:
 *      External 3v3 Power Supply   -> VIN
 *      External GND and ESP32 GND  -> GND
 *      ESP32 GPIO18                -> SCK
 *      ESP32 GPIO19                -> MISO
 *      ESP32 GPIO23                -> MOSI
 *      ESP32 GPIO5                 -> CS
 *      N.C.                        -> IRQ
 *      ESP32 GPIO4                 -> RST
 *    - Commands for testing from Mac terminal
 *
 * (c) Thanksgiving Point Exhibits Electronics Team — 2025
*/

#include <SPI.h>
#include <Ethernet.h>
#include <EthernetUdp.h>

// ===== HARDWARE CONFIG =====
const unsigned int W5500_CS = 14;    // Chip Select (CS)
const unsigned int W5500_RST = 9;    // Reset (RST)
const unsigned int W5500_INT = 10;   // Interrupt (INT) - optional
const unsigned int W5500_MISO = 12;  // MISO
const unsigned int W5500_MOSI = 11;  // MOSI
const unsigned int W5500_SCK = 13;   // SPI Clock (SCK)
const unsigned int BUTTON1_PIN = 26;
const unsigned int BUTTON2_PIN = 27;
const unsigned int BUTTON3_PIN = 32;

// ===== INTERRUPT FLAGS =====
volatile bool isButton1Pressed = false;
volatile bool isButton2Pressed = false;
volatile bool isButton3Pressed = false;

// ===== INTERRUPT TIMESTAMPTS =====
volatile unsigned long lastButton1Press = 0;
volatile unsigned long lastButton2Press = 0;
volatile unsigned long lastButton3Press = 0;
const unsigned long BUTTON_DEBOUNCE_MS = 250;

// ===== NETWORK CONFIG =====
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01 };  // esp32 MAC address
IPAddress ip(192, 168, 50, 2);                        // esp32 static IP address
IPAddress macIp(192, 168, 50, 1);
const unsigned int UDP_PORT = 5000;
const unsigned int MAC_UDP_PORT = 5000;

EthernetUDP udp;

// receive buffer
const size_t BUFFER_SIZE = 256;
char packetBuffer[BUFFER_SIZE];

// simple state tracking
bool ethernetInitialized = false;
bool linkUp = false;
unsigned long packetsReceived = 0;
unsigned long packetsSent = 0;
const unsigned long LINK_TIMEOUT = 10000;

// ===== INTERRUPT FUNCTIONS =====
void IRAM_ATTR button_isr1() {
  unsigned long currentButton1Press = millis();
  if (currentButton1Press - lastButton1Press > BUTTON_DEBOUNCE_MS) {
    isButton1Pressed = true;
  }
  lastButton1Press = currentButton1Press;
}
void IRAM_ATTR button_isr2() {
  unsigned long currentButton2Press = millis();
  if (currentButton2Press - lastButton2Press > BUTTON_DEBOUNCE_MS) {
    isButton2Pressed = true;
  }
  lastButton2Press = currentButton2Press;
}
void IRAM_ATTR button_isr3() {
  unsigned long currentButton3Press = millis();
  if (currentButton3Press - lastButton3Press > BUTTON_DEBOUNCE_MS) {
    isButton3Pressed = true;
  }
  lastButton3Press = currentButton3Press;
}

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println();
  Serial.println("==========================================");
  Serial.println("       W5500 + ESP32 ETHERNET TEST        ");
  Serial.println("==========================================");
  Serial.println();

  pinMode(BUTTON1_PIN, INPUT);
  pinMode(BUTTON2_PIN, INPUT);
  pinMode(BUTTON3_PIN, INPUT);
  attachInterrupt(BUTTON1_PIN, button_isr1, FALLING);
  attachInterrupt(BUTTON2_PIN, button_isr2, FALLING);
  attachInterrupt(BUTTON3_PIN, button_isr3, FALLING);

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
  Ethernet.begin(mac, ip);
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
    if (isButton1Pressed) {
      isButton1Pressed = false;
      sendUdp(1);
    }
    if (isButton2Pressed) {
      isButton2Pressed = false;
      sendUdp(2);
    }
    if (isButton3Pressed) {
      isButton3Pressed = false;
      sendUdp(3);
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

void sendUdp(size_t button) {
  String response;
  switch (button) {
    case 1:
      response = "FOO";
      break;
    case 2:
      response = "BAR";
      break;
    case 3:
      response = "BAZ";
      break;
    default:
      break;
  }

  udp.beginPacket(macIp, MAC_UDP_PORT);  // address and port to send data to
  udp.print(response);                   // build data
  udp.endPacket();                       // send it NOW

  Serial.print("Button {");
  Serial.print(button);
  Serial.print("} pressed! Sending: ");
  Serial.println(response);

  // update
  packetsSent++;
}

void printPeriodicStatus() {
  static unsigned long lastStatusTime = 0;
  const unsigned long STATUS_INTERVAL = 10000;  // Every 10 seconds

  if (millis() - lastStatusTime >= STATUS_INTERVAL) {
    lastStatusTime = millis();

    Serial.print("[");
    printTimestamp();
    Serial.print("] Status: Link=");
    Serial.print(linkUp ? "UP" : "DOWN");
    Serial.print(", IP=");
    Serial.print(Ethernet.localIP());
    Serial.print(", RX=");
    Serial.print(packetsReceived);
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
