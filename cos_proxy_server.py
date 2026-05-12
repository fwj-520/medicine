#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ESP32-CAM 腾讯云 COS 代理服务器
提供简单的 HTTP API 接口，用于接收 ESP32 发送的照片并上传到腾讯云 COS
"""

import os
import sys
import time
import logging
import socketserver
import http.server
import json
import io
from qcloud_cos import CosConfig
from qcloud_cos import CosS3Client

# 配置
PORT = 8000
UPLOAD_DIR = "temp_uploads"
LOG_LEVEL = logging.INFO

# 腾讯云 COS 配置（与 ESP32 代码一致）
COS_CONFIG = {
    'SecretId': '你的SecretId',
    'SecretKey': '你的SecretKey',
    'Region': 'ap-beijing',
    'Bucket': 'esp32-photo-1428250703',
    'Schema': 'https'
}

# 配置日志
logging.basicConfig(level=LOG_LEVEL,
                    format='%(asctime)s - %(levelname)s - %(message)s',
                    handlers=[
                        logging.StreamHandler()
                    ])
logger = logging.getLogger(__name__)

def create_cos_client():
    """创建 COS 客户端"""
    config = CosConfig(Region=COS_CONFIG['Region'],
                     SecretId=COS_CONFIG['SecretId'],
                     SecretKey=COS_CONFIG['SecretKey'])
    return CosS3Client(config)

class MyHTTPRequestHandler(http.server.BaseHTTPRequestHandler):
    """自定义 HTTP 请求处理器"""

    def _set_headers(self, status_code=200, content_type="application/json"):
        self.send_response(status_code)
        self.send_header("Content-Type", content_type)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "POST, GET, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()

    def do_OPTIONS(self):
        """处理 CORS 预检请求"""
        self._set_headers()

    def do_GET(self):
        """处理 GET 请求"""
        if self.path == "/":
            self._set_headers(content_type="text/html")
            html_content = """
            <html>
            <head>
                <title>ESP32-CAM 代理服务器</title>
            </head>
            <body>
                <h1>ESP32-CAM 腾讯云 COS 代理服务器</h1>
                <div style="background-color: #d1ecf1; color: #0c5460; padding: 10px; border-radius: 5px; margin: 10px 0;">
                    <p><strong>服务器状态:</strong> 运行中</p>
                    <p><strong>监听端口:</strong> 8000</p>
                    <p><strong>API 端点:</strong> <code>/upload</code> (POST 请求)</p>
                </div>

                <h3>测试上传</h3>
                <form id="uploadForm">
                    <div style="margin: 15px 0;">
                        <label style="display: block; margin-bottom: 5px; font-weight: bold;">选择图片文件:</label>
                        <input type="file" id="fileInput" name="file" accept="image/*" required style="width: 100%; padding: 8px; border: 1px solid #ddd; border-radius: 4px;">
                    </div>
                    <button type="submit" style="background-color: #007bff; color: white; padding: 10px 15px; border: none; border-radius: 4px; cursor: pointer;">上传到腾讯云 COS</button>
                </form>

                <div id="result"></div>

                <script>
                    document.getElementById('uploadForm').addEventListener('submit', function(e) {
                        e.preventDefault();
                        const fileInput = document.getElementById('fileInput');
                        const file = fileInput.files[0];

                        if (!file) {
                            alert('请选择一个图片文件');
                            return;
                        }

                        const xhr = new XMLHttpRequest();
                        xhr.open('POST', '/upload', true);
                        xhr.setRequestHeader('Content-Type', 'image/jpeg');

                        xhr.onload = function() {
                            const resultDiv = document.getElementById('result');
                            try {
                                const response = JSON.parse(xhr.responseText);
                                if (response.success) {
                                    resultDiv.innerHTML = `
                                        <div style="background-color: #d4edda; color: #155724; padding: 10px; border-radius: 5px; margin: 10px 0;">
                                            <h4>上传成功!</h4>
                                            <p><strong>照片 URL:</strong> <a href="${response.photo_url}" target="_blank">${response.photo_url}</a></p>
                                            <p><strong>文件大小:</strong> ${response.file_size} bytes</p>
                                            <p><strong>时间:</strong> ${new Date(response.timestamp * 1000).toLocaleString()}</p>
                                        </div>
                                        <img src="${response.photo_url}" style="max-width: 100%; margin-top: 10px;">
                                    `;
                                } else {
                                    resultDiv.innerHTML = '<div style="background-color: #f8d7da; color: #721c24; padding: 10px; border-radius: 5px; margin: 10px 0;">错误: ' + response.error + '</div>';
                                }
                            } catch (e) {
                                resultDiv.innerHTML = '<div style="background-color: #f8d7da; color: #721c24; padding: 10px; border-radius: 5px; margin: 10px 0;">响应解析错误</div>';
                            }
                        };

                        xhr.onerror = function() {
                            document.getElementById('result').innerHTML = '<div style="background-color: #f8d7da; color: #721c24; padding: 10px; border-radius: 5px; margin: 10px 0;">网络连接错误</div>';
                        };

                        const reader = new FileReader();
                        reader.onload = function(e) {
                            xhr.send(e.target.result);
                        };
                        reader.readAsArrayBuffer(file);
                    });
                </script>
            </body>
            </html>
            """
            html_response = html_content.encode('utf-8')
            logger.debug("发送响应内容，长度: {}".format(len(html_response)))
            logger.debug("响应内容前 200 字节: {}".format(html_response[:200]))

            self.wfile.write(html_response)
        else:
            self._set_headers(status_code=404)
            self.wfile.write(json.dumps({"success": False, "error": "路径未找到"}).encode())

    def do_POST(self):
        """处理 POST 请求"""
        if self.path == "/upload":
            logger.info("收到图片上传请求")

            # 获取请求头中的 Content-Type，应该是 image/jpeg
            content_type = self.headers.get('Content-Type')
            logger.debug("请求 Content-Type: {}".format(content_type))

            # 获取请求体大小
            content_length = int(self.headers.get('Content-Length', 0))
            logger.debug("请求体大小: {} 字节".format(content_length))

            try:
                # 读取请求体数据
                img_data = self.rfile.read(content_length)

                # 检查是否成功读取到数据
                if not img_data:
                    logger.error("无法读取请求体数据")
                    self._set_headers(status_code=400)
                    self.wfile.write(json.dumps({
                        "success": False,
                        "error": "请求体为空"
                    }).encode())
                    return

                logger.debug("成功读取到图片数据，大小: {} 字节".format(len(img_data)))

                # 上传到腾讯云 COS
                file_url = self.upload_to_cos(img_data)

                if file_url:
                    logger.info("图片上传到 COS 成功: {}".format(file_url))
                    self._set_headers()
                    self.wfile.write(json.dumps({
                        "success": True,
                        "photo_url": file_url,
                        "file_size": len(img_data),
                        "timestamp": int(time.time())
                    }).encode())
                else:
                    logger.error("图片上传到 COS 失败")
                    self._set_headers(status_code=500)
                    self.wfile.write(json.dumps({
                        "success": False,
                        "error": "上传到腾讯云 COS 失败"
                    }).encode())

            except Exception as e:
                logger.error("处理请求时发生错误: {}".format(e))
                self._set_headers(status_code=500)
                self.wfile.write(json.dumps({
                    "success": False,
                    "error": "服务器内部错误: {}".format(str(e))
                }).encode())
        else:
            self._set_headers(status_code=404)
            self.wfile.write(json.dumps({"success": False, "error": "路径未找到"}).encode())

    def upload_to_cos(self, image_data):
        """将图片数据上传到腾讯云 COS"""
        try:
            client = create_cos_client()

            # 生成文件名
            filename = "medicinebox_{}.jpg".format(int(time.time()))

            logger.debug("准备上传文件: {}".format(filename))

            # 上传到 COS
            response = client.put_object(
                Bucket=COS_CONFIG['Bucket'],
                Body=io.BytesIO(image_data),
                Key=filename,
                StorageClass='STANDARD',
                ContentType='image/jpeg'
            )

            logger.debug("COS 响应状态: {}".format(response.get('status')))

            # 生成访问 URL
            file_url = "{Schema}://{Bucket}.cos.{Region}.myqcloud.com/{Key}".format(
                Schema=COS_CONFIG['Schema'],
                Bucket=COS_CONFIG['Bucket'],
                Region=COS_CONFIG['Region'],
                Key=filename
            )

            return file_url

        except Exception as e:
            logger.error("上传到 COS 时出错: {}".format(e))
            return None

def main():
    """主函数"""
    logger.info("ESP32-CAM 腾讯云 COS 代理服务器启动")
    logger.info("-" * 50)

    # 确保临时文件夹存在
    if not os.path.exists(UPLOAD_DIR):
        os.makedirs(UPLOAD_DIR)
        logger.debug("创建临时文件夹: {}".format(UPLOAD_DIR))

    # 测试 COS 连接
    logger.info("测试腾讯云 COS 连接...")
    try:
        client = create_cos_client()
        buckets = client.list_buckets()
        logger.info("COS 连接成功")
        logger.debug("找到存储桶数量: {}".format(len(buckets['Buckets']['Bucket'])))
    except Exception as e:
        logger.error("COS 连接失败: {}".format(e))
        logger.error("请检查配置信息后重试")
        return False

    # 启动服务器
    logger.info("启动 HTTP 服务器...")
    try:
        with socketserver.TCPServer(("", PORT), MyHTTPRequestHandler) as httpd:
            logger.info("服务器启动成功")
            logger.info("服务器地址: http://localhost:{}".format(PORT))
            logger.info("API 接口: http://localhost:{}/upload".format(PORT))
            logger.info("按 Ctrl+C 停止服务器")

            httpd.serve_forever()

    except KeyboardInterrupt:
        logger.info("服务器已停止")
        return True

    except Exception as e:
        logger.error("服务器启动失败: {}".format(e))
        return False

if __name__ == "__main__":
    # 检查依赖
    dependencies = ['qcloud_cos']
    for dep in dependencies:
        try:
            __import__(dep)
        except ImportError:
            logger.error("缺少依赖库: {}".format(dep))
            logger.error("请运行: pip install {}".format(dep))
            sys.exit(1)

    success = main()
    sys.exit(0 if success else 1)
