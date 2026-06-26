#include "mcu_uid.h"
#include "uart_test.h"
#include "./RGBLCD/rgblcd.h"
#include "main.h"
#include <stdio.h>

void mcu_uid_read(mcu_uid_t *uid)
{
  if (uid == NULL)
  {
    return;
  }

  uid->w0 = HAL_GetUIDw0();
  uid->w1 = HAL_GetUIDw1();
  uid->w2 = HAL_GetUIDw2();
}

void mcu_uid_print_uart(void)
{
  mcu_uid_t uid;
  char line[56];

  mcu_uid_read(&uid);

  (void)uart_test_tx_line("\r\n=== MCU Unique ID (96-bit) ===");
  (void)snprintf(line, sizeof(line), "UID[31:0]:   %08lX", (unsigned long)uid.w0);
  (void)uart_test_tx_line(line);
  (void)snprintf(line, sizeof(line), "UID[63:32]:  %08lX", (unsigned long)uid.w1);
  (void)uart_test_tx_line(line);
  (void)snprintf(line, sizeof(line), "UID[95:64]:  %08lX", (unsigned long)uid.w2);
  (void)uart_test_tx_line(line);
  (void)snprintf(line, sizeof(line), "UID full: %08lX%08lX%08lX",
                 (unsigned long)uid.w2, (unsigned long)uid.w1, (unsigned long)uid.w0);
  (void)uart_test_tx_line(line);
  (void)uart_test_tx_line("==============================\r\n");
}

void mcu_uid_show_lcd(uint16_t x, uint16_t y, uint16_t color)
{
  mcu_uid_t uid;
  char line1[32];
  char line2[32];

  mcu_uid_read(&uid);
  (void)snprintf(line1, sizeof(line1), "UID:%08lX", (unsigned long)uid.w0);
  (void)snprintf(line2, sizeof(line2), "%08lX%08lX", (unsigned long)uid.w1, (unsigned long)uid.w2);
  rgblcd_show_string(x, y, 240, 16, 16, line1, color);
  rgblcd_show_string(x, (uint16_t)(y + 16U), 240, 16, 16, line2, color);
}
