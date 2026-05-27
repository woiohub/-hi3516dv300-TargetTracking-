#!/usr/bin/env python3
"""
YOLOv8n 人体检测模型训练脚本

功能:
1. 使用ultralytics训练YOLOv8n模型
2. 在COCO128 person数据集上微调
3. 输出best.pt权重文件

使用方式:
    python3 train.py

前置条件:
    - 已运行 download_coco_person.py 准备数据集
    - 已安装 ultralytics 和 PyTorch CUDA
"""

from ultralytics import YOLO
from pathlib import Path

# ============================================================
# 训练配置
# ============================================================

PROJECT_DIR = Path(__file__).parent
DATASET_YAML = PROJECT_DIR / "dataset" / "data.yaml"
OUTPUT_DIR = PROJECT_DIR

# 模型配置
MODEL_NAME = "yolov8n.pt"       # nano模型, 3.2M参数, 最适合嵌入式
IMG_SIZE = 416                   # 与NNIE输入尺寸一致
BATCH_SIZE = 16                  # COCO128数据集较小, 使用小batch
EPOCHS = 30                      # 数据集小, 减少轮数避免过拟合
WORKERS = 4                      # 数据加载线程数
DEVICE = 0                       # GPU编号

# 优化器配置
OPTIMIZER = "AdamW"
LR0 = 0.01                       # 初始学习率
LRF = 0.01                       # 最终学习率 = LR0 * LRF
MOMENTUM = 0.937
WEIGHT_DECAY = 0.0005
WARMUP_EPOCHS = 3.0

# 数据增强
HSV_H = 0.015                    # 色调增强
HSV_S = 0.7                      # 饱和度增强
HSV_V = 0.4                      # 亮度增强
DEGREES = 0.0                    # 旋转角度
TRANSLATE = 0.1                  # 平移
SCALE = 0.5                      # 缩放
FLIPUD = 0.0                     # 上下翻转概率
Fliplr = 0.5                     # 左右翻转概率
MOSAIC = 0.5                     # 降低Mosaic概率, 小数据集避免过度增强


def main():
    print("=" * 60)
    print("  YOLOv8n 人体检测模型训练 (COCO128)")
    print("=" * 60)

    # 检查数据集
    if not DATASET_YAML.exists():
        print(f"错误: 数据集配置文件不存在: {DATASET_YAML}")
        print("请先运行 download_coco_person.py 准备数据集")
        return

    print(f"\n配置信息:")
    print(f"  模型: {MODEL_NAME}")
    print(f"  输入尺寸: {IMG_SIZE}")
    print(f"  Batch大小: {BATCH_SIZE}")
    print(f"  训练轮数: {EPOCHS}")
    print(f"  数据集: {DATASET_YAML}")
    print(f"  输出目录: {OUTPUT_DIR}")

    # 加载预训练模型
    print(f"\n加载预训练模型: {MODEL_NAME}")
    model = YOLO(MODEL_NAME)

    # 开始训练
    print(f"\n开始训练...")
    results = model.train(
        # 数据配置
        data=str(DATASET_YAML),

        # 训练参数
        imgsz=IMG_SIZE,
        epochs=EPOCHS,
        batch=BATCH_SIZE,
        workers=WORKERS,
        device=DEVICE,

        # 优化器
        optimizer=OPTIMIZER,
        lr0=LR0,
        lrf=LRF,
        momentum=MOMENTUM,
        weight_decay=WEIGHT_DECAY,
        warmup_epochs=WARMUP_EPOCHS,

        # 数据增强
        hsv_h=HSV_H,
        hsv_s=HSV_S,
        hsv_v=HSV_V,
        degrees=DEGREES,
        translate=TRANSLATE,
        scale=SCALE,
        flipud=FLIPUD,
        fliplr=Fliplr,
        mosaic=MOSAIC,

        # 输出
        project=str(OUTPUT_DIR),
        name="person_detect",
        exist_ok=True,

        # 其他
        pretrained=True,
        verbose=True,
        seed=42,
        deterministic=True,
    )

    # 输出训练结果
    best_pt = OUTPUT_DIR / "person_detect" / "weights" / "best.pt"
    last_pt = OUTPUT_DIR / "person_detect" / "weights" / "last.pt"

    print(f"\n{'=' * 60}")
    print(f"  训练完成!")
    print(f"  最佳权重: {best_pt}")
    print(f"  最终权重: {last_pt}")
    print(f"{'=' * 60}")

    return best_pt


if __name__ == "__main__":
    main()
