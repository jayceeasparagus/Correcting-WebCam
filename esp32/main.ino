/*
 * ESP32-S3 Nano UART-only test for talking to the STM32.
 *
 * Open the ESP32 Serial Monitor at 115200 baud, set line ending to Newline,
 * and type:
 *   PING
 *   LED_ON
 *   LED_OFF
 *   BLINK
 *
 * The ESP32 forwards each line to the STM32 and prints the STM32 reply.
 */

#include <Arduino.h>

static constexpr uint32_t USB_BAUD = 115200;
static constexpr uint32_t STM32_BAUD = 115200;

// Arduino Nano ESP32 / ESP32-S3 Nano labeled pins.
// D0 is this board's UART RX pin, D1 is this board's UART TX pin.
static constexpr int STM32_UART_RX_PIN = D0;  // Connect to STM32 USART1_TX / PA9
static constexpr int STM32_UART_TX_PIN = D1;  // Connect to STM32 USART1_RX / PA10

static String usbLine;
static String stm32Line;

static void readUsbSerial();
static void readStm32Serial();
static void sendToStm32(const String& command);

void setup()
{
  Serial.begin(USB_BAUD);
  Serial1.begin(STM32_BAUD, SERIAL_8N1, STM32_UART_RX_PIN, STM32_UART_TX_PIN);

  delay(1000);
  Serial.println();
  Serial.println("ESP32-S3 Nano UART-only test ready.");
  Serial.println("Type PING, LED_ON, LED_OFF, or BLINK.");
}

void loop()
{
  readUsbSerial();
  readStm32Serial();
}

static void readUsbSerial()
{
  while (Serial.available() > 0) {
    char c = (char)Serial.read();

    if (c == '\r') {
      continue;
    }

    if (c == '\n') {
      usbLine.trim();

      if (usbLine.length() > 0) {
        sendToStm32(usbLine);
      }

      usbLine = "";
      continue;
    }

    usbLine += c;
  }
}

static void readStm32Serial()
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
      }

      stm32Line = "";
      continue;
    }

    stm32Line += c;
  }
}

static void sendToStm32(const String& command)
{
  Serial.print("ESP32 -> STM32: ");
  Serial.println(command);

  Serial1.print(command);
  Serial1.print('\n');
}
