/*
 * ESP32-S3 Nano WiFi -> STM32 UART bridge.
 *
 * PC sends newline-delimited JSON over TCP:
 *   {"pan":"RIGHT","tilt":"UP","error_x":120,"error_y":-40}
 *
 * ESP32 forwards this compact UART command to STM32:
 *   PAN=RIGHT,TILT=UP
 */

#include <Arduino.h>
#include <WiFi.h>

static constexpr char WIFI_SSID[] = "JRS";
static constexpr char WIFI_PASSWORD[] = "0324010324";

static constexpr uint16_t TCP_PORT = 5000;
static constexpr uint32_t USB_BAUD = 115200;
static constexpr uint32_t STM32_BAUD = 115200;

static constexpr int STM32_UART_RX_PIN = D0;  // ESP32 RX: connect to STM32 PA9 / USART1_TX
static constexpr int STM32_UART_TX_PIN = D1;  // ESP32 TX: connect to STM32 PA10 / USART1_RX

static WiFiServer server(TCP_PORT);
static WiFiClient client;

static String usbLine;
static String stm32Line;

static void connectToWiFi();
static void acceptTcpClient();
static void readTcpCommand();
static void readUsbCommand();
static void readStm32Response();
static void handleIncomingCommand(const String& message, bool cameFromTcp);
static void sendToStm32(const String& command);
static String makeStm32Command(const String& message);
static String extractJsonString(const String& json, const String& key);
static bool isDirection(const String& value);
static void publishToTcp(const String& message);

void setup()
{
  Serial.begin(USB_BAUD);
  Serial1.begin(STM32_BAUD, SERIAL_8N1, STM32_UART_RX_PIN, STM32_UART_TX_PIN);

  delay(1000);
  Serial.println();
  Serial.println("ESP32-S3 Nano WiFi -> STM32 UART bridge starting.");

  connectToWiFi();
  server.begin();

  Serial.print("TCP server listening on ");
  Serial.print(WiFi.localIP());
  Serial.print(":");
  Serial.println(TCP_PORT);
  Serial.println("Serial Monitor test commands: PING, LED_ON, LED_OFF, BLINK, PAN=RIGHT,TILT=UP");
}

void loop()
{
  acceptTcpClient();
  readTcpCommand();
  readUsbCommand();
  readStm32Response();
}

static void connectToWiFi()
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("Connected. ESP32 IP: ");
  Serial.println(WiFi.localIP());
}

static void acceptTcpClient()
{
  if (client && client.connected()) {
    return;
  }

  WiFiClient newClient = server.available();
  if (newClient) {
    client = newClient;
    Serial.println("PC connected.");
    publishToTcp("{\"status\":\"connected\"}");
  }
}

static void readTcpCommand()
{
  if (!client || !client.connected() || !client.available()) {
    return;
  }

  String message = client.readStringUntil('\n');
  message.trim();

  if (message.length() > 0) {
    handleIncomingCommand(message, true);
  }
}

static void readUsbCommand()
{
  while (Serial.available() > 0) {
    char c = (char)Serial.read();

    if (c == '\r') {
      continue;
    }

    if (c == '\n') {
      usbLine.trim();
      if (usbLine.length() > 0) {
        handleIncomingCommand(usbLine, false);
      }
      usbLine = "";
      continue;
    }

    usbLine += c;
  }
}

static void readStm32Response()
{
  while (Serial1.available() > 0) {
    char c = (char)Serial1.read();

    if (c == '\r') {
      continue;
    }

    if (c == '\n') {
      stm32Line.trim();

      if (stm32Line.length() > 0) {
        Serial.print("STM32 -> ESP32: ");
        Serial.println(stm32Line);
        publishToTcp(String("{\"stm32\":\"") + stm32Line + "\"}");
      }

      stm32Line = "";
      continue;
    }

    stm32Line += c;
  }
}

static void handleIncomingCommand(const String& message, bool cameFromTcp)
{
  String stm32Command = makeStm32Command(message);

  Serial.print(cameFromTcp ? "PC -> ESP32: " : "USB -> ESP32: ");
  Serial.println(message);

  if (stm32Command.length() == 0) {
    Serial.println("ESP32: command parse failed.");
    if (cameFromTcp) {
      publishToTcp("{\"status\":\"bad_command\"}");
    }
    return;
  }

  sendToStm32(stm32Command);

  if (cameFromTcp) {
    publishToTcp(String("{\"forwarded\":\"") + stm32Command + "\"}");
  }
}

static void sendToStm32(const String& command)
{
  Serial.print("ESP32 -> STM32: ");
  Serial.println(command);
  Serial1.print(command);
  Serial1.print('\n');
}

static String makeStm32Command(const String& message)
{
  if (message == "PING" || message == "LED_ON" || message == "LED_OFF" || message == "BLINK") {
    return message;
  }

  if (message.startsWith("PAN=")) {
    return message;
  }

  String pan = extractJsonString(message, "pan");
  String tilt = extractJsonString(message, "tilt");
  pan.toUpperCase();
  tilt.toUpperCase();

  if (!isDirection(pan) || !isDirection(tilt)) {
    return "";
  }

  return String("PAN=") + pan + ",TILT=" + tilt;
}

static String extractJsonString(const String& json, const String& key)
{
  String pattern = String("\"") + key + "\"";
  int keyIndex = json.indexOf(pattern);
  if (keyIndex < 0) {
    return "";
  }

  int colonIndex = json.indexOf(':', keyIndex + pattern.length());
  if (colonIndex < 0) {
    return "";
  }

  int firstQuote = json.indexOf('"', colonIndex + 1);
  if (firstQuote < 0) {
    return "";
  }

  int secondQuote = json.indexOf('"', firstQuote + 1);
  if (secondQuote < 0) {
    return "";
  }

  return json.substring(firstQuote + 1, secondQuote);
}

static bool isDirection(const String& value)
{
  return value == "LEFT" || value == "RIGHT" || value == "UP" ||
         value == "DOWN" || value == "STOP";
}

static void publishToTcp(const String& message)
{
  if (client && client.connected()) {
    client.println(message);
  }
}
