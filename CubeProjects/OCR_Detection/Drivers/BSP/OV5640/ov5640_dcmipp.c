/**
 ****************************************************************************************************
 * @file        ov5640_dcmipp.c
 * @author      正点原子团队(ALIENTEK)
 * @version     V1.0
 * @date        2025-01-13
 * @brief       OV5640 DCMIPP驱动代码
 * @license     Copyright (c) 2020-2032, 广州市星翼电子科技有限公司
 ****************************************************************************************************
 * @attention
 * 
 * 实验平台:正点原子 N647开发板
 * 在线视频:www.yuanzige.com
 * 技术论坛:www.openedv.com
 * 公司网址:www.alientek.com
 * 购买地址:openedv.taobao.com
 * 
 ****************************************************************************************************
 */

#include "ov5640_dcmipp.h"
#include "ocr_dbg.h"

extern DCMIPP_HandleTypeDef hdcmipp;    /* DCMIPP句柄 */

uint8_t ov5640_dcmipp_buf[2 * 1024 * 1024] __attribute__((aligned(16))) __attribute__((section(".EXTRAM")));

/**
 * @brief   初始化OV5640 DCMIPP
 * @param   无
 * @retval  无
 */
void ov5640_dcmipp_init(void)
{
    __HAL_DCMIPP_DISABLE_IT(&hdcmipp, DCMIPP_IT_AXI_TRANSFER_ERROR | DCMIPP_IT_PARALLEL_SYNC_ERROR | DCMIPP_IT_PIPE0_FRAME | DCMIPP_IT_PIPE0_VSYNC | DCMIPP_IT_PIPE0_LINE | DCMIPP_IT_PIPE0_LIMIT | DCMIPP_IT_PIPE0_OVR);
    __HAL_DCMIPP_ENABLE_IT(&hdcmipp, DCMIPP_IT_PIPE0_FRAME);
}

/**
 * @brief   启动OV5640 DCMIPP传输
 * @param   无
 * @retval  无
 */
void ov5640_dcmipp_start(void)
{
    HAL_DCMIPP_PIPE_Start(&hdcmipp, DCMIPP_PIPE0, (uint32_t)ov5640_dcmipp_buf, DCMIPP_MODE_CONTINUOUS);
}

/**
 * @brief   停止OV5640 DCMIPP传输
 * @param   无
 * @retval  无
 */
void ov5640_dcmipp_stop(void)
{
  HAL_DCMIPP_PIPE_Stop(&hdcmipp, DCMIPP_PIPE0);
}

/* 单次拍照：SNAPSHOT 模式抓一帧到 ov5640_dcmipp_buf */
static volatile uint8_t s_snap_pending;
static volatile uint8_t s_snap_done;

void ov5640_dcmipp_frame_event(void)
{
  if (s_snap_pending != 0U)
  {
    s_snap_done = 1U;
    s_snap_pending = 0U;
  }
}

uint8_t ov5640_dcmipp_snap(uint32_t timeout_ms)
{
  uint32_t t0 = HAL_GetTick();

  /* 连续预览下不能直接 SNAPSHOT：须先停 pipe，否则 Start 失败或帧 IRQ 误完成 */
  HAL_DCMIPP_PIPE_Stop(&hdcmipp, DCMIPP_PIPE0);
  HAL_Delay(10);

  s_snap_done = 0U;
  s_snap_pending = 0U;
  __HAL_DCMIPP_ENABLE_IT(&hdcmipp, DCMIPP_IT_PIPE0_FRAME);

  s_snap_pending = 1U;

  if (HAL_DCMIPP_PIPE_Start(&hdcmipp, DCMIPP_PIPE0, (uint32_t)ov5640_dcmipp_buf,
                            DCMIPP_MODE_SNAPSHOT) != HAL_OK)
  {
    OCR_DBG("snap: PIPE_Start FAIL");
    s_snap_pending = 0U;
    return 1U;
  }

  while (s_snap_done == 0U)
  {
    if ((HAL_GetTick() - t0) >= timeout_ms)
    {
      OCR_DBG("snap: timeout %lums", (unsigned long)timeout_ms);
      s_snap_pending = 0U;
      HAL_DCMIPP_PIPE_Stop(&hdcmipp, DCMIPP_PIPE0);
      return 2U;
    }
  }

  OCR_DBG("snap: frame done in %lums", (unsigned long)(HAL_GetTick() - t0));

  return 0U;
}
