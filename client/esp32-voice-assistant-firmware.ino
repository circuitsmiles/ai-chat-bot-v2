#include <Arduino.h>
#include <driver/i2s.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiManager.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

// --- Configuration ---
// I2S connections for MAX98357A amplifier and INMP441 digital microphone
// Reference the project_pinout.md for connections
const int AMP_BCLK_PIN = 27; // Amplifier Bit Clock
const int AMP_LRC_PIN = 26;  // Amplifier Left/Right Clock
const int AMP_DIN_PIN = 25;  // Amplifier Data In

const int MIC_SD_PIN = 32;   // Microphone Serial Data
const int MIC_SCK_PIN = 14;  // Microphone Serial Clock (Bit Clock)
const int MIC_WS_PIN = 15;   // Microphone Word Select (Left/Right Clock)

// TFT Display pins (ST7789V)
// Reference the project_pinout.md for connections
#define TFT_CS  5
#define TFT_DC  2
#define TFT_RST 4
#define TFT_WIDTH 170 // Correct width for a 1.90" display
#define TFT_HEIGHT 320 // Correct height for a 1.90" display

// Button pins with internal pull-ups
const int WAKE_BUTTON_PIN = 12; // "Next" button for pre-written prompts
const int FUNCTION_BUTTON_PIN = 14; // "Previous" button for pre-written prompts
const int MIC_BUTTON_PIN = 13; // New button to trigger microphone recording

// LED pins
const int GREEN_LED_PIN = 16;
const int RED_LED_PIN = 17;

// --- Audio Configuration (Must match server settings) ---
const int SAMPLE_RATE = 16000;
const int CHUNK_SIZE = 4096;

// --- Server URL ---
// You will need to change this to your computer's IP address
const char* SERVER_URL = "http://192.168.2.10:5002/get_audio_response";

// --- Static Sentence Array ---
const char* sentences[] = {
 "temperature in amsterdam",
 "how are you",
 "your favorite color",
 "your favorite pokemon",
 "what is 2+2",
 "what is the capital of France",
 "what is your name",
 "what is the weather like today",
 "do you like dogs",
 "tell me a joke"
};
const int NUM_SENTENCES = 10;
int currentSentenceIndex = 0;

// --- State Machine ---
enum class AppState {
 INITIALIZING,
 CONNECTING_WIFI,
 READY,
 LISTENING,
 GETTING_RESPONSE, 
 PLAYING_AUDIO,
 ERROR
};
AppState currentState = AppState::INITIALIZING;
AppState previousState = AppState::INITIALIZING;

// --- Global Objects ---
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
HTTPClient http;
WiFiClient *audioStream = nullptr; // Global pointer to the audio stream

// --- Buffers for audio data ---
int32_t read_buffer[CHUNK_SIZE] __attribute__((aligned(4))); // 32-bit samples from INMP441
int16_t mono_buffer[CHUNK_SIZE / 2]; // 16-bit mono buffer for server

// --- Helper Functions ---
void setLEDs(bool red, bool green) {
 digitalWrite(RED_LED_PIN, red ? HIGH : LOW);
 digitalWrite(GREEN_LED_PIN, green ? HIGH : LOW);
}

void displayStatus(const char* message, uint16_t color = ST77XX_WHITE) {
 tft.fillScreen(ST77XX_BLACK);
 tft.setCursor(0, 0);
 tft.setTextSize(2);
 tft.setTextColor(color);
 tft.println(message);
}

void setupI2S() {
 i2s_config_t i2s_config = {
  .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX), // Now supports both TX and RX
  .sample_rate = SAMPLE_RATE,
  .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT, // 32-bit is standard for I2S mics
  .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT, // Stereo channel for I2S mic and mono for amp
  .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_STAND_I2S),
  .intr_alloc_flags = 0,
  .dma_buf_count = 8,
  .dma_buf_len = 64,
  .use_apll = false,
 };

 i2s_driver_install(I2S_NUM_0, &i2s_config, 0, nullptr);
 i2s_pin_config_t pin_config = {
  .bck_io_num = MIC_SCK_PIN,
  .ws_io_num = MIC_WS_PIN,
  .data_out_num = AMP_DIN_PIN,
  .data_in_num = MIC_SD_PIN 
 };
 i2s_set_pin(I2S_NUM_0, &pin_config);
}

// Function to handle the HTTP request and return the audio stream
WiFiClient* getAudioStream(const char* prompt) {
 // Display "Thinking..." and turn on red LED while waiting
 displayStatus("Thinking...", ST77XX_YELLOW);
 setLEDs(true, false);

 // Use HTTPClient to send the request
 http.begin(SERVER_URL);
 http.addHeader("Content-Type", "application/json");
 http.setTimeout(30000); 

 String jsonPayload = "{\"prompt\": \"" + String(prompt) + "\"}";
 Serial.print("Sending prompt: ");
 Serial.println(prompt);

 int httpResponseCode = http.POST(jsonPayload);
 
 if (httpResponseCode == HTTP_CODE_OK) {
  Serial.printf("[HTTP] POST success, code: %d\n", httpResponseCode);
  int payloadSize = http.getSize();
  Serial.printf("Payload size: %d\n", payloadSize);
  return http.getStreamPtr();
 } else {
  Serial.printf("[HTTP] POST failed, error: %s\n", http.errorToString(httpResponseCode).c_str());
  http.end();
  return nullptr;
 }
}

// --- Arduino Setup ---
void setup() {
 Serial.begin(115200);

 // Initialize LEDs
 pinMode(GREEN_LED_PIN, OUTPUT);
 pinMode(RED_LED_PIN, OUTPUT);
 setLEDs(false, false);

 // Initialize TFT
 SPI.begin();
 tft.init(TFT_WIDTH, TFT_HEIGHT);
 tft.setRotation(3); 
 tft.fillScreen(ST77XX_BLACK);

 // Initialize Buttons
 pinMode(WAKE_BUTTON_PIN, INPUT_PULLUP);
 pinMode(FUNCTION_BUTTON_PIN, INPUT_PULLUP);
  pinMode(MIC_BUTTON_PIN, INPUT_PULLUP);

 // State: Initializing
 displayStatus("Initializing...", ST77XX_WHITE);
 currentState = AppState::CONNECTING_WIFI;
}

// --- Arduino Main Loop ---
void loop() {
 if (currentState != previousState) {
  // Only update the display when the state changes
  switch (currentState) {
   case AppState::CONNECTING_WIFI:
    displayStatus("Connecting...", ST77XX_YELLOW);
    break;
   case AppState::READY:
    displayStatus("Ready\n\n<< Prev  Next >>\n\n", ST77XX_GREEN);
    tft.setCursor(0, 70);
    tft.setTextSize(2);
    tft.setTextColor(ST77XX_WHITE);
    tft.println(sentences[currentSentenceIndex]);
    setLEDs(false, true); // Green LED on
    break;
   case AppState::LISTENING:
    displayStatus("Listening...", ST77XX_RED);
    setLEDs(true, false);
    break;
   case AppState::GETTING_RESPONSE:
    // This state is now handled inside the loop()
    break;
   case AppState::PLAYING_AUDIO:
    displayStatus("Speaking...", ST77XX_CYAN); 
    break;
   case AppState::ERROR:
    displayStatus("Error!", ST77XX_RED);
    setLEDs(true, false);
    break;
   default:
    break;
  }
  previousState = currentState;
 }

 // Handle state transitions and actions in the main loop
 switch (currentState) {
  case AppState::CONNECTING_WIFI:
   {
    WiFiManager wifiManager;
    wifiManager.autoConnect("ESP32-Assistant");
    
    if (WiFi.status() == WL_CONNECTED) {
     Serial.println("\nWiFi connected.");
     Serial.print("IP Address: ");
     Serial.println(WiFi.localIP());
     setupI2S();
     currentState = AppState::READY;
    } else {
     delay(1000);
    }
   }
   break;
   
  case AppState::READY:
   if (digitalRead(WAKE_BUTTON_PIN) == LOW) { // Next button
    delay(200); 
    currentSentenceIndex = (currentSentenceIndex + 1) % NUM_SENTENCES;
    currentState = AppState::GETTING_RESPONSE;
   }
   
   if (digitalRead(FUNCTION_BUTTON_PIN) == LOW) { // Previous button
    delay(200); 
    currentSentenceIndex = (currentSentenceIndex - 1 + NUM_SENTENCES) % NUM_SENTENCES;
    currentState = AppState::GETTING_RESPONSE;
   }

   if (digitalRead(MIC_BUTTON_PIN) == LOW) {
    delay(200);
    // Prepare to record and send audio
    http.begin(SERVER_URL);
    http.addHeader("Content-Type", "application/octet-stream");
    http.setTimeout(10000);
    i2s_start(I2S_NUM_0);
    currentState = AppState::LISTENING;
   }
   break;

  case AppState::LISTENING: {
   if (digitalRead(MIC_BUTTON_PIN) == HIGH) {
    // Button released, stop streaming
    i2s_stop(I2S_NUM_0);
    http.end();
    currentState = AppState::GETTING_RESPONSE;
   } else {
    size_t bytes_read = 0;
    i2s_read(I2S_NUM_0, &read_buffer, sizeof(read_buffer), &bytes_read, portMAX_DELAY);
    if (bytes_read > 0) {
     // Convert 32-bit interleaved stereo to 16-bit mono
     int mono_index = 0;
     for (int i = 0; i < bytes_read / sizeof(int32_t); i += 2) {
      // The INMP441 often provides data on the left channel (first sample).
      // It's a good practice to use this or average both channels.
      mono_buffer[mono_index++] = (int16_t)(read_buffer[i] >> 16);
     }
     http.sendRequest("POST", (uint8_t*)mono_buffer, mono_index * sizeof(int16_t));
    }
   }
   break;
  }
  
  case AppState::GETTING_RESPONSE:
   // Initiate the request and get the stream
   // If coming from LISTENING, this will be skipped
   if (previousState != AppState::LISTENING) {
    audioStream = getAudioStream(sentences[currentSentenceIndex]);
   }
   
   if (audioStream != nullptr && audioStream->connected()) {
    // Transition to playing state only if the stream is successfully started
    currentState = AppState::PLAYING_AUDIO;
   } else {
    currentState = AppState::ERROR;
   }
   break;

  case AppState::PLAYING_AUDIO:
  {
   // Blinking red LED and solid green LED logic
   static unsigned long lastBlink = 0;
   const unsigned long blinkInterval = 500; 
   if (millis() - lastBlink > blinkInterval) {
    digitalWrite(RED_LED_PIN, !digitalRead(RED_LED_PIN)); 
    digitalWrite(GREEN_LED_PIN, HIGH); 
    lastBlink = millis();
   }

   // Check for available data and process it
   int read_bytes = 0;
   if (audioStream->available()) {
    uint8_t buffer[1024];
    read_bytes = audioStream->readBytes(buffer, sizeof(buffer));
    if (read_bytes > 0) {
     size_t bytes_written;
     i2s_write(I2S_NUM_0, buffer, read_bytes, &bytes_written, portMAX_DELAY);
    }
   }
   
   // Check if playback is finished.
   if (!audioStream->connected()) {
    Serial.println("Audio playback complete. Closing stream.");
    http.end();
    i2s_zero_dma_buffer(I2S_NUM_0);
    setLEDs(false, false);
    currentState = AppState::READY;
   }
   break;
  }
   
  case AppState::ERROR:
   delay(5000); 
   currentState = AppState::READY;
   break;
  
  case AppState::INITIALIZING:
   break;
 }
}
