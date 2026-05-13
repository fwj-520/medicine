// 定义相机模型（ESP32-CAM 最常用的是 AI-Thinker 模型）
#define CAMERA_MODEL_AI_THINKER

#include <Arduino.h>
#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <HTTPClient.h>
#include "camera_pins.h"

// ========== 配置区域 ==========
const char *ssid = "2101-2";
const char *password = "dsj-2101";

// 腾讯云 COS 配置（使用新生成的密钥！）
const char *cos_bucket = "esp32-photo-1428250703";
const char *cos_region = "ap-beijing";
const char *cos_secret_id = "你的SecretId";      // 替换为你的腾讯云 SecretID
const char *cos_secret_key = "你的SecretKey";    // 替换为你的腾讯云 SecretKey

// MQTT 配置（EMQX Serverless 版本）
const char *mqtt_server = "uf4b0611.ala.cn-hangzhou.emqxsl.cn";    // EMQX Serverless 地址
const int mqtt_port = 8883;                                        // MQTT over TLS/SSL 端口
const char *mqtt_username = "esp32cam";                        // EMQX 用户名（自定义）
const char *mqtt_password = "573210979q";                    // EMQX 密码（自定义）
const char *mqtt_photo_topic = "esp32/medicinebox/photo";  // 照片 URL 主题（LangChain 订阅）
const char *mqtt_status_topic = "esp32/medicinebox/status";  // 药盒状态主题

// 门磁开关配置（MC-38 门磁开关）
const int door_sensor_pin = 13;  // MC-38 门磁开关连接到 GPIO13
bool door_last_state = HIGH;     // 门的上一个状态（默认为高电平，表示门打开）
unsigned long door_debounce_delay = 500;  // 门磁开关消抖延迟（毫秒）
unsigned long last_door_change_time = 0;  // 门状态最后一次改变的时间

// ========== 全局变量 ==========
WiFiClientSecure wifiClient;  // 使用 TLS/SSL 客户端
PubSubClient mqttClient(wifiClient);

// ========== 函数声明 ==========
void connectWiFi();
void connectMQTT();
void reconnectMQTT();
void uploadToProxyServer(camera_fb_t *fb);
void takePhotoAndUpload();

// ========== 摄像头初始化（保留你原来的代码）==========
void initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.frame_size = FRAMESIZE_UXGA;
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 8;  // 提高 JPEG 质量（范围 0-63，0 最高质量）
  config.fb_count = 1;

  if (psramFound()) {
    config.jpeg_quality = 6;  // PSRAM 存在时使用更高质量
    config.fb_count = 2;
    config.grab_mode = CAMERA_GRAB_LATEST;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.fb_location = CAMERA_FB_IN_DRAM;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  sensor_t *s = esp_camera_sensor_get();
  if (s->id.PID == OV2640_PID) {
    s->set_vflip(s, 1);
    s->set_brightness(s, 1);
    s->set_saturation(s, -2);
  }
  // 提高照片质量，使用高分辨率
  s->set_framesize(s, FRAMESIZE_UXGA);  // UXGA (1600x1200) 分辨率
}

// ========== 设置 ==========
void setup() {
  Serial.begin(115200);
  delay(1000); // 启动延迟
  Serial.println("ESP32-CAM Medicine Box Project");

  // 门磁开关引脚配置（使用内部上拉输入模式）
  pinMode(door_sensor_pin, INPUT_PULLUP);  // 使用内部上拉电阻，提高检测稳定性

  // 检测门磁开关的初始状态
  bool initial_state = digitalRead(door_sensor_pin);
  Serial.print("Door sensor initial state: ");
  Serial.print(initial_state);
  Serial.println(initial_state ? " (HIGH)" : " (LOW)");
  door_last_state = initial_state;

  Serial.print("Initializing camera...");
  initCamera();
  Serial.println("OK");

  Serial.print("Connecting to WiFi...");
  connectWiFi();

  Serial.print("Connecting to MQTT...");
  mqttClient.setServer(mqtt_server, mqtt_port);
  connectMQTT();

  Serial.println("\nReady! Close medicine box lid to trigger photo");
  Serial.println("---");
  Serial.println("Debug info:");
  Serial.println("- Check if sensor state changes when door is opened/closed");
  Serial.println("- If always HIGH, check wiring or sensor");
  Serial.println("---");
}

// ========== 主循环 ==========
void loop() {
  mqttClient.loop();

  // 检测 MQTT 连接状态，如需重连
  if (!mqttClient.connected()) {
    reconnectMQTT();
    delay(1000);
  }

  // 门磁开关状态检测（药盒盖子盖上触发）
  bool door_current_state = digitalRead(door_sensor_pin);

  // 持续打印门磁开关的原始状态（用于调试）
  static unsigned long last_print_time = 0;
  if (millis() - last_print_time > 500) {
    last_print_time = millis();
    Serial.print("Door sensor raw state: ");
    Serial.print(door_current_state);
    Serial.println(door_current_state ? " (HIGH)" : " (LOW)");
  }

  // 消抖处理
  if (door_current_state != door_last_state) {
    last_door_change_time = millis();
  }

  if ((millis() - last_door_change_time) > door_debounce_delay) {
    if (door_current_state != door_last_state) {
      door_last_state = door_current_state;
      Serial.println("--- Door state confirmed (debounced) ---");
      Serial.print("New state: ");
      Serial.println(door_current_state ? "HIGH (open)" : "LOW (closed)");

      // 门磁开关逻辑：门打开时引脚为 HIGH，门关闭时引脚为 LOW（使用内部上拉）
      if (door_current_state == HIGH) {
        Serial.println("Medicine box lid open");
        // 发送状态到 MQTT
        String statusMessage = "{\"status\": \"door_open\", \"timestamp\": " + String(millis()) + "}";
        mqttClient.publish(mqtt_status_topic, statusMessage.c_str());
      } else {
        Serial.println("Medicine box lid closed! Taking photo...");
        Serial.flush(); // 确保所有调试信息都发送到串口
        takePhotoAndUpload();  // 拍照并上传
      }
    }
  }

  delay(100);
}


// ========== WiFi 连接 ==========
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  WiFi.setSleep(false);

  Serial.print("Connecting to ");
  Serial.print(ssid);
  int retryCount = 0;
  int maxRetries = 30; // 30秒超时

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
    retryCount++;

    if (retryCount > maxRetries) {
      Serial.println("\nWiFi connection failed!");
      Serial.println("Check WiFi SSID and password configuration");
      Serial.println("SSID: " + String(ssid));
      Serial.println("Password: " + String(password));

      // 重启设备
      Serial.println("Rebooting in 5 seconds...");
      delay(5000);
      ESP.restart();
    }
  }

  Serial.println("\nWiFi connected!");
  Serial.println("IP Address: " + WiFi.localIP().toString());
  Serial.println("Signal Strength: " + String(WiFi.RSSI()) + " dBm");
}

// ========== MQTT 连接 ==========
void connectMQTT() {
  // 禁用 TLS 证书验证（对于 Serverless 版本，由于证书链复杂，可能需要禁用验证）
  wifiClient.setInsecure();

  int retryCount = 0;
  int maxRetries = 3;

  while (!mqttClient.connected() && retryCount < maxRetries) {
    Serial.print("Connecting to MQTT...");
    if (mqttClient.connect("ESP32CAM_Client", mqtt_username, mqtt_password)) {
      Serial.println("connected");
      // 连接成功后，发送在线状态
      String statusMessage = "{\"status\": \"online\", \"timestamp\": " + String(millis()) + "}";
      mqttClient.publish(mqtt_status_topic, statusMessage.c_str());
      return;
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" retrying in 5 seconds");
      delay(5000);
      retryCount++;
    }
  }

  if (retryCount >= maxRetries) {
    Serial.println("MQTT connection failed after multiple attempts");
    Serial.println("Continuing without MQTT support");
  }
}


// ========== 上传到代理服务器函数 ==========
void uploadToProxyServer(camera_fb_t *fb) {
  Serial.println("=== uploadToProxyServer() function started ===");

  // 代理服务器配置（您电脑的 IP 地址，在同一 WiFi 下）
  const char* proxy_server_ip = "192.168.3.181";  // 需要替换为您电脑的实际 IP
  const int proxy_server_port = 8000;

  WiFiClient client;

  // 构建代理服务器 URL
  String url = String("http://") + String(proxy_server_ip) + ":" + String(proxy_server_port) + "/upload";

  Serial.print("Connecting to proxy server: ");
  Serial.println(url);

  HTTPClient http;

  if (http.begin(client, url)) {
    http.addHeader("Content-Type", "image/jpeg");

    // 发送 POST 请求上传文件
    int httpCode = http.sendRequest("POST", (uint8_t*)fb->buf, fb->len);

    if (httpCode > 0) {
      Serial.printf("Proxy server response code: %d\n", httpCode);

      if (httpCode == HTTP_CODE_OK) {
        String response = http.getString();
        Serial.println("Proxy server response: " + response);

        // 简化解析逻辑，直接查找完整的 photo_url
        int urlStart = response.indexOf("https://");
        if (urlStart > 0) {
          int urlEnd = response.indexOf("\"", urlStart);
          if (urlEnd > urlStart) {
            String photoUrl = response.substring(urlStart, urlEnd);
            Serial.print("Photo URL from proxy: ");
            Serial.println(photoUrl);

            // 发送照片 URL 到 MQTT
            String message = "{\"event\": \"medicine_box_closed\", \"photo_url\": \"" + photoUrl + "\", \"timestamp\": " + String(millis()) + "}";
            if (mqttClient.publish(mqtt_photo_topic, message.c_str())) {
              Serial.println("MQTT message published successfully!");
            } else {
              Serial.println("ERROR: Failed to publish MQTT message");
            }

            // 发送状态到 MQTT
            String statusMessage = "{\"status\": \"photo_taken\", \"photo_url\": \"" + photoUrl + "\", \"timestamp\": " + String(millis()) + "}";
            mqttClient.publish(mqtt_status_topic, statusMessage.c_str());
          } else {
            Serial.println("ERROR: Failed to find end of URL");
          }
        } else {
          Serial.println("ERROR: Failed to find https:// in response");
        }
      } else {
        String response = http.getString();
        Serial.print("Proxy server failed: ");
        Serial.println(response);
      }
    } else {
      Serial.printf("ERROR: Failed to connect to proxy server, error: %s\n", http.errorToString(httpCode).c_str());
    }

    http.end();
  } else {
    Serial.println("ERROR: Failed to initialize HTTP connection");
  }

  Serial.println("=== uploadToProxyServer() function completed ===");
}

// ========== 拍照并上传到腾讯云 COS ==========
void takePhotoAndUpload() {
  Serial.println("=== takePhotoAndUpload() function started ===");

  // 拍照
  Serial.println("Step 1: Taking photo...");
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("ERROR: Failed to get camera frame");
    Serial.println("=== takePhotoAndUpload() function ended ===");
    return;
  }

  Serial.printf("Step 1 completed: Photo taken, size: %zu bytes\n", fb->len);

  // 上传到代理服务器
  uploadToProxyServer(fb);

  // 释放帧缓存
  Serial.println("Step 2: Releasing camera frame buffer...");
  esp_camera_fb_return(fb);

  Serial.println("=== takePhotoAndUpload() function completed ===");
}

// ========== MQTT 重连函数 ==========
void reconnectMQTT() {
  if (!mqttClient.connected()) {
    Serial.print("Reconnecting to MQTT...");
    if (mqttClient.connect("ESP32CAM_Client", mqtt_username, mqtt_password)) {
      Serial.println("connected");
      // 连接成功后，发送在线状态
      String statusMessage = "{\"status\": \"online\", \"timestamp\": " + String(millis()) + "}";
      mqttClient.publish(mqtt_status_topic, statusMessage.c_str());
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" will retry later");
    }
  }
}