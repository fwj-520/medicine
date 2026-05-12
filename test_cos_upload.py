#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
腾讯云 COS 上传测试脚本
用于测试腾讯云 COS 配置是否正确
运行前请确保安装了 qcloud_cos 库：pip install -U cos-python-sdk-v5
"""

import os
import sys
import time
import logging
from qcloud_cos import CosConfig
from qcloud_cos import CosS3Client

# 配置日志
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')
logger = logging.getLogger(__name__)

# 腾讯云 COS 配置
COS_CONFIG = {
    'SecretId': '你的SecretId',
    'SecretKey': '你的SecretKey',
    'Region': 'ap-beijing',
    'Bucket': 'esp32-photo-1428250703',
    'Schema': 'https'
}

def create_cos_client():
    """创建 COS 客户端"""
    config = CosConfig(Region=COS_CONFIG['Region'], SecretId=COS_CONFIG['SecretId'],
                     SecretKey=COS_CONFIG['SecretKey'])
    return CosS3Client(config)

def test_cos_connection():
    """测试 COS 连接"""
    logger.info("测试腾讯云 COS 连接...")
    try:
        client = create_cos_client()
        response = client.list_buckets()

        logger.info("连接成功!")
        logger.info("当前账号的存储桶列表:")
        for bucket in response['Buckets']['Bucket']:
            logger.info(f"  - {bucket['Name']} ({bucket['Location']})")

        return True
    except Exception as e:
        logger.error(f"连接失败: {e}")
        logger.error("请检查 SecretId、SecretKey、Region 和网络连接")
        return False

def test_upload_file(client, file_path, remote_filename):
    """测试文件上传"""
    logger.info(f"测试上传文件: {file_path}")

    try:
        with open(file_path, 'rb') as f:
            response = client.put_object(
                Bucket=COS_CONFIG['Bucket'],
                Body=f,
                Key=remote_filename,
                StorageClass='STANDARD',
                ContentType='image/jpeg'
            )

        logger.info(f"上传成功!")
        logger.info(f"响应状态: {response['status']}")
        logger.info(f"文件大小: {response['ContentLength']} bytes")

        # 生成访问 URL
        file_url = f"{COS_CONFIG['Schema']}://{COS_CONFIG['Bucket']}.cos.{COS_CONFIG['Region']}.myqcloud.com/{remote_filename}"
        logger.info(f"文件访问 URL: {file_url}")

        return file_url
    except Exception as e:
        logger.error(f"上传失败: {e}")
        return None

def create_test_image():
    """创建一个简单的测试图片（使用 PIL）"""
    try:
        from PIL import Image, ImageDraw, ImageFont
        import io

        # 创建一个简单的测试图片
        img = Image.new('RGB', (320, 240), color='lightblue')
        draw = ImageDraw.Draw(img)

        # 添加测试文字
        try:
            font = ImageFont.truetype('arial.ttf', 20)
        except:
            font = ImageFont.load_default()

        draw.text((10, 10), "ESP32-CAM Test Image", fill='black', font=font)
        draw.text((10, 40), f"Timestamp: {time.time()}", fill='black', font=font)
        draw.text((10, 70), "This is a test image", fill='black', font=font)

        # 保存到内存
        buf = io.BytesIO()
        img.save(buf, format='JPEG')
        buf.seek(0)

        test_file = 'test_image.jpg'
        with open(test_file, 'wb') as f:
            f.write(buf.getvalue())

        logger.info(f"创建测试图片成功: {test_file}")
        return test_file
    except Exception as e:
        logger.error(f"无法创建测试图片: {e}")
        logger.error("请确保已安装 pillow 库: pip install pillow")
        return None

def main():
    logger.info("开始腾讯云 COS 上传测试")
    logger.info("=" * 50)

    # 测试连接
    if not test_cos_connection():
        logger.error("连接测试失败，无法继续")
        return False

    logger.info("=" * 50)

    # 创建测试文件
    test_file = create_test_image()
    if not test_file:
        logger.error("无法创建测试文件，无法继续")
        return False

    try:
        # 上传测试文件
        client = create_cos_client()
        remote_filename = f"test_{int(time.time())}.jpg"
        file_url = test_upload_file(client, test_file, remote_filename)

        if file_url:
            logger.info("=" * 50)
            logger.info("测试成功!")
            logger.info(f"文件已上传到: {file_url}")
            return True
        else:
            return False

    finally:
        # 清理测试文件
        if os.path.exists(test_file):
            try:
                os.remove(test_file)
                logger.info(f"删除临时文件: {test_file}")
            except:
                pass

if __name__ == "__main__":
    logger.info("腾讯云 COS 上传测试脚本")

    # 检查依赖
    dependencies = ['qcloud_cos', 'pillow']
    for dep in dependencies:
        try:
            __import__(dep)
        except ImportError:
            logger.error(f"缺少依赖库 {dep}")
            logger.error(f"请运行: pip install {dep}")
            sys.exit(1)

    try:
        success = main()
        sys.exit(0 if success else 1)
    except KeyboardInterrupt:
        logger.info("\n测试已停止")
        sys.exit(0)
