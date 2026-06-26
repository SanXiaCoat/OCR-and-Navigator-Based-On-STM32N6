/**
 * @file fonts_xspi_install.c
 * @brief Copy GBK font pack from XSPI2 flash into SD NAND (scheme C, no SD card).
 */

#include <string.h>
#include "fonts_xspi.h"
#include "fonts.h"
#include "malloc.h"
#include "./RGBLCD/rgblcd.h"
#include "./SD_NAND/sd_nand.h"

typedef struct __attribute__((packed))
{
    uint32_t magic;
    uint32_t version;
    uint32_t ugbk_size;
    uint32_t gbk12_size;
    uint32_t gbk16_size;
    uint32_t gbk24_size;
    uint32_t gbk32_size;
} font_pack_hdr_t;

static const char *const s_font_labels[5] =
{
    "UNIGBK",
    "GBK12",
    "GBK16",
    "GBK24",
    "GBK32",
};

static uint32_t font_pack_blocks(uint32_t byte_size)
{
    if (byte_size == 0U)
    {
        return 0U;
    }

    if ((byte_size % 512U) != 0U)
    {
        return (byte_size >> 9) + 1U;
    }

    return byte_size >> 9;
}

static void font_install_progress(uint16_t x, uint16_t y, uint8_t size,
                                  uint32_t total_bytes, uint32_t done_bytes, uint16_t color)
{
    uint8_t percent;

    if (total_bytes == 0U)
    {
        return;
    }

    percent = (uint8_t)((done_bytes * 100U) / total_bytes);
    if (percent > 100U)
    {
        percent = 100U;
    }

    rgblcd_show_string(x, y, 240, 320, size, (char *)"Installing ", color);
    rgblcd_show_num(x + (size >> 1) * 11, y, percent, 3, size, color);
    rgblcd_show_string(x + (size >> 1) * 14, y, 240, 320, size, (char *)"%", color);
}

static uint8_t font_copy_blob_to_nand(const uint8_t *src, uint32_t size, uint32_t nand_block,
                                      uint16_t x, uint16_t y, uint8_t font_size, uint16_t color)
{
    uint8_t *tempbuf;
    uint32_t offx = 0U;
    uint32_t bread;
    uint8_t res = 0U;

    if (size == 0U)
    {
        return 0U;
    }

    tempbuf = (uint8_t *)mymalloc(SRAMIN, 4096U);
    if (tempbuf == NULL)
    {
        return 1U;
    }

    while (offx < size)
    {
        bread = size - offx;
        if (bread > 4096U)
        {
            bread = 4096U;
        }

        memcpy(tempbuf, src + offx, bread);

        if (bread == 4096U)
        {
            if (sd_nand_write_disk(tempbuf, nand_block + offx / 512U, 8U) != 0U)
            {
                res = 1U;
                break;
            }
            offx += 4096U;
        }
        else
        {
            uint32_t blocks = font_pack_blocks(bread);
            if (sd_nand_write_disk(tempbuf, nand_block + offx / 512U, blocks) != 0U)
            {
                res = 1U;
            }
            offx = size;
        }

        font_install_progress(x, y, font_size, size, offx, color);
    }

    myfree(SRAMIN, tempbuf);
    return res;
}

static uint8_t font_install_one(const uint8_t *src, uint32_t size, uint32_t nand_block,
                                uint16_t x, uint16_t y, uint8_t font_size, uint8_t index, uint16_t color)
{
    rgblcd_show_string(x, y - font_size, 240, 320, font_size, (char *)s_font_labels[index], color);
    return font_copy_blob_to_nand(src, size, nand_block, x, y, font_size, color);
}

uint8_t fonts_install_from_xspi(uint16_t x, uint16_t y, uint8_t size, uint16_t color)
{
    const font_pack_hdr_t *hdr = (const font_pack_hdr_t *)FONT_PACK_XSPI2_BASE;
    const uint8_t *payload;
    uint32_t offset;
    uint32_t nand_addrs[5];
    uint8_t *tempbuf;
    uint16_t index;
    uint8_t res = 0U;
    uint32_t sizes[5];
    const uint8_t *addrs[5];

    if (hdr->magic != FONT_PACK_MAGIC)
    {
        return 1U;
    }

    if (hdr->version != FONT_PACK_VERSION)
    {
        return 2U;
    }

    sizes[0] = hdr->ugbk_size;
    sizes[1] = hdr->gbk12_size;
    sizes[2] = hdr->gbk16_size;
    sizes[3] = hdr->gbk24_size;
    sizes[4] = hdr->gbk32_size;

    payload = (const uint8_t *)(FONT_PACK_XSPI2_BASE + sizeof(font_pack_hdr_t));
    offset = 0U;
    for (index = 0U; index < 5U; index++)
    {
        offset += sizes[index];
    }

    if (offset == 0U)
    {
        return 3U;
    }

    FONTINFOADDR = g_sd_nand_info_struct.LogBlockNbr - SD_NAND_FONT_BLK_NUM;

    ftinfo.fontok = 0xFFU;

    addrs[0] = payload;
    addrs[1] = payload + sizes[0];
    addrs[2] = payload + sizes[0] + sizes[1];
    addrs[3] = payload + sizes[0] + sizes[1] + sizes[2];
    addrs[4] = payload + sizes[0] + sizes[1] + sizes[2] + sizes[3];

    ftinfo.ugbkaddr = FONTINFOADDR + 1U;
    ftinfo.ugbksize = sizes[0];

    if (sizes[1] % 512U)
    {
        ftinfo.f12addr = ftinfo.ugbkaddr + font_pack_blocks(sizes[0]);
    }
    else
    {
        ftinfo.f12addr = ftinfo.ugbkaddr + (sizes[0] >> 9);
    }
    ftinfo.gbk12size = sizes[1];

    if (sizes[2] % 512U)
    {
        ftinfo.f16addr = ftinfo.f12addr + font_pack_blocks(sizes[1]);
    }
    else
    {
        ftinfo.f16addr = ftinfo.f12addr + (sizes[1] >> 9);
    }
    ftinfo.gbk16size = sizes[2];

    if (sizes[3] % 512U)
    {
        ftinfo.f24addr = ftinfo.f16addr + font_pack_blocks(sizes[2]);
    }
    else
    {
        ftinfo.f24addr = ftinfo.f16addr + (sizes[2] >> 9);
    }
    ftinfo.gbk24size = sizes[3];

    if (sizes[4] % 512U)
    {
        ftinfo.f32addr = ftinfo.f24addr + font_pack_blocks(sizes[3]);
    }
    else
    {
        ftinfo.f32addr = ftinfo.f24addr + (sizes[3] >> 9);
    }
    ftinfo.gbk32size = sizes[4];

    nand_addrs[0] = ftinfo.ugbkaddr;
    nand_addrs[1] = ftinfo.f12addr;
    nand_addrs[2] = ftinfo.f16addr;
    nand_addrs[3] = ftinfo.f24addr;
    nand_addrs[4] = ftinfo.f32addr;

    for (index = 0U; index < 5U; index++)
    {
        res = font_install_one(addrs[index], sizes[index], nand_addrs[index], x, y, size, (uint8_t)index, color);
        if (res != 0U)
        {
            return (uint8_t)(10U + index);
        }
    }

    ftinfo.fontok = 0xAAU;
    tempbuf = (uint8_t *)mymalloc(SRAMIN, 512U);
    if (tempbuf == NULL)
    {
        return 4U;
    }

    for (index = 0U; index < sizeof(ftinfo); index++)
    {
        tempbuf[index] = ((uint8_t *)&ftinfo)[index];
    }

    if (sd_nand_write_disk(tempbuf, FONTINFOADDR, 1U) != 0U)
    {
        myfree(SRAMIN, tempbuf);
        return 5U;
    }

    myfree(SRAMIN, tempbuf);
    return 0U;
}
