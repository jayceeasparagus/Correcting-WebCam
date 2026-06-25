/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : FreeRTOS UART command receiver for ESP32 -> STM32 testing
  ******************************************************************************
  */
/* USER CODE END Header */

#include "main.h"

/* USER CODE BEGIN Includes */
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include <string.h>
/* USER CODE END Includes */

/* USER CODE BEGIN PD */
#define UART_RX_LINE_MAX 64U
#define COMMAND_QUEUE_LENGTH 8U
#define LED_QUEUE_LENGTH 8U
#define COMMAND_TIMEOUT_MS 500U
/* USER CODE END PD */

/* USER CODE BEGIN PTD */
typedef enum
{
  COMMAND_PING,
  COMMAND_LED_ON,
  COMMAND_LED_OFF,
  COMMAND_BLINK,
  COMMAND_PAN_TILT,
  COMMAND_STOP,
  COMMAND_TIMEOUT_STOP,
  COMMAND_UNKNOWN
} CommandType;

typedef struct
{
  CommandType type;
  char text[UART_RX_LINE_MAX];
} CommandMessage;

typedef enum
{
  LED_ACTION_OFF,
  LED_ACTION_ON,
  LED_ACTION_BLINK_ONCE,
  LED_ACTION_BLINK_THREE
} LedAction;
/* USER CODE END PTD */

UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */
static QueueHandle_t commandQueue;
static QueueHandle_t ledQueue;
static volatile TickType_t lastCommandTick;
static volatile uint8_t commandSeen;
static volatile uint8_t timeoutStopSent;
/* USER CODE END PV */

void SystemClock_Config(void);
static void MX_GPIO_Init(void);

/* USER CODE BEGIN PFP */
static void MX_USART1_UART_Init(void);
static void UartRxTask(void *argument);
static void CommandTask(void *argument);
static void SafetyTask(void *argument);
static void LedTask(void *argument);
static void ParseCommand(const char *line, CommandMessage *message);
static void ProcessCommand(const CommandMessage *message);
static void UART_SendText(const char *text);
static void QueueLedAction(LedAction action);
static void BlinkStatus(uint8_t count);
/* USER CODE END PFP */

int main(void)
{
  HAL_Init();
  SystemClock_Config();

  MX_GPIO_Init();
  MX_USART1_UART_Init();

  commandQueue = xQueueCreate(COMMAND_QUEUE_LENGTH, sizeof(CommandMessage));
  if (commandQueue == NULL)
  {
    Error_Handler();
  }

  ledQueue = xQueueCreate(LED_QUEUE_LENGTH, sizeof(LedAction));
  if (ledQueue == NULL)
  {
    Error_Handler();
  }

  lastCommandTick = xTaskGetTickCount();
  commandSeen = 0U;
  timeoutStopSent = 0U;

  if (xTaskCreate(UartRxTask, "uart_rx", 256, NULL, 2, NULL) != pdPASS)
  {
    Error_Handler();
  }

  if (xTaskCreate(CommandTask, "command", 256, NULL, 3, NULL) != pdPASS)
  {
    Error_Handler();
  }

  if (xTaskCreate(SafetyTask, "safety", 128, NULL, 1, NULL) != pdPASS)
  {
    Error_Handler();
  }

  if (xTaskCreate(LedTask, "led", 128, NULL, 2, NULL) != pdPASS)
  {
    Error_Handler();
  }

  UART_SendText("\r\nSTM32 FreeRTOS receiver ready.\r\n");
  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_SET);
  HAL_Delay(100U);
  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);
  vTaskStartScheduler();

  Error_Handler();
}

void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 16;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
  RCC_OscInitStruct.PLL.PLLQ = 7;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                              | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();

  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);

  GPIO_InitStruct.Pin = LD2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LD2_GPIO_Port, &GPIO_InitStruct);
}

static void MX_USART1_UART_Init(void)
{
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
}

static void UartRxTask(void *argument)
{
  (void)argument;

  char line[UART_RX_LINE_MAX];
  uint8_t lineIndex = 0U;
  uint8_t rxByte = 0U;

  UART_SendText("UART_RX_TASK_STARTED\r\n");

  for (;;)
  {
    if (HAL_UART_Receive(&huart1, &rxByte, 1U, 10U) != HAL_OK)
    {
      vTaskDelay(pdMS_TO_TICKS(1U));
      continue;
    }

    if ((rxByte == '\r') || (rxByte == '\n'))
    {
      if (lineIndex > 0U)
      {
        CommandMessage message;
        line[lineIndex] = '\0';
        ParseCommand(line, &message);
        (void)xQueueSend(commandQueue, &message, 0U);
        lineIndex = 0U;
      }
      continue;
    }

    if (lineIndex < (UART_RX_LINE_MAX - 1U))
    {
      line[lineIndex] = (char)rxByte;
      lineIndex++;
    }
    else
    {
      lineIndex = 0U;
      UART_SendText("ERR LINE_TOO_LONG\r\n");
    }
  }
}

static void CommandTask(void *argument)
{
  (void)argument;

  CommandMessage message;

  for (;;)
  {
    if (xQueueReceive(commandQueue, &message, portMAX_DELAY) == pdPASS)
    {
      if (message.type != COMMAND_TIMEOUT_STOP)
      {
        lastCommandTick = xTaskGetTickCount();
        commandSeen = 1U;
        timeoutStopSent = 0U;
      }

      ProcessCommand(&message);
    }
  }
}

static void SafetyTask(void *argument)
{
  (void)argument;

  for (;;)
  {
    TickType_t now = xTaskGetTickCount();

    if (commandSeen && !timeoutStopSent && ((now - lastCommandTick) >= pdMS_TO_TICKS(COMMAND_TIMEOUT_MS)))
    {
      CommandMessage stopMessage = {0};
      stopMessage.type = COMMAND_TIMEOUT_STOP;
      (void)strncpy(stopMessage.text, "PAN=STOP,TILT=STOP", sizeof(stopMessage.text) - 1U);
      (void)xQueueSend(commandQueue, &stopMessage, 0U);
      timeoutStopSent = 1U;
    }

    vTaskDelay(pdMS_TO_TICKS(50U));
  }
}

static void LedTask(void *argument)
{
  (void)argument;

  LedAction action;

  for (;;)
  {
    if (xQueueReceive(ledQueue, &action, portMAX_DELAY) == pdPASS)
    {
      switch (action)
      {
        case LED_ACTION_ON:
          HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_SET);
          break;

        case LED_ACTION_OFF:
          HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);
          break;

        case LED_ACTION_BLINK_ONCE:
          BlinkStatus(1U);
          break;

        case LED_ACTION_BLINK_THREE:
          BlinkStatus(3U);
          break;

        default:
          break;
      }
    }
  }
}

static void ParseCommand(const char *line, CommandMessage *message)
{
  memset(message, 0, sizeof(*message));
  (void)strncpy(message->text, line, sizeof(message->text) - 1U);

  if (strcmp(line, "PING") == 0)
  {
    message->type = COMMAND_PING;
  }
  else if (strcmp(line, "LED_ON") == 0)
  {
    message->type = COMMAND_LED_ON;
  }
  else if (strcmp(line, "LED_OFF") == 0)
  {
    message->type = COMMAND_LED_OFF;
  }
  else if (strcmp(line, "BLINK") == 0)
  {
    message->type = COMMAND_BLINK;
  }
  else if (strcmp(line, "PAN=STOP,TILT=STOP") == 0)
  {
    message->type = COMMAND_STOP;
  }
  else if (strncmp(line, "PAN=", 4) == 0)
  {
    message->type = COMMAND_PAN_TILT;
  }
  else
  {
    message->type = COMMAND_UNKNOWN;
  }
}

static void ProcessCommand(const CommandMessage *message)
{
  switch (message->type)
  {
    case COMMAND_PING:
      UART_SendText("OK PING\r\n");
      QueueLedAction(LED_ACTION_BLINK_ONCE);
      break;

    case COMMAND_LED_ON:
      QueueLedAction(LED_ACTION_ON);
      UART_SendText("OK LED_ON\r\n");
      break;

    case COMMAND_LED_OFF:
      QueueLedAction(LED_ACTION_OFF);
      UART_SendText("OK LED_OFF\r\n");
      break;

    case COMMAND_BLINK:
      UART_SendText("OK BLINK\r\n");
      QueueLedAction(LED_ACTION_BLINK_THREE);
      break;

    case COMMAND_STOP:
      QueueLedAction(LED_ACTION_OFF);
      UART_SendText("OK ");
      UART_SendText(message->text);
      UART_SendText("\r\n");
      break;

    case COMMAND_TIMEOUT_STOP:
      QueueLedAction(LED_ACTION_OFF);
      UART_SendText("OK TIMEOUT_STOP\r\n");
      break;

    case COMMAND_PAN_TILT:
      QueueLedAction(LED_ACTION_ON);
      UART_SendText("OK ");
      UART_SendText(message->text);
      UART_SendText("\r\n");
      break;

    default:
      UART_SendText("ERR UNKNOWN\r\n");
      break;
  }
}

static void UART_SendText(const char *text)
{
  HAL_UART_Transmit(&huart1, (uint8_t *)text, (uint16_t)strlen(text), HAL_MAX_DELAY);
}

static void QueueLedAction(LedAction action)
{
  if (ledQueue != NULL)
  {
    (void)xQueueSend(ledQueue, &action, 0U);
  }
}

static void BlinkStatus(uint8_t count)
{
  for (uint8_t i = 0U; i < count; i++)
  {
    HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_SET);
    vTaskDelay(pdMS_TO_TICKS(200U));
    HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);
    vTaskDelay(pdMS_TO_TICKS(200U));
  }
}

void Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
  }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
  (void)file;
  (void)line;
}
#endif
