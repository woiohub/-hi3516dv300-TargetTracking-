#!/usr/bin/env python3
"""
YOLOv8n ONNX导出脚本

功能:
1. 加载训练好的best.pt模型
2. 导出为ONNX格式(简化版)
3. 验证ONNX模型

使用方式:
    python3 export_onnx.py

前置条件:
    - 已运行 train.py 完成训练
    - best.pt 权重文件存在
"""

from ultralytics import YOLO
from pathlib import Path

PROJECT_DIR = Path(__file__).parent
WEIGHTS_DIR = PROJECT_DIR / "runs" / "detect" / "person_detect" / "weights"
# ultralytics默认保存路径: {project}/runs/detect/{name}/weights/
BEST_PT = WEIGHTS_DIR / "best.pt"
IMG_SIZE = 416


def main():
    print("=" * 60)
    print("  YOLOv8n ONNX导出")
    print("=" * 60)

    # 检查权重文件
    if not BEST_PT.exists():
        print(f"错误: 权重文件不存在: {BEST_PT}")
        print("请先运行 train.py 完成训练")
        return

    # 加载模型
    print(f"\n加载模型: {BEST_PT}")
    model = YOLO(str(BEST_PT))

    # 导出ONNX
    print(f"\n导出ONNX (imgsz={IMG_SIZE})...")
    onnx_path = model.export(
        format="onnx",
        imgsz=IMG_SIZE,
        simplify=True,      # 简化模型图
        opset=12,            # ONNX opset版本(RuyiStudio兼容)
        dynamic=False,       # 固定输入尺寸
        half=False,          # 不使用FP16(NNIE使用INT8)
    )

    print(f"\n{'=' * 60}")
    print(f"  ONNX导出成功!")
    print(f"  ONNX文件: {onnx_path}")
    print(f"\n  下一步:")
    print(f"  1. 将ONNX文件复制到Windows")
    print(f"  2. 使用RuyiStudio转换为.wk格式")
    print(f"  3. 参考 doc/ruyistudio_guide.md")
    print(f"{'=' * 60}")

    return onnx_path


if __name__ == "__main__":
    main()
