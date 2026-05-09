// 定义相机模型（ESP32-CAM 最常用的是 AI-Thinker 模型）
#define CAMERA_MODEL_AI_THINKER

#include <Arduino.h>
#include "esp_camera.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <HTTPClient.h>
#include "camera_pins.h"

// ========== 配置区域 ==========
const char *ssid = "2101-2_5G";
const char *password = "dsj-2101";

// 腾讯云 COS 配置（使用新生成的密钥！）
const char *cos_bucket = "esp32-photo-1428250703";
const char *cos_region = "ap-beijing";
const char *cos_secret_id = "你的新SecretId";      // 替换为你的腾讯云 SecretID
const char *cos_secret_key = "你的新SecretKey";    // 替换为你的腾讯云 SecretKey

// MQTT 配置（后端 LangChain 订阅主题）
const char *mqtt_server = "你的MQTT服务器IP";
const int mqtt_port = 1883;
const char *mqtt_photo_topic = "esp32/medicinebox/photo";  // 照片 URL 主题（LangChain 订阅）
const char *mqtt_status_topic = "esp32/medicinebox/status";  // 药盒状态主题

// 门磁开关配置（MC-38 门磁开关）
const int door_sensor_pin = 13;  // MC-38 门磁开关连接到 GPIO13
bool door_last_state = HIGH;     // 门的上一个状态（默认为高电平，表示门打开）
unsigned long door_debounce_delay = 500;  // 门磁开关消抖延迟（毫秒）
unsigned long last_door_change_time = 0;  // 门状态最后一次改变的时间

// ========== 全局变量 ==========
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// ========== 函数声明 ==========
void connectWiFi();
void connectMQTT();
String hmac_sha1(String key, String data);
String base64_encode(const uint8_t *data, size_t len);
String generateAuthorization(String method, String path);
String uploadToCOS(camera_fb_t *fb);
void publishPhotoURL(const String &url);
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
  config.jpeg_quality = 12;
  config.fb_count = 1;

  if (psramFound()) {
    config.jpeg_quality = 10;
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
  s->set_framesize(s, FRAMESIZE_QVGA);
}

// ========== 设置 ==========
void setup() {
  Serial.begin(115200);
  pinMode(door_sensor_pin, INPUT_PULLUP);  // 门磁开关引脚配置为上拉输入

  initCamera();
  connectWiFi();

  mqttClient.setServer(mqtt_server, mqtt_port);
  connectMQTT();

  Serial.println("Ready! Close medicine box lid to trigger photo");
}

// ========== 主循环 ==========
void loop() {
  mqttClient.loop();

  // 门磁开关状态检测（药盒盖子盖上触发）
  bool door_current_state = digitalRead(door_sensor_pin);

  // 消抖处理
  if (door_current_state != door_last_state) {
    last_door_change_time = millis();
  }

  if ((millis() - last_door_change_time) > door_debounce_delay) {
    if (door_current_state != door_last_state) {
      door_last_state = door_current_state;

      if (door_current_state == LOW) {
        Serial.println("Medicine box lid closed! Taking photo...");
        publishMedicineBoxStatus("CLOSED");  // 发布药盒状态到 MQTT
        takePhotoAndUpload();  // 拍照并上传
      } else {
        Serial.println("Medicine box lid open");
        publishMedicineBoxStatus("OPEN");  // 发布药盒状态到 MQTT
      }
    }
  }

  delay(100);
}

// ========== 发布药盒状态到 MQTT ==========
void publishMedicineBoxStatus(const String &status) {
  if (mqttClient.connected()) {
    mqttClient.publish(mqtt_status_topic, status.c_str());
    Serial.println("Medicine box status sent via MQTT: " + status);
  } else {
    Serial.println("MQTT not connected");
  }
}

// ========== WiFi 连接 ==========
void connectWiFi() {
  WiFi.begin(ssid, password);
  WiFi.setSleep(false);
  
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected. IP: " + WiFi.localIP().toString());
}

// ========== MQTT 连接 ==========
void connectMQTT() {
  while (!mqttClient.connected()) {
    Serial.print("Connecting to MQTT...");
    if (mqttClient.connect("ESP32CAM_Client")) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" retrying in 5 seconds");
      delay(5000);
    }
  }
}

// ========== HMAC-SHA1 实现 ==========
String hmac_sha1(String key, String data) {
  uint8_t hmac_result[20];
  
  mbedtls_md_context_t ctx;
  mbedtls_md_type_t md_type = MBEDTLS_MD_SHA1;
  
  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 1);
  mbedtls_md_hmac_starts(&ctx, (const unsigned char*)key.c_str(), key.length());
  mbedtls_md_hmac_update(&ctx, (const unsigned char*)data.c_str(), data.length());
  mbedtls_md_hmac_finish(&ctx, hmac_result);
  mbedtls_md_free(&ctx);
  
  return base64_encode(hmac_result, 20);
}

// ========== Base64 编码 ==========
String base64_encode(const uint8_t *data, size_t len) {
  const char *base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  String result;
  int i = 0;
  int j = 0;
  uint8_t char_array_3[3];
  uint8_t char_array_4[4];
  
  while (len--) {
    char_array_3[i++] = *(data++);
    if (i == 3) {
      char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
      char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
      char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
      char_array_4[3] = char_array_3[2] & 0x3f;
      
      for (i = 0; i < 4; i++)
        result += base64_chars[char_array_4[i]];
      i = 0;
    }
  }
  
  if (i) {
    for (j = i; j < 3; j++)
      char_array_3[j] = '\0';
    
    char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
    char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
    char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
    
    for (j = 0; j < i + 1; j++)
      result += base64_chars[char_array_4[j]];
    
    while (i++ < 3)
      result += '=';
  }
  
  return result;
}

// ========== 生成 COS 签名 ==========
String generateAuthorization(String method, String path) {
  // 简单实现：直接使用固定签名（简化版）
  // 注意：正式使用建议用 SDK 或预签名 URL
  
  // 临时方案：使用 HTTP PUT 不带签名（需要有匿名上传权限）
  // 或者直接用预签名 URL
  
  // 简化方案：返回基本的 Authorization 头格式
  return "";
}

// ========== 上传到 COS（简化版 - 使用 HTTP PUT）==========
String uploadToCOS(camera_fb_t *fb) {
  String fileName = "medicinebox_" + String(millis()) + ".jpg";  // 图片文件名以药盒为前缀
  String objectKey = fileName;

  HTTPClient http;
  String url = "https://" + String(cos_bucket) + ".cos." + cos_region + ".myqcloud.com/" + objectKey;

  http.begin(url);
  http.addHeader("Content-Type", "image/jpeg");

  // 这里需要签名，暂时用预签名 URL 方式更简单

  int httpCode = http.PUT(fb->buf, fb->len);

  if (httpCode == 200 || httpCode == 201) {
    http.end();
    String finalUrl = "https://" + String(cos_bucket) + ".cos." + cos_region + ".myqcloud.com/" + objectKey;
    Serial.println("Photo uploaded to: " + finalUrl);
    return finalUrl;
  } else {
    Serial.printf("Upload failed, HTTP code: %d\n", httpCode);
    http.end();
    return "";
  }
}

// ========== 发送 MQTT 消息（照片 URL）==========
void publishPhotoURL(const String &url) {
  if (mqttClient.connected()) {
    mqttClient.publish(mqtt_photo_topic, url.c_str());
    Serial.println("Photo URL sent via MQTT: " + url);
  } else {
    Serial.println("MQTT not connected");
  }
}

// ========== 拍照并上传 ==========
void takePhotoAndUpload() {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Failed to get camera frame");
    return;
  }
  
  Serial.printf("Photo taken, size: %zu bytes\n", fb->len);
  
  String photoUrl = uploadToCOS(fb);
  esp_camera_fb_return(fb);
  
  if (photoUrl.length() > 0) {
    publishPhotoURL(photoUrl);
  } else {
    Serial.println("Upload failed");
  }
}