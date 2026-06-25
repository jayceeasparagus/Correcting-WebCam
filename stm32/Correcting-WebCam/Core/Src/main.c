/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : UART-only ESP32 <-> STM32 test
  ******************************************************************************
  */
/* USER CODE END Header */

#include "main.h"

/* USER CODE BEGIN Includes */
#include <string.h>
/* USER CODE END Includes */

/* USER CODE BEGIN PD */
#define UART_RX_LINE_MAX 64U
/* USER CODE END PD */

UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */
static char uart_rx_line[UART_RX_LINE_MAX];
static uint8_t uart_rx_index = 0U;
/* USER CODE END PV */

void SystemClock_Config(void);
static void MX_GPIO_Init(void);

/* USER CODE BEGIN PFP */
static void MX_USART1_UART_Init(void);
static void UART_PollCommand(void);
static void UART_ProcessCommand(const char *command);
static void UART_SendText(const char *text);
static void BlinkStatus(uint8_t count);
/* USER CODE END PFP */

int main(void)
{
  HAL_Init();
  SystemClock_Config();

  MX_GPIO_Init();
  MX_USART1_UART_Init();

  UART_SendText("\r\nSTM32 command receiver ready.\r\n");

  while (1)
  {
    UART_PollCommand();
  }
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

static void UART_PollCommand(void)
{
  uint8_t rx_byte = 0U;

  if (HAL_UART_Receive(&huart1, &rx_byte, 1U, 0U) != HAL_OK)
  {
    return;
  }

  if ((rx_byte == '\r') || (rx_byte == '\n'))
  {
    if (uart_rx_index > 0U)
    {
      uart_rx_line[uart_rx_index] = '\0';
      UART_ProcessCommand(uart_rx_line);
      uart_rx_index = 0U;
    }
    return;
  }

  if (uart_rx_index < (UART_RX_LINE_MAX - 1U))
  {
    uart_rx_line[uart_rx_index] = (char)rx_byte;
    uart_rx_index++;
  }
  else
  {
    uart_rx_index = 0U;
    UART_SendText("ERR LINE_TOO_LONG\r\n");
  }
}

static void UART_ProcessCommand(const char *command)
{
  if (strcmp(command, "PING") == 0)
  {
    UART_SendText("OK PING\r\n");
    BlinkStatus(1U);
  }
  else if (strcmp(command, "LED_ON") == 0)
  {
    HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_SET);
    UART_SendText("OK LED_ON\r\n");
  }
  else if (strcmp(command, "LED_OFF") == 0)
  {
    HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);
    UART_SendText("OK LED_OFF\r\n");
  }
  else if (strcmp(command, "BLINK") == 0)
  {
    UART_SendText("OK BLINK\r\n");
    BlinkStatus(3U);
  }
  else if (strncmp(command, "PAN=", 4) == 0)
  {
    if (strstr(command, "PAN=STOP") && strstr(command, "TILT=STOP"))
    {
      HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);
    }
    else
    {
      HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_SET);
    }

    UART_SendText("OK ");
    UART_SendText(command);
    UART_SendText("\r\n");
  }
  else
  {
    UART_SendText("ERR UNKNOWN\r\n");
  }
}

static void UART_SendText(const char *text)
{
  HAL_UART_Transmit(&huart1, (uint8_t *)text, (uint16_t)strlen(text), HAL_MAX_DELAY);
}

static void BlinkStatus(uint8_t count)
{
  for (uint8_t i = 0U; i < count; i++)
  {
    HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_SET);
    HAL_Delay(80U);
    HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);
    HAL_Delay(80U);
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
