/**
 * @brief fonts_init + ftinfo only (SD-card update path not used in AI_vision).
 */

#include "fonts.h"
#include "malloc.h"
#include "./SD_NAND/sd_nand.h"

_font_info ftinfo;
uint32_t FONTINFOADDR;

uint8_t fonts_init(void)
{
    uint8_t t = 0U;
    uint8_t *tempbuf;
    uint16_t index;

    FONTINFOADDR = g_sd_nand_info_struct.LogBlockNbr - SD_NAND_FONT_BLK_NUM;

    tempbuf = (uint8_t *)mymalloc(SRAMIN, 512U);
    if (tempbuf == NULL)
    {
        return 1U;
    }

    while (t < 10U)
    {
        t++;
        sd_nand_read_disk(tempbuf, FONTINFOADDR, 1U);
        for (index = 0U; index < sizeof(ftinfo); index++)
        {
            ((uint8_t *)&ftinfo)[index] = tempbuf[index];
        }

        if (ftinfo.fontok == 0xAAU)
        {
            break;
        }

        HAL_Delay(20);
    }

    myfree(SRAMIN, tempbuf);

    if (ftinfo.fontok != 0xAAU)
    {
        return 1U;
    }

    return 0U;
}
