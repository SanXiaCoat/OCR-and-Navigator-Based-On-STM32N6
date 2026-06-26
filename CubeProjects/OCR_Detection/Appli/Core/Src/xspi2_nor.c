/**

 * @file xspi2_nor.c

 * @brief XSPI2 外挂 NOR mmap，供 ocr_det 权重 @ 0x71000000

 * @note  顺序须与 991_AI 一致：MX_XSPI2_Init(早) → HyperRAM_Init → NORFlash_Init → mmap

 */



#include "xspi2_nor.h"

#include "main.h"

#include "norflash.h"
#include "norflash_xspi.h"

#include <string.h>



extern XSPI_HandleTypeDef hxspi2;



static NORFlash_ObjectTypeDef s_nor_flash;

static uint8_t s_last_jedec[3];

static uint8_t s_nor_inited;
static uint32_t s_bulk_fail_off;



static void xspi2_nor_capture_jedec(void)

{

  NORFlash_ObjectTypeDef probe;



  memset(s_last_jedec, 0, sizeof(s_last_jedec));

  memset(&probe, 0, sizeof(probe));



  NORFlash_XSPI_Init(&(probe.XSPIObject), &hxspi2);

  if (NORFlash_XSPI_SetClock(&(probe.XSPIObject),

                             HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_XSPI2),

                             50000000U, NULL) != NORFlash_XSPI_OK)

  {

    return;

  }



  NORFlash_Reset(&probe);

  HAL_Delay(10U);



  (void)NORFlash_XSPI_CommandRead(&(probe.XSPIObject), 0x9FU, s_last_jedec, 3U);

}



void xspi2_nor_get_last_jedec(uint8_t jedec[3])

{

  if (jedec != NULL)

  {

    jedec[0] = s_last_jedec[0];

    jedec[1] = s_last_jedec[1];

    jedec[2] = s_last_jedec[2];

  }

}



uint8_t xspi2_nor_init(void)

{

  /* hxspi2 已在 main SysInit 中 MX_XSPI2_Init；勿在此 DeInit 以免破坏 XSPIM 共享状态 */

  if (NORFlash_Init(&s_nor_flash, &hxspi2, HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_XSPI2)) != NORFlash_OK)

  {

    xspi2_nor_capture_jedec();

    s_nor_inited = 0U;

    return 1U;

  }



  s_nor_inited = 1U;

  return 0U;

}



uint8_t xspi2_nor_enable_mmap(void)

{

  if (s_nor_inited == 0U)

  {

    return 2U;

  }



  if (NORFlash_EnableMemoryMappedMode(&s_nor_flash) != NORFlash_OK)

  {

    return 2U;

  }



  return 0U;

}



uint8_t xspi2_nor_mmap_init(void)

{

  uint8_t ret;



  ret = xspi2_nor_init();

  if (ret != 0U)

  {

    return ret;

  }



  return xspi2_nor_enable_mmap();

}



#define XSPI2_NOR_CHUNK_BYTES  128U

static uint8_t s_nor_staging[XSPI2_NOR_CHUNK_BYTES] __attribute__((aligned(32)));

static uint32_t xspi2_nor_xfer_timeout_ms(uint32_t len)
{
  /* 按传输量放宽超时，避免大块读误判失败 */
  uint32_t ms = (len / 1024U) + 2000U;

  if (ms < 5000U)
  {
    ms = 5000U;
  }
  return ms;
}

static NORFlash_XSPI_StatusTypeDef xspi2_nor_indirect_read(NORFlash_ObjectTypeDef *nor, uint32_t nor_addr,
                                                           uint8_t *data, uint32_t len)
{
  NORFlash_XSPI_ObjectTypeDef *xo = &nor->XSPIObject;
  XSPI_RegularCmdTypeDef Cmd;
  uint32_t timeout_ms = xspi2_nor_xfer_timeout_ms(len);
  uint8_t cmd = nor->Command.MapRead.Command;
  uint8_t dummy = nor->Command.MapRead.Dummy;

  Cmd = xo->BaseCommand;
  Cmd.OperationType = HAL_XSPI_OPTYPE_COMMON_CFG;
  if (Cmd.InstructionWidth == HAL_XSPI_INSTRUCTION_16_BITS)
  {
    Cmd.Instruction = (uint16_t)(((uint16_t)cmd << 8) | (uint8_t)(~cmd & 0xFFU));
  }
  else
  {
    Cmd.Instruction = cmd;
  }
  Cmd.Address = nor_addr;
  Cmd.AddressWidth = HAL_XSPI_ADDRESS_32_BITS;
  Cmd.DummyCycles = dummy;
  Cmd.DataLength = len;
  Cmd.DataMode = (len == 0U) ? HAL_XSPI_DATA_NONE : Cmd.DataMode;

  if (HAL_XSPI_Command(xo->XSPIHandle, &Cmd, timeout_ms) != HAL_OK)
  {
    goto Error;
  }
  if ((len != 0U) && (HAL_XSPI_Receive(xo->XSPIHandle, data, timeout_ms) != HAL_OK))
  {
    goto Error;
  }

  return NORFlash_XSPI_OK;

Error:
  HAL_XSPI_Abort(xo->XSPIHandle);
  return NORFlash_XSPI_ERROR;
}

uint32_t xspi2_nor_last_fail_offset(void)
{
  return s_bulk_fail_off;
}

#define XSPI2_NOR_PROGRESS_STEP  (512U * 1024U)

uint8_t xspi2_nor_read_bulk(uint32_t cpu_addr, void *buf, uint32_t len,
                            xspi2_nor_progress_fn progress)
{
  uint32_t nor_addr;
  uint32_t off = 0U;
  uint32_t next_progress = XSPI2_NOR_PROGRESS_STEP;
  NORFlash_XSPI_StatusTypeDef st;
  NORFlash_XSPI_ObjectTypeDef *xo = &s_nor_flash.XSPIObject;

  s_bulk_fail_off = 0U;

  if ((s_nor_inited == 0U) || (buf == NULL) || (len == 0U))
  {
    return 1U;
  }
  if (cpu_addr < XSPI2_NOR_CPU_BASE)
  {
    return 1U;
  }

  nor_addr = cpu_addr - XSPI2_NOR_CPU_BASE;

  if (NORFlash_DisableMemoryMappedMode(&s_nor_flash) != NORFlash_OK)
  {
    return 2U;
  }

  __DSB();
  __ISB();

  if (NORFlash_XSPI_ConfigPHYLink(xo, NORFlash_PHY_LINK_8D8D8D) != NORFlash_XSPI_OK)
  {
    (void)xspi2_nor_enable_mmap();
    s_bulk_fail_off = 0U;
    return 5U;
  }

  while (off < len)
  {
    uint32_t n = XSPI2_NOR_CHUNK_BYTES;
    uint8_t *dst = (uint8_t *)buf + off;

    if ((off + n) > len)
    {
      n = len - off;
    }

    st = xspi2_nor_indirect_read(&s_nor_flash, nor_addr + off, s_nor_staging, n);
    if (st != NORFlash_XSPI_OK)
    {
      s_bulk_fail_off = off;
      (void)xspi2_nor_enable_mmap();
      return 3U;
    }

    SCB_InvalidateDCache_by_Addr((uint32_t *)(uintptr_t)s_nor_staging, (int32_t)n);
    memcpy(dst, s_nor_staging, n);
    off += n;

    if ((progress != NULL) && (off >= next_progress))
    {
      progress(off, len);
      next_progress += XSPI2_NOR_PROGRESS_STEP;
    }
  }

  if (progress != NULL)
  {
    progress(len, len);
  }

  if (xspi2_nor_enable_mmap() != 0U)
  {
    s_bulk_fail_off = len;
    return 4U;
  }

  return 0U;
}

uint8_t xspi2_nor_read_range(uint32_t cpu_addr, void *buf, uint32_t len)
{
  return xspi2_nor_read_bulk(cpu_addr, buf, len, NULL);
}

uint8_t xspi2_nor_ensure_mmap(void)

{

  if (s_nor_inited == 0U)

  {

    return 1U;

  }



  (void)NORFlash_DisableMemoryMappedMode(&s_nor_flash);

  __DSB();

  __ISB();



  if (xspi2_nor_enable_mmap() != 0U)

  {

    return 2U;

  }



  return 0U;

}

uint8_t xspi2_nor_read_cpu(uint32_t cpu_addr, void *buf, uint32_t len)

{

  if ((s_nor_inited == 0U) || (buf == NULL) || (len == 0U))

  {

    return 1U;

  }

  if (cpu_addr < XSPI2_NOR_CPU_BASE)

  {

    return 1U;

  }

  return xspi2_nor_read_range(cpu_addr, buf, len);

}


