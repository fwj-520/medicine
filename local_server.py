#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ESP32 局域网照片接收服务器
运行方式：python local_server.py
然后在浏览器访问：http://localhost:8000
"""

import http.server
import socketserver
import os
import datetime
import threading
from pathlib import Path

# 配置
PORT = 8000
UPLOAD_DIR = "photos"
HTML_TEMPLATE = """
<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32 局域网照片接收</title>
    <style>
        body {{
            font-family: Arial, sans-serif;
            max-width: 800px;
            margin: 0 auto;
            padding: 20px;
            background-color: #f5f5f5;
        }}
        .header {{
            text-align: center;
            margin-bottom: 30px;
            color: #333;
        }}
        .photo {{
            margin: 20px 0;
            padding: 15px;
            background: white;
            border-radius: 8px;
            box-shadow: 0 2px 5px rgba(0,0,0,0.1);
        }}
        .photo img {{
            max-width: 100%;
            height: auto;
            border-radius: 4px;
        }}
        .info {{
            color: #666;
            font-size: 14px;
            margin-top: 10px;
        }}
        .status {{
            background-color: #e7f3ff;
            border: 1px solid #b6d4fe;
            color: #084298;
            padding: 10px;
            margin: 20px 0;
            border-radius: 4px;
            font-family: monospace;
        }}
        .btn {{
            display: inline-block;
            padding: 8px 16px;
            background-color: #0d6efd;
            color: white;
            text-decoration: none;
            border-radius: 4px;
            margin-right: 10px;
        }}
        .btn:hover {{
            background-color: #0b5ed7;
        }}
        .refresh {{
            background-color: #198754;
        }}
        .refresh:hover {{
            background-color: #157347;
        }}
        .clear {{
            background-color: #dc3545;
        }}
        .clear:hover {{
            background-color: #bb2d3b;
        }}
        .upload-form {{
            background: white;
            padding: 20px;
            border-radius: 8px;
            margin-bottom: 30px;
        }}
    </style>
</head>
<body>
    <div class="header">
        <h1>ESP32 药盒项目</h1>
        <p>局域网照片接收服务器 - 门磁开关触发拍照</p>
    </div>

    <div class="status">
        <strong>服务器状态:</strong><br>
        运行中，等待 ESP32 发送照片...<br>
        服务器地址: {server_address}<br>
        已接收照片数: {photo_count} 张
    </div>

    <div class="upload-form">
        <h3>操作</h3>
        <a href="/" class="btn refresh">刷新页面</a>
        <a href="/clear" class="btn clear" onclick="return confirm('确定要删除所有照片吗？')">清空照片</a>
    </div>

    <h3>已接收的照片</h3>
    {photos_html}
</body>
</html>
"""


class MyHTTPRequestHandler(http.server.BaseHTTPRequestHandler):
    def _set_headers(self, status_code=200, content_type="text/html"):
        self.send_response(status_code)
        self.send_header("Content-Type", content_type)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()

    def do_POST(self):
        if self.path == "/upload":
            # 获取内容长度
            content_length = int(self.headers.get("Content-Length"))

            # 读取图片数据
            image_data = self.rfile.read(content_length)

            # 保存图片
            filename = datetime.datetime.now().strftime("medicinebox_%Y%m%d_%H%M%S.jpg")
            filepath = os.path.join(UPLOAD_DIR, filename)

            try:
                with open(filepath, "wb") as f:
                    f.write(image_data)
                print(f"[+] 照片已保存: {filename} ({content_length} bytes)")
            except Exception as e:
                print(f"[-] 保存照片失败: {e}")
                self._set_headers(500)
                self.wfile.write(b"Server Error")
                return

            # 发送成功响应
            self._set_headers(200, "text/plain")
            self.wfile.write(f"OK: photo saved as {filename}".encode())
        else:
            self._set_headers(404)
            self.wfile.write(b"Not Found")

    def do_GET(self):
        if self.path == "/":
            # 显示主页
            photo_files = self._get_photo_files()

            photos_html = ""
            for filename in sorted(photo_files, reverse=True):
                filepath = os.path.join(UPLOAD_DIR, filename)
                photos_html += f"""
                    <div class="photo">
                        <img src="/photos/{filename}" alt="{filename}">
                        <div class="info">
                            文件名: {filename}<br>
                            大小: {os.path.getsize(filepath) // 1024} KB
                        </div>
                    </div>
                """

            response = HTML_TEMPLATE.format(
                server_address=self.server_address,
                photo_count=len(photo_files),
                photos_html=photos_html
            )

            self._set_headers()
            self.wfile.write(response.encode("utf-8"))

        elif self.path.startswith("/photos/"):
            # 提供照片下载
            filename = self.path.split("/")[-1]
            filepath = os.path.join(UPLOAD_DIR, filename)

            if os.path.exists(filepath):
                self._set_headers(content_type="image/jpeg")
                with open(filepath, "rb") as f:
                    self.wfile.write(f.read())
            else:
                self._set_headers(404)
                self.wfile.write(b"Not Found")

        elif self.path == "/clear":
            # 清空照片
            self._clear_photos()
            self.send_response(303)  # See Other
            self.send_header("Location", "/")
            self.end_headers()

        else:
            self._set_headers(404)
            self.wfile.write(b"Not Found")

    def _get_photo_files(self):
        if not os.path.exists(UPLOAD_DIR):
            return []
        return [f for f in os.listdir(UPLOAD_DIR) if f.startswith("medicinebox_") and f.endswith(".jpg")]

    def _clear_photos(self):
        if os.path.exists(UPLOAD_DIR):
            for filename in self._get_photo_files():
                try:
                    os.remove(os.path.join(UPLOAD_DIR, filename))
                    print(f"[+] 删除照片: {filename}")
                except Exception as e:
                    print(f"[-] 删除照片失败: {e}")


def main():
    # 确保照片目录存在
    if not os.path.exists(UPLOAD_DIR):
        os.makedirs(UPLOAD_DIR)
        print(f"[+] 创建照片存储目录: {UPLOAD_DIR}")

    # 创建服务器
    with socketserver.TCPServer(("", PORT), MyHTTPRequestHandler) as httpd:
        server_ip = "0.0.0.0"  # 监听所有网络接口
        print(f"\n[+] 服务器启动成功！")
        print(f"[+] 服务器地址: http://localhost:{PORT}")
        print(f"[+] 照片保存目录: {os.path.abspath(UPLOAD_DIR)}")
        print(f"[+] ESP32 发送地址: /upload")
        print(f"\n[-] 按 Ctrl+C 停止服务器\n")

        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print(f"\n[+] 服务器已停止")


if __name__ == "__main__":
    main()
