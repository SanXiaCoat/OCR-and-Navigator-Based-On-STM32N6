# PP-OCRv4 en rec 48x320 QDQ + STEdgeAI generate
$ErrorActionPreference = "Stop"
$Root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$Tools = Join-Path $Root "OCR_model\tools"
$Raw = Join-Path $Root "OCR_model\raw"
$Calib = Join-Path $Root "OCR_model\calib_images"
$RecPaddle = Join-Path $Root "资料\model\en_PP-OCRv4_rec_infer"
$OutOnnx = Join-Path $Root "OCR_model\en_PP-OCRv4_rec_qdq.onnx"

New-Item -ItemType Directory -Force -Path $Raw, $Calib | Out-Null

if (-not (Test-Path $RecPaddle)) {
    Write-Host "Missing $RecPaddle — download en_PP-OCRv4_rec_infer.tar first."
    exit 1
}

$FloatOnnx = Join-Path $Raw "en_PP-OCRv4_rec_infer.onnx"
if (-not (Test-Path $FloatOnnx)) {
    paddle2onnx --model_dir $RecPaddle `
        --model_filename inference.pdmodel `
        --params_filename inference.pdiparams `
        --save_file $FloatOnnx `
        --opset_version 13 --enable_onnx_checker True
}

python (Join-Path $Tools "quantize_ppocr_n6.py") rec `
    --input $FloatOnnx `
    --output $OutOnnx `
    --calib-dir $Calib `
    --height 48 --width 320

python (Join-Path $Tools "gen_ocr_rec_dict.py")

Write-Host ""
Write-Host "Next: CubeMX Generate ocr_rec from $OutOnnx"
Write-Host "  Flash ocr_rec_weights.hex @ 0x71600000"
Write-Host "  Set OCR_REC_ENABLE=1 in Appli/Core/Inc/ocr_rec_infer.h and rebuild"
