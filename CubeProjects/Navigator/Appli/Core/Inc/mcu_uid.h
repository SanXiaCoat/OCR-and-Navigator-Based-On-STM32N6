#ifndef __MCU_UID_H
#define __MCU_UID_H

#include <stdint.h>

typedef struct
{
  uint32_t w0;
  uint32_t w1;
  uint32_t w2;
} mcu_uid_t;

void mcu_uid_read(mcu_uid_t *uid);
void mcu_uid_print_uart(void);
void mcu_uid_show_lcd(uint16_t x, uint16_t y, uint16_t color);

#endif
