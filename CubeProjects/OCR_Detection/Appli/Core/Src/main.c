/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "./SYS/sys.h"
#include "./HyperRAM/hyperram.h"
#include "./LED/led.h"
#include "./KEY/key.h"
#include "./RGBLCD/rgblcd.h"
#include "./OV5640/ov5640.h"
#include "./OV5640/ov5640_dcmipp.h"
#include "./SD_NAND/sd_nand.h"
#include "malloc.h"
#include "text.h"
#include "ocr_display.h"
#include "ocr_infer.h"
#include "ocr_pipeline.h"
#include "ocr_weights_reloc.h"
#include "ocr_uart_dbg.h"
#include "ocr_dbg.h"
#include "xspi2_nor.h"
#include "fonts.h"
#include "fonts_xspi.h"
#include "uart_port.h"
#include "uart_test.h"
#include <stdio.h>
#include <string.h>

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

DCMIPP_HandleTypeDef hdcmipp;

DMA2D_HandleTypeDef hdma2d;

LTDC_HandleTypeDef hltdc;

XSPI_HandleTypeDef hxspi1;
XSPI_HandleTypeDef hxspi2;

SD_HandleTypeDef hsd2;
static uint32_t hsd2_last_error;

/* USER CODE BEGIN PV */
static HyperRAM_ObjectTypeDef HyperRAMObject = {0};
static uint16_t img_width;
static uint16_t img_height;
static uint16_t img_x_offset;
static uint16_t img_y_offset;
static const char *effects_tbl[] = {  /* 图像特效 */
  "Normal",
  "Cool",
  "Warm",
  "B&W",
  "Yellowish",
  "Inverse",
  "Greenish"
};

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
static void MX_GPIO_Init(void);
static void MX_DMA2D_Init(void);
static void MX_LTDC_Init(void);
static void MX_DCMIPP_Init(void);
static void MX_XSPI2_Init(void);
static void SystemIsolation_Config(void);
/* USER CODE BEGIN PFP */
static uint8_t MX_SDMMC2_SD_Init(void);
static void MPU_Config(void);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
  uint8_t t = 0;
  uint8_t key;
  uint8_t effect = 0;
  uint8_t sdmmc2_ok;
  uint8_t nor_ok;
  uint8_t hyperram_mmap_ok;
  uint8_t font_ok;
  uint8_t font_res;
  char status_msg[64];
  /* 结构体含 rec_text[8][64] 等约 0.7KB，放 BSS 以免占用 OCR 大栈导致 post 阶段栈溢出 */
  static ocr_det_result_t ocr_res;
  static char status_hint[] = "KEY0:OCR KEY1:FX KEY2:USART3";
  /* UTF-8: KEY0:拍照OCR  KEY1:特效/恢复预览 */
  /* USER CODE END 1 */

  MPU_Config();

  /* Enable the CPU Cache */

  /* Enable I-Cache---------------------------------------------------------*/
  SCB_EnableICache();

  /* Enable D-Cache---------------------------------------------------------*/
  SCB_EnableDCache();

  /* MCU Configuration--------------------------------------------------------*/
  HAL_Init();

  /* USER CODE BEGIN Init */
#ifdef DEBUG
  /* Debug(LRUN)：代码在 RAM，须自行配 PLL + 初始化 XSPI */
  sys_clock_config_debug();
#endif
  SystemCoreClockUpdate();

  /* USER CODE END Init */

  /* USER CODE BEGIN SysInit */
  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  led_init();
  LED0(1);
  sdmmc2_ok = MX_SDMMC2_SD_Init();
  SystemIsolation_Config();
  /* USER CODE BEGIN 2 */
#ifdef DEBUG
  /*
   * LRUN：FSBL 未运行，须完整初始化 XSPI1(HyperRAM) + XSPI2(NOR)。
   * 切勿在 Release/XIP 下执行：Appli 正从 XSPI2 NOR 取指，再 Init XSPI2 会死机。
   */
  MX_XSPI2_Init();
  nor_ok = xspi2_nor_init();
  MX_XSPI1_Init();
  hyperram_mmap_ok = 0U;
  if (HyperRAM_Init(&HyperRAMObject, &hxspi1) != HyperRAM_OK)
  {
    nor_ok = 3U;
  }
  else if (HyperRAM_EnableMemoryMappedMode(&HyperRAMObject) != HyperRAM_OK)
  {
    nor_ok = 4U;
  }
  else
  {
    hyperram_mmap_ok = 1U;
    if ((nor_ok == 0U) && (xspi2_nor_enable_mmap() != 0U))
    {
      nor_ok = 2U;
    }
  }
#else
  /* Release/XIP：right_fsbl 已 mmap HyperRAM@0x90000000 + NOR@0x70000000 */
  nor_ok = 0U;
  hyperram_mmap_ok = 1U;
#endif

  /* g_ltdc_lcd_framebuf @ 0x90000000，未 mmap 时 Init LTDC 会 HardFault */
  if (hyperram_mmap_ok != 0U)
  {
    MX_DMA2D_Init();
    MX_LTDC_Init();
    MX_DCMIPP_Init();
  }

  key_init();             /* 初始化按键 */
  my_mem_init(SRAMIN);    /* AXISRAM heap for font/text */
  if (hyperram_mmap_ok == 0U)
  {
    /* HyperRAM 未就绪：LED 快闪次数 = nor_ok（3=HyperRAM Init，4=HyperRAM mmap） */
    while (1)
    {
      uint8_t i;
      uint8_t code = (nor_ok >= 3U) ? nor_ok : 3U;

      for (i = 0U; i < code; i++)
      {
        LED0(1);
        HAL_Delay(120);
        LED0(0);
        HAL_Delay(120);
      }
      HAL_Delay(800);
    }
  }
  my_mem_init(SRAMEX);    /* HyperRAM heap，字库/预览缓冲均可能分配 */
  rgblcd_init();          /* 初始化RGB LCD */
  rgblcd_display_dir(1);  /* 设置RGB LCD显示方向 */

  rgblcd_show_string(30, 50, 200, 16, 16, "STM32", RED);
  rgblcd_show_string(30, 70, 200, 16, 16, "AI Vision OCR", RED);

  if (sdmmc2_ok != 0U)
  {
    /* HAL_SD_Init 失败：ErrorCode 位域见 stm32n6xx_ll_sdmmc.h（35456=0x8A80 多为总线无响应） */
    while (1)
    {
      rgblcd_show_string(30, 90, 240, 16, 16, "SDMMC2 Init Err!", RED);
      rgblcd_show_num(30, 110, hsd2_last_error, 5, 16, RED);
      rgblcd_show_string(30, 130, 200, 16, 16, "0x", RED);
      rgblcd_show_num(50, 130, hsd2_last_error, 5, 16, RED);
      rgblcd_show_num(120, 130, HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_SDMMC2) / 1000000U, 3, 16, RED);
      HAL_Delay(500);
      rgblcd_show_string(30, 90, 240, 16, 16, "Please Check!     ", RED);
      HAL_Delay(500);
      LED0_TOGGLE();
    }
  }

  while (sd_nand_init() != 0U)
  {
    rgblcd_show_string(30, 110, 240, 16, 16, "SD NAND Error!", RED);
    HAL_Delay(500);
    rgblcd_show_string(30, 110, 240, 16, 16, "Please Check!   ", RED);
    HAL_Delay(500);
    LED0_TOGGLE();
  }
  rgblcd_show_string(30, 110, 240, 16, 16, "SD NAND OK!     ", GREEN);

  if (nor_ok != 0U)
  {
    uint8_t jedec[3];

    while (1)
    {
      rgblcd_show_string(30, 130, 240, 16, 16, "XSPI2 NOR Err!", RED);
      rgblcd_show_num(30, 150, (uint32_t)nor_ok, 3, 16, RED);
      /* 1=NOR init/JEDEC；2=mmap；3=HyperRAM init；4=HyperRAM mmap */
      if (nor_ok == 1U)
      {
        xspi2_nor_get_last_jedec(jedec);
        /* MX25UM25645G: C2 80 39；0 0 0 = 总线无响应/未贴 Octo NOR */
        rgblcd_show_num(30, 170, jedec[0], 3, 16, RED);
        rgblcd_show_num(60, 170, jedec[1], 3, 16, RED);
        rgblcd_show_num(90, 170, jedec[2], 3, 16, RED);
        /* XSPI2 时钟 Hz/1e6，正常约 200；若为 0 则 RCC/Init 有问题 */
        rgblcd_show_num(130, 170,
                        HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_XSPI2) / 1000000U,
                        3, 16, RED);
      }
      HAL_Delay(500);
      LED0_TOGGLE();
    }
  }
  rgblcd_show_string(30, 130, 240, 16, 16, "XSPI2 NOR OK!   ", GREEN);

  if (nor_ok == 0U)
  {
    ocr_infer_init();
    rgblcd_show_string(30, 150, 240, 16, 16, "NPU Init OK     ", GREEN);
    /* 权重区勿在上电 mmap 读（会挂死）；KEY0 时再用间接读检查 */
    rgblcd_show_string(30, 170, 240, 16, 16, "Wgt@71000000    ", BLUE);
  }

  /* GBK 字库在 SD NAND；NOR pack @ 0x71200000（与 OCR 权重错开） */
  font_ok = fonts_init();
  if ((font_ok != 0U) && (nor_ok == 0U))
  {
    rgblcd_show_string(30, 150, 240, 16, 16, "Font install NOR", BLUE);
    font_res = fonts_install_from_xspi(30, 170, 16, RED);
    if (font_res == 0U)
    {
      font_ok = fonts_init();
    }
  }

  if (font_ok != 0U)
  {
    /* OCR 状态栏为 ASCII，无 GBK 字库仍可推理 */
    rgblcd_show_string(30, 150, 240, 16, 16, "Font: ASCII only", YELLOW);
    rgblcd_show_string(30, 170, 240, 16, 16, "Flash@71200000", YELLOW);
    HAL_Delay(2000);
  }
  else
  {
    rgblcd_show_string(30, 150, 240, 16, 16, "Font OK         ", GREEN);
  }

#if 0
  /* 旧逻辑：无字库则死循环；pack 曾与权重同址 0x71000000 */
  font_ok = fonts_init();
  if (font_ok != 0U)
  {
    while (1)
    {
      rgblcd_show_string(30, 150, 240, 16, 16, "Font SD Err!", RED);
      HAL_Delay(500);
      rgblcd_show_string(30, 150, 240, 16, 16, "Check SD font ", RED);
      HAL_Delay(500);
      LED0_TOGGLE();
    }
  }
  rgblcd_show_string(30, 150, 240, 16, 16, "Font OK         ", GREEN);

  while (fonts_init() != 0U)
  {
    rgblcd_show_string(30, 110, 240, 16, 16, "Font install XSPI...", RED);
    font_res = fonts_install_from_xspi(30, 130, 16, RED);
    if (font_res != 0U)
    {
      rgblcd_show_string(30, 150, 240, 16, 16, "Font pack missing!", RED);
      rgblcd_show_num(30, 170, font_res, 3, 16, RED);
      HAL_Delay(1000);
      LED0_TOGGLE();
    }
  }
#endif

  while (ov5640_init())   /* 初始化OV5640 */
  {
    rgblcd_show_string(30, 110, 200, 16, 16, "OV5640 Error!", RED);
    HAL_Delay(500);
    rgblcd_show_string(30, 110, 200, 16, 16, "Please Check!", RED);
    HAL_Delay(500);
    LED0_TOGGLE();
  }
  rgblcd_show_string(30, 110, 200, 16, 16, "OV5640 OK!   ", RED);
  rgblcd_show_string(30, 130, 200, 16, 16, "KEY0: OCR Capture", RED);
  rgblcd_show_string(30, 150, 200, 16, 16, "KEY1: Effects", RED);

  ov5640_rgb565_mode();
  {
    uint8_t af_rc;

    af_rc = ov5640_focus_init();
    ocr_display_focus_set_init(af_rc);
    if (af_rc != 0U)
    {
      ocr_display_status_show2(status_hint, "AF init FAIL", RED);
    }
  }
  ov5640_light_mode(0);
  ov5640_color_saturation(3);
  ov5640_brightness(4);
  ov5640_contrast(3);
  ov5640_sharpness(33);
  {
    uint8_t af_rc;

    af_rc = ov5640_focus_constant();
    ocr_display_focus_set_const(af_rc);
    ocr_display_focus_poll();
    if (af_rc != 0U)
    {
      ocr_display_status_show2(status_hint, "AF const FAIL", RED);
    }
  }

  img_width = rgblcddev.width;
  img_height = rgblcddev.height;
  if (img_width >= 800)
  {
    img_width = 640;
    img_height = 480;
  }
  /* 预览区在状态栏下方，避免提示被 DCMIPP 画面覆盖 */
  {
    uint16_t bar_h = ocr_display_bar_height();
    uint16_t avail_h = (rgblcddev.height > bar_h) ? (uint16_t)(rgblcddev.height - bar_h) : rgblcddev.height;

    if (img_height > avail_h)
    {
      img_height = avail_h;
    }
    img_y_offset = bar_h;
    img_x_offset = (rgblcddev.width - img_width) >> 1;
  }
  ov5640_outsize_set(4, 0, img_width, img_height);

  ov5640_dcmipp_init();
  ov5640_dcmipp_start();

  /* NPU 延后到首次 OCR 再 init，避免上电阶段 HardFault */
  rgblcd_show_string(30, 170, 240, 16, 16, "NPU Ready       ", GREEN);

  rgblcd_clear(WHITE);
  ocr_display_status_show(status_hint, BLUE);

  {
    int uart_rc = uart_port_init();

    if (uart_rc == 0)
    {
      (void)uart_test_start();
      ocr_display_status_show2(status_hint, "USART3 OK PD8/9", BLUE);
    }
    else
    {
      char uart_err[24];

      (void)snprintf(uart_err, sizeof(uart_err), "USART3 init %d", uart_rc);
      ocr_display_status_show2(status_hint, uart_err, RED);
    }
  }
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  {
    while (1)
    {
    uart_test_poll_echo();
    key = key_scan(0);
    if (key)
    {
      switch (key)
      {
        case KEY0_PRES:
        {
          uint8_t af_rc;

          ocr_uart_phase("KEY0");
          /* 暂停连续 AF → 单次对焦 → 稳定后再拍 */
          ocr_display_status_show("\xE5\xAF\xB9\xE7\x84\xA6\xE4\xB8\xAD...", BLUE);
          ov5640_write_reg(0x3022, 0x08);
          HAL_Delay(80);
          ocr_display_focus_poll();
          af_rc = ov5640_focus_single();
          ocr_display_focus_set_single(af_rc);
          ocr_display_focus_poll();
          if (af_rc != 0U)
          {
            ocr_display_status_show2("\xE5\xAF\xB9\xE7\x84\xA6\xE4\xB8\xAD...",
                                   "AF single TIMEOUT", RED);
            HAL_Delay(200);
            af_rc = ov5640_focus_single();
            ocr_display_focus_set_single(af_rc);
            ocr_display_focus_poll();
          }
          HAL_Delay(400);
          ocr_uart_phase("AF done");
          ocr_pipeline_preview_hold_set(1U);
          ocr_display_status_show("\xE8\xAF\x86\xE5\x88\xAB\xE4\xB8\xAD...", BLUE);
          {
            int cap_rc = ocr_pipeline_key0_capture(img_width, img_height, img_x_offset, img_y_offset, &ocr_res);
            if (cap_rc == 0)
            {
              ocr_display_focus_poll();
              ocr_pipeline_show_result(&ocr_res, img_x_offset, img_y_offset, img_width, img_height);
              /* 无检测框时保留 result_text 调试信息，不覆盖 */
              if (ocr_res.nb_box > 0)
              {
                ocr_display_status_show(
                    "KEY1:\xE6\x81\xA2\xE5\xA4\x8D\xE9\xA2\x84\xE8\xA7\x88 KEY0:\xE5\x86\x8D\xE6\x8B\x8D",
                    BLUE);
              }
            }
            else
            {
              char detail[48];

              ocr_pipeline_preview_hold_set(0U);
              ocr_display_focus_poll();
              ocr_display_debug_det_right(ocr_infer_get_dbg_stats());
              ocr_display_show_preproc_left(img_x_offset, img_y_offset, img_width, img_height);
              if (ocr_res.result_text[0] != '\0')
              {
                ocr_weights_get_last_error_detail(detail, sizeof(detail));
                if (strncmp(detail, "WGT LOAD FAIL", 13) != 0)
                {
                  (void)strncpy(detail, ocr_res.result_text, sizeof(detail) - 1U);
                  detail[sizeof(detail) - 1U] = '\0';
                }
                ocr_display_status_show2(ocr_res.result_text, detail, RED);
              }
              else
              {
                ocr_display_status_show("\xE8\xAF\x86\xE5\x88\xAB\xE5\xA4\xB1\xE8\xB4\xA5", RED);
              }
              ov5640_dcmipp_start();
            }
          }
          break;
        }
        case KEY2_PRES:
        {
          uint32_t n;

          ov5640_dcmipp_stop();
          ocr_uart_set_enabled(1U);
          ocr_display_status_show2("USART3 echo 30s", "PD8/9 115200", BLUE);
          n = uart_test_echo_session(30000U);
          ocr_uart_set_enabled(0U);
          (void)snprintf(status_msg, sizeof(status_msg), "USART3 %lu B", (unsigned long)n);
          ocr_display_status_show2("USART3 done", status_msg, BLUE);
          ov5640_dcmipp_start();
          break;
        }
        case KEY1_PRES:
        {
          /* 识别结果保持中：KEY1 恢复实时预览 */
          if (ocr_pipeline_preview_hold_get() != 0U)
          {
            ocr_pipeline_preview_hold_set(0U);
            ocr_display_error_clear();
            ocr_display_status_show(status_hint, BLUE);
            ov5640_dcmipp_start();
            break;
          }
          ov5640_dcmipp_stop();
          if (++effect > 6)
          {
            effect = 0;
          }
          ov5640_special_effects(effect);
          (void)snprintf(status_msg, sizeof(status_msg), "FX: %s", effects_tbl[effect]);
          ocr_display_status_show(status_msg, BLUE);
          HAL_Delay(800);
          ocr_display_status_show(status_hint, BLUE);
          ov5640_dcmipp_start();
          break;
        }
        default:
        {
          break;
        }
      }
    }

    if (++t == 20)
    {
      t = 0;
      LED0_TOGGLE();
    }

    HAL_Delay(10);
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    }
  }
  /* USER CODE END 3 */
}

/**
  * @brief DCMIPP Initialization Function
  * @param None
  * @retval None
  */
static void MX_DCMIPP_Init(void)
{

  /* USER CODE BEGIN DCMIPP_Init 0 */

  /* USER CODE END DCMIPP_Init 0 */

  DCMIPP_ParallelConfTypeDef pParallelConfig = {0};
  DCMIPP_PipeConfTypeDef pPipeConfig = {0};

  /* USER CODE BEGIN DCMIPP_Init 1 */

  /* USER CODE END DCMIPP_Init 1 */
  hdcmipp.Instance = DCMIPP;
  if (HAL_DCMIPP_Init(&hdcmipp) != HAL_OK)
  {
    Error_Handler();
  }

  /** Parallel Config
  */
  pParallelConfig.PCKPolarity = DCMIPP_PCKPOLARITY_RISING ;
  pParallelConfig.HSPolarity = DCMIPP_HSPOLARITY_LOW ;
  pParallelConfig.VSPolarity = DCMIPP_VSPOLARITY_LOW ;
  pParallelConfig.ExtendedDataMode = DCMIPP_INTERFACE_8BITS;
  pParallelConfig.Format = DCMIPP_FORMAT_RGB565;
  pParallelConfig.SwapBits = DCMIPP_SWAPBITS_DISABLE;
  pParallelConfig.SwapCycles = DCMIPP_SWAPCYCLES_ENABLE;
  pParallelConfig.SynchroMode = DCMIPP_SYNCHRO_HARDWARE;
  HAL_DCMIPP_PARALLEL_SetConfig(&hdcmipp, &pParallelConfig);

  /** Pipe 0 Config
  */
  pPipeConfig.FrameRate = DCMIPP_FRAME_RATE_ALL;
  pPipeConfig.PixelPipePitch = 10;
  pPipeConfig.PixelPackerFormat = DCMIPP_PIXEL_PACKER_FORMAT_RGB888_YUV444_1;
  if (HAL_DCMIPP_PIPE_SetConfig(&hdcmipp, DCMIPP_PIPE0, &pPipeConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN DCMIPP_Init 2 */

  /* USER CODE END DCMIPP_Init 2 */

}

/**
  * @brief DMA2D Initialization Function
  * @param None
  * @retval None
  */
static void MX_DMA2D_Init(void)
{

  /* USER CODE BEGIN DMA2D_Init 0 */

  /* USER CODE END DMA2D_Init 0 */

  /* USER CODE BEGIN DMA2D_Init 1 */

  /* USER CODE END DMA2D_Init 1 */
  hdma2d.Instance = DMA2D;
  hdma2d.Init.Mode = DMA2D_M2M_PFC;
  hdma2d.Init.ColorMode = DMA2D_OUTPUT_RGB565;
  hdma2d.Init.OutputOffset = 0;
  hdma2d.LayerCfg[1].InputOffset = 0;
  hdma2d.LayerCfg[1].InputColorMode = DMA2D_INPUT_RGB565;
  hdma2d.LayerCfg[1].AlphaMode = DMA2D_NO_MODIF_ALPHA;
  hdma2d.LayerCfg[1].InputAlpha = 255;
  if (HAL_DMA2D_Init(&hdma2d) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_DMA2D_ConfigLayer(&hdma2d, 1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN DMA2D_Init 2 */

  /* USER CODE END DMA2D_Init 2 */

}

/**
  * @brief LTDC Initialization Function
  * @param None
  * @retval None
  */
static void MX_LTDC_Init(void)
{

  /* USER CODE BEGIN LTDC_Init 0 */

  /* USER CODE END LTDC_Init 0 */

  LTDC_LayerCfgTypeDef pLayerCfg = {0};

  /* USER CODE BEGIN LTDC_Init 1 */

  /* USER CODE END LTDC_Init 1 */
  hltdc.Instance = LTDC;
  hltdc.Init.HSPolarity = LTDC_HSPOLARITY_AL;
  hltdc.Init.VSPolarity = LTDC_VSPOLARITY_AL;
  hltdc.Init.DEPolarity = LTDC_DEPOLARITY_AL;
  hltdc.Init.PCPolarity = LTDC_PCPOLARITY_IPC;
  hltdc.Init.HorizontalSync = 0;
  hltdc.Init.VerticalSync = 0;
  hltdc.Init.AccumulatedHBP = 40;
  hltdc.Init.AccumulatedVBP = 8;
  hltdc.Init.AccumulatedActiveW = 520;
  hltdc.Init.AccumulatedActiveH = 280;
  hltdc.Init.TotalWidth = 525;
  hltdc.Init.TotalHeigh = 288;
  hltdc.Init.Backcolor.Blue = 0;
  hltdc.Init.Backcolor.Green = 0;
  hltdc.Init.Backcolor.Red = 0;
  if (HAL_LTDC_Init(&hltdc) != HAL_OK)
  {
    Error_Handler();
  }
  pLayerCfg.WindowX0 = 0;
  pLayerCfg.WindowX1 = 480;
  pLayerCfg.WindowY0 = 0;
  pLayerCfg.WindowY1 = 272;
  pLayerCfg.PixelFormat = LTDC_PIXEL_FORMAT_RGB565;
  pLayerCfg.Alpha = 255;
  pLayerCfg.Alpha0 = 0;
  pLayerCfg.BlendingFactor1 = LTDC_BLENDING_FACTOR1_CA;
  pLayerCfg.BlendingFactor2 = LTDC_BLENDING_FACTOR2_CA;
  pLayerCfg.FBStartAdress = 0;
  pLayerCfg.ImageWidth = 480;
  pLayerCfg.ImageHeight = 272;
  pLayerCfg.Backcolor.Blue = 0;
  pLayerCfg.Backcolor.Green = 0;
  pLayerCfg.Backcolor.Red = 0;
  if (HAL_LTDC_ConfigLayer(&hltdc, &pLayerCfg, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN LTDC_Init 2 */

  /* USER CODE END LTDC_Init 2 */

}

/**
  * @brief RIF Initialization Function
  * @param None
  * @retval None
  */
  static void SystemIsolation_Config(void)
{

  /* USER CODE BEGIN RIF_Init 0 */

  /* USER CODE END RIF_Init 0 */

  /* set all required IPs as secure privileged */
  __HAL_RCC_RIFSC_CLK_ENABLE();

  /*RIMC configuration*/
  RIMC_MasterConfig_t RIMC_master = {0};
  RIMC_master.MasterCID = RIF_CID_1;
  RIMC_master.SecPriv = RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV;
  HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_DCMIPP, &RIMC_master);

  HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_DMA2D, &RIMC_master);

  HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_LTDC1, &RIMC_master);

  HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_SDMMC2, &RIMC_master);

  HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_NPU, &RIMC_master);

  /*RISUP configuration*/
  HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_DCMIPP , RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
  HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_DMA2D , RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
  HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_LTDCL1 , RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
  HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_SDMMC2 , RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
  HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_NPU , RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);

  /* RIF-Aware IPs Config */

  /* set up PWR configuration */
  HAL_PWR_ConfigAttributes(PWR_ITEM_0,PWR_SEC_NPRIV);

  /* set up GPIO configuration */
  HAL_GPIO_ConfigPinAttributes(GPIOA,GPIO_PIN_0,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOA,GPIO_PIN_1,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOA,GPIO_PIN_2,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOA,GPIO_PIN_3,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOA,GPIO_PIN_5,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOA,GPIO_PIN_8,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOA,GPIO_PIN_9,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOA,GPIO_PIN_10,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOA,GPIO_PIN_11,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOB,GPIO_PIN_7,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOB,GPIO_PIN_8,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOB,GPIO_PIN_9,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOB,GPIO_PIN_10,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOB,GPIO_PIN_11,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOB,GPIO_PIN_12,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOB,GPIO_PIN_15,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOC,GPIO_PIN_6,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOC,GPIO_PIN_0,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOC,GPIO_PIN_3,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOC,GPIO_PIN_4,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOC,GPIO_PIN_5,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOC,GPIO_PIN_13,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOD,GPIO_PIN_0,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOD,GPIO_PIN_1,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOD,GPIO_PIN_2,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOD,GPIO_PIN_5,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOD,GPIO_PIN_7,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOE,GPIO_PIN_0,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOE,GPIO_PIN_4,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOE,GPIO_PIN_5,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOE,GPIO_PIN_6,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOE,GPIO_PIN_8,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOE,GPIO_PIN_10,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOE,GPIO_PIN_13,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOE,GPIO_PIN_14,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOF,GPIO_PIN_8,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOF,GPIO_PIN_9,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOG,GPIO_PIN_0,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOG,GPIO_PIN_9,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOG,GPIO_PIN_10,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOG,GPIO_PIN_11,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOG,GPIO_PIN_13,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOG,GPIO_PIN_14,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOH,GPIO_PIN_4,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOH,GPIO_PIN_9,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPION,GPIO_PIN_0,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPION,GPIO_PIN_1,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPION,GPIO_PIN_2,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPION,GPIO_PIN_3,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPION,GPIO_PIN_4,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPION,GPIO_PIN_5,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPION,GPIO_PIN_6,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPION,GPIO_PIN_8,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPION,GPIO_PIN_9,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPION,GPIO_PIN_10,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPION,GPIO_PIN_11,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOO,GPIO_PIN_0,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOO,GPIO_PIN_2,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOO,GPIO_PIN_4,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOO,GPIO_PIN_5,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOP,GPIO_PIN_0,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOP,GPIO_PIN_1,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOP,GPIO_PIN_2,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOP,GPIO_PIN_3,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOP,GPIO_PIN_4,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOP,GPIO_PIN_5,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOP,GPIO_PIN_6,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOP,GPIO_PIN_7,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOQ,GPIO_PIN_2,GPIO_PIN_SEC|GPIO_PIN_NPRIV);

  /* USER CODE BEGIN RIF_Init 1 */
  /* USER CODE END RIF_Init 1 */
  /* USER CODE BEGIN RIF_Init 2 */

  /* USER CODE END RIF_Init 2 */

}

/**
  * @brief XSPI1 Initialization Function
  * @param None
  * @retval None
  */
void MX_XSPI1_Init(void)
{

  /* USER CODE BEGIN XSPI1_Init 0 */

  /* USER CODE END XSPI1_Init 0 */

  XSPIM_CfgTypeDef sXspiManagerCfg = {0};
  XSPI_HyperbusCfgTypeDef sHyperBusCfg = {0};

  /* USER CODE BEGIN XSPI1_Init 1 */

  /* USER CODE END XSPI1_Init 1 */
  /* XSPI1 parameter configuration*/
  hxspi1.Instance = XSPI1;
  hxspi1.Init.FifoThresholdByte = 4;
  hxspi1.Init.MemoryMode = HAL_XSPI_SINGLE_MEM;
  hxspi1.Init.MemoryType = HAL_XSPI_MEMTYPE_HYPERBUS;
  hxspi1.Init.MemorySize = HAL_XSPI_SIZE_256MB;
  hxspi1.Init.ChipSelectHighTimeCycle = 2;
  hxspi1.Init.FreeRunningClock = HAL_XSPI_FREERUNCLK_DISABLE;
  hxspi1.Init.ClockMode = HAL_XSPI_CLOCK_MODE_0;
  hxspi1.Init.WrapSize = HAL_XSPI_WRAP_32_BYTES;
  hxspi1.Init.ClockPrescaler = 1 - 1;
  hxspi1.Init.SampleShifting = HAL_XSPI_SAMPLE_SHIFT_NONE;
  hxspi1.Init.DelayHoldQuarterCycle = HAL_XSPI_DHQC_DISABLE;
  hxspi1.Init.ChipSelectBoundary = HAL_XSPI_BONDARYOF_NONE;
  hxspi1.Init.MaxTran = 0;
  hxspi1.Init.Refresh = 0;
  hxspi1.Init.MemorySelect = HAL_XSPI_CSSEL_NCS1;
  if (HAL_XSPI_Init(&hxspi1) != HAL_OK)
  {
    Error_Handler();
  }
  sXspiManagerCfg.nCSOverride = HAL_XSPI_CSSEL_OVR_NCS1;
  sXspiManagerCfg.IOPort = HAL_XSPIM_IOPORT_1;
  sXspiManagerCfg.Req2AckTime = 1;
  if (HAL_XSPIM_Config(&hxspi1, &sXspiManagerCfg, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    Error_Handler();
  }
  sHyperBusCfg.RWRecoveryTimeCycle = 7;
  sHyperBusCfg.AccessTimeCycle = 7;
  sHyperBusCfg.WriteZeroLatency = HAL_XSPI_LATENCY_ON_WRITE;
  sHyperBusCfg.LatencyMode = HAL_XSPI_FIXED_LATENCY;
  if (HAL_XSPI_HyperbusCfg(&hxspi1, &sHyperBusCfg, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN XSPI1_Init 2 */

  /* USER CODE END XSPI1_Init 2 */

}

/**
  * @brief XSPI2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_XSPI2_Init(void)
{

  /* USER CODE BEGIN XSPI2_Init 0 */

  /* USER CODE END XSPI2_Init 0 */

  XSPIM_CfgTypeDef sXspiManagerCfg = {0};

  /* USER CODE BEGIN XSPI2_Init 1 */

  /* USER CODE END XSPI2_Init 1 */
  /* XSPI2 parameter configuration*/
  hxspi2.Instance = XSPI2;
  hxspi2.Init.FifoThresholdByte = 4;
  hxspi2.Init.MemoryMode = HAL_XSPI_SINGLE_MEM;
  hxspi2.Init.MemoryType = HAL_XSPI_MEMTYPE_MACRONIX;
  hxspi2.Init.MemorySize = HAL_XSPI_SIZE_256MB;
  hxspi2.Init.ChipSelectHighTimeCycle = 1;
  hxspi2.Init.FreeRunningClock = HAL_XSPI_FREERUNCLK_DISABLE;
  hxspi2.Init.ClockMode = HAL_XSPI_CLOCK_MODE_0;
  hxspi2.Init.WrapSize = HAL_XSPI_WRAP_NOT_SUPPORTED;
  hxspi2.Init.ClockPrescaler = 0;
  hxspi2.Init.SampleShifting = HAL_XSPI_SAMPLE_SHIFT_NONE;
  hxspi2.Init.DelayHoldQuarterCycle = HAL_XSPI_DHQC_DISABLE;
  hxspi2.Init.ChipSelectBoundary = HAL_XSPI_BONDARYOF_NONE;
  hxspi2.Init.MaxTran = 0;
  hxspi2.Init.Refresh = 0;
  hxspi2.Init.MemorySelect = HAL_XSPI_CSSEL_NCS1;
  if (HAL_XSPI_Init(&hxspi2) != HAL_OK)
  {
    Error_Handler();
  }
  sXspiManagerCfg.nCSOverride = HAL_XSPI_CSSEL_OVR_NCS1;
  sXspiManagerCfg.IOPort = HAL_XSPIM_IOPORT_2;
  sXspiManagerCfg.Req2AckTime = 1;
  if (HAL_XSPIM_Config(&hxspi2, &sXspiManagerCfg, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN XSPI2_Init 2 */

  /* USER CODE END XSPI2_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */
  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOQ_CLK_ENABLE();
  __HAL_RCC_GPIOP_CLK_ENABLE();
  __HAL_RCC_GPIOO_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPION_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOE, GPIO_PIN_13|GPIO_PIN_14|GPIO_PIN_10, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOQ, GPIO_PIN_2, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOG, GPIO_PIN_14, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_3, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOG, GPIO_PIN_10, GPIO_PIN_SET);

  /*Configure GPIO pin : PE13 */
  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  /*Configure GPIO pin : PD1 */
  GPIO_InitStruct.Pin = GPIO_PIN_1;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /*Configure GPIO pin : PC6 */
  GPIO_InitStruct.Pin = GPIO_PIN_6;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : PE14 */
  GPIO_InitStruct.Pin = GPIO_PIN_14;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  /*Configure GPIO pin : PE10 */
  GPIO_InitStruct.Pin = GPIO_PIN_10;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  /*Configure GPIO pin : PQ2 */
  GPIO_InitStruct.Pin = GPIO_PIN_2;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(GPIOQ, &GPIO_InitStruct);

  /*Configure GPIO pin : PC13 */
  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : PG14 */
  GPIO_InitStruct.Pin = GPIO_PIN_14;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

  /*Configure GPIO pin : PA3 */
  GPIO_InitStruct.Pin = GPIO_PIN_3;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : PG10 */
  GPIO_InitStruct.Pin = GPIO_PIN_10;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

  /*Configure GPIO pin : PG11 */
  GPIO_InitStruct.Pin = GPIO_PIN_11;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */
  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

static uint8_t MX_SDMMC2_SD_Init(void)
{
  uint8_t retry;

  hsd2_last_error = 0U;

  /* 与 37_SD_NAND 一致：须在 SystemIsolation 之前完成 SDMMC2 首次初始化 */
  HAL_Delay(50);

  for (retry = 0U; retry < 3U; retry++)
  {
    hsd2.Instance = SDMMC2;
    hsd2.Init.ClockEdge = SDMMC_CLOCK_EDGE_RISING;
    hsd2.Init.ClockPowerSave = SDMMC_CLOCK_POWER_SAVE_DISABLE;
    hsd2.Init.BusWide = SDMMC_BUS_WIDE_4B;
    hsd2.Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_ENABLE;
    /* 首次用较慢时钟，重试再恢复例程 ClockDiv=4 */
    hsd2.Init.ClockDiv = (retry == 0U) ? 8U : 4U;
    if (HAL_SD_Init(&hsd2) == HAL_OK)
    {
      return 0U;
    }
    hsd2_last_error = hsd2.ErrorCode;
    HAL_SD_DeInit(&hsd2);
    HAL_Delay(100);
  }
  return 1U;
}

/**
  * @brief  HyperRAM 映射区 MPU：LTDC 帧缓冲/DCMIPP 缓冲与 CPU 缓存一致
  * @note   链接脚本 EXTRAM @ 0x90000000，长度 32MB；须于 SCB_EnableDCache 之前调用
  */
static void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct = {0};
  MPU_Attributes_InitTypeDef MPU_Attr = {0};

  HAL_MPU_Disable();

  /* 属性 0：外设映射区 non-cacheable（NOR 权重 + HyperRAM 帧缓冲） */
  MPU_Attr.Number = MPU_ATTRIBUTES_NUMBER0;
  MPU_Attr.Attributes = INNER_OUTER(MPU_NOT_CACHEABLE);
  HAL_MPU_ConfigMemoryAttributes(&MPU_Attr);

  /* 地址升序：Region0 = XSPI2 NOR @0x70000000（含权重 0x71000000） */
  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER0;
  MPU_InitStruct.BaseAddress = 0x70000000U;
  MPU_InitStruct.LimitAddress = 0x77FFFFFFU;
  MPU_InitStruct.AttributesIndex = MPU_ATTRIBUTES_NUMBER0;
  MPU_InitStruct.AccessPermission = MPU_REGION_ALL_RW;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /* Region1 = HyperRAM @0x90000000，LTDC/DCMIPP 帧缓冲 */
  MPU_InitStruct.Number = MPU_REGION_NUMBER1;
  MPU_InitStruct.BaseAddress = 0x90000000U;
  MPU_InitStruct.LimitAddress = 0x91FFFFFFU;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

/**
 * @brief   HAL库DCMIPP pipe帧事件回调函数
 * @param   pHdcmipp: DCMIPP句柄指针
 * @param   Pipe: DCMIPP pipe号
 * @retval  无
 */
void HAL_DCMIPP_PIPE_FrameEventCallback(DCMIPP_HandleTypeDef *hdcmipp, uint32_t Pipe)
{
  if (hdcmipp->Instance == DCMIPP)
  {
    if (Pipe == DCMIPP_PIPE0)
    {
      /* 单次拍照完成标志须始终更新，不能因 preview_hold 跳过 */
      ov5640_dcmipp_frame_event();
      /* 识别结果冻结时不再刷连续预览，保留检测框 */
      if (ocr_pipeline_preview_hold_get() == 0U)
      {
        rgblcd_color_fill(img_x_offset, img_y_offset, img_x_offset + img_width - 1, img_y_offset + img_height - 1, (uint16_t *)ov5640_dcmipp_buf);
        ocr_display_status_redraw();
      }
    }
  }
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
