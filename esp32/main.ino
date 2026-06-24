#include <WiFi.h>

const char* WIFI_SSID = "JRS";
const char* WIFI_PASSWORD = "0324010324";

const uint16_t TCP_PORT = 5000;
const uint32_t USB_BAUD = 115200;
const uint32_t STM32_BAUD = 115200;

const int STM32_RX_PIN = D0;  // ESP32-S3 Nano RX: connect to STM32 TX
const int STM32_TX_PIN = D1;  // ESP32-S3 Nano TX: connect to STM32 RX

WiFiServer server(TCP_PORT);
WiFiClient client;

String usbLine;
String stm32Line;

void connectToWiFi();
void acceptTcpClient();
void readTcpCommand();
void readUsbCommand();
void readStm32Response();
void sendCommandToStm32(const String& command);
void publishStm32Response(const String& response);

void setup() {
  Serial.begin(USB_BAUD);
  Serial1.begin(STM32_BAUD, SERIAL_8N1, STM32_RX_PIN, STM32_TX_PIN);
  delay(2000);

  Serial.println();
  Serial.println("ESP32-S3 Nano bridge starting.");
  Serial.print("STM32 UART RX pin: D0, TX pin: D1, baud: ");
  Serial.println(STM32_BAUD);

  connectToWiFi();
  server.begin();

  Serial.print("TCP server listening on port ");
  Serial.println(TCP_PORT);
  Serial.println("You can also type PING/PAN_LEFT/STOP in this Serial Monitor.");
}

void loop() {
  acceptTcpClient();
  readTcpCommand();
  readUsbCommand();
  readStm32Response();
}

void connectToWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting to Wi-Fi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("Connected!");
  Serial.print("ESP32 IP: ");
  Serial.println(WiFi.localIP());
}

void acceptTcpClient() {
  if (client && client.connected()) {
    return;
  }

  WiFiClient newClient = server.available();

  if (newClient) {
    client = newClient;
    Serial.println("Laptop connected.");
    client.println("{\"status\":\"connected\"}");
  }
}

void readTcpCommand() {
  if (!client || !client.connected() || !client.available()) {
    return;
  }

  String message = client.readStringUntil('\n');
  message.trim();

  if (message.length() > 0) {
    Serial.print("PC -> ESP32: ");
    Serial.println(message);
    sendCommandToStm32(message);
  }
}

void readUsbCommand() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();

    if (c == '\r') {
      continue;
    }

    if (c == '\n') {
      usbLine.trim();

      if (usbLine.length() > 0) {
        Serial.print("USB -> ESP32: ");
        Serial.println(usbLine);
        sendCommandToStm32(usbLine);
      }

      usbLine = "";
    } else {
      usbLine += c;
    }
  }
}

void readStm32Response() {
  while (Serial1.available() > 0) {
    char c = (char)Serial1.read();

    if (c == '\r') {
      continue;
    }

    if (c == '\n') {
      stm32Line.trim();

      if (stm32Line.length() > 0) {
        publishStm32Response(stm32Line);
      }

      stm32Line = "";
    } else {
      stm32Line += c;
    }
  }
}

void sendCommandToStm32(const String& command) {
  Serial.print("ESP32 -> STM32: ");
  Serial.println(command);

  Serial1.print(command);
  Serial1.print('\n');
}

void publishStm32Response(const String& response) {
  Serial.print("STM32 -> ESP32: ");
  Serial.println(response);

  if (client && client.connected()) {
    client.print("{\"stm32\":\"");
    client.print(response);
    client.println("\"}");
  }
}
