#include <Arduino.h>
#if defined(ESP32)
  #include <WiFi.h>
#endif 
#include <WiFiClientSecure.h>
//WIFI CONNECTION LIBRARIES
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"
#include <ArduinoJson.h>
//CAMERA LIBRARIES
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_camera.h"
#include <ESP32Servo.h>

//DATABASE INFORMATION
#define DATABASE_URL "https://smarttollsystem-default-rtdb.asia-southeast1.firebasedatabase.app/"
#define API_KEY "-"
//WIFI INFORMATION
#define WIFI_SSID "-"
#define WIFI_PASSWORD "-"

// Camera GPIO pins - adjust based on your ESP32-CAM board
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27
#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22
#define flashLight 4      // GPIO pin for the flashlight

// Setup proximity
const int trigPin = 13;
const int echoPin = 2;
#define SOUND_SPEED 0.034
#define CM_TO_INCH 0.393701
long duration;
float distanceCm;
float distanceInch;

WiFiClientSecure client; // Secure client for HTTPS communication

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

bool signupOK = false;
unsigned long sendDataPrevMillis = 0;

static const int servoPin = 14;

Servo servo1;

//ALPR API
String serverName = "www.circuitdigest.cloud";
String serverPath = "/readnumberplate";
const int serverPort = 443;
String apiKey = "-";




void setup() {
  Serial.begin(115200);
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  pinMode(flashLight, OUTPUT);
  digitalWrite(flashLight, LOW);  
  pinMode(trigPin, OUTPUT); // Sets the trigPin as an Output
  pinMode(echoPin, INPUT); // Sets the echoPin as an Input
  servo1.attach(servoPin);

  // Configure camera
  camera_config_t cam_config;
  cam_config.ledc_channel = LEDC_CHANNEL_0;
  cam_config.ledc_timer = LEDC_TIMER_0;
  cam_config.pin_d0 = Y2_GPIO_NUM;
  cam_config.pin_d1 = Y3_GPIO_NUM;
  cam_config.pin_d2 = Y4_GPIO_NUM;
  cam_config.pin_d3 = Y5_GPIO_NUM;
  cam_config.pin_d4 = Y6_GPIO_NUM;
  cam_config.pin_d5 = Y7_GPIO_NUM;
  cam_config.pin_d6 = Y8_GPIO_NUM;
  cam_config.pin_d7 = Y9_GPIO_NUM;
  cam_config.pin_xclk = XCLK_GPIO_NUM;
  cam_config.pin_pclk = PCLK_GPIO_NUM;
  cam_config.pin_vsync = VSYNC_GPIO_NUM;
  cam_config.pin_href = HREF_GPIO_NUM;
  cam_config.pin_sscb_sda = SIOD_GPIO_NUM;
  cam_config.pin_sscb_scl = SIOC_GPIO_NUM;
  cam_config.pin_pwdn = PWDN_GPIO_NUM;
  cam_config.pin_reset = RESET_GPIO_NUM;
  cam_config.xclk_freq_hz = 20000000;
  cam_config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    cam_config.frame_size = FRAMESIZE_SVGA;
    cam_config.jpeg_quality = 5;  // Lower number means higher quality (0-63)
    cam_config.fb_count = 2;
    Serial.println("PSRAM found");
  } else {
    cam_config.frame_size = FRAMESIZE_CIF;
    cam_config.jpeg_quality = 12;  // Lower number means higher quality (0-63)
    cam_config.fb_count = 1;
  }

  esp_err_t err = esp_camera_init(&cam_config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    delay(1000);
    ESP.restart();
  }



  //WIFI CONNECTION
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(300);
  }
  Serial.println();
  Serial.println("Wi-Fi connection Successful.");
  Serial.print("Connected with IP: ");
  Serial.println(WiFi.localIP());
  Serial.println();



  //FIREBASE CONNECTION
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  
  if (Firebase.signUp(&config, &auth, "", "")) {
    Serial.println("ok");
    Serial.println("Firebase connection Successful.");
    signupOK = true;
  }
  else {
    Serial.printf("%s\n", config.signer.signupError.message.c_str());
  }

  config.token_status_callback = tokenStatusCallback; //see addons/TokenHelper.h

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
}

// Function to extract a JSON string value by key
String extractJsonStringValue(const String& jsonString, const String& key) {
  int keyIndex = jsonString.indexOf(key);
  if (keyIndex == -1) {
    return "";
  }
  int startIndex = jsonString.indexOf(':', keyIndex) + 2;
  int endIndex = jsonString.indexOf('"', startIndex);
  if (startIndex == -1 || endIndex == -1) {
    return "";
  }
  return jsonString.substring(startIndex, endIndex);
}

int sendPhoto(String &plate) {
  camera_fb_t* fb = NULL;
  
  delay(100);
  fb = esp_camera_fb_get();
  delay(100);

  if (!fb) {
    Serial.println("Camera capture failed");
    Serial.println("Camera capture failed");
    return -1; 
  }

  // Display success message
  Serial.printf("Image Capture Success");
  delay(300);

  // Connect to server
  Serial.println("Connecting to server:" + serverName);
  client.setInsecure();  // Skip certificate validation for simplicity

  if (client.connect(serverName.c_str(), serverPort)) {
    Serial.println("Connection successful!");
    delay(300);

    // Increment count and prepare file name
    
    String filename = apiKey + ".jpeg";

    // Prepare HTTP POST request
    String head = "--CircuitDigest\r\nContent-Disposition: form-data; name=\"imageFile\"; filename=\"" + filename + "\"\r\nContent-Type: image/jpeg\r\n\r\n";
    String tail = "\r\n--CircuitDigest--\r\n";
    uint32_t imageLen = fb->len;
    uint32_t extraLen = head.length() + tail.length();
    uint32_t totalLen = imageLen + extraLen;

    client.println("POST " + serverPath + " HTTP/1.1");
    client.println("Host: " + serverName);
    client.println("Content-Length: " + String(totalLen));
    client.println("Content-Type: multipart/form-data; boundary=CircuitDigest");
    client.println("Authorization:" + apiKey);
    client.println();
    client.print(head);

    // Send image data in chunks
    uint8_t* fbBuf = fb->buf;
    size_t fbLen = fb->len;
    for (size_t n = 0; n < fbLen; n += 1024) {
      if (n + 1024 < fbLen) {
        client.write(fbBuf, 1024);
        fbBuf += 1024;
      } else {
        size_t remainder = fbLen % 1024;
        client.write(fbBuf, remainder);
      }
    }

    client.print(tail);

    // Clean up
    esp_camera_fb_return(fb);
    Serial.printf("Waiting For Response!");

    // Wait for server response
    String response;
    long startTime = millis();
    while (client.connected() && millis() - startTime < 5000) {
      if (client.available()) {
        char c = client.read();
        response += c;
      }
      yield();  // Let the system process background tasks and reset the watchdog timer.
    }

    // Extract and display NPR data from response
    String NPRData = extractJsonStringValue(response, "\"number_plate\"");
    String imageLink = extractJsonStringValue(response, "\"view_image\"");

    
    Serial.println("Number Plate Recognized: " + NPRData);
    
   plate = NPRData;

    client.stop();
    esp_camera_fb_return(fb);
    return 0;
  } else {
    Serial.println("Connection to server failed");
    esp_camera_fb_return(fb);
    Serial.println("Connection to server failed");
    return -2;
  }
}

void checkDatabase(String plateNumber){
  int intValue;
  if (Firebase.ready() && signupOK && (millis() - sendDataPrevMillis > 15000 || sendDataPrevMillis == 0)) {
    sendDataPrevMillis = millis();
    //Serial.print(String("Line 1: ") + "/vehicles/" + plateNumber + "/tollWallet");
    if (Firebase.RTDB.getInt(&fbdo, "/vehicles/" + plateNumber + "/tollWallet")) {
      //Serial.print(String("Line 2: ") + "/vehicles/" + plateNumber + "/tollWallet");
      if (fbdo.dataType() == "int") {
        intValue = fbdo.intData();
        Serial.println(intValue);
        Serial.print("Plate number Checked in database: " + plateNumber);

        Serial.print("\nToll Wallet: RM" + String(intValue));
        deductValue(intValue, plateNumber);
      }
    }
    else {
      Serial.println(fbdo.errorReason());
    }
  }
}

void deductValue(int wallet, String plateNumber){
  if(wallet >= 5){
    wallet -=5;
    tollPollOpen();
    if(Firebase.ready()){
      if (Firebase.RTDB.setInt(&fbdo, "vehicles/" + plateNumber + "/tollWallet", wallet)){
        Serial.println("PASSED");
        Serial.println("PATH: " + fbdo.dataPath());
        Serial.println("TYPE: " + fbdo.dataType());
      }else{
        Serial.println("FAILED");
        Serial.println("REASON: " + fbdo.errorReason());
      }
    }else{
      Serial.println("Firebase not ready for setting data.");
    }
  }else{
     Serial.println("Insufficient funds to pay toll. The toll balance is: RM" + wallet);
  }
}

void tollPollOpen(){
  float distance = 0;
  for(int posDegrees = 0; posDegrees <= 90; posDegrees++) {
    servo1.write(posDegrees);
    Serial.println("Raising!");
    delay(5);
  }
  delay (5000);
  do {
    distance = getDistance();
    Serial.print("Current distance: ");
    Serial.println(distance);
    delay(1000);
  } while (distance <= 15 && distance > 0); // Ensure distance > 0 to avoid sensor noise issue
  tollPollClose();
}

void tollPollClose(){
  for(int posDegrees = 90; posDegrees >= 0; posDegrees--) {
    servo1.write(posDegrees);
    Serial.println("Lowering!");
    delay(5);
  }
}

float getDistance(){
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  // Sets the trigPin on HIGH state for 10 micro seconds
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  duration = pulseIn(echoPin, HIGH);
  
  // Calculate the distance
  distanceCm = duration * SOUND_SPEED/2;

  return distanceCm;
}


void loop() {
  String plate = "";
  float distance = getDistance();
  if (distance <= 10 && distance != 0) {
    delay(1000);
    int status = sendPhoto(plate);
    Serial.print("The plate detected: " + plate);
    checkDatabase(plate);
    if (status == -1){
      Serial.printf("Image capture failed!");
    }else if (status == 0) {
      Serial.printf("Server connection succesfull!");
    } else{
      Serial.printf("Server connection failed!!");
    }
  }
  delay (1000);
}