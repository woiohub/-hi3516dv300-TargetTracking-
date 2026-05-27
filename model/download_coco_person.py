#!/usr/bin/env python3
"""
COCO128 Person数据集下载与预处理脚本

功能:
1. 下载COCO128数据集(128张图片, ~7MB)
2. 过滤只保留person类别
3. 拆分为训练集(100张)和验证集(28张)
4. 生成data.yaml配置文件

使用方式:
    python3 download_coco_person.py

输出目录结构:
    model/dataset/
    ├── train/
    │   ├── images/    (训练图片)
    │   └── labels/    (YOLO格式标注)
    ├── val/
    │   ├── images/    (验证图片)
    │   └── labels/    (YOLO格式标注)
    └── data.yaml      (训练配置)
"""

import os
import zipfile
import urllib.request
import random
from pathlib import Path

# ============================================================
# 配置
# ============================================================

PROJECT_DIR = Path(__file__).parent
DATASET_DIR = PROJECT_DIR / "dataset"

# COCO128下载地址 (ultralytics官方, ~7MB)
COCO128_URL = "https://github.com/ultralytics/assets/releases/download/v0.0.0/coco128.zip"

# COCO person类别ID (YOLO格式中person=0)
PERSON_CLASS_ID = 0

# 训练/验证集拆分比例
TRAIN_RATIO = 0.78  # 约100张训练, 28张验证


def download_file(url, dest_path, max_retries=3):
    """下载文件，优先使用curl/wget，回退到urllib"""
    import subprocess
    import shutil
    import time

    if dest_path.exists():
        print(f"  文件已存在，跳过下载: {dest_path.name}")
        return

    print(f"  下载中: {url}")
    print(f"  保存到: {dest_path}")

    for attempt in range(1, max_retries + 1):
        try:
            # 优先使用curl
            if shutil.which("curl"):
                print(f"  使用curl下载...")
                subprocess.run(
                    ["curl", "-L", "-o", str(dest_path), "-#", "--retry", "3", url],
                    check=True,
                )
                print()
                return

            # 回退到wget
            if shutil.which("wget"):
                print(f"  使用wget下载...")
                subprocess.run(
                    ["wget", "-q", "--show-progress", "-O", str(dest_path), url],
                    check=True,
                )
                print()
                return

            # 最后回退到urllib
            print(f"  使用urllib下载...")
            import ssl
            ctx = ssl.create_default_context()
            ctx.check_hostname = False
            ctx.verify_mode = ssl.CERT_NONE
            https_handler = urllib.request.HTTPSHandler(context=ctx)
            opener = urllib.request.build_opener(https_handler)

            response = opener.open(url, timeout=120)
            total_size = int(response.headers.get('Content-Length', 0))
            block_size = 8192
            block_num = 0

            with open(dest_path, 'wb') as f:
                while True:
                    chunk = response.read(block_size)
                    if not chunk:
                        break
                    f.write(chunk)
                    block_num += 1
                    downloaded = block_num * block_size
                    if total_size > 0:
                        percent = min(100, downloaded * 100 / total_size)
                        mb = downloaded / (1024 * 1024)
                        mb_total = total_size / (1024 * 1024)
                        print(f"\r  进度: {percent:.1f}% ({mb:.1f}/{mb_total:.1f} MB)", end="", flush=True)

            print()
            return

        except Exception as e:
            print(f"\n  下载失败 (尝试 {attempt}/{max_retries}): {e}")
            if dest_path.exists():
                dest_path.unlink()
            if attempt < max_retries:
                wait = 5 * attempt
                print(f"  {wait}秒后重试...")
                time.sleep(wait)
            else:
                raise


def extract_zip(zip_path, extract_to):
    """解压zip文件"""
    if not zip_path.exists():
        print(f"  错误: 文件不存在 {zip_path}")
        return

    print(f"  解压中: {zip_path.name}")
    with zipfile.ZipFile(zip_path, 'r') as zf:
        zf.extractall(extract_to)
    print(f"  解压完成")


def filter_person_labels(src_labels_dir, dst_labels_dir):
    """
    过滤YOLO标注文件，只保留person类别(class_id=0)

    YOLO格式: class_id cx cy w h (归一化)
    """
    dst_labels_dir.mkdir(parents=True, exist_ok=True)
    count = 0
    total_boxes = 0

    for label_file in sorted(src_labels_dir.glob("*.txt")):
        person_lines = []
        with open(label_file, 'r') as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                parts = line.split()
                if int(parts[0]) == PERSON_CLASS_ID:
                    person_lines.append(line)
                    total_boxes += 1

        # 只保留包含person标注的文件
        if person_lines:
            dst_file = dst_labels_dir / label_file.name
            with open(dst_file, 'w') as f:
                f.write('\n'.join(person_lines))
            count += 1

    return count, total_boxes


def generate_data_yaml(dataset_dir):
    """生成YOLO训练配置文件"""
    yaml_content = f"""# COCO128 Person数据集配置
# 只包含person一个类别, 从COCO128子集中过滤

path: {dataset_dir.absolute()}
train: train/images
val: val/images

# 类别数
nc: 1

# 类别名称
names: ['person']
"""

    yaml_path = dataset_dir / "data.yaml"
    with open(yaml_path, 'w') as f:
        f.write(yaml_content)

    print(f"  配置文件已生成: {yaml_path}")
    return yaml_path


def main():
    print("=" * 60)
    print("  COCO128 Person数据集下载与预处理")
    print("=" * 60)

    # 创建目录
    download_dir = PROJECT_DIR / "downloads"
    download_dir.mkdir(exist_ok=True)
    DATASET_DIR.mkdir(exist_ok=True)

    # ---- 步骤1: 下载COCO128 ----
    print("\n[步骤1] 下载COCO128数据集 (~7MB)")

    zip_path = download_dir / "coco128.zip"
    download_file(COCO128_URL, zip_path)

    # ---- 步骤2: 解压 ----
    print("\n[步骤2] 解压数据集")

    coco128_dir = download_dir / "coco128"
    if not coco128_dir.exists():
        extract_zip(zip_path, download_dir)
    else:
        print(f"  已解压，跳过: {coco128_dir}")

    # 检查解压结果
    src_images = coco128_dir / "images" / "train2017"
    src_labels = coco128_dir / "labels" / "train2017"

    if not src_images.exists() or not src_labels.exists():
        print(f"  错误: 解压后目录结构异常")
        print(f"    图片目录: {src_images} (存在: {src_images.exists()})")
        print(f"    标注目录: {src_labels} (存在: {src_labels.exists()})")
        return

    all_images = sorted(src_images.glob("*.jpg"))
    print(f"  找到图片: {len(all_images)} 张")

    # ---- 步骤3: 过滤person类别 ----
    print("\n[步骤3] 过滤person类别标注")

    filtered_labels_dir = download_dir / "coco128_person_labels"
    img_count, box_count = filter_person_labels(src_labels, filtered_labels_dir)
    print(f"  包含person的图片: {img_count} 张")
    print(f"  person标注框数: {box_count} 个")

    # 收集包含person标注的图片
    person_images = []
    for label_file in sorted(filtered_labels_dir.glob("*.txt")):
        img_name = label_file.stem + ".jpg"
        img_path = src_images / img_name
        if img_path.exists():
            person_images.append(img_path)

    print(f"  有效图片: {len(person_images)} 张")

    if len(person_images) == 0:
        print("  错误: 没有找到包含person的图片")
        return

    # ---- 步骤4: 拆分训练集/验证集 ----
    print("\n[步骤4] 拆分训练集/验证集")

    random.seed(42)
    random.shuffle(person_images)

    split_idx = int(len(person_images) * TRAIN_RATIO)
    train_images = person_images[:split_idx]
    val_images = person_images[split_idx:]

    print(f"  训练集: {len(train_images)} 张")
    print(f"  验证集: {len(val_images)} 张")

    # 复制文件到目标目录
    for split_name, img_list in [("train", train_images), ("val", val_images)]:
        out_images = DATASET_DIR / split_name / "images"
        out_labels = DATASET_DIR / split_name / "labels"
        out_images.mkdir(parents=True, exist_ok=True)
        out_labels.mkdir(parents=True, exist_ok=True)

        for img_path in img_list:
            # 复制图片
            dst_img = out_images / img_path.name
            if not dst_img.exists():
                import shutil
                shutil.copy2(img_path, dst_img)

            # 复制标注
            label_name = img_path.stem + ".txt"
            src_label = filtered_labels_dir / label_name
            dst_label = out_labels / label_name
            if src_label.exists() and not dst_label.exists():
                import shutil
                shutil.copy2(src_label, dst_label)

        print(f"  {split_name}: 复制完成")

    # ---- 步骤5: 生成data.yaml ----
    print("\n[步骤5] 生成训练配置文件")
    generate_data_yaml(DATASET_DIR)

    # ---- 完成 ----
    print("\n" + "=" * 60)
    print("  数据集准备完成!")
    print(f"  数据集目录: {DATASET_DIR}")
    print(f"  训练集: {len(train_images)} 张")
    print(f"  验证集: {len(val_images)} 张")
    print(f"  配置文件: {DATASET_DIR / 'data.yaml'}")
    print("=" * 60)


if __name__ == "__main__":
    main()
