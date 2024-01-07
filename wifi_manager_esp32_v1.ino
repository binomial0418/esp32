#include <WiFi.h>
#include <ESPAsyncWebSrv.h>  // 注意此处的修改
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <EasyButton.h>
#include <Arduino.h>

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

void setup() {
  // 初始化串口
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
  }
  
}

void loop() {
  // 空循环
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
 
  //寫入EEPROM 0~31的位置
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
