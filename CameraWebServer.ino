#include "esp_camera.h"
#include <WiFi.h>
#include <ESPAsyncWebSrv.h>  // 注意此处的修改
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <EasyButton.h>
#include <Arduino.h>

//
// WARNING!!! PSRAM IC required for UXGA resolution and high JPEG quality
//            Ensure ESP32 Wrover Module or other board with PSRAM is selected
//            Partial images will be transmitted if image exceeds buffer size
//
//            You must select partition scheme from the board menu that has at least 3MB APP space.
//            Face Recognition is DISABLED for ESP32 and ESP32-S2, because it takes up from 15 
//            seconds to process single frame. Face Detection is ENABLED if PSRAM is enabled as well

// ===================
// Select camera model
// ===================
//#define CAMERA_MODEL_WROVER_KIT // Has PSRAM
//#define CAMERA_MODEL_ESP_EYE // Has PSRAM
//#define CAMERA_MODEL_ESP32S3_EYE // Has PSRAM
//#define CAMERA_MODEL_M5STACK_PSRAM // Has PSRAM
//#define CAMERA_MODEL_M5STACK_V2_PSRAM // M5Camera version B Has PSRAM
//#define CAMERA_MODEL_M5STACK_WIDE // Has PSRAM
//#define CAMERA_MODEL_M5STACK_ESP32CAM // No PSRAM
//#define CAMERA_MODEL_M5STACK_UNITCAM // No PSRAM
#define CAMERA_MODEL_AI_THINKER // Has PSRAM
//#define CAMERA_MODEL_TTGO_T_JOURNAL // No PSRAM
//#define CAMERA_MODEL_XIAO_ESP32S3 // Has PSRAM
// ** Espressif Internal Boards **
//#define CAMERA_MODEL_ESP32_CAM_BOARD
//#define CAMERA_MODEL_ESP32S2_CAM_BOARD
//#define CAMERA_MODEL_ESP32S3_CAM_LCD
//#define CAMERA_MODEL_DFRobot_FireBeetle2_ESP32S3 // Has PSRAM
//#define CAMERA_MODEL_DFRobot_Romeo_ESP32S3 // Has PSRAM
#include "camera_pins.h"

//設定flash鍵
// Arduino pin where the button is connected to.
#define BUTTON_PIN 0
EasyButton button(BUTTON_PIN);

// EEPROM地址，用于存储SSID和密码
int eep_rom_use_length = 160; 
String ini_data;
String read_data = "";
String wifi_ssid = "";
String wifi_pass = "";
String dev_nam   = "";
bool   goAPMode  = false;

// 创建AsyncWebServer实例
AsyncWebServer server(80);

// HTML页面
const char *htmlContent = R"(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>WiFi Configuration</title>
  <style>
    body {
      font-family: Arial, sans-serif;
      margin: 20px;
    }

    label {
      display: block;
      margin-bottom: 10px;
    }

    input {
      width: 100%;
      padding: 8px;
      margin-bottom: 10px;
      box-sizing: border-box;
    }

    button {
      padding: 10px;
    }
  </style>
</head>
<body>
  <h1>WiFi Configuration</h1>
  <form action="/save" method="post">
    <label for="devnam">裝置名稱:</label>
    <input type="text" id="devnam" name="devnam" required>

    <label for="ssid">WiFi SSID:</label>
    <input type="text" id="ssid" name="ssid" required>

    <label for="password">WiFi Password:</label>
    <input type="password" id="password" name="password" required>

    <button type="submit">Save</button>
  </form>
</body>
</html>
)";

void startCameraServer();
void setupLedFlash(int pin);

void setup() {
  Serial.begin(115200);
  
  //設定flash動作
  // Initialize the button.
  button.begin();
  // Add the callback function to be called when the button is pressed.
  //button.onPressed(onPressed); 
  button.onSequence(3, 1500, sequenceEllapsed);
  if (button.supportsInterrupt())
  {
    button.enableInterrupt(buttonISR);
    Serial.println("Button will be used through interrupts");
  }

  // pinMode(LED_BUILTIN, OUTPUT);
  //讀取eep_rom中資料
  ini_data = readConfigFile();
  if (ini_data.length() > 0){
    wifi_ssid = getValFromJson(ini_data,"ssid");
    wifi_pass = getValFromJson(ini_data,"pwd");
    dev_nam   = getValFromJson(ini_data,"nam");
    if (wifi_ssid != "0"){
      Serial.println(wifi_ssid);
      Serial.println(wifi_pass);
      Serial.println(dev_nam);
      goAPMode = false;
    } else{
      goAPMode = true;  
    }
  } else{
    goAPMode = true;
  }
  //goAPMode = true; //for test
  if (goAPMode == false){
    WiFi.begin(wifi_ssid, wifi_pass);
    WiFi.setSleep(false);

    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
    Serial.println("");
    Serial.println("WiFi connected");
    }
  if (goAPMode == true){
    // 进入AP模式
    uint64_t chipid = ESP.getEfuseMac(); // The chip ID is essentially its MAC address(length: 6 bytes).
    uint16_t chip = (uint16_t)(chipid >> 32);
    WiFi.softAP(String("ESP32-") + String(chip)); 

    // 获取本地IP地址
    IPAddress IP = WiFi.softAPIP();
    Serial.println("AP Mode IP Address: " + IP.toString());

    // 设置路由
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send(200, "text/html", htmlContent);
    });

    server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request){
      // 获取通过POST请求发送的数据
      String ssid = request->arg("ssid");
      String password = request->arg("password");
      String devnam = request->arg("devnam");
      // 将SSID和密码保存到EEPROM
      if (ssid.length() > 0){
        writeWifiConfigFile(devnam,ssid,password);
        request->send(200, "text/plain", "WiFi credentials saved!");
      }
      
    });

    // 启动服务器
    server.begin();
  } else {
    Serial.setDebugOutput(true);
    Serial.println();

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
    config.pixel_format = PIXFORMAT_JPEG; // for streaming
    //config.pixel_format = PIXFORMAT_RGB565; // for face detection/recognition
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.jpeg_quality = 12;
    config.fb_count = 1;
    
    // if PSRAM IC present, init with UXGA resolution and higher JPEG quality
    //                      for larger pre-allocated frame buffer.
    if(config.pixel_format == PIXFORMAT_JPEG){
      if(psramFound()){
        config.jpeg_quality = 10;
        config.fb_count = 2;
        config.grab_mode = CAMERA_GRAB_LATEST;
      } else {
        // Limit the frame size when PSRAM is not available
        config.frame_size = FRAMESIZE_SVGA;
        config.fb_location = CAMERA_FB_IN_DRAM;
      }
    } else {
      // Best option for face detection/recognition
      config.frame_size = FRAMESIZE_240X240;
  #if CONFIG_IDF_TARGET_ESP32S3
      config.fb_count = 2;
  #endif
    }

  #if defined(CAMERA_MODEL_ESP_EYE)
    pinMode(13, INPUT_PULLUP);
    pinMode(14, INPUT_PULLUP);
  #endif

    // camera init
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
      Serial.printf("Camera init failed with error 0x%x", err);
      return;
    }

    sensor_t * s = esp_camera_sensor_get();
    // initial sensors are flipped vertically and colors are a bit saturated
    if (s->id.PID == OV3660_PID) {
      s->set_vflip(s, 1); // flip it back
      s->set_brightness(s, 1); // up the brightness just a bit
      s->set_saturation(s, -2); // lower the saturation
    }
    // drop down frame size for higher initial frame rate
    if(config.pixel_format == PIXFORMAT_JPEG){
      s->set_framesize(s, FRAMESIZE_QVGA);
    }

  #if defined(CAMERA_MODEL_M5STACK_WIDE) || defined(CAMERA_MODEL_M5STACK_ESP32CAM)
    s->set_vflip(s, 1);
    s->set_hmirror(s, 1);
  #endif

  #if defined(CAMERA_MODEL_ESP32S3_EYE)
    s->set_vflip(s, 1);
  #endif

  // Setup LED FLash if LED pin is defined in camera_pins.h
  #if defined(LED_GPIO_NUM)
    setupLedFlash(LED_GPIO_NUM);
  #endif

    

    startCameraServer();

    Serial.print("Camera Ready! Use 'http://");
    Serial.print(WiFi.localIP());
    Serial.println("' to connect"); 
  }
}

void loop() {
  // Do nothing. Everything is done in another task by the web server
  //delay(10000);
}

void writeWifiConfigFile(String nam,String ssid,String pwd){
  String str;
  String ini_data;
  int eepromAddress = 0;
  StaticJsonDocument<200> json_doc;
  char json_output[100];
  json_doc["nam"] = nam;
  json_doc["ssid"] = ssid;
  json_doc["pwd"]  = pwd;
  serializeJson(json_doc, json_output);
  
  //宣告使用EEPROM 160個位置
  EEPROM.begin(eep_rom_use_length);
 
  //清空EEPROM 0~160的位置
  Serial.println("clearing eeprom");
  for (int i = 0; i < eep_rom_use_length; ++i) { EEPROM.write(i, 0); }
 
  Serial.println("writing eeprom ssid:");
  for (int i = 0; i < 100; ++i)
    {
      EEPROM.write(i, json_output[i]);
      eepromAddress = i;
    }
  EEPROM.write(eepromAddress+1, '\0');  
  //一次寫入  
  EEPROM.commit();
  ini_data = readConfigFile();
  if (ini_data.length() > 0){
    wifi_ssid = getValFromJson(ini_data,"ssid");
    wifi_pass = getValFromJson(ini_data,"pwd");
    dev_nam   = getValFromJson(ini_data,"nam");
    Serial.println(wifi_ssid);
    Serial.println(wifi_pass);
    Serial.println(dev_nam);
  }
}

String getValFromJson(String json_str,String field){
  JsonDocument doc;
  DeserializationError json_error = deserializeJson(doc, json_str);

  if (!json_error) {
    //  const char*  json_data = doc[field];
     String s    = doc[field];
     return s;
  }
}

String readConfigFile(){
  String read_data;
 
  EEPROM.begin(eep_rom_use_length);
  Serial.println("Reading EEPROM");
  //讀取EEPROM 0~eep_rom_use_length的位置
  for (int i = 0; i < eep_rom_use_length; ++i)
    {
      if (char(EEPROM.read(i)) == '\0'){
        break;
      }
      read_data += char(EEPROM.read(i));
    }
  Serial.print("read EEPROM: ");
  Serial.println(read_data);   
  return read_data;
}
void onPressed() {
    Serial.println("Button has been pressed!");
}
//利用點三下flash按鈕重設wifi資訊
void sequenceEllapsed()
{
  Serial.println("reset wifi data");
  writeWifiConfigFile("0","0","0");
  ESP.restart();
}
void buttonISR()
{
  /*
    When button is being used through external interrupts, 
    parameter INTERRUPT must be passed to read() function
   */
  button.read();
}
