#include <WiFi.h>
#include "esp_camera.h"
#include <ESPAsyncWebSrv.h>  // 注意此处的修改
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <EasyButton.h>


#define SERVER      "www.inskychen.com"  // 請改成你的網站伺服器位址或域名
#define UPLOAD_URL  "/esp32cam"
#define PORT        8099

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
WiFiClient client;

const int timerInterval = 1000;    // 上傳影像的間隔毫秒數
unsigned long previousMillis = 0;

bool initCamera() {
  // 設定攝像頭的接腳和影像格式與尺寸
  static camera_config_t camera_config = {
    .pin_pwdn       = 32,  // 斷電腳
    .pin_reset      = -1,  // 重置腳
    .pin_xclk       = 0,   // 外部時脈腳
    .pin_sscb_sda   = 26,  // I2C資料腳
    .pin_sscb_scl   = 27,  // I2C時脈腳
    .pin_d7         = 35,  // 資料腳
    .pin_d6         = 34,
    .pin_d5         = 39,
    .pin_d4         = 36,
    .pin_d3         = 21,
    .pin_d2         = 19,
    .pin_d1         = 18,
    .pin_d0         = 5,
    .pin_vsync      = 25,   // 垂直同步腳
    .pin_href       = 23,   // 水平同步腳
    .pin_pclk       = 22,   // 像素時脈腳
    .xclk_freq_hz   = 20000000,       // 設定外部時脈：20MHz
    .ledc_timer     = LEDC_TIMER_0,   // 指定產生XCLK時脈的計時器
    .ledc_channel   = LEDC_CHANNEL_0, // 指定產生XCLM時脈的通道
    .pixel_format   = PIXFORMAT_JPEG, // 設定影像格式：JPEG
    .frame_size     = FRAMESIZE_SVGA, // 設定影像大小：SVGA
    .jpeg_quality   = 10,  // 設定JPEG影像畫質，有效值介於0-63，數字越低畫質越高。
    .fb_count       = 1    // 影像緩衝記憶區數量
  };

  // 初始化攝像頭
  esp_err_t err = esp_camera_init(&camera_config);
  if (err != ESP_OK) {
    Serial.printf("攝像頭出錯了，錯誤碼：0x%x", err);
    return false;
  }

  return true;
}

void postImage() {
  camera_fb_t *fb = NULL;    // 宣告儲存影像結構資料的變數
  fb = esp_camera_fb_get();  // 拍照

  if(!fb) {
    Serial.println("無法取得影像資料…");
    delay(1000);
    ESP.restart();  // 重新啟動
  }

  Serial.printf("連接伺服器：%s\n", SERVER);

  if (client.connect(SERVER, PORT)) {
    Serial.println("開始上傳影像…");     

    String boundBegin = "--ESP32CAM\r\n";
    //boundBegin += "Content-Disposition: form-data; name=\"filename\"; filename=\"pict.jpg\"\r\n";
    boundBegin += "Content-Disposition: form-data; name=\"filename\"; filename=\"" + dev_nam + ".jpg\"\r\n";
    boundBegin += "Content-Type: image/jpeg\r\n";
    boundBegin += "\r\n";

    String boundEnd = "\r\n--ESP32CAM--\r\n";

    uint32_t imgSize = fb->len;  // 取得影像檔的大小
    uint32_t payloadSize = boundBegin.length() + imgSize + boundEnd.length();

    String httpMsg = String("POST ") + UPLOAD_URL + " HTTP/1.1\r\n";
    httpMsg += String("Host: ") + SERVER + "\r\n";
    httpMsg += "User-Agent: Arduino/ESP32CAM\r\n";
    httpMsg += "Content-Length: " + String(payloadSize) + "\r\n";
    httpMsg += "Content-Type: multipart/form-data; boundary=ESP32CAM\r\n";
    httpMsg += "\r\n";
    httpMsg += boundBegin;

    // 送出HTTP標頭訊息
    client.print(httpMsg.c_str());

    // 上傳檔案
    uint8_t *buf = fb->buf;

    for (uint32_t i=0; i<imgSize; i+=1024) {
      if (i+1024 < imgSize) {
        client.write(buf, 1024);
        buf += 1024;
      } else if (imgSize%1024>0) {
        uint32_t remainder = imgSize%1024;
        client.write(buf, remainder);
      }
    }

    client.print(boundEnd.c_str());

    esp_camera_fb_return(fb);

    // 等待伺服器的回應（10秒）
    long timout = 10000L + millis();

    while (timout > millis()) {
      Serial.print(".");
      delay(100);

      if (client.available()){
        // 讀取伺服器的回應
        Serial.println("\n伺服器回應：");
        String line = client.readStringUntil('\r');
        Serial.println(line);
        break;
      }
    }

    Serial.println("關閉連線");
  } else {
    Serial.printf("無法連接伺服器：%s\n", SERVER);
  }

  client.stop();  // 關閉用戶端
}
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
    bool cameraReady = initCamera();
    if (!cameraReady) {
      Serial.println("攝像頭出錯了…");
      delay(1000);
      ESP.restart();  // 重新啟動ESP32
    }
  
    postImage();   // 上傳影像  
  }

//------------------









  

  
}

void loop() {
  unsigned long currentMillis = millis();
  if (goAPMode == false){
    if (currentMillis - previousMillis >= timerInterval) {
      postImage();  // 上傳影像
      previousMillis = currentMillis;
    }  
  }
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
