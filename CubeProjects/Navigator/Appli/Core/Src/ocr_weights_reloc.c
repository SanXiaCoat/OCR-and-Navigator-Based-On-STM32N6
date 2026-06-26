/**

 * @file ocr_weights_reloc.c

 * @brief 将 ocr_det 权重从 NOR @0x71000000 拷到 HyperRAM，供 SW 算子 CPU 访问

 */

#include "ocr_weights_reloc.h"

#include "main.h"
#include "ocr_dbg.h"

#include "ocr_display.h"

#include "ocr_infer.h"

#include "uart_test.h"

#include "xspi2_nor.h"



#include <stdio.h>

#include <string.h>



uint8_t g_ocr_wgt_pool[OCR_DET_WEIGHTS_BYTES]
    __attribute__((section(".ocr_wgt"), aligned(32)));

static uint8_t s_weights_loaded;

static uint8_t s_wgt_load_rc;

static uint32_t s_wgt_load_off_kb;



static void ocr_weights_set_error(uint8_t rc, uint32_t off_kb)
{
  char full_err[48];

  s_wgt_load_rc = rc;
  s_wgt_load_off_kb = off_kb;

  (void)snprintf(full_err, sizeof(full_err), "WGT LOAD FAIL err=%u @%luK",
                 (unsigned)rc, (unsigned long)off_kb);
  ocr_display_error_show(full_err);
}



void ocr_weights_get_last_error(char *buf, size_t n)

{

  if ((buf == NULL) || (n == 0U))

  {

    return;

  }



  if (s_wgt_load_rc == 0U)

  {

    (void)snprintf(buf, n, "WGT OK");

    return;

  }



  (void)snprintf(buf, n, "WGT E%u@%luK", (unsigned)s_wgt_load_rc,
                 (unsigned long)s_wgt_load_off_kb);
}

void ocr_weights_get_last_error_detail(char *buf, size_t n)
{
  if ((buf == NULL) || (n == 0U))
  {
    return;
  }

  if (s_wgt_load_rc == 0U)
  {
    (void)snprintf(buf, n, "WGT OK");
    return;
  }

  (void)snprintf(buf, n, "WGT LOAD FAIL err=%u @%luK", (unsigned)s_wgt_load_rc,
                 (unsigned long)s_wgt_load_off_kb);
}



static void ocr_weights_progress(uint32_t done_bytes, uint32_t total_bytes)

{

  char msg[32];



  (void)total_bytes;

  (void)snprintf(msg, sizeof(msg), "load %luK", (unsigned long)(done_bytes / 1024U));

  OCR_DBG("%s", msg);
  (void)uart_test_tx_line(msg);

}



uintptr_t ocr_aton_phys_to_virt(uintptr_t address)

{

  if ((s_weights_loaded != 0U) && (address >= OCR_DET_WEIGHTS_XSPI2_BASE) &&

      (address < (OCR_DET_WEIGHTS_XSPI2_BASE + OCR_DET_WEIGHTS_BYTES)))

  {

    return address - OCR_DET_WEIGHTS_XSPI2_BASE + OCR_WGT_RAM_BASE;

  }



  return address;

}



int ocr_weights_load_to_hyperram(void)

{

  uint32_t nor_cpu = OCR_DET_WEIGHTS_XSPI2_BASE;

  uint32_t dst = (uint32_t)(uintptr_t)g_ocr_wgt_pool;

  uint8_t rc;



  if (s_weights_loaded != 0U)

  {

    return 0;

  }



  s_wgt_load_rc = 0U;

  s_wgt_load_off_kb = 0U;

  ocr_display_error_clear();



  if (ocr_infer_weights_ok() == 0)

  {

    return -2;

  }



  OCR_DBG("load NOR->HRAM");
  /* 探测读已验证 NOR 可达；bulk 前不再重复 disable/enable mmap */
  rc = xspi2_nor_read_bulk(nor_cpu, (void *)(uintptr_t)dst, OCR_DET_WEIGHTS_BYTES,
                           ocr_weights_progress);

  if (rc != 0U)

  {

    uint32_t off_kb = xspi2_nor_last_fail_offset() / 1024U;



    ocr_weights_set_error(rc, off_kb);

    OCR_DBG("load err=%u@%luK", (unsigned)rc, (unsigned long)off_kb);

    return -1;

  }



  SCB_InvalidateDCache_by_Addr((uint32_t *)(uintptr_t)g_ocr_wgt_pool,
                               (int32_t)OCR_DET_WEIGHTS_BYTES);

  s_weights_loaded = 1U;
  OCR_DBG("load wgt OK %uK @%lX", (unsigned)(OCR_DET_WEIGHTS_BYTES / 1024U),
          (unsigned long)dst);
  return 0;

}

