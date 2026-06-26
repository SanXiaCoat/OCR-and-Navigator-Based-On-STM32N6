# 工程文件夹说明

基于 **STM32N647**（正点原子 ATK-CNN647B）的 OCR 文字识别 + 导航显示项目。硬件平台相同，固件拆成两个 Cube 工程；Android App 负责高德导航并通过蓝牙把数据发给开发板。

---

## 根目录

| 路径 | 说明 |
|------|------|
| `CubeProjects/` | STM32 CubeMX / CubeIDE 固件工程 |
| `Android Applications/` | Android 导航 App 及 macOS 调试接收端 |

---

## CubeProjects

两个工程目录结构相近，均基于 `AI_Vision.ioc` 生成，共用 BSP 驱动与 X-CUBE-AI 模型框架，但应用逻辑不同。

| 工程 | 用途 | 默认模式 |
|------|------|----------|
| `OCR_Detection/` | 摄像头拍照 + PP-OCR 板端推理，LCD 显示识别文字 | 纯 OCR |
| `Navigator/` | UART 接收导航 JSON，LCD 全屏显示转向/路名/剩余距离等 | 纯导航（`APP_OCR_ENABLE=0`） |

**硬件依赖（两工程共用）**：OV5640 摄像头、RGB LCD、HyperRAM、板载 NOR Flash（存模型权重）、SD NAND（GBK 字库）。

---

### `OCR_Detection/` — OCR 识别固件

OV5640 采集图像，经 NPU 运行 PP-OCR **检测（det）+ 识别（rec）**，结果绘制到 LCD；USART3（PD8/9）用于串口调试。

| 子目录 / 文件 | 说明 |
|---------------|------|
| `Appli/Core/` | 应用源码：`main.c`、`ocr_infer`、`ocr_pipeline`、`ocr_display`、字库与权重搬运等 |
| `Drivers/BSP/` | 板级驱动：HyperRAM、OV5640、RGBLCD、KEY、LED、SD_NAND、NORFlash、SYS、UART |
| `Middlewares/` | `MALLOC` 内存管理、`TEXT` 汉字显示、`ST/AI` X-CUBE-AI 运行时 |
| `ExtMemLoader/` | 外部存储引导加载器；`X-CUBE-AI/App/` 含 `ocr_det` 模型生成代码 |
| `STM32CubeIDE/` | CubeIDE 工程（`Appli`、`ExtMemLoader`），在此编译 |
| `Binary/` | 编译产物（如 `appli.hex`） |
| `Secure_nsclib/` | TrustZone 非安全侧库 |
| `AI_Vision/` | 与根目录平行的工程副本（Appli / ExtMemLoader / CubeIDE 等），便于整包分享或导入 |
| `OCR_model/` | PP-OCR 模型与量化：`*.onnx`、`calib_images/`、`stedgeai_out/`、`tools/`（量化与生成脚本） |
| `AI_Vision.ioc` | CubeMX 芯片与外设配置 |
| `ocr_det_weights.hex` | 检测模型权重，烧录至 NOR `@0x71000000` |
| `ocr_det_atonbuf.xSPI2.raw` | 检测模型 ATON 缓冲区原始数据 |

---

### `Navigator/` — 导航显示固件

通过 **UART4**（PC10/11，115200）接收 Android App 发来的 `navi_update` JSON，解析后在 LCD 显示导航信息；不启用摄像头预览。`main.c` 中 `APP_OCR_ENABLE` 设为 `1` 可切回 OCR 模式。

| 子目录 / 文件 | 说明 |
|---------------|------|
| `Appli/Core/` | 应用源码；导航相关：`nav_uart`（JSON 解析与刷新）、`nav_glyphs_builtin`（内置转向图标） |
| `Drivers/BSP/` | 同 OCR 工程板级驱动（导航模式主要用 LCD、UART、字库） |
| `Middlewares/` | 内存、字库、AI 中间件 |
| `ExtMemLoader/` | 外部存储加载器及 X-CUBE-AI 代码 |
| `STM32CubeIDE/` | CubeIDE 工程；`Application/User/Core/` 为实际参与编译的源码目录 |
| `Binary/` | 编译产物 |
| `Secure_nsclib/` | TrustZone 非安全侧库 |
| `tools/` | 字库与导航字形工具：`pack_fonts.py`、`gen_nav_glyphs.py` |
| `AI_Vision.ioc` | CubeMX 配置 |
| `ocr_det_weights.hex` | OCR 模式备用权重（导航模式默认不用） |

> `Navigator/` 内含独立 `.git` 仓库，可单独版本管理。

---

## Android Applications

### `navigator/` — 高德导航 App（包名 `com.sanxia.stm32ARmap`）

集成高德地图 / 导航 SDK：POI 搜索、步行与骑行导航，并将实时导航状态序列化为 JSON 发出。

| 子目录 / 文件 | 说明 |
|---------------|------|
| `app/src/main/java/` | Java 源码 |
| `app/src/main/res/` | 布局、图标等资源 |
| `gradle/`、`build.gradle` | Gradle 构建配置 |
| `macos/` | macOS BLE 调试接收端（见下） |

**主要类**

| 类 | 说明 |
|----|------|
| `MainActivity` | 主界面：搜索、导航、蓝牙连接与 JSON 发送 |
| `BluetoothJsonSender` | 经典蓝牙 SPP，向 STM32（UART4）发送导航 JSON |
| `MacBleJsonSender` | BLE，向 macOS 接收端发送 JSON（调试用） |
| `NaviActivity` / `MapActivity` | 导航与地图示例页面 |

**`macos/`** — 电脑端调试

| 文件 | 说明 |
|------|------|
| `NaviJsonMac.app` | macOS BLE 接收应用 |
| `BleNaviReceiver.swift` | 接收端 Swift 源码 |
| `build_receiver_app.sh` | 打包脚本 |
| `README.md` | 运行说明；日志写入 `~/Desktop/NaviJsonMac.log` |

---

## 数据流

```
Android navigator App
    ├─ 经典蓝牙 SPP ──► STM32 Navigator（UART4）──► LCD 导航界面
    └─ BLE ──────────► macOS NaviJsonMac（调试）

STM32 OCR_Detection
    OV5640 拍照 ──► NPU PP-OCR ──► LCD 显示文字
```

OCR 与导航为**两个独立固件**，按需分别烧录；Android App 只与 Navigator 固件通信。
