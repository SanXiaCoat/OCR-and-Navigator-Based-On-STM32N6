# PP-OCRv4 det 320x320 QDQ + STEdgeAI generate (run on PC with CubeMX / STEdgeAI)
$ErrorActionPreference = "Stop"
$Root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$Tools = Join-Path $Root "OCR_model\tools"
$Raw = Join-Path $Root "OCR_model\raw"
$Calib = Join-Path $Root "OCR_model\calib_images"
$DetPaddle = Join-Path $Root "资料\model\ch_PP-OCRv4_det_infer"
$OutOnnx = Join-Path $Root "OCR_model\ch_PP-OCRv4_det_320_qdq.onnx"

New-Item -ItemType Directory -Force -Path $Raw, $Calib | Out-Null

if (-not (Test-Path $DetPaddle)) {
    Write-Host "Missing $DetPaddle — download ch_PP-OCRv4_det_infer.tar first."
    exit 1
}

if ((Get-ChildItem $Calib -File -ErrorAction SilentlyContinue | Measure-Object).Count -lt 5) {
    Write-Host "Add 50+ English/document photos to $Calib (board captures recommended)."
}

$FloatOnnx = Join-Path $Raw "ch_PP-OCRv4_det_infer.onnx"
if (-not (Test-Path $FloatOnnx)) {
    Write-Host "Export float ONNX..."
    paddle2onnx --model_dir $DetPaddle `
        --model_filename inference.pdmodel `
        --params_filename inference.pdiparams `
        --save_file $FloatOnnx `
        --opset_version 13 --enable_onnx_checker True
}

Write-Host "Quantize 320x320 QDQ..."
python (Join-Path $Tools "quantize_ppocr_n6.py") det `
    --input $FloatOnnx `
    --output $OutOnnx `
    --calib-dir $Calib `
    --height 320 --width 320

Write-Host ""
Write-Host "Next: CubeMX / STEdgeAI"
Write-Host "  Model: $OutOnnx"
Write-Host "  Profile: n6-allmems-O3 or profile_O3_force8bits"
Write-Host "  Analyze -> Generate -> copy ocr_det.* + weights.hex to ExtMemLoader"
Write-Host "  python OCR_model/tools/update_ocr_infer_from_c_info.py OCR_model/stedgeai_out/ocr_det_c_info.json"
Write-Host "  Flash ocr_det_weights.hex @ 0x71000000"
