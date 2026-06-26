/**
 ****************************************************************************************************
 * @file        text.c
 * @author      正�?��??子�?��??(ALIENTEK)
 * @version     V1.0
 * @date        2025-02-19
 * @brief       �?�?�?�示 代码
 *              提�?text_show_font�??text_show_string两个�?��?�,�?��?�?�示�?�?
 * @license     Copyright (c) 2020-2032, 广�?�?�??翼�?�子�?�??�??�?��?�司
 ****************************************************************************************************
 * @attention
 *
 * �?�?平台:正�?��??子 STM32�?�?板
 * �?�线�?�?:www.yuanzige.com
 * �??�?�论�?:www.openedv.com
 * �?�司�?�?:www.alientek.com
 * 购买�?��?:openedv.taobao.com
 *
 * 修�?�说�??
 * V1.0 20250219
 * 添�?��?�?�?�?��?SD_NAND
 *
 ****************************************************************************************************
 */

#include "string.h"
#include "text.h"
#include "./RGBLCD/rgblcd.h"
#include "malloc.h"
#include "./SD_NAND/sd_nand.h"


/**
 * @brief       UTF-8转UNICODE�?码
 * @param       utf8  : �?符串�?��?
 * @retval      �?符�??UNICODE�?码
 */
uint32_t utf8_to_unicode(char *utf8)
{
    uint32_t unicode = 0;
    /* �?��?�UTF-8�?码�??�?�??�?� */
    if ((utf8[0] & 0x80) == 0x00) 
    {
        /* 1�?�??�?码 */
        unicode = utf8[0];
    } 
    else if ((utf8[0] & 0xE0) == 0xC0) 
    {
        /* 2�?�??�?码 */
        unicode = ((utf8[0] & 0x1F) << 6) | (utf8[1] & 0x3F);
    } 
    else if ((utf8[0] & 0xF0) == 0xE0) 
    {
        /* 3�?�??�?码 */
        unicode = ((utf8[0] & 0x0F) << 12) | ((utf8[1] & 0x3F) << 6) | (utf8[2] & 0x3F);
    }
     else if ((utf8[0] & 0xF8) == 0xF0) 
     {
        /* 4�?�??�?码 */
        unicode = ((utf8[0] & 0x07) << 18) | ((utf8[1] & 0x3F) << 12) | ((utf8[2] & 0x3F) << 6) | (utf8[3] & 0x3F);
    } else {
        /* �?��??�??UTF-8�?码 */ 
        unicode = 0xFFFD; /* �?�换�?符�?REPLACEMENT CHARACTER�?*/
    }

    return unicode;
}

/**
 * @brief       UNICODE转GBK�?码
 * @param       char_unicode  : �?�?��?符�?码(UNICODE�?码)
 *   @note      传�?��??�?�?�为两个�?�??�??UNICODE�?码
 * @retval      �?符�??GBK�?码
 */
uint16_t unicode_to_gbk(uint16_t char_unicode)
{
    uint16_t char_gbk = 0,offset;
    uint8_t *buf;
    uint32_t hi,li,i;
    uint8_t n;
    unsigned long foffset;

    buf = mymalloc(SRAMIN, 512); /* �??�?�512个�?�??空�?� */
    hi = 87172 / 4 - 1;
    li = 0;
    /* Unicode转GBK */
    for(n = 16; n > 0; n--)  /* �?�??�?�?�表 */
    {
        i = (int)(li + (hi - li) / 2);
        foffset = (i*4) >> 9;  /* �?�达偏移�?对�?�??�? */
        sd_nand_read_disk(buf, ftinfo.ugbkaddr + foffset, 1);
        offset = (i*4) % 512;
        if (char_unicode == (buf[offset + 1] << 8 | buf[offset])) 
        {
            break;
        }
        if (char_unicode > (buf[offset + 1] << 8 | buf[offset]))
        {
            li = i;
        }
        else hi = i;
    }
    foffset = (i*4 +2) >> 9;
    sd_nand_read_disk(buf, ftinfo.ugbkaddr + foffset, 1);
    offset = (i*4+2) % 512;
    char_gbk = buf[offset + 1] << 8 | buf[offset];
    myfree(SRAMIN, buf); /* �??�?��??�? */
    return char_gbk;
}

/**
 * @brief       �?��?�?�?�?��?��?�据
 * @param       code  : �?�?��?�?�?码(GBK码)
 * @param       mat   : �?�?��?�?�?��?��?�据�?�?��?��?
 * @param       size  : �?�?大小
 *   @note      size大小�??�?�?,�?��?��?��?�据大小为: (size / 8 + ((size % 8) ? 1 : 0)) * (size)  �?�??
 * @retval      �?�
 */
static void text_get_hz_mat(unsigned char *code, unsigned char *mat, uint8_t size)
{
    uint16_t fdataend,blkoffset,rdata;
    uint8_t *tempbuf;
    uint8_t *ptempbuf;    /* �?��?�??�?tempbuf�??�?�?��? */
    unsigned char qh, ql;
    unsigned char i;
    unsigned long foffset,offset;  /* �?��?�?�?�SDNAND�??�?�?�??�?偏移�?��??�?�??偏移 */
    uint8_t csize = (size / 8 + ((size % 8) ? 1 : 0)) * (size); /* �?�?��?�?�?个�?符对�?�?��?��??�??占�??�?�??�?� */
    tempbuf = mymalloc(SRAMIN, 512); /* �??�?�512个�?�??空�?� */
    ptempbuf = tempbuf;   /* �??�?tempbuf�??�?�?��? */
    qh = *code;
    ql = *(++code);
    /* GBK�?�?偏移计�? */
    if (qh < 0x81 || ql < 0x40 || ql == 0xff || qh == 0xff)   /* �?常�?��?�? */
    {
        for (i = 0; i < csize; i++)
        {
            *mat++ = 0x00;  /* 填�??满格 */
        }

        return;     /* �?�?访�?� */
    }

    if (ql < 0x7f)
    {
        ql -= 0x40; /* 注�?�! */
    }
    else
    {
        ql -= 0x41;
    }

    qh -= 0x81;
    offset = ((unsigned long)190 * qh + ql) * csize; /* �?�?��?�?中�??�?�??偏移�?� */

    foffset = offset >> 9;          /* �?�?��?�?�?�SDNAND起�?�? */
    blkoffset = offset % 512;       /* �?符�?��?�?�??偏移�?� */
    fdataend = blkoffset + csize;   /* �?��?计�?�?符�?��?�?�否跨�??�?��?�?� */

    switch (size)
    {
        case 12:
            if(fdataend <= 512)
            {
                sd_nand_read_disk(tempbuf,foffset + ftinfo.f12addr, 1);
                tempbuf += blkoffset;
                for (i = 0; i < csize; i++)
                {
                    *mat++ = *tempbuf++;  /* 填�??满格 */
                }
            }
            else
            {
                rdata = fdataend - 512;
                sd_nand_read_disk(tempbuf,foffset + ftinfo.f12addr, 1);
                tempbuf += blkoffset;
                for (i = 0; i < csize; i++)
                {
                    if (i == (csize - rdata)) /* �?�?��?�?�据读�? */
                    {
                        tempbuf = ptempbuf;  /* �?��?偏移�??�?�?��? */
                        sd_nand_read_disk(tempbuf,foffset + ftinfo.f12addr + 1, 1); /* 读�?个�?�??�?�据 */
                    }  
                    *mat++ = *tempbuf++;  /* 填�??满格 */                
                }
            }
            break;

        case 16:
            if(fdataend <= 512)
            {
                sd_nand_read_disk(tempbuf,foffset + ftinfo.f16addr, 1);
                tempbuf += blkoffset;
                for (i = 0; i < csize; i++)
                {
                    *mat++ = *tempbuf++;  /* 填�??满格 */
                }
            }
            else
            {
                rdata = fdataend - 512;
                sd_nand_read_disk(tempbuf,foffset + ftinfo.f16addr, 1);
                tempbuf += blkoffset;
                for (i = 0; i < csize; i++)
                {
                    if (i == (csize - rdata)) /* �?�?��?�?�据读�? */
                    {
                        tempbuf = ptempbuf;  /* �?��?偏移�??�?�?��? */
                        sd_nand_read_disk(tempbuf,foffset + ftinfo.f16addr + 1, 1); /* 读�?个�?�??�?�据 */
                    }  
                    *mat++ = *tempbuf++;  /* 填�??满格 */                
                }
            }
            break;

        case 24:
            if(fdataend <= 512)
            {
                sd_nand_read_disk(tempbuf,foffset + ftinfo.f24addr, 1);
                tempbuf += blkoffset;
                for (i = 0; i < csize; i++)
                {
                    *mat++ = *tempbuf++;  /* 填�??满格 */
                }
            }
            else
            {
                rdata = fdataend - 512;
                sd_nand_read_disk(tempbuf,foffset + ftinfo.f24addr, 1);
                tempbuf += blkoffset;
                for (i = 0; i < csize; i++)
                {
                    if (i == (csize - rdata)) /* �?�?��?�?�据读�? */
                    {
                        tempbuf = ptempbuf;  /* �?��?偏移�??�?�?��? */
                        sd_nand_read_disk(tempbuf,foffset + ftinfo.f24addr + 1, 1); /* 读�?个�?�??�?�据 */
                    }  
                    *mat++ = *tempbuf++;  /* 填�??满格 */                
                }
            }
            break;

        case 32:
            if(fdataend <= 512)
            {
                sd_nand_read_disk(tempbuf,foffset + ftinfo.f32addr, 1);
                tempbuf += blkoffset;
                for (i = 0; i < csize; i++)
                {
                    *mat++ = *tempbuf++;  /* 填�??满格 */
                }
            }
            else
            {
                rdata = fdataend - 512;
                sd_nand_read_disk(tempbuf,foffset + ftinfo.f32addr, 1);
                tempbuf += blkoffset;
                for (i = 0; i < csize; i++)
                {
                    if (i == (csize - rdata)) /* �?�?��?�?�据读�? */
                    {
                        tempbuf = ptempbuf;  /* �?��?偏移�??�?�?��? */
                        sd_nand_read_disk(tempbuf,foffset + ftinfo.f32addr + 1, 1); /* 读�?个�?�??�?�据 */
                    }  
                    *mat++ = *tempbuf++;  /* 填�??满格 */                
                }
            }
            break;

    }
    myfree(SRAMIN, ptempbuf); /* �??�?��??�? */
}

/**
 * @brief       �?�示�?个�??�?大小�??�?�?
 * @param       x,y   : �?�?�??坐�?
 * @param       font  : �?�?GBK码
 * @param       size  : �?�?大小
 * @param       mode  : �?�示模式
 *   @note              0, 正常�?�示(不�??要�?�示�??�?�,�?�LCD�??�?��?�填�??,即g_back_color)
 *   @note              1, 叠�?��?�示(�?�?�示�??要�?�示�??�?�, 不�??要�?�示�??�?�, 不�?�?�?)
 * @param       color : �?�?�?�?�
 * @retval      �?�
 */
void text_show_font(uint16_t x, uint16_t y, uint8_t *font, uint8_t size, uint8_t mode, uint16_t color)
{
    uint8_t temp, t, t1;
    uint16_t y0 = y;
    uint8_t *dzk;
    uint8_t csize = (size / 8 + ((size % 8) ? 1 : 0)) * (size);     /* �?�?��?�?�?个�?符对�?�?��?��??�??占�??�?�??�?� */

    if (size != 12 && size != 16 && size != 24 && size != 32)
    {
        return;     /* 不�?��?��??size */
    }

    dzk = mymalloc(SRAMIN, size);       /* �?�请�??�? */

    if (dzk == 0) return;               /* �??�?不�?�? */

    text_get_hz_mat(font, dzk, size);   /* �?�?��?��?大小�??�?��?��?�据 */

    for (t = 0; t < csize; t++)
    {
        temp = dzk[t];                  /* �?�?��?��?��?�据 */

        for (t1 = 0; t1 < 8; t1++)
        {
            if (temp & 0x80)
            {
                rgblcd_draw_point(x, y, color);        /* �?��??要�?�示�??�?� */
            }
            else if (mode == 0)     /* �?�??�?叠�?�模式, 不�??要�?�示�??�?�,�?��??�?��?�填�?? */
            {
                rgblcd_draw_point(x, y, g_back_color);  /* 填�??�??�?��?� */
            }

            temp <<= 1;
            y++;

            if ((y - y0) == size)
            {
                y = y0;
                x++;
                break;
            }
        }
    }

    myfree(SRAMIN, dzk);    /* �??�?��??�? */
}

/**
 * @brief       �?��??�?位置�?�?�?�示�?个�?符串
 *   @note      该�?��?��?��?��?��?�换�?
 * @param       x,y   : 起�?坐�?
 * @param       width : �?�示�?��??宽度
 * @param       height: �?�示�?��??�?度
 * @param       str   : �?符串
 * @param       size  : �?�?大小
 * @param       mode  : �?�示模式
 *   @note              0, 正常�?�示(不�??要�?�示�??�?�,�?�LCD�??�?��?�填�??,即g_back_color)
 *   @note              1, 叠�?��?�示(�?�?�示�??要�?�示�??�?�, 不�??要�?�示�??�?�, 不�?�?�?)
 * @param       encode : �?符串�?码�?�式�?UTF-8:0 GBK:1
 * @param       color : �?�?�?�?�
 * @retval      �?�
 */
void text_show_string(uint16_t x, uint16_t y, uint16_t width, uint16_t height, char *str, uint8_t size, uint8_t mode, uint8_t encode, uint16_t color)
{
    uint16_t x0 = x;
    uint16_t y0 = y;
    uint8_t bHz = 0;                /* �?符�??�??中�?? */
    uint16_t char_gbk = 0;
    uint32_t unicode = 0;
    uint8_t hz_gbk[2];
    uint8_t *pstr = (uint8_t *)str; /* �??�?char*�??�?符串�?�?��? */

    while (*pstr != 0)   /* �?�据�?��?�? */
    {
        if (!bHz)
        {
            if (*pstr > 0x80)   /* 中�?? */
            {
                bHz = 1;    /* �?记�?�中�?? */
            }
            else            /* �?符 */
            {
                if (x > (x0 + width - size / 2))    /* 换�? */
                {
                    y += size;
                    x = x0;
                }

                if (y > (y0 + height - size))break; /* �?�??�?�?? */

                if (*pstr == 13)   /* 换�?符号 */
                {
                    y += size;
                    x = x0;
                    pstr++;
                }
                else
                {
                    rgblcd_show_char(x, y, *pstr, size, mode, color);   /* �??�??�?��??�??�?� */
                }

                pstr++;

                x += size / 2;  /* �?��??�?符宽度, 为中�??�?�?宽度�??�?�? */
            }
        }
        else     /* 中�?? */
        {
            bHz = 0; /* �??�?�?�? */

            if (x > (x0 + width - size))   /* 换�? */
            {
                y += size;
                x = x0;
            }

            if (y > (y0 + height - size))break; /* �?�??�?�?? */

            if (encode)
            {
                /* GBK�?码�?不�??要转码 */
                text_show_font(x, y, pstr, size, mode, color); /* �?�示�?个�?�?,空�?�?�示 */
                pstr += 2;
            }
            else
            {
                /* UTF-8�?码�?�??要�??转码 */
                unicode = utf8_to_unicode((char *)pstr); 
                char_gbk = unicode_to_gbk((uint16_t)unicode);
                /* �?�?��?�?�??GBK�?码 */
                hz_gbk[1] = char_gbk & 0xff;
                hz_gbk[0] = char_gbk >> 8 & 0xff;
                text_show_font(x, y, hz_gbk, size, mode, color); /* �?�示�?个�?�?,空�?�?�示 */
                pstr += 3;
            }
            x += size; /* �?�?个�?�?偏移 */
        }
    }
}


/**
 * @brief       �?��??�?宽度�??中�?��?�示�?符串
 *   @note      �?�??�?符�?�度�?�?�?len,�??�?�text_show_string_middle�?�示
 * @param       x,y   : 起�?坐�?
 * @param       str   : �?符串
 * @param       size  : �?�?大小
 * @param       width : �?�示�?��??宽度
 * @param       color : �?�?�?�?�
 * @retval      �?�
 */
void text_show_string_middle(uint16_t x, uint16_t y, char *str, uint8_t size, uint16_t width, uint16_t color)
{
    uint16_t strlenth = 0;
    strlenth = strlen((const char *)str);
    strlenth *= size / 2;

    if (strlenth > width) /* �?�?�?, 不�?��?中�?�示 */
    {
        text_show_string(x, y, rgblcddev.width, rgblcddev.height, str, size, 1, 0, color);
    }
    else
    {
        strlenth = (width - strlenth) / 2;
        text_show_string(strlenth + x, y, rgblcddev.width, rgblcddev.height, str, size, 1, 0, color);
    }
}













