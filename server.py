import io
import speech_recognition as sr
from flask import Flask, request, jsonify, Response
from pydub import AudioSegment
from datetime import datetime
from gtts import gTTS
import requests
from dotenv import dotenv_values

app = Flask(__name__)

# Load environment variables from the .env file
config = dotenv_values(".env")
GEMINI_API_KEY = config.get("GEMINI_API_KEY")

def _get_gemini_response(prompt_text):
    """
    Sends the prompt to the Gemini API and returns the text response.
    """
    if not GEMINI_API_KEY:
        raise ValueError("GEMINI_API_KEY not found. Please set it in your .env file.")

    gemini_url = "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash-lite:generateContent"
    payload = {
        "contents": [{
            "parts": [{"text": prompt_text}]
        }],
        "generationConfig": {
            "maxOutputTokens": 25  # Limits the response to approximately 25 words
        }
    }
    
    gemini_response = requests.post(
        gemini_url,
        params={"key": GEMINI_API_KEY},
        json=payload
    )
    gemini_response.raise_for_status()
    
    gemini_text_response = gemini_response.json()['candidates'][0]['content']['parts'][0]['text']

    # Clean the output by removing special characters like asterisks
    cleaned_text = gemini_text_response.replace('*', '')

    return cleaned_text

def _generate_audio_data(text_response):
    """
    Converts text to audio using gTTS and formats it for the ESP32.
    """
    # Use BytesIO to store the audio in memory
    temp_audio_file = io.BytesIO()
    
    # Generate the speech using gTTS
    tts = gTTS(text=text_response, lang='en')
    tts.write_to_fp(temp_audio_file)
    temp_audio_file.seek(0)

    # Load the audio into pydub from the in-memory file
    audio_segment = AudioSegment.from_file(temp_audio_file, format="mp3")

    # Resample the audio to 16000 Hz and convert it to 16-bit mono
    audio_segment = audio_segment.set_frame_rate(16000).set_sample_width(2).set_channels(1)

    return audio_segment.raw_data

@app.route('/get_audio_response', methods=['POST'])
def get_audio_response():
    """
    Handles a POST request by orchestrating the Gemini API call and
    local audio generation, then streaming the audio back to the client.
    This endpoint can now handle both JSON payloads and raw audio streams.
    """
    try:
        # Check the content type to determine the input method
        if 'application/octet-stream' in request.content_type:
            # New logic for handling raw audio from the INMP441
            raw_audio_data = request.data
            
            # Use pydub to handle the raw audio data
            audio_segment = AudioSegment(
                raw_audio_data,
                sample_width=2, # 16-bit audio
                frame_rate=16000,
                channels=1
            )
            
            # Use SpeechRecognition to transcribe the audio
            r = sr.Recognizer()
            audio_data = sr.AudioData(audio_segment.raw_data, 16000, 2)
            prompt_text = r.recognize_google(audio_data)

            print(f"[{datetime.now().strftime('%Y-%m-%d %H:%M:%S')}] Transcribed prompt: {prompt_text}")

        elif 'application/json' in request.content_type:
            # Existing logic for handling a JSON payload
            data = request.get_json()
            if not data or 'prompt' not in data:
                return jsonify({"error": "Invalid JSON or missing 'prompt' key"}), 400
            prompt_text = data['prompt']
            print(f"[{datetime.now().strftime('%Y-%m-%d %H:%M:%S')}] Received text prompt: {prompt_text}")

        else:
            return jsonify({"error": "Unsupported Content-Type"}), 415

        # Get the text response from Gemini (this part is the same)
        gemini_text_response = _get_gemini_response(prompt_text)
        print(f"[{datetime.now().strftime('%Y-%m-%d %H:%M:%S')}] Gemini responded with: {gemini_text_response}")

        # Generate the audio data (this part is the same)
        audio_data = _generate_audio_data(gemini_text_response)
        print(f"[{datetime.now().strftime('%Y-%m-%d %H:%M:%S')}] Successfully generated local audio. Sending {len(audio_data)} bytes back to client.")

        # Return the raw audio bytes directly to the client
        return Response(audio_data, mimetype="audio/L16")

    except sr.UnknownValueError:
        print(f"[{datetime.now().strftime('%Y-%m-%d %H:%M:%S')}] Speech Recognition could not understand audio")
        return jsonify({"error": "Could not understand audio"}), 400
    except requests.exceptions.HTTPError as e:
        print(f"[{datetime.now().strftime('%Y-%m-%d %H:%M:%S')}] HTTP Error from Gemini API: {e}")
        return jsonify({"error": "Gemini API request failed"}), 502
    except Exception as e:
        print(f"[{datetime.now().strftime('%Y-%m-%d %H:%M:%S')}] An error occurred: {e}")
        return jsonify({"error": str(e)}), 500

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5002)
