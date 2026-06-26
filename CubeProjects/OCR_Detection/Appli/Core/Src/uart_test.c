#include "uart_test.h"
#include "uart_port.h"
#include <string.h>

#define UART_TEST_RX_TIMEOUT_MS  30U
#define UART_TEST_SESSION_IDLE_MS  60000U

static UART_HandleTypeDef *s_huart;

static int uart_test_tx(const uint8_t *data, uint16_t len, uint32_t timeout_ms)
{
  if ((s_huart == NULL) || (data == NULL) || (len == 0U))
  {
    return -1;
  }
  if (HAL_UART_Transmit(s_huart, (uint8_t *)(uintptr_t)data, len, timeout_ms) != HAL_OK)
  {
    return -2;
  }
  return 0;
}

static int uart_test_tx_str(const char *s)
{
  if (s == NULL)
  {
    return -1;
  }
  return uart_test_tx((const uint8_t *)s, (uint16_t)strlen(s), 2000U);
}

static int uart_test_rx_byte(uint8_t *byte, uint32_t timeout_ms)
{
  if ((s_huart == NULL) || (byte == NULL))
  {
    return -1;
  }
  if (HAL_UART_Receive(s_huart, byte, 1U, timeout_ms) != HAL_OK)
  {
    return -2;
  }
  return 0;
}

int uart_test_start(void)
{
  s_huart = uart_port_handle();
  if (s_huart == NULL)
  {
    return -1;
  }

  (void)uart_test_tx_str(
      "\r\n=== ATK-CNN647B USART3 Debug ===\r\n"
      "PD8=TX  PD9=RX  115200 8N1\r\n"
      "Send chars: MCU echoes each byte.\r\n"
      "KEY2: enter 30s echo session.\r\n\r\n");
  return 0;
}

void uart_test_poll_echo(void)
{
  uint8_t ch;

  if (s_huart == NULL)
  {
    return;
  }
  if (uart_test_rx_byte(&ch, UART_TEST_RX_TIMEOUT_MS) != 0)
  {
    return;
  }
  (void)uart_test_tx(&ch, 1U, 100U);
}

uint32_t uart_test_echo_session(uint32_t duration_ms)
{
  uint32_t t0 = HAL_GetTick();
  uint32_t last_rx = t0;
  uint32_t count = 0U;
  uint8_t ch;

  if (s_huart == NULL)
  {
    s_huart = uart_port_handle();
  }
  if (s_huart == NULL)
  {
    return 0U;
  }

  (void)uart_test_tx_str("\r\n[USART3 echo session start]\r\n");

  while (1)
  {
    uint32_t now = HAL_GetTick();

    if ((duration_ms > 0U) && ((now - t0) >= duration_ms))
    {
      break;
    }
    if ((duration_ms == 0U) && ((now - last_rx) >= UART_TEST_SESSION_IDLE_MS))
    {
      break;
    }

    if (uart_test_rx_byte(&ch, 50U) == 0)
    {
      (void)uart_test_tx(&ch, 1U, 100U);
      count++;
      last_rx = now;
    }
  }

  (void)uart_test_tx_str("\r\n[USART3 echo session end]\r\n");
  return count;
}

int uart_test_tx_line(const char *line)
{
  if (line == NULL)
  {
    return -1;
  }
  if (s_huart == NULL)
  {
    s_huart = uart_port_handle();
  }
  if (s_huart == NULL)
  {
    return 0;
  }
  /* 短超时，避免未接串口时在 TC 等待处永久阻塞 */
  return uart_test_tx((const uint8_t *)line, (uint16_t)strlen(line), 50U);
}
