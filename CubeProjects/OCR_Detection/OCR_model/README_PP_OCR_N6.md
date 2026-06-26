# PP-OCR on STM32N647 (Neural-ART) — 推荐方案

当前 **float ONNX + native-float** 会得到 **259/264 SW epoch**（几乎全 CPU，几十分钟/次）。
继续用 PP-OCR 的正确做法是：**官方模型 → QDQ INT8 ONNX → CubeMX Analyze 通过后再 Generate**。

---

## 1. 模型选型（英文 + 数字）

| 角色 | 推荐模型 | 输入 | 说明 |
|------|----------|------|------|
| **det** | `ch_PP-OCRv4_det_infer` 固定 **320×320**（推荐）或 160×160 | 1×3×H×W | 与 `ocr_infer` 预处理一致 |
| **rec** | **`en_PP-OCRv4_rec_infer`**（或 mobile 版） | 1×3×48×320 | 英文 + 数字 + 常用符号 |
| **cls** | 暂不需要 | — | 假设文本基本水平；倾斜大再加 `en_PP-OCRv3_cls` |

官方下载（PaddleOCR）：

```text
https://paddleocr.bj.bcebos.com/PP-OCRv4/chinese/ch_PP-OCRv4_det_infer.tar
https://paddleocr.bj.bcebos.com/PP-OCRv4/english/en_PP-OCRv4_rec_infer.tar
```

---

## 2. PC 端流程（一次性）

### 2.1 Paddle → float ONNX

```bash
pip install paddle2onnx onnx onnxsim onnxruntime

paddle2onnx --model_dir ./ch_PP-OCRv4_det_infer \
  --model_filename inference.pdmodel \
  --params_filename inference.pdiparams \
  --save_file ./raw/ch_PP-OCRv4_det_infer.onnx \
  --opset_version 13 --enable_onnx_checker True

paddle2onnx --model_dir ./en_PP-OCRv4_rec_infer \
  --model_filename inference.pdmodel \
  --params_filename inference.pdiparams \
  --save_file ./raw/en_PP-OCRv4_rec_infer.onnx \
  --opset_version 13 --enable_onnx_checker True
```

### 2.2 校准集

`OCR_model/calib_images/` 放 **50～200 张** 含英文/数字的实拍或文档图（与 OV5640 场景接近）。

### 2.3 float ONNX → **QDQ INT8**（关键）

```bash
cd OCR_model/tools

python quantize_ppocr_n6.py det \
  --input ../raw/ch_PP-OCRv4_det_infer.onnx \
  --output ../ch_PP-OCRv4_det_320_qdq.onnx \
  --calib-dir ../calib_images \
  --height 320 --width 320

python quantize_ppocr_n6.py rec \
  --input ../raw/en_PP-OCRv4_rec_infer.onnx \
  --output ../en_PP-OCRv4_rec_qdq.onnx \
  --calib-dir ../calib_images \
  --height 48 --width 320
```

ST 文档：[ONNX QDQ 量化说明](https://wiki.st.com/stm32mcu/wiki/AI:X-CUBE-AI_support_of_ONNX_and_TensorFlow_quantized_models)

---

## 3. CubeMX / STEdgeAI

对每个网络（**先 det，再 rec**）：

1. **Model file** → `*_qdq.onnx`
2. **Runtime** → Neural-ART™
3. **Profile** → `profile_O3_force8bits`（或 `n6-allmems-O3`）
4. **Analyze** → 打开 report，检查：

```text
Total number of epochs: XXX of which YYY implemented in software
```

| 网络 | 合格线（建议） | 不合格时 |
|------|----------------|----------|
| det | YYY **< 80** | 换 `PP-OCRv3 mobile det` 320 再 QDQ；或简化 ONNX |
| rec | YYY **< 50** | 换 `en_PP-OCRv4_mobile_rec` |

5. **Generate** → 得到 `ocr_det.c` / `ocr_rec.c` + 各自 `weights.hex`

---

## 4. NOR 权重布局

| 区域 | 地址 | 内容 |
|------|------|------|
| det 权重 | `0x71000000` | 现有 `ocr_det_weights.hex` |
| rec 权重 | `0x71600000` | 新生 `ocr_rec_weights.hex`（Analyze 后按 report 定大小） |

两网 **分开烧录**；Appli 里各维护 `OCR_DET_WEIGHTS_BYTES` / `OCR_REC_WEIGHTS_BYTES`。

---

## 5. Appli 软件改动（Generate 通过后）

现有工程已具备 **det 管线 + 框绘制**；缺 **rec** 与 **INT8 预处理**。

```
KEY0
 → 拍照 RGB565
 → det 预处理(INT8) → NPU det → 热力图/框
 → 对每个框裁剪 → rec 预处理 → NPU rec → CTC 解码
 → LCD 显示 "ABC123" + 画框
```

| 模块 | 工作 |
|------|------|
| `ocr_infer.c` | det 输入改为 INT8（按 `ocr_det.h` / c_info）；超时改 ≥60s |
| `ocr_rec_infer.c`（新） | rec 裁剪、48×320 预处理、推理、CTC |
| `ocr_rec_dict.h` | 从 PaddleOCR `en_dict.txt` 生成字符表 |
| `ocr_weights_reloc.c` | 支持 rec 权重 @0x71600000 加载 |
| `ocr_infer_postprocess` | det 后调用 rec，填充 `result_text` |
| CubeMX | 增加第二个网络 `ocr_rec`（或手动拷贝生成代码） |

---

## 6. 若 QDQ 后 det 仍然 SW 很多

按顺序尝试：

1. **PP-OCRv3 mobile det**（更小、算子更简单）→ QDQ → Analyze  
2. 校准集换成 **你的板子实际拍照**  
3. `onnxsim` 简化图（脚本已调用）  
4. 仍不行：det 保留 CPU 但 **仅 160×160 每 2～3 秒跑一次**；**rec 必须 INT8 上 NPU**（单框 ~50ms，体验可接受）

---

## 7. 预期性能（QDQ 成功时）

| 步骤 | 粗估 |
|------|------|
| det 一次 | 0.5～5 s |
| rec 每个框 | 0.05～0.3 s |
| 3 个框合计 | **约 1～6 s**（可接受） |

对比现在 float det：**10～30+ 分钟**。

---

## 8. 板端固件（已实现）

- KEY0：拍照 → det NPU → DB 后处理 → 画框；`OCR_REC_ENABLE=1` 时级联 rec
- 诊断默认 **LCD 状态栏**（UART 仅 KEY2 echo 会话）
- 320 det：`OCR_model/tools/build_det_320.ps1` → Generate → `update_ocr_infer_from_c_info.py`
- rec：`OCR_model/tools/build_rec.ps1` → Generate → `OCR_REC_ENABLE=1`

## 9. 你现在要做的事（ checklist ）

- [ ] 下载 det + en rec 推理包，转 float ONNX  
- [ ] 准备 `calib_images/`  
- [ ] 运行 `tools/quantize_ppocr_n6.py` 生成 `*_qdq.onnx`  
- [ ] CubeMX Analyze det，把 **SW epoch 行** 发出来确认  
- [ ] 通过后 Generate + 烧 det 权重  
- [ ] 同样流程做 rec  
- [ ] 再改 Appli 接 rec（可在 det Analyze 通过后开始）

---

## 10. 不要做的事

- 不要继续用 **float ONNX + ModelCompression=None**（必慢）  
- 不要指望只改 `profile_O3_force8bits` 而不做 QDQ（已验证仍 259 SW）  
- 不要换 YOLO 又坚持 PP-OCR rec 混用（后处理不兼容）
