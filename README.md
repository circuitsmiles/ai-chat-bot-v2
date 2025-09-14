# **ESP32 Voice Assistant**

This is a comprehensive guide to building a voice assistant using an ESP32 microcontroller, a TFT display, and a Python server for AI integration. The project allows you to speak to the device or select from a list of predefined phrases to generate a spoken response.

### **Features**

* **Real-time Voice Interaction:** The ESP32 can capture audio input and stream it to a Python server for processing.  
* **Offline Operation:** The project can operate without a microphone by using "Next" and "Previous" buttons to select from a list of pre-written prompts. This allows for quick, repeatable responses and functionality in noisy environments.  
* **Gemini API Integration:** The Python server uses the Gemini API to transcribe your voice and generate a text response, or to convert a predefined text prompt into a spoken response.  
* **Text-to-Speech (TTS):** The server uses the gTTS library to convert the Gemini text response into an audio stream.  
* **Visual Feedback:** A color TFT display shows the device's current status (e.g., "Thinking...", "Speaking...", "Ready").  
* **Status LEDs:** LEDs provide visual cues for various states.

### **Hardware Components**

Here is a list of all the hardware components required for this project.

* **Microcontroller:** ESP32 Dev Kit C  
* **Display:** 1.90" TFT Display (ST7789V)  
* **Audio Input (Optional):** MAX9814 Electret Microphone Amplifier  
* **Audio Output:** MAX98357A I2S Class-D Amplifier connected to an 8-ohm speaker  
* **User Input:** Two tactile buttons for "Next" and "Previous" functions.  
* **Visual Cues:** One red LED and one green LED  
* **Cables and Wires:** Breadboard, jumper wires, and a 3A USB charger

### **Pinout Guide**

This guide details the specific pin connections for all components in your voice assistant project. All GND pins on the components should be connected to a common ground rail.

| Component | Pin Name | ESP32 GPIO Pin |
| :---- | :---- | :---- |
| **TFT Display (ST7789)** | CS | 5 |
|  | DC | 2 |
|  | RST | 4 |
|  | MOSI | 23 |
|  | SCLK | 18 |
| **I2S Amplifier (MAX98357A)** | DIN | 25 |
|  | BCLK | 27 |
|  | LRC | 26 |
| **I2S Microphone (MAX9814)** | OUT | 35 |
| **Button 1 (Next)** | \- | 12 |
| **Button 2 (Previous)** | \- | 14 |
| **LED (Red)** | \- | 17 |
| **LED (Green)** | \- | 16 |

### **System Overview**

The system operates in a series of states managed by the ESP32 firmware:

1. **Initialization:** The ESP32 starts, connects to your Wi-Fi network, and enters the READY state. The screen displays "Ready" and the green LED is solid.  
2. **User Input (Button Mode):**  
   * Pressing the **"Next" button** cycles to the next static sentence.  
   * Pressing the **"Previous" button** cycles to the previous static sentence.  
3. **User Input (Microphone Mode):**  
   * When the user presses the microphone button, the ESP32 enters the LISTENING state. It captures audio from the microphone via the I2S interface and continuously streams it to a Python server.  
   * When the button is released, the ESP32 sends a signal to the server to process the recorded audio.  
4. **Processing:** The screen displays "Thinking..." and the red LED is solid while the server processes the request.  
5. **Audio Playback:** Once the server has returned a response, the ESP32 enters the PLAYING\_AUDIO state. The screen changes to "Speaking...", the green LED is solid, and the red LED blinks to indicate that the audio is playing.  
6. **Completion:** After the audio stream is finished, the ESP32 returns to the READY state, and the LEDs and screen revert to their initial state.

### **Usage Guide**

Follow these steps to set up and run the voice assistant.

#### **1\. Firmware Setup (Arduino IDE)**

1. **Install the Arduino IDE:** Download and install the Arduino IDE if you don't have it already.  
2. **Add the ESP32 Board:** Go to File \> Preferences and add the ESP32 board manager URL: https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package\_esp32\_index.json  
3. **Install the Board:** Go to Tools \> Board \> Boards Manager, search for "esp32", and install the esp32 package.  
4. **Install Libraries:**  
   * Adafruit GFX Library: Go to Sketch \> Include Library \> Manage Libraries, search for "Adafruit GFX", and install it.  
   * Adafruit ST7789 Library: Install this library from the Library Manager as well.  
   * WiFiManager: Install this library from the Library Manager.  
5. **Upload the Code:** Open the esp32-voice-assistant-firmware.ino file, configure your Wi-Fi credentials in the setup() function, and upload the code to your ESP32.

#### **2\. Server Setup (Python)**

1. **Install Python:** Ensure you have Python 3 installed on your computer.  
2. Create and Activate a Virtual Environment:  
   It is highly recommended to use a virtual environment to manage project dependencies.  
   * On macOS/Linux:  
     python3 \-m venv venv  
     source venv/bin/activate

   * On Windows:  
     python \-m venv venv  
     venv\\Scripts\\activate

3. **Install Dependencies:** Ensure that the requirements.txt file is in the same directory as your server code. Next, open a terminal or command prompt and run the following command to install all the required Python libraries:  
   pip install \-r requirements.txt

4. **Create .env File:** In the same directory as the app.py file, create a new file named .env. Inside this file, add your Gemini API key:  
   GEMINI\_API\_KEY="YOUR\_API\_KEY\_HERE"

5. **Run the Server:** Run the server from your terminal. This will start the server on your computer's local network.  
   python server.py  
