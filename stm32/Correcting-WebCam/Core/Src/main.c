/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : FreeRTOS ESP32 UART to two-servo controller
  ******************************************************************************
  */
/* USER CODE END Header */

#include "main.h"

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include <string.h>

/* Servo pins:
 *   PAN  = D12 / PA6
 *   TILT = D11 / PA7
 */
#define PAN_Pin GPIO_PIN_6
#define PAN_GPIO_Port GPIOA
#define TILT_Pin GPIO_PIN_7
#define TILT_GPIO_Port GPIOA

/* Tune this first. Larger values track faster but can look jumpy. */
#define SERVO_STEP_US 25U

#define PAN_MIN_US 800U
#define PAN_CENTER_US 1500U
#define PAN_MAX_US 2200U

#define TILT_MIN_US 1200U
#define TILT_CENTER_US 1500U
#define TILT_MAX_US 2000U

#define UART_LINE_MAX 64U
#define COMMAND_QUEUE_LENGTH 8U
#define SERVO_QUEUE_LENGTH 8U
#define COMMAND_TIMEOUT_MS 500U

typedef enum
{
  COMMAND_PING,
  COMMAND_PAN_TILT,
  COMMAND_STOP,
  COMMAND_CENTER,
  COMMAND_TIMEOUT_STOP,
  COMMAND_UNKNOWN
} CommandType;

typedef enum
{
  DIR_STOP,
  DIR_LEFT,
  DIR_RIGHT,
  DIR_UP,
  DIR_DOWN
} Direction;

typedef struct
{
  CommandType type;
  Direction pan;
  Direction tilt;
  char text[UART_LINE_MAX];
} CommandMessage;

typedef struct
{
  CommandType type;
  Direction pan;
  Direction tilt;
} ServoCommand;

UART_HandleTypeDef huart1;

static QueueHandle_t commandQueue;
static QueueHandle_t servoQueue;
static volatile TickType_t lastCommandTick;
static volatile uint8_t commandSeen;
static volatile uint8_t timeoutStopSent;

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);

static void UartRxTask(void *argument);
static void CommandTask(void *argument);
static void ServoTask(void *argument);
static void SafetyTask(void *argument);

static void ParseCommand(const char *line, CommandMessage *message);
static Direction ParsePanDirection(const char *line);
static Direction ParseTiltDirection(const char *line);
static void ProcessCommand(const CommandMessage *message);
static void QueueServoCommand(CommandType type, Direction pan, Direction tilt);

static void Servo_ApplyCommand(const ServoCommand *command, uint16_t *pan_us, uint16_t *tilt_us);
static uint16_t ClampPulse(int32_t pulse, uint16_t min_us, uint16_t max_us);
static void Servo_WriteFrame(uint16_t pan_us, uint16_t tilt_us);
static void Delay_Us(uint32_t microseconds);

static void UART_SendText(const char *text);
static void BlinkOk(void);

int main(void)
{
  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();
  MX_USART1_UART_Init();

  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

  commandQueue = xQueueCreate(COMMAND_QUEUE_LENGTH, sizeof(CommandMessage));
  servoQueue = xQueueCreate(SERVO_QUEUE_LENGTH, sizeof(ServoCommand));
  if ((commandQueue == NULL) || (servoQueue == NULL))
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

  if (xTaskCreate(ServoTask, "servo", 256, NULL, 4, NULL) != pdPASS)
  {
    Error_Handler();
  }

  if (xTaskCreate(SafetyTask, "safety", 128, NULL, 1, NULL) != pdPASS)
  {
    Error_Handler();
  }

  UART_SendText("\r\nSTM32 FreeRTOS servo controller ready.\r\n");
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
  HAL_GPIO_WritePin(PAN_GPIO_Port, PAN_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(TILT_GPIO_Port, TILT_Pin, GPIO_PIN_RESET);

  GPIO_InitStruct.Pin = LD2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LD2_GPIO_Port, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = PAN_Pin | TILT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
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

  char line[UART_LINE_MAX];
  uint8_t lineIndex = 0U;
  uint8_t rxByte = 0U;

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

    if (lineIndex < (UART_LINE_MAX - 1U))
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

static void ServoTask(void *argument)
{
  (void)argument;

  uint16_t pan_us = PAN_CENTER_US;
  uint16_t tilt_us = TILT_CENTER_US;
  ServoCommand command;
  TickType_t lastWake = xTaskGetTickCount();

  for (;;)
  {
    while (xQueueReceive(servoQueue, &command, 0U) == pdPASS)
    {
      Servo_ApplyCommand(&command, &pan_us, &tilt_us);
    }

    Servo_WriteFrame(pan_us, tilt_us);
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(20U));
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
      QueueServoCommand(COMMAND_TIMEOUT_STOP, DIR_STOP, DIR_STOP);
      UART_SendText("OK TIMEOUT_STOP\r\n");
      timeoutStopSent = 1U;
    }

    vTaskDelay(pdMS_TO_TICKS(50U));
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
  else if (strcmp(line, "CENTER") == 0)
  {
    message->type = COMMAND_CENTER;
  }
  else if (strcmp(line, "PAN=STOP,TILT=STOP") == 0)
  {
    message->type = COMMAND_STOP;
    message->pan = DIR_STOP;
    message->tilt = DIR_STOP;
  }
  else if (strncmp(line, "PAN=", 4) == 0)
  {
    message->type = COMMAND_PAN_TILT;
    message->pan = ParsePanDirection(line);
    message->tilt = ParseTiltDirection(line);
  }
  else
  {
    message->type = COMMAND_UNKNOWN;
  }
}

static Direction ParsePanDirection(const char *line)
{
  if (strstr(line, "PAN=LEFT") != NULL)
  {
    return DIR_LEFT;
  }
  if (strstr(line, "PAN=RIGHT") != NULL)
  {
    return DIR_RIGHT;
  }
  return DIR_STOP;
}

static Direction ParseTiltDirection(const char *line)
{
  if (strstr(line, "TILT=UP") != NULL)
  {
    return DIR_UP;
  }
  if (strstr(line, "TILT=DOWN") != NULL)
  {
    return DIR_DOWN;
  }
  return DIR_STOP;
}

static void ProcessCommand(const CommandMessage *message)
{
  switch (message->type)
  {
    case COMMAND_PING:
      UART_SendText("OK PING\r\n");
      BlinkOk();
      break;

    case COMMAND_CENTER:
      QueueServoCommand(COMMAND_CENTER, DIR_STOP, DIR_STOP);
      UART_SendText("OK CENTER\r\n");
      break;

    case COMMAND_STOP:
      QueueServoCommand(COMMAND_STOP, DIR_STOP, DIR_STOP);
      UART_SendText("OK PAN=STOP,TILT=STOP\r\n");
      break;

    case COMMAND_PAN_TILT:
      QueueServoCommand(COMMAND_PAN_TILT, message->pan, message->tilt);
      UART_SendText("OK ");
      UART_SendText(message->text);
      UART_SendText("\r\n");
      break;

    default:
      UART_SendText("ERR UNKNOWN\r\n");
      break;
  }
}

static void QueueServoCommand(CommandType type, Direction pan, Direction tilt)
{
  ServoCommand command;
  command.type = type;
  command.pan = pan;
  command.tilt = tilt;
  (void)xQueueSend(servoQueue, &command, 0U);
}

static void Servo_ApplyCommand(const ServoCommand *command, uint16_t *pan_us, uint16_t *tilt_us)
{
  if (command->type == COMMAND_CENTER)
  {
    *pan_us = PAN_CENTER_US;
    *tilt_us = TILT_CENTER_US;
    return;
  }

  if ((command->type == COMMAND_STOP) || (command->type == COMMAND_TIMEOUT_STOP))
  {
    return;
  }

  if (command->pan == DIR_LEFT)
  {
    *pan_us = ClampPulse((int32_t)*pan_us - (int32_t)SERVO_STEP_US, PAN_MIN_US, PAN_MAX_US);
  }
  else if (command->pan == DIR_RIGHT)
  {
    *pan_us = ClampPulse((int32_t)*pan_us + (int32_t)SERVO_STEP_US, PAN_MIN_US, PAN_MAX_US);
  }

  if (command->tilt == DIR_UP)
  {
    *tilt_us = ClampPulse((int32_t)*tilt_us - (int32_t)SERVO_STEP_US, TILT_MIN_US, TILT_MAX_US);
  }
  else if (command->tilt == DIR_DOWN)
  {
    *tilt_us = ClampPulse((int32_t)*tilt_us + (int32_t)SERVO_STEP_US, TILT_MIN_US, TILT_MAX_US);
  }
}

static uint16_t ClampPulse(int32_t pulse, uint16_t min_us, uint16_t max_us)
{
  if (pulse < (int32_t)min_us)
  {
    return min_us;
  }
  if (pulse > (int32_t)max_us)
  {
    return max_us;
  }
  return (uint16_t)pulse;
}

static void Servo_WriteFrame(uint16_t pan_us, uint16_t tilt_us)
{
  uint16_t first = pan_us;
  uint16_t second = tilt_us;
  uint32_t elapsed_us = 0U;

  taskENTER_CRITICAL();

  HAL_GPIO_WritePin(PAN_GPIO_Port, PAN_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(TILT_GPIO_Port, TILT_Pin, GPIO_PIN_SET);

  if (first <= second)
  {
    Delay_Us(first);
    HAL_GPIO_WritePin(PAN_GPIO_Port, PAN_Pin, GPIO_PIN_RESET);
    elapsed_us = first;

    Delay_Us(second - first);
    HAL_GPIO_WritePin(TILT_GPIO_Port, TILT_Pin, GPIO_PIN_RESET);
    elapsed_us = second;
  }
  else
  {
    Delay_Us(second);
    HAL_GPIO_WritePin(TILT_GPIO_Port, TILT_Pin, GPIO_PIN_RESET);
    elapsed_us = second;

    Delay_Us(first - second);
    HAL_GPIO_WritePin(PAN_GPIO_Port, PAN_Pin, GPIO_PIN_RESET);
    elapsed_us = first;
  }

  (void)elapsed_us;

  taskEXIT_CRITICAL();
}

static void Delay_Us(uint32_t microseconds)
{
  uint32_t start = DWT->CYCCNT;
  uint32_t ticks = microseconds * (SystemCoreClock / 1000000U);

  while ((DWT->CYCCNT - start) < ticks)
  {
  }
}

static void UART_SendText(const char *text)
{
  HAL_UART_Transmit(&huart1, (uint8_t *)text, (uint16_t)strlen(text), HAL_MAX_DELAY);
}

static void BlinkOk(void)
{
  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_SET);
  vTaskDelay(pdMS_TO_TICKS(80U));
  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);
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
