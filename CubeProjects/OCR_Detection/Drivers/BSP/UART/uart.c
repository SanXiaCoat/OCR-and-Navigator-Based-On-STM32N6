/**
 ****************************************************************************************************
 * @file        uart.c
 * @brief       USART1 @ PE5/PE6（与 05_Serial 一致）
 *
 * 警告：本 OCR 工程 DCMIPP 占用 PE5/PE6，请勿在摄像头运行时调用 uart_init()。
 *       插入 USB-UART 会与并口数据线争用，导致画面花屏 + 串口乱码。
 ****************************************************************************************************
 */

#include "./UART/uart.h"
#include "stm32n6xx_ll_usart.h"
#include <unistd.h>

#define UART_USART1_CLK_HZ  64000000U

static void uart_gpio_init(void)
{
  GPIO_InitTypeDef gpio_init_struct = {0};

  __HAL_RCC_USART1_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();

  gpio_init_struct.Pin = GPIO_PIN_5 | GPIO_PIN_6;
  gpio_init_struct.Mode = GPIO_MODE_AF_PP;
  gpio_init_struct.Pull = GPIO_PULLUP;
  gpio_init_struct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio_init_struct.Alternate = GPIO_AF7_USART1;
  HAL_GPIO_Init(GPIOE, &gpio_init_struct);
}

static void uart_clock_init(void)
{
  RCC_PeriphCLKInitTypeDef clk = {0};

  clk.PeriphClockSelection = RCC_PERIPHCLK_USART1;
  clk.Usart1ClockSelection = RCC_USART1CLKSOURCE_CLKP;
  (void)HAL_RCCEx_PeriphCLKConfig(&clk);
}

static void uart_putc(char ch)
{
  while (LL_USART_IsActiveFlag_TXE(USART1) == 0U)
  {
  }
  LL_USART_TransmitData8(USART1, (uint8_t)ch);
}

void uart_init(uint32_t baudrate)
{
  uint32_t usart_clk = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_USART1);

  if (usart_clk == 0U)
  {
    usart_clk = UART_USART1_CLK_HZ;
  }

  uart_clock_init();
  uart_gpio_init();

  LL_USART_Disable(USART1);
  LL_USART_ConfigAsyncMode(USART1);
  LL_USART_SetTransferDirection(USART1, LL_USART_DIRECTION_TX_RX);
  LL_USART_SetDataWidth(USART1, LL_USART_DATAWIDTH_8B);
  LL_USART_SetParity(USART1, LL_USART_PARITY_NONE);
  LL_USART_SetStopBitsLength(USART1, LL_USART_STOPBITS_1);
  LL_USART_SetPrescaler(USART1, LL_USART_PRESCALER_DIV1);
  LL_USART_SetBaudRate(USART1, usart_clk, LL_USART_PRESCALER_DIV1,
                       LL_USART_OVERSAMPLING_16, baudrate);
  LL_USART_Enable(USART1);

  while (LL_USART_IsActiveFlag_TEACK(USART1) == 0U)
  {
  }
}

int __io_putchar(int ch)
{
  uart_putc((char)ch);
  return ch;
}

int fputc(int ch, FILE *f)
{
  (void)f;
  uart_putc((char)ch);
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

  for (i = 0; i < len; i++)
  {
    uart_putc(ptr[i]);
  }
  return len;
}
