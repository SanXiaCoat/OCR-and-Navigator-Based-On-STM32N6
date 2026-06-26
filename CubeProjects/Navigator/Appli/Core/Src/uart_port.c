#include "uart_port.h"
#include "./UART/uart.h"
#include <string.h>

UART_HandleTypeDef huart4;

static uint8_t s_uart_ready;

int uart_port_is_ready(void)
{
  return (s_uart_ready != 0U) ? 1 : 0;
}

int uart_port_tx(const uint8_t *data, uint16_t len, uint32_t timeout_ms)
{
  if ((s_uart_ready == 0U) || (data == NULL) || (len == 0U))
  {
    return -1;
  }
  if (HAL_UART_Transmit(&huart4, (uint8_t *)(uintptr_t)data, len, timeout_ms) != HAL_OK)
  {
    return -2;
  }
  if (uart_ch340_ready() != 0U)
  {
    uart_ch340_write(data, len);
  }
  return 0;
}

int uart_port_tx_str(const char *s)
{
  size_t len;

  if (s == NULL)
  {
    return -1;
  }
  len = strlen(s);
  if (len == 0U)
  {
    return 0;
  }
  if (len > 0xFFFFU)
  {
    len = 0xFFFFU;
  }
  return uart_port_tx((const uint8_t *)s, (uint16_t)len, 3000U);
}

void uart_port_boot_banner(void)
{
  (void)uart_port_tx_str(
      "\r\n\r\n=== AI_Vision BOOT ===\r\n"
      "Log: USB-CH340(USART1) OR UART4 PC10/PC11 @115200\r\n"
      "Nav JSON RX: UART4 PC10(TX)/PC11(RX) only\r\n\r\n");
}

void uart_port_reinit(void)
{
  if (s_uart_ready != 0U)
  {
    (void)HAL_UART_DeInit(&huart4);
    s_uart_ready = 0U;
  }
  (void)uart_port_init();
}

int uart_port_init(void)
{
  if (s_uart_ready != 0U)
  {
    return 0;
  }

  huart4.Instance = UART4;
  huart4.Init.BaudRate = 115200;
  huart4.Init.WordLength = UART_WORDLENGTH_8B;
  huart4.Init.StopBits = UART_STOPBITS_1;
  huart4.Init.Parity = UART_PARITY_NONE;
  huart4.Init.Mode = UART_MODE_TX_RX;
  huart4.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart4.Init.OverSampling = UART_OVERSAMPLING_16;
  huart4.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart4.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart4.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

  if (HAL_UART_Init(&huart4) != HAL_OK)
  {
    return -1;
  }

  /* FIFO 配置失败不阻断（部分复位状态下 DisableFifo 可能返回错误） */
  (void)HAL_UARTEx_SetTxFifoThreshold(&huart4, UART_TXFIFO_THRESHOLD_1_8);
  (void)HAL_UARTEx_SetRxFifoThreshold(&huart4, UART_RXFIFO_THRESHOLD_1_8);
  (void)HAL_UARTEx_DisableFifoMode(&huart4);

  s_uart_ready = 1U;
  return 0;
}

UART_HandleTypeDef *uart_port_handle(void)
{
  if (s_uart_ready == 0U)
  {
    return NULL;
  }
  return &huart4;
}

int __io_putchar(int ch)
{
  uint8_t c = (uint8_t)ch;

  if (s_uart_ready != 0U)
  {
    (void)HAL_UART_Transmit(&huart4, &c, 1U, 100U);
  }
  return ch;
}

int _write(int file, char *ptr, int len)
{
  int i;

  (void)file;
  if ((ptr == NULL) || (len <= 0))
  {
    return 0;
  }
  if (s_uart_ready != 0U)
  {
    if (HAL_UART_Transmit(&huart4, (uint8_t *)(uintptr_t)ptr, (uint16_t)len, 3000U) != HAL_OK)
    {
      return 0;
    }
    return len;
  }

  for (i = 0; i < len; i++)
  {
    (void)__io_putchar((int)ptr[i]);
  }
  return len;
}
