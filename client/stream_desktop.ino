#pragma GCC push_options
#pragma GCC optimize ("O3") // O3 boosts fps by 20%

#define WIFI_SSID ""
#define WIFI_PASSWORD ""
#define HOST_IP ""
#define HOST_PORT 5451

#define BUFFER_SIZE 25000 // size of incoming jpeg buffer. can be smaller as each frame is less than 10kb at 50 jpeg quality @ 240x135
#define SENSOR_POLL_INTERVAL 100
#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 135
#define L_BUTTON_GPIO 0
#define R_BUTTON_GPIO 35

#include "JPEGDEC.h"
#include <TFT_eSPI.h>
#include <WiFi.h>

TFT_eSPI tft = TFT_eSPI();
WiFiClient client;
JPEGDEC jpeg;

uint8_t *buffer;
int bufferLength = 0;
const byte requestMessage[] = {0x55, 0x44, 0x55, 0x11}; // request message should probably be longer to avoid a false positive
unsigned long lastUpdate = 0;
volatile int updates = 0;
volatile int fps = 0;
bool LPressed = false;
bool RPressed = false;
volatile bool showFPS = false;
volatile int rotation = 3;
volatile int setRotation = rotation;
bool brightnessMode = false;
int brightness = 10;
//unsigned long lastRequestSent = 0;
bool ignoreButtonPress = false;
volatile bool readyToDraw = false;
TFT_eSprite fpsSprite = TFT_eSprite(&tft);
void *fpsPtr;
unsigned long lastSensorRead = 0;

struct JPEGData {
  uint16_t pPixels[20000]; // jpeg mcu's won't be this big but ram isn't the main constraint here
  int x;
  int y;
  int iWidth;
  int iHeight;
};

JPEGData *jpegBlock;

void setup() {
  buffer = (uint8_t*)malloc(BUFFER_SIZE * sizeof(uint8_t));
  jpegBlock = (JPEGData*)malloc(sizeof(JPEGData));
  fpsSprite.createSprite(24, 16);
  fpsPtr = fpsSprite.getPointer();
  
  Serial.begin(115200); // fps is reported over serial
  
  client = WiFi.begin(WIFI_SSID, WIFI_PASSWORD); // TODO: allow device to reconnect if connection is lost
    
  pinMode(L_BUTTON_GPIO, INPUT);
  pinMode(R_BUTTON_GPIO, INPUT);
  
  tft.init();
  tft.setRotation(rotation);
  tft.setTextSize(2);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_GREEN);
  tft.println("Connecting to Wi-Fi");
  tft.setTextColor(TFT_WHITE);
  tft.println(WIFI_SSID);

  fpsSprite.setTextSize(2);
  fpsSprite.setTextColor(TFT_GREEN);
  
  pinMode(TFT_BL, OUTPUT); // this pwm output controls display brightness
  ledcSetup(0, 5000, 8);
  ledcAttachPin(TFT_BL, 0);
  ledcWrite(0, brightness * 25);

  xTaskCreatePinnedToCore( // start second thread while waiting on wifi
    drawPixels,
    "drawPixels",
    5000,
    NULL,
    0,
    NULL,
    0);

  while (WiFi.status() != WL_CONNECTED) { 
    vTaskDelay(50);
  }
  
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_GREEN);
  tft.setCursor(0, 0);
  tft.println("Wi-Fi connected");

  if(client.connect(HOST_IP, HOST_PORT)) {
    tft.println("Connected to server");
  } else {
    tft.setTextColor(TFT_RED);
    tft.println("FAILED TO CONNECT");
    tft.println("TO SERVER");
  }
  
  renderFPS();
  
  lastUpdate = millis();
}

void loop() {
  int dataAvailible = client.available();

  if(dataAvailible > 0) {
    client.read((uint8_t*)buffer + bufferLength, dataAvailible);
    bufferLength += dataAvailible;

    if(bufferLength > 4 && buffer[bufferLength - 4] == 0x55 && buffer[bufferLength - 3] == 0x44 && buffer[bufferLength - 2] == 0x55 && buffer[bufferLength - 1] == 0x11) { // more elegent solution required. if multiple images end up in the buffer the jpeg will likely fail to decode
      int dataLength = bufferLength - 5;
      bufferLength = 0;

      client.write(requestMessage, 4); // queue up a new frame before decoding the current one

      if(jpeg.openRAM(buffer, dataLength, copyJpegBlock)) {
        jpeg.setPixelType(RGB565_BIG_ENDIAN);

        if(jpeg.decode(0, 0, 1)) {
          if(brightnessMode) {
            while(readyToDraw) { // prevent drawing brightness menu while jpeg mcu is being drawn to display
              taskYIELD();
            }
            
            tft.fillRect(0, 30, SCREEN_WIDTH, 15, TFT_BLACK); // TODO: handle brightness menu like the fps counter is handled to avoid flicker
            tft.setCursor(30, 30);
            tft.print("Brightness: ");
            tft.print(brightness);
          }
          
          updates += 1;
        } else {
          Serial.println("Could not decode jpeg");
        }
      } else {
        Serial.println("Could not open jpeg");
      }
    }
  } /*else {
    if(lastRequestSent > 0 && millis() + 10000 > lastRequestSent) {
      client.write(requestMessage, 4); // request new image after timeout
      lastRequestSent = millis();
    }
  }*/

  unsigned long time = millis();

  if(time - lastUpdate >= 1000) {
    float overtime = float(time - lastUpdate) / 1000.0;
    fps = floor((float)updates / overtime);
    
    lastUpdate = time;
    updates = 0;

    renderFPS();

    Serial.print("FPS: ");
    Serial.println(fps);
  }
}

void renderFPS() {
  fpsSprite.fillRect(0, 0, fpsSprite.width(), fpsSprite.height(), TFT_BLACK);
  fpsSprite.setCursor(0, 0);
  fpsSprite.print(fps);
}

void drawPixels(void* pvParameters) {
  for(;;) {
    if(readyToDraw) {
      if(showFPS && jpegBlock->x < 24 && jpegBlock->y < 16) {
        placeImageData(jpegBlock->pPixels, fpsPtr, 0, 0, (int)fpsSprite.width(), (int)fpsSprite.height(), jpegBlock->iWidth, jpegBlock->iHeight, min(jpegBlock->iWidth, (int)fpsSprite.width()));
      }

      tft.pushImage(jpegBlock->x, jpegBlock->y, jpegBlock->iWidth, jpegBlock->iHeight, jpegBlock->pPixels);
      
      readyToDraw = false;
    
      if(setRotation != rotation) {
        tft.setRotation(rotation);
        setRotation = rotation;
      }
    }

    unsigned long time = millis();
    
    if(time - lastSensorRead >= SENSOR_POLL_INTERVAL) {
      lastSensorRead = time;
      
      bool LStatus = digitalRead(L_BUTTON_GPIO) == LOW;
      bool RStatus = digitalRead(R_BUTTON_GPIO) == LOW;
      
      if((LStatus || LPressed) && (RStatus || RPressed) && !ignoreButtonPress) {
        ignoreButtonPress = true;
        brightnessMode = !brightnessMode;
        LPressed = false;
        RPressed = false;
        
        vTaskDelay(200);
        continue;
      }
      
      if(LStatus == false && LPressed == true && !ignoreButtonPress) {
        if(brightnessMode == true) {
          changeBrightness(1);
        } else {
          showFPS = !showFPS;
        }
      }
      
      if(RStatus == false && RPressed == true && !ignoreButtonPress) {
        if(brightnessMode == true) {
          changeBrightness(-1);
        } else {
          if(rotation == 3) {
            rotation = 1;
          } else {
            rotation = 3;
          }
        }
      }
      
      if(ignoreButtonPress && !LStatus && !RStatus && !LPressed && !RPressed) {
        ignoreButtonPress = false;
      }
      
      LPressed = LStatus;
      RPressed = RStatus;
    }
    
    //taskYIELD();
  }
}

void placeImageData(void* destination, void* source, int x, int y, int sourceWidth, int sourceHeight, int destinationWidth, int destinationHeight, int copyWidth) {
  for(int row = 0; row < sourceHeight; ++row) { 
    int index = ((y * SCREEN_WIDTH) + ((row * destinationWidth) + x)) * 2;
    memcpy(destination + index, source + (row * sourceWidth * 2), copyWidth * 2);
  }
}

int copyJpegBlock(JPEGDRAW *pDraw) {
  for(;;) {
    while(readyToDraw) {
      taskYIELD();
    }
    
    memcpy(jpegBlock->pPixels, pDraw->pPixels, pDraw->iWidth * pDraw->iHeight * sizeof(uint16_t));
    jpegBlock->x = pDraw->x;
    jpegBlock->y = pDraw->y;
    jpegBlock->iWidth = pDraw->iWidth;
    jpegBlock->iHeight = pDraw->iHeight;
    
    readyToDraw = true;

    //taskYIELD();
    return 1;
  }
}

void changeBrightness(int amount) {
  int newBrightness = brightness;
  newBrightness += amount;

  if(newBrightness > 10) {
    newBrightness = 10;
  } else if(newBrightness < 1) {
    newBrightness = 1;
  }

  brightness = newBrightness;
  ledcSetup(0, 5000, 8);
  ledcAttachPin(TFT_BL, 0);
  ledcWrite(0, brightness * 25);
}

#pragma GCC pop_options
