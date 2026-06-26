#include "uart_test.h"
#include "uart_port.h"
#include <string.h>

#define UART_TEST_RX_TIMEOUT_MS  30U
#define UART_TEST_SESSION_IDLE_MS  60000U

static UART_HandleTypeDef *s_huart;

static int uart_test_tx(const uint8_t *data, uint16_t len, uint32_t timeout_ms)
{
  if ((data == NULL) || (len == 0U))
  {
    return -1;
  }
  return uart_port_tx(data, len, timeout_ms);
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
    if (uart_port_init() != 0)
    {
      return -1;
    }
    s_huart = uart_port_handle();
  }
  if (s_huart == NULL)
  {
    return -1;
  }

  (void)uart_test_tx_str(
      "\r\n=== ATK-CNN647B UART Test ===\r\n"
      "Log TX: USB-CH340 and/or UART4 PC10/PC11 115200\r\n"
      "Nav JSON RX: UART4 PC11 only\r\n"
      "KEY2: 30s echo (UART4)\r\n\r\n");
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

  (void)uart_test_tx_str("\r\n[UART4 echo session start]\r\n");

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

  (void)uart_test_tx_str("\r\n[UART4 echo session end]\r\n");
  return count;
}

int uart_test_tx_line(const char *line)
{
  size_t len;

  if (line == NULL)
  {
    return -1;
  }
  if (s_huart == NULL)
  {
    s_huart = uart_port_handle();
  }
  if (uart_test_tx_str(line) != 0)
  {
    return -2;
  }

  len = strlen(line);
  if ((len >= 2U) && (line[len - 2U] == '\r') && (line[len - 1U] == '\n'))
  {
    return 0;
  }
  return uart_test_tx_str("\r\n");
}
