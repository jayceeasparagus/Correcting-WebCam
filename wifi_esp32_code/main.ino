#include <WiFi.h>

const char* WIFI_SSID = "JRS";
const char* WIFI_PASSWORD = "0324010324";

const uint16_t TCP_PORT = 5000;

WiFiServer server(TCP_PORT);
WiFiClient client;

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

void setup() {
  Serial.begin(115200);
  delay(2000);

  connectToWiFi();

  server.begin();

  Serial.print("TCP server listening on port ");
  Serial.println(TCP_PORT);
}

void loop() {
  // Accept a laptop connection.
  if (!client || !client.connected()) {
    WiFiClient newClient = server.available();

    if (newClient) {
      client = newClient;
      Serial.println("Laptop connected.");
    }
  }

  // Read one newline-terminated command.
  if (client && client.connected() && client.available()) {
    String message = client.readStringUntil('\n');
    message.trim();

    if (message.length() > 0) {
      Serial.print("Received: ");
      Serial.println(message);

      client.println("{\"status\":\"ok\"}");
    }
  }
}