#!/usr/bin/env python3
import os
import sys
import time
from qcloud_cos import CosConfig
from qcloud_cos import CosS3Client

# Configuration
COS_CONFIG = {
    'SecretId': '你的SecretId',
    'SecretKey': '你的SecretKey',
    'Region': 'ap-beijing',
    'Bucket': 'esp32-photo-1428250703',
    'Schema': 'https'
}

def test_connection():
    try:
        config = CosConfig(Region=COS_CONFIG['Region'], SecretId=COS_CONFIG['SecretId'],
                         SecretKey=COS_CONFIG['SecretKey'])
        client = CosS3Client(config)

        print("Connection successful!")

        response = client.list_buckets()
        print("Buckets count:", len(response['Buckets']['Bucket']))

        return client
    except Exception as e:
        print(f"Connection failed: {e}")
        return None

def test_upload(client):
    try:
        filename = f"simple_test_{int(time.time())}.txt"
        content = "This is a test file for ESP32-CAM medicine box project"

        response = client.put_object(
            Bucket=COS_CONFIG['Bucket'],
            Body=content.encode(),
            Key=filename,
            StorageClass='STANDARD',
            ContentType='text/plain'
        )

        print(f"Upload successful! File: {filename}")
        print(f"Response: {response}")

        file_url = f"{COS_CONFIG['Schema']}://{COS_CONFIG['Bucket']}.cos.{COS_CONFIG['Region']}.myqcloud.com/{filename}"
        print(f"File URL: {file_url}")

        return file_url
    except Exception as e:
        print(f"Upload failed: {e}")
        return None

def main():
    print("=== ESP32-CAM COS Test ===")
    print("1. Testing connection...")

    client = test_connection()
    if client:
        print("\n2. Testing upload...")
        file_url = test_upload(client)

        if file_url:
            print("\n=== Success! ===")
            print("Test file created and uploaded to COS.")

if __name__ == "__main__":
    main()
