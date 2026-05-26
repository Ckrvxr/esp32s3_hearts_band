#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// 震动引脚
#define VIBRATE_PIN 2

// BLE UUID（必须和小程序里的一模一样！）
#define SERVICE_UUID        "0000FFE0-0000-1000-8000-00805F9B34FB"
#define CHARACTERISTIC_UUID "0000FFE1-0000-1000-8000-00805F9B34FB"

BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;
bool deviceConnected = false;

class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    deviceConnected = true;
    Serial.println("✅ 小程序蓝牙已连接");
  }

  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
    Serial.println("❌ 蓝牙断开");
    pServer->startAdvertising();
  }
};

// 接收小程序指令
class MyCharacteristicCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) {
    std::string value = pCharacteristic->getValue();
    if (value.length() == 0) return;

    String cmd = String(value.c_str());
    Serial.println("收到指令：" + cmd);

    if (cmd == "LINK_OK") {
      Serial.println("✅ 握手成功");
    }
    if (cmd == "LOW_SPO2") {
      vibrate(300);
    }
    if (cmd == "ABNORMAL_HR") {
      vibrate(500);
    }
    if (cmd == "DRINK_REMIND") {
      vibrate(200);
    }
    if (cmd == "MEDICINE_REMIND") {
      vibrate(200);
    }
  }
};

void setup() {
  Serial.begin(115200);
  pinMode(VIBRATE_PIN, OUTPUT);
  digitalWrite(VIBRATE_PIN, LOW);

  // 创建蓝牙设备
  BLEDevice::init("ESP32_BLE_Hand"); // 蓝牙名字
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // 创建服务
  BLEService* pService = pServer->createService(SERVICE_UUID);

  // 创建特征（可读写 + 通知）
  pCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_WRITE |
    BLECharacteristic::PROPERTY_NOTIFY
  );

  pCharacteristic->addDescriptor(new BLE2902());
  pCharacteristic->setCallbacks(new MyCharacteristicCallbacks());

  // 启动服务
  pService->start();
  BLEDevice::startAdvertising();
  Serial.println("蓝牙已启动，等待小程序连接...");
}

void loop() {
  // 模拟心率血氧（和你原来完全一样）
  int spo2 = 97;
  int heart = 72;
  String data = "SPO2:" + String(spo2) + ",HR:" + String(heart);

  // 每隔2秒发送一次（蓝牙发送方式）
  static uint32_t t = 0;
  if (millis() - t > 2000 && deviceConnected) {
    t = millis();
    pCharacteristic->setValue(data.c_str());
    pCharacteristic->notify(); // 发送给小程序
    Serial.println("发送：" + data);
  }
}

// 震动函数（完全不变）
void vibrate(int ms) {
  digitalWrite(VIBRATE_PIN, HIGH);
  delay(ms);
  digitalWrite(VIBRATE_PIN, LOW);
}