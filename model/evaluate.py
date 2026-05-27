#!/usr/bin/env python3
"""
YOLOv8n 模型评估脚本

功能:
1. 在验证集上评估模型性能
2. 输出mAP@0.5、mAP@0.5:0.95等指标
3. 测试推理速度

使用方式:
    python3 evaluate.py
"""

from ultralytics import YOLO
from pathlib import Path

PROJECT_DIR = Path(__file__).parent
DATASET_YAML = PROJECT_DIR / "dataset" / "data.yaml"
WEIGHTS_DIR = PROJECT_DIR / "runs" / "detect" / "person_detect" / "weights"
# ultralytics默认保存路径: {project}/runs/detect/{name}/weights/
BEST_PT = WEIGHTS_DIR / "best.pt"
IMG_SIZE = 416


def main():
    print("=" * 60)
    print("  YOLOv8n 模型评估")
    print("=" * 60)

    # 检查文件
    if not BEST_PT.exists():
        print(f"错误: 权重文件不存在: {BEST_PT}")
        return
    if not DATASET_YAML.exists():
        print(f"错误: 数据集配置不存在: {DATASET_YAML}")
        return

    # 加载模型
    print(f"\n加载模型: {BEST_PT}")
    model = YOLO(str(BEST_PT))

    # 在验证集上评估
    print(f"\n在验证集上评估...")
    metrics = model.val(
        data=str(DATASET_YAML),
        imgsz=IMG_SIZE,
        batch=16,
        device=0,
        verbose=True,
    )

    # 输出结果
    print(f"\n{'=' * 60}")
    print(f"  评估结果:")
    print(f"  mAP@0.5:      {metrics.box.map50:.4f}")
    print(f"  mAP@0.5:0.95: {metrics.box.map:.4f}")
    print(f"  Precision:    {metrics.box.mp:.4f}")
    print(f"  Recall:       {metrics.box.mr:.4f}")
    print(f"{'=' * 60}")

    # 测试推理速度
    print(f"\n测试推理速度 (imgsz={IMG_SIZE})...")
    import torch
    import time

    model.to('cuda:0')
    dummy_input = torch.randn(1, 3, IMG_SIZE, IMG_SIZE).cuda()

    # 预热
    for _ in range(10):
        model.predict(dummy_input, verbose=False)

    # 测试
    times = []
    for _ in range(100):
        start = time.perf_counter()
        model.predict(dummy_input, verbose=False)
        torch.cuda.synchronize()
        times.append((time.perf_counter() - start) * 1000)

    avg_time = sum(times) / len(times)
    print(f"  平均推理时间: {avg_time:.1f} ms")
    print(f"  推理FPS: {1000/avg_time:.1f}")
    print(f"  (注: GPU推理速度，NNIE端速度可能不同)")


if __name__ == "__main__":
    main()
