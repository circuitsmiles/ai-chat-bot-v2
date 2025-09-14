#include <Arduino.h>
#include <driver/i2s.h>
#include "driver/adc.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiManager.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

// --- Configuration ---
// I2S connections for MAX98357A amplifier
const int BCLK_PIN = 27;
const int LRC_PIN = 26;
const int DIN_PIN = 25;

// TFT Display pins (ST7789V)
// Reference the project_pinout.md for connections
// The following pins are for hardware SPI
#define TFT_CS   5
#define TFT_DC   2
#define TFT_RST  4
#define TFT_WIDTH  170  // Correct width for a 1.90" display
#define TFT_HEIGHT 320  // Correct height for a 1.90" display

// Button pins with internal pull-ups
const int WAKE_BUTTON_PIN = 12; // Now the "Next" button
const int FUNCTION_BUTTON_PIN = 14; // Now the "Previous" button

// LED pins
const int GREEN_LED_PIN = 16;
const int RED_LED_PIN = 17;

// --- Audio Configuration (Must match server settings) ---
const int SAMPLE_RATE = 16000;

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
  GETTING_RESPONSE, 
  PLAYING_AUDIO, // This state is now managed within the loop()
  ERROR
};
AppState currentState = AppState::INITIALIZING;
AppState previousState = AppState::INITIALIZING;

// --- Global Objects ---
// Use the hardware SPI constructor with explicit pins
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
HTTPClient http;
WiFiClient *audioStream = nullptr; // Global pointer to the audio stream

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
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX), // Only TX (output) mode is needed now
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT, // Correctly set to MONO to match the server
    .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_STAND_I2S),
    .intr_alloc_flags = 0,
    .dma_buf_count = 8,
    .dma_buf_len = 64,
    .use_apll = false,
  };

  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, nullptr);
  i2s_pin_config_t pin_config = {
    .bck_io_num = BCLK_PIN,
    .ws_io_num = LRC_PIN,
    .data_out_num = DIN_PIN,
    .data_in_num = I2S_PIN_NO_CHANGE // Not needed for TX mode
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
  // This will begin the hardware SPI connection
  SPI.begin();
  tft.init(TFT_WIDTH, TFT_HEIGHT);
  tft.setRotation(3); // Adjust rotation as needed
  tft.fillScreen(ST77XX_BLACK);

  // Initialize Buttons
  pinMode(WAKE_BUTTON_PIN, INPUT_PULLUP);
  pinMode(FUNCTION_BUTTON_PIN, INPUT_PULLUP);

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
        displayStatus("Ready\n\n<< Prev   Next >>\n\n", ST77XX_GREEN);
        tft.setCursor(0, 70);
        tft.setTextSize(2);
        tft.setTextColor(ST77XX_WHITE);
        tft.println(sentences[currentSentenceIndex]);
        setLEDs(false, true); // Green LED on
        break;
      case AppState::GETTING_RESPONSE:
        // This state is now handled inside the loop()
        break;
      case AppState::PLAYING_AUDIO:
        displayStatus("Speaking...", ST77XX_CYAN); // This is the fix to show "Speaking"
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
        delay(200); // Debounce
        currentSentenceIndex = (currentSentenceIndex + 1) % NUM_SENTENCES;
        currentState = AppState::GETTING_RESPONSE;
      }
      
      if (digitalRead(FUNCTION_BUTTON_PIN) == LOW) { // Previous button
        delay(200); // Debounce
        currentSentenceIndex = (currentSentenceIndex - 1 + NUM_SENTENCES) % NUM_SENTENCES;
        currentState = AppState::GETTING_RESPONSE;
      }
      break;
    
    case AppState::GETTING_RESPONSE:
      // Initiate the request and get the stream
      audioStream = getAudioStream(sentences[currentSentenceIndex]);
      if (audioStream != nullptr) {
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
      const unsigned long blinkInterval = 500; // 0.5 seconds
      if (millis() - lastBlink > blinkInterval) {
        digitalWrite(RED_LED_PIN, !digitalRead(RED_LED_PIN)); // Toggle red LED
        digitalWrite(GREEN_LED_PIN, HIGH); // Green LED is solid
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
      
      // Check if playback is finished. This condition is now more robust.
      // The process is complete when the stream is no longer connected.
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
      delay(5000); // Stay in error state for 5 seconds
      currentState = AppState::READY;
      break;
    
    case AppState::INITIALIZING:
      // Handled in setup
      break;
  }
}
