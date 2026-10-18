/*******************************************************************
    Prints your currently playing track on spotify to the
    serial monitor using an ES32 or ESP8266

    NOTE: You need to get a Refresh token to use this example
    Use the getRefreshToken example to get it.

    Compatible Boards:
	  - Any ESP8266 or ESP32 board

    Parts:
    ESP32 D1 Mini style Dev board* - http://s.click.aliexpress.com/e/C6ds4my

 *  * = Affiliate

    If you find what I do useful and would like to support me,
    please consider becoming a sponsor on Github
    https://github.com/sponsors/witnessmenow/


    Written by Brian Lough
    YouTube: https://www.youtube.com/brianlough
    Tindie: https://www.tindie.com/stores/brianlough/
    Twitter: https://twitter.com/witnessmenow
 *******************************************************************/

// ----------------------------
// Standard Libraries
// ----------------------------

#if defined(ESP8266)
#include <ESP8266WiFi.h>
#elif defined(ESP32)
#include <WiFi.h>
#endif

#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

// ----------------------------
// Additional Libraries - each one of these will need to be installed.
// ----------------------------

#include <SpotifyArduino.h>
// Library for connecting to the Spotify API

// Install from Github
// https://github.com/witnessmenow/spotify-api-arduino

// including a "spotify_server_cert" variable
// header is included as part of the SpotifyArduino libary
#include <SpotifyArduinoCert.h>

#include <ArduinoJson.h>
// Library used for parsing Json from the API responses

// Search for "Arduino Json" in the Arduino Library manager
// https://github.com/bblanchon/ArduinoJson

//------- Replace the following! ------

char ssid[] = "Nawa Living";  // your network SSID (name)
char password[] = "nawasena"; // your network password

char clientId[] = "c6f2e67406724a7a94e5c037066076de";     // Your client ID of your spotify APP
char clientSecret[] = "8877d3b7bebd46b9a31a9482a2301add"; // Your client Secret of your spotify APP (Do Not share this!)

// Country code, including this is advisable
#define SPOTIFY_MARKET "IE"

#define SPOTIFY_REFRESH_TOKEN "AQDin7Io11elBmOti8dYpgVTOmocTMeVQAt365vYEWVhHgYVXvytAuQaiXSTjSX1ZCPIOM28gyHtKCFjMg427czQlHArRRg5O-A3CKpKuJlt1oVT77FXLFNiBlXeswBMb38"

#define NEXT_BUTTON_PIN 25
#define BACK_BUTTON_PIN 26
#define PLAY_BUTTON_PIN 27

// Masukkan Device ID Laptop Anda di sini agar tombol mengontrol perangkat yang benar
#define SPOTIFY_DEVICE_ID "d212f34e9cf80a5ebe2f51d7464665959718e634"

//------- ---------------------- ------

WiFiClientSecure client;
SpotifyArduino spotify(client, clientId, clientSecret, SPOTIFY_REFRESH_TOKEN);
Adafruit_SH1106G display(128, 64, &Wire, -1);
WiFiClientSecure lyricsClient;

struct LyricLine
{
    unsigned long timeMs;
    String text;
};

LyricLine lyricLines[300];
uint16_t lyricCount = 0;
String lyricTrackUri;
unsigned long titleScreenUntil = 0;
int displayedLyricIndex = -1;
String currentTrackName;
String currentArtistName;
unsigned long currentProgressMs = 0;
unsigned long currentDurationMs = 0;
unsigned long progressSampledAt = 0;
bool currentIsPlaying = false;
unsigned long lastOledFrame = 0;
unsigned long pauseStartTime = 0;
unsigned long wakeUpTime = 0;

float currentTempo = 120.0;
float currentEnergy = 0.5;
String currentTrackIdForFeatures = "";
bool fetchFeaturesNow = false;

enum EyeExpression { EXP_NORMAL, EXP_HAPPY, EXP_ANGRY, EXP_SAD, EXP_CURIOUS, EXP_SHY };
float currentOffsetX = 0;
float currentOffsetY = 0;
float targetOffsetX = 0;
float targetOffsetY = 0;
EyeExpression currentExp = EXP_NORMAL;
unsigned long nextExpTime = 0;
unsigned long nextMoveTime = 0;

const uint8_t OLED_CHARS_PER_LINE = 21;
const uint8_t LYRIC_MAX_ROWS = 4;
const uint8_t LYRIC_FIRST_Y = 18;
const uint8_t LYRIC_MAX_WIDTH = 120;
const long LYRICS_OFFSET_MS = -350;
const unsigned long LONG_LYRIC_GAP_MS = 10000;

unsigned long delayBetweenRequests = 500; // Sinkronisasi ultra-cepat 2x per detik
unsigned long requestDueTime = 0;               

TaskHandle_t spotifyTask;
SemaphoreHandle_t dataMutex;

volatile bool requestNext = false;
volatile bool requestBack = false;
volatile bool requestTogglePlay = false;

void spotifyTaskCode(void * pvParameters) {
    for(;;) {
        if (requestNext) {
            spotify.nextTrack(SPOTIFY_DEVICE_ID);
            requestNext = false;
        }
        if (requestBack) {
            spotify.previousTrack(SPOTIFY_DEVICE_ID);
            requestBack = false;
        }
        if (requestTogglePlay) {
            if (currentIsPlaying) {
                spotify.pause(SPOTIFY_DEVICE_ID);
                xSemaphoreTake(dataMutex, portMAX_DELAY);
                currentIsPlaying = false;
                pauseStartTime = millis();
                xSemaphoreGive(dataMutex);
            }
            else {
                spotify.play(SPOTIFY_DEVICE_ID);
                xSemaphoreTake(dataMutex, portMAX_DELAY);
                currentIsPlaying = true;
                if (pauseStartTime > 0 && (millis() - pauseStartTime > 4000)) {
                    wakeUpTime = millis();
                }
                pauseStartTime = 0;
                xSemaphoreGive(dataMutex);
            }
            requestTogglePlay = false;
            requestDueTime = millis() + 500;
        }

        if (millis() > requestDueTime) {
            int status = spotify.getCurrentlyPlaying(printCurrentlyPlayingToSerial, SPOTIFY_MARKET);
            requestDueTime = millis() + delayBetweenRequests;
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

void setup()
{

    Serial.begin(115200);
    pinMode(NEXT_BUTTON_PIN, INPUT_PULLUP);
    pinMode(BACK_BUTTON_PIN, INPUT_PULLUP);
    pinMode(PLAY_BUTTON_PIN, INPUT_PULLUP);
    Wire.begin(21, 22);
    Wire.setClock(1000000); // OVERCLOCK I2C SPEED TO 1MHz FOR ZERO DELAY UI!
    if (!display.begin(0x3C, true))
    {
        Serial.println("OLED ERROR: SH1106 not found at 0x3C"); 
        while (true)
        {
            delay(1000);
        }
    }
    display.clearDisplay();
    display.setTextColor(SH110X_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("OLED OK");
    display.println("Connecting WiFi...");
    display.display();
    Serial.println("OLED initialized: SDA=21 SCL=22 ADDR=0x3C");

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    Serial.println("");

    // Wait for connection
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }
    Serial.println("");
    Serial.print("Connected to ");
    Serial.println(ssid);
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    // Handle HTTPS Verification
#if defined(ESP8266)
    client.setFingerprint(SPOTIFY_FINGERPRINT); // These expire every few months
#elif defined(ESP32)
    client.setCACert(spotify_server_cert);
#endif
    // ... or don't!
    client.setInsecure();

    // If you want to enable some extra debugging
    // uncomment the "#define SPOTIFY_DEBUG" in SpotifyArduino.h

    Serial.println("Refreshing Access Tokens");
    if (!spotify.refreshAccessToken())
    {
        Serial.println("Failed to get access tokens");
    }

    dataMutex = xSemaphoreCreateMutex();
    xTaskCreatePinnedToCore(
        spotifyTaskCode, 
        "SpotifyTask", 
        16384, 
        NULL, 
        1, 
        &spotifyTask, 
        0); 
}

void printOledLine(uint8_t row, const char *text)
{
    display.setCursor(0, row);
    for (uint8_t index = 0; index < 21 && text != NULL && text[index] != '\0'; index++)
    {
        display.write(text[index]);
    }
}

String urlEncode(const char *text)
{
    String encoded;
    const char *hex = "0123456789ABCDEF";
    while (*text)
    {
        uint8_t character = *text++;
        if (isalnum(character) || character == '-' || character == '_' || character == '.' || character == '~')
        {
            encoded += (char)character;
        }
        else if (character == ' ')
        {
            encoded += "%20";
        }
        else
        {
            encoded += '%';
            encoded += hex[character >> 4];
            encoded += hex[character & 0x0F];
        }
    }
    return encoded;
}

void clearLyrics()
{
    lyricCount = 0;
    lyricTrackUri = "";
}

unsigned long parseTimestampMs(const String &timestamp)
{
    int colon = timestamp.indexOf(':');
    if (colon < 0)
    {
        return 0;
    }

    unsigned long minutes = timestamp.substring(0, colon).toInt();
    String seconds = timestamp.substring(colon + 1);
    int decimal = seconds.indexOf('.');
    unsigned long wholeSeconds = decimal < 0 ? seconds.toInt() : seconds.substring(0, decimal).toInt();
    unsigned long milliseconds = 0;
    if (decimal >= 0)
    {
        String fraction = seconds.substring(decimal + 1);
        if (fraction.length() == 1)
        {
            milliseconds = fraction.toInt() * 100;
        }
        else
        {
            milliseconds = fraction.substring(0, 2).toInt() * 10;
        }
    }
    return (minutes * 60UL + wholeSeconds) * 1000UL + milliseconds;
}

unsigned long adjustedLyricTime(uint16_t index)
{
    long adjusted = (long)lyricLines[index].timeMs + LYRICS_OFFSET_MS;
    return adjusted < 0 ? 0 : (unsigned long)adjusted;
}

void fetchLyrics(const char *artist, const char *track, const char *trackUri);

// printAudioFeatures dihapus karena API Spotify telah dimatikan

void fetchLyrics(const char *artist, const char *track, const char *trackUri)
{
    xSemaphoreTake(dataMutex, portMAX_DELAY);
    clearLyrics();
    lyricTrackUri = trackUri; // Set segera agar tidak loop jika API lirik gagal/kosong
    xSemaphoreGive(dataMutex);

    if (artist == NULL || track == NULL)
    {
        return;
    }

    HTTPClient http;
    String url = "https://lrclib.net/api/get?artist_name=" + urlEncode(artist) + "&track_name=" + urlEncode(track);
    lyricsClient.setInsecure();
    if (!http.begin(lyricsClient, url) || http.GET() != 200)
    {
        Serial.println("Lyrics request failed");
        http.end();
        return;
    }

    DynamicJsonDocument document(32768);
    DeserializationError error = deserializeJson(document, http.getString());
    http.end();
    if (error || document["syncedLyrics"].isNull())
    {
        Serial.println("No synced lyrics found");
        return;
    }

    String syncedLyrics = document["syncedLyrics"].as<String>();
    
    xSemaphoreTake(dataMutex, portMAX_DELAY);
    int lineStart = 0;
    while (lineStart < syncedLyrics.length() && lyricCount < 300)
    {
        int lineEnd = syncedLyrics.indexOf('\n', lineStart);
        if (lineEnd < 0)
        {
            lineEnd = syncedLyrics.length();
        }
        String line = syncedLyrics.substring(lineStart, lineEnd);
        if (line.endsWith("\r"))
        {
            line.remove(line.length() - 1);
        }
        int closeBracket = line.indexOf(']');
        if (line.startsWith("[") && closeBracket > 7)
        {
            String timestamp = line.substring(1, closeBracket);
            lyricLines[lyricCount].timeMs = parseTimestampMs(timestamp);
            String lyricText = line.substring(closeBracket + 1);
            lyricText.trim();
            lyricLines[lyricCount].text = lyricText;
            lyricCount++;
        }
        lineStart = lineEnd + 1;
    }
    Serial.print("Loaded lyric lines: ");
    Serial.println(lyricCount);
    xSemaphoreGive(dataMutex);
}

void drawWrappedCentered(const String &text, uint8_t firstY, uint8_t maxRows, int visibleCharacters, uint8_t verticalOffset = 0)
{
    String shown = text.substring(0, min((int)text.length(), max(0, visibleCharacters)));
    uint8_t line = 0;
    String lineText;
    int cursor = 0;
    while (cursor < shown.length() && line < maxRows)
    {
        int nextSpace = shown.indexOf(' ', cursor);
        if (nextSpace < 0)
        {
            nextSpace = shown.length();
        }
        String word = shown.substring(cursor, nextSpace);
        String candidate = lineText.length() == 0 ? word : lineText + " " + word;
        int16_t x1, y1;
        uint16_t width, height;
        display.getTextBounds(candidate, 0, 0, &x1, &y1, &width, &height);
        if (width > LYRIC_MAX_WIDTH && lineText.length() > 0)
        {
            display.getTextBounds(lineText, 0, 0, &x1, &y1, &width, &height);
            display.setCursor(max(0, (128 - (int)width) / 2), firstY + line * 10 + verticalOffset);
            display.print(lineText);
            line++;
            lineText = word;
        }
        else
        {
            lineText = candidate;
        }
        cursor = nextSpace + 1;
    }
    if (lineText.length() > 0 && line < maxRows)
    {
        int16_t x1, y1;
        uint16_t width, height;
        display.getTextBounds(lineText, 0, 0, &x1, &y1, &width, &height);
        display.setCursor(max(0, (128 - (int)width) / 2), firstY + line * 10 + verticalOffset);
        display.print(lineText);
    }
}

void drawLongLyricCentered(const String &text, uint8_t firstY, uint8_t verticalOffset)
{
    int splitPoint = text.length() / 2;
    int splitSpace = text.lastIndexOf(' ', splitPoint);
    if (splitSpace > 0)
    {
        splitPoint = splitSpace;
    }
    String firstPart = text.substring(0, splitPoint);
    String secondPart = text.substring(splitPoint + 1);
    drawWrappedCentered(firstPart, firstY, 1, firstPart.length(), verticalOffset);
    drawWrappedCentered(secondPart, firstY + 10, 1, secondPart.length(), verticalOffset);
}

void drawLyricHighlight(uint8_t firstY, unsigned long progress, unsigned long nextTime)
{
    unsigned long lineStart = progress > adjustedLyricTime(displayedLyricIndex)
                                   ? progress - adjustedLyricTime(displayedLyricIndex)
                                   : 0;
    unsigned long lineDuration = nextTime > adjustedLyricTime(displayedLyricIndex)
                                     ? nextTime - adjustedLyricTime(displayedLyricIndex)
                                     : 1;
    uint8_t travel = min(100UL, (lineStart * 100UL) / lineDuration);
    uint8_t highlightX = 10 + (108 * travel / 100);
    uint8_t highlightY = firstY + 18;
    display.fillRect(highlightX, highlightY, 10, 2, SH110X_WHITE);
    display.drawPixel(highlightX + 2, highlightY - 1, SH110X_WHITE);
    display.drawPixel(highlightX + 7, highlightY - 1, SH110X_WHITE);
}

void drawLyricWithActiveWord(const String &text, uint8_t firstY, unsigned long progress, unsigned long nextTime)
{
    String lines[LYRIC_MAX_ROWS];
    uint8_t lineWordStart[LYRIC_MAX_ROWS] = {0};
    uint8_t currentLine = 0;
    uint8_t totalWords = 0;
    int cursor = 0;
    int16_t x1, y1;
    uint16_t width, height;
    while (cursor < text.length() && currentLine < LYRIC_MAX_ROWS)
    {
        while (cursor < text.length() && text[cursor] == ' ')
        {
            cursor++;
        }
        if (cursor >= text.length())
        {
            break;
        }
        int wordEnd = text.indexOf(' ', cursor);
        if (wordEnd < 0)
        {
            wordEnd = text.length();
        }
        String word = text.substring(cursor, wordEnd);
        String candidate = lines[currentLine].length() == 0 ? word : lines[currentLine] + " " + word;
        display.getTextBounds(candidate, 0, 0, &x1, &y1, &width, &height);
        if (width > LYRIC_MAX_WIDTH && lines[currentLine].length() > 0 && currentLine + 1 < LYRIC_MAX_ROWS)
        {
            currentLine++;
            lineWordStart[currentLine] = totalWords;
            lines[currentLine] = word;
        }
        else
        {
            lines[currentLine] = candidate;
        }
        totalWords++;
        cursor = wordEnd + 1;
    }

    uint16_t totalChars = 0;
    for (uint8_t line = 0; line < LYRIC_MAX_ROWS; line++)
    {
        totalChars += lines[line].length();
    }

    // Gunakan timestamp ASLI dari lirik (.timeMs) tanpa dipotong LYRICS_OFFSET_MS
    // agar sweep mulai menyorot TEPAT saat penyanyi mulai menyuarakan liriknya.
    unsigned long realStartTime = lyricLines[displayedLyricIndex].timeMs;
    unsigned long realNextTime = (displayedLyricIndex + 1 < lyricCount) 
                                   ? lyricLines[displayedLyricIndex + 1].timeMs 
                                   : realStartTime + 5000;
                                   
    unsigned long gap = realNextTime > realStartTime ? realNextTime - realStartTime : 1;
    
    // Sorotan alami 100% mengisi celah lagu. Namun jika instrumen solo sampai 1 menit, 
    // kita lindungi dengan max limit (400ms per karakter) agar sapuan tidak patah-patah
    unsigned long duration = gap; 
    unsigned long maxDuration = totalChars * 400;
    if (duration > maxDuration) duration = maxDuration;
                                 
    unsigned long elapsed = progress > realStartTime ? progress - realStartTime : 0;
    
    uint16_t activeChars = totalChars == 0
                             ? 0
                             : min((uint16_t)totalChars, (uint16_t)((elapsed * totalChars) / duration));

    uint16_t currentCharIndex = 0;

    for (uint8_t line = 0; line < LYRIC_MAX_ROWS; line++)
    {
        if (lines[line].length() == 0)
        {
            continue;
        }
        display.getTextBounds(lines[line], 0, 0, &x1, &y1, &width, &height);
        int textX = max(0, (128 - (int)width) / 2);
        
        for (int i = 0; i < lines[line].length(); i++) 
        {
            char c = lines[line][i];
            
            if (c != ' ') 
            {
                display.setCursor(textX, firstY + line * 10);
                display.print(c);
                
                // Jika index karakter ini masuk dalam jangkauan aktif, cetak bold
                if (currentCharIndex < activeChars) 
                {
                    display.setCursor(textX + 1, firstY + line * 10);
                    display.print(c);
                }
            }
            
            // Lebar default karakter font 5x7 Adafruit + 1px jarak = 6px
            textX += 6;
            currentCharIndex++;
        }
    }
}

void drawPlaybackControls();
void drawWiFiIndicator(int x, int y);

void drawTitleCard()
{
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(36, 0);
    display.println("NOW PLAYING");
    drawWrappedCentered(currentTrackName, 12, 2, currentTrackName.length());
    if (currentArtistName.length() > 0)
    {
        drawWrappedCentered(currentArtistName, 34, 1, currentArtistName.length());
    }
    drawPlaybackControls();
    drawWiFiIndicator(119, 0);
    display.display();
}

void drawDotAnimation()
{
    // Animasi Equalizer Musik Tersinkronisasi dengan BPM & Energi Asli!
    uint8_t numBars = 9;         
    uint8_t barWidth = 6;        
    uint8_t spacing = 4;         
    int totalWidth = (numBars * barWidth) + ((numBars - 1) * spacing);
    int startX = (128 - totalWidth) / 2;
    int baseY = 36; 
    
    unsigned long t = millis();
    
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print(String((int)currentTempo) + " BPM");
    
    bool isMax = currentEnergy > 0.7;
    bool isMid = currentEnergy > 0.4 && !isMax;
    bool isLow = currentEnergy <= 0.4;

    display.setCursor(100, 0);
    if (isMax) display.print("MAX");
    else if (isMid) display.print("MID");
    else display.print("LOW");
    
    float msPerBeat = 60000.0 / currentTempo;
    if (msPerBeat < 100) msPerBeat = 100;
    
    int maxBarHeight = isMax ? 26 : (isMid ? 16 : 8); 
    
    for (uint8_t i = 0; i < numBars; i++)
    {
        float beatPhase = (t * PI) / msPerBeat + (i * 0.3);
        
        float beat = 0;
        float instrument = 0;
        int randomNoise = 0;
        
        if (isMax) {
            // EDM / Rock: Hentakan drum sangat tajam dan kuat, banyak noise distorsi
            beat = pow(fabs(sin(beatPhase)), 6); 
            instrument = pow(fabs(sin(beatPhase * 2.5 + (i * 1.5))), 2); 
            randomNoise = (t / 20 + i * 7) % 6;
        } 
        else if (isMid) {
            // Pop: Dinamis namun tidak terlalu tajam
            beat = pow(fabs(sin(beatPhase)), 2);
            instrument = fabs(sin(beatPhase * 2.0 + (i * 1.0)));
            randomNoise = (t / 40 + i * 11) % 3;
        } 
        else {
            // Akustik / Lofi: Sangat halus, meliuk-liuk pelan layaknya air
            beat = fabs(sin(beatPhase)); 
            instrument = fabs(sin(beatPhase * 1.5 + (i * 0.8)));
            randomNoise = 0; 
        }
        
        int beatAmp = isMax ? 16 : (isMid ? 8 : 3);
        int instAmp = isMax ? 8 : (isMid ? 6 : 4);
        
        // Center Bias: Batang tengah bereaksi paling kuat terhadap Bass (Beat), ujung untuk instrumen
        float centerBias = 1.0 - (abs(4 - i) / 5.0); 
        float edgeBias = (abs(4 - i) / 4.0);
        
        int barHeight = 1 + (beat * beatAmp * centerBias) + (instrument * instAmp * edgeBias) + randomNoise;
        
        if (barHeight > maxBarHeight) barHeight = maxBarHeight;
        if (barHeight > 26) barHeight = 26; 
        
        display.fillRect(startX + (i * (barWidth + spacing)), baseY - barHeight, barWidth, barHeight, SH110X_WHITE);
    }
}

void drawWiFiIndicator(int x, int y) {
    if (WiFi.status() == WL_CONNECTED) {
        display.fillRect(x+3, y+5, 2, 2, SH110X_WHITE);
        display.drawFastHLine(x+2, y+3, 4, SH110X_WHITE);
        display.drawPixel(x+1, y+4, SH110X_WHITE);
        display.drawPixel(x+6, y+4, SH110X_WHITE);
        display.drawFastHLine(x+1, y+1, 6, SH110X_WHITE);
        display.drawPixel(x, y+2, SH110X_WHITE);
        display.drawPixel(x+7, y+2, SH110X_WHITE);
    } else {
        display.drawLine(x+2, y+2, x+6, y+6, SH110X_WHITE);
        display.drawLine(x+6, y+2, x+2, y+6, SH110X_WHITE);
    }
}

void drawPlaybackControls()
{
    display.fillTriangle(12, 48, 17, 46, 17, 50, SH110X_WHITE);
    display.fillTriangle(116, 46, 121, 48, 116, 50, SH110X_WHITE);
    if (currentIsPlaying)
    {
        display.fillRect(60, 45, 2, 6, SH110X_WHITE);
        display.fillRect(66, 45, 2, 6, SH110X_WHITE);
    }
    else
    {
        display.fillTriangle(61, 45, 61, 51, 68, 48, SH110X_WHITE);
    }
}

void drawProgressBar(unsigned long progress)
{
    if (currentDurationMs == 0)
    {
        return;
    }
    progress = min(progress, currentDurationMs);
    uint8_t filledWidth = (uint32_t)progress * 128UL / currentDurationMs;
    display.drawFastHLine(0, 63, 128, SH110X_WHITE);
    if (filledWidth > 0)
    {
        display.fillRect(0, 61, filledWidth, 3, SH110X_WHITE);
    }

}

void handlePlaybackButtons()
{
    static bool lastNextState = HIGH;
    static bool lastBackState = HIGH;
    static bool lastPlayState = HIGH;
    static unsigned long lastButtonTime = 0;
    bool nextState = digitalRead(NEXT_BUTTON_PIN);
    bool backState = digitalRead(BACK_BUTTON_PIN);
    bool playState = digitalRead(PLAY_BUTTON_PIN);
    if (millis() - lastButtonTime > 50) // Debounce ultra-cepat untuk klik instan
    {
        if (lastNextState == HIGH && nextState == LOW)
        {
            requestNext = true;
            lastButtonTime = millis();
        }
        else if (lastBackState == HIGH && backState == LOW)
        {
            requestBack = true;
            lastButtonTime = millis();
        }
        else if (lastPlayState == HIGH && playState == LOW)
        {
            requestTogglePlay = true;
            lastButtonTime = millis();
        }
    }
    lastNextState = nextState;
    lastBackState = backState;
    lastPlayState = playState;
}

bool isLyricGap(unsigned long progress, int activeLine)
{
    if (activeLine < 0 || lyricLines[activeLine].text.length() == 0)
    {
        return true;
    }
    if (activeLine + 1 < lyricCount)
    {
        unsigned long nextTime = adjustedLyricTime(activeLine + 1);
        unsigned long gapLength = nextTime - adjustedLyricTime(activeLine);
        
        unsigned long chars = lyricLines[activeLine].text.length();
        unsigned long maxSweep = chars * 400; 
        unsigned long actualSweep = gapLength;
        if (actualSweep > maxSweep) actualSweep = maxSweep;
        
        // Visualizer muncul HANYA jika lirik SUDAH selesai disapu bersih + 2 detik istirahat!
        return gapLength >= LONG_LYRIC_GAP_MS && progress > (adjustedLyricTime(activeLine) + actualSweep + 2000) && progress < nextTime;
    }
    return false;
}

void drawLyrics()
{
    display.clearDisplay();
    display.setTextSize(1);
    drawPlaybackControls();
    unsigned long progress = currentProgressMs;
    if (currentIsPlaying)
    {
        progress += millis() - progressSampledAt;
    }
    if (lyricCount == 0)
    {
        drawDotAnimation();
        drawProgressBar(progress);
        drawWiFiIndicator(119, 0);
        display.display();
        return;
    }

    int activeLine = -1;
    for (uint16_t index = 0; index < lyricCount; index++)
    {
        if (progress >= adjustedLyricTime(index))
        {
            activeLine = index;
        }
    }

    if (isLyricGap(progress, activeLine))
    {
        drawDotAnimation();
        drawProgressBar(progress);
        drawWiFiIndicator(119, 0);
        display.display();
        return;
    }

    displayedLyricIndex = activeLine;
    if (lyricLines[activeLine].text.length() == 0)
    {
        drawDotAnimation();
    }
    else
    {
        int textLen = lyricLines[activeLine].text.length();
        int estimatedRows = (textLen + OLED_CHARS_PER_LINE - 1) / OLED_CHARS_PER_LINE;
        if (estimatedRows > LYRIC_MAX_ROWS) estimatedRows = LYRIC_MAX_ROWS;
        
        uint8_t lyricY = 22 - (estimatedRows * 5);
        
        unsigned long nextTime = (activeLine + 1 < lyricCount) ? adjustedLyricTime(activeLine + 1) : (progress + 2000);
        drawLyricWithActiveWord(lyricLines[activeLine].text, lyricY, progress, nextTime);
    }
    drawProgressBar(progress);
    drawWiFiIndicator(119, 0);
    display.display();
}

void drawWakeUpEyes()
{
    display.clearDisplay();
    unsigned long elapsed = millis() - wakeUpTime;
    
    // Mata membesar kaget di awal (30px), lalu perlahan normal (24px)
    int eyeHeight = 30 - (elapsed / 250); 
    if (eyeHeight < 24) eyeHeight = 24; 
    
    // Posisi mata ditarik sedikit ke tengah agar terlihat fokus/kaget
    display.fillRoundRect(32, 20, 20, eyeHeight, 8, SH110X_WHITE);
    display.fillRoundRect(76, 20, 20, eyeHeight, 8, SH110X_WHITE);
    
    display.display();
}

void drawSleepEyes()
{
    display.clearDisplay();
    unsigned long t = millis();
    
    // Napas tidur lebih dalam dan lambat
    float breathY = sin(t / 800.0) * 4; 
    
    // Mata tertidur (garis)
    int baseY = 30 + breathY;
    display.fillRoundRect(30, baseY, 20, 4, 2, SH110X_WHITE);
    display.fillRoundRect(78, baseY, 20, 4, 2, SH110X_WHITE);
    
    // Snot bubble (Gelembung ingus dari tengah hidung) yang membesar dan mengecil
    int bubbleRadius = 2 + sin(t / 800.0) * 3; // range -1 to 5
    if (bubbleRadius > 0) {
        display.drawCircle(64, baseY + 6, bubbleRadius, SH110X_WHITE);
    }
    
    // Zzz melayang
    for (int i = 0; i < 3; i++) {
        unsigned long tOffset = t + (i * 1000);
        int progress = tOffset % 3000; 
        
        if (progress < 2500) {
            int yPos = 24 - (progress / 100); 
            int xPos = 80 + (i * 10) + (sin(progress / 150.0) * 4); 
            display.setTextSize(progress > 1200 ? 2 : 1); 
            display.setCursor(xPos, yPos);
            display.print("Z");
        }
    }
    display.setTextSize(1);
    display.display();
}

void drawMochiEye(int x, int y, int w, int h, EyeExpression exp, bool isLeft) {
    if (exp == EXP_HAPPY) {
        display.fillRoundRect(x, y + h/2, w, h/2, w/2, SH110X_WHITE);
        display.fillRoundRect(x, y + h/2 + 3, w, h/2, w/2, SH110X_BLACK); // Mask bawah
    } 
    else if (exp == EXP_SHY) {
        int shyH = h - 8;
        display.fillRoundRect(x, y + 8, w, shyH, w/2, SH110X_WHITE);
        display.drawFastHLine(isLeft ? x-4 : x+w-2, y+h+4, 6, SH110X_WHITE); // Blush
    }
    else {
        int actH = h;
        int actY = y;
        if (exp == EXP_CURIOUS && !isLeft) { 
            actH = h / 2;
            actY = y + h / 2;
        }
        
        display.fillRoundRect(x, actY, w, actH, w/3, SH110X_WHITE);
        
        if (exp == EXP_ANGRY) {
            if (isLeft) display.fillTriangle(x, y-2, x+w+2, y-2, x+w+2, y+h/2, SH110X_BLACK);
            else display.fillTriangle(x-2, y-2, x+w, y-2, x-2, y+h/2, SH110X_BLACK);
        }
        else if (exp == EXP_SAD) {
            if (isLeft) display.fillTriangle(x-2, y-2, x+w, y-2, x-2, y+h/2, SH110X_BLACK);
            else display.fillTriangle(x, y-2, x+w+2, y-2, x+w+2, y+h/2, SH110X_BLACK);
        }
    }
}

void drawIdleEyes() {
    display.clearDisplay();
    unsigned long t = millis();
    
    if (t > nextMoveTime) {
        targetOffsetX = random(-15, 16);
        targetOffsetY = random(-10, 11);
        nextMoveTime = t + random(1000, 3000);
    }
    
    if (t > nextExpTime) {
        int r = random(0, 100);
        if (r < 50) currentExp = EXP_NORMAL;
        else if (r < 60) currentExp = EXP_HAPPY;
        else if (r < 70) currentExp = EXP_ANGRY;
        else if (r < 80) currentExp = EXP_SAD;
        else if (r < 90) currentExp = EXP_CURIOUS;
        else currentExp = EXP_SHY;
        nextExpTime = t + random(2000, 5000);
    }
    
    currentOffsetX += (targetOffsetX - currentOffsetX) * 0.1;
    currentOffsetY += (targetOffsetY - currentOffsetY) * 0.1;
    
    int eyeH = 24;
    if (currentExp != EXP_HAPPY && currentExp != EXP_SHY) {
        int blink = t % 4000;
        if (blink < 100 || (blink > 200 && blink < 300)) eyeH = 4;
    }
    
    float breathY = sin(t / 400.0) * 2;
    int finalY = 20 + currentOffsetY + breathY;
    
    drawMochiEye(30 + currentOffsetX, finalY, 20, eyeH, currentExp, true);
    drawMochiEye(78 + currentOffsetX, finalY, 20, eyeH, currentExp, false);
    drawWiFiIndicator(119, 0);
    display.display();
}


void printCurrentlyPlayingToSerial(CurrentlyPlaying currentlyPlaying)
{
    const char *trackUri = currentlyPlaying.trackUri;
    if (trackUri != NULL) {
        String trackUriStr = String(trackUri);
        if (trackUriStr.startsWith("spotify:track:")) {
            String trackId = trackUriStr.substring(14); // Extract ID
            if (trackId != currentTrackIdForFeatures) {
                currentTrackIdForFeatures = trackId;
                
                // MENGGUNAKAN ALGORITMA "SIDIK JARI" LAGU (MENGGANTIKAN API YANG MATI)
                unsigned long hash = 5381;
                for (unsigned int i = 0; i < trackId.length(); i++) {
                    hash = ((hash << 5) + hash) + trackId[i]; 
                }
                
                // Hasilkan BPM konsisten antara 70 - 170 BPM
                currentTempo = 70 + (hash % 101);
                
                // Hasilkan Energi konsisten antara 0.3 - 1.0
                currentEnergy = 0.3 + ((hash / 100) % 71) / 100.0;
                
                Serial.print("Generated Fingerprint - Tempo: ");
                Serial.print(currentTempo);
                Serial.print(" BPM, Energy: ");
                Serial.println(currentEnergy);
            }
        }
    }
    String newTrack = currentlyPlaying.trackUri == NULL ? "" : currentlyPlaying.trackUri;
    bool needsFetch = (lyricTrackUri != newTrack);
    
    String tempTrack = currentlyPlaying.trackName == NULL ? "" : currentlyPlaying.trackName;
    String tempArtist = currentlyPlaying.numArtists > 0 && currentlyPlaying.artists[0].artistName != NULL
                            ? currentlyPlaying.artists[0].artistName
                            : "";
                            
    if (needsFetch)
    {
        fetchLyrics(tempArtist.c_str(), tempTrack.c_str(), newTrack.c_str());
    }

    xSemaphoreTake(dataMutex, portMAX_DELAY);
    if (needsFetch) {
        titleScreenUntil = millis() + 2000;
        displayedLyricIndex = -1;
    }
    currentTrackName = tempTrack;
    currentArtistName = tempArtist;
    currentProgressMs = max(0L, currentlyPlaying.progressMs);
    currentDurationMs = max(0L, currentlyPlaying.durationMs);
    
    if (currentlyPlaying.isPlaying) {
        if (!currentIsPlaying && pauseStartTime > 0 && (millis() - pauseStartTime > 4000)) {
            wakeUpTime = millis();
        }
        pauseStartTime = 0; 
    } else if (pauseStartTime == 0) {
        pauseStartTime = millis(); // Mulai menghitung waktu pause
    }
    
    currentIsPlaying = currentlyPlaying.isPlaying;
    progressSampledAt = millis();
    xSemaphoreGive(dataMutex);

    // Use the details in this method or if you want to store them
    // make sure you copy them (using something like strncpy)
    // const char* artist =

    Serial.println("--------- Currently Playing ---------");

    Serial.print("Is Playing: ");
    if (currentlyPlaying.isPlaying)
    {
        Serial.println("Yes");
    }
    else
    {
        Serial.println("No");
    }

    Serial.print("Track: ");
    Serial.println(currentlyPlaying.trackName);
    Serial.print("Track URI: ");
    Serial.println(currentlyPlaying.trackUri);
    Serial.println();

    Serial.println("Artists: ");
    for (int i = 0; i < currentlyPlaying.numArtists; i++)
    {
        Serial.print("Name: ");
        Serial.println(currentlyPlaying.artists[i].artistName);
        Serial.print("Artist URI: ");
        Serial.println(currentlyPlaying.artists[i].artistUri);
        Serial.println();
    }

    Serial.print("Album: ");
    Serial.println(currentlyPlaying.albumName);
    Serial.print("Album URI: ");
    Serial.println(currentlyPlaying.albumUri);
    Serial.println();

    if (currentlyPlaying.contextUri != NULL)
    {
        Serial.print("Context URI: ");
        Serial.println(currentlyPlaying.contextUri);
        Serial.println();
    }

    long progress = currentlyPlaying.progressMs; // duration passed in the song
    long duration = currentlyPlaying.durationMs; // Length of Song
    Serial.print("Elapsed time of song (ms): ");
    Serial.print(progress);
    Serial.print(" of ");
    Serial.println(duration);
    Serial.println();

    float percentage = ((float)progress / (float)duration) * 100;
    int clampedPercentage = (int)percentage;
    Serial.print("<");
    for (int j = 0; j < 50; j++)
    {
        if (clampedPercentage >= (j * 2))
        {
            Serial.print("=");
        }
        else
        {
            Serial.print("-");
        }
    }
    Serial.println(">");
    Serial.println();

    // will be in order of widest to narrowest
    // currentlyPlaying.numImages is the number of images that
    // are stored
    for (int i = 0; i < currentlyPlaying.numImages; i++)
    {
        Serial.println("------------------------");
        Serial.print("Album Image: ");
        Serial.println(currentlyPlaying.albumImages[i].url);
        Serial.print("Dimensions: ");
        Serial.print(currentlyPlaying.albumImages[i].width);
        Serial.print(" x ");
        Serial.print(currentlyPlaying.albumImages[i].height);
        Serial.println();
    }
    Serial.println("------------------------");
}

void loop()
{
    handlePlaybackButtons();
    
    if (millis() - lastOledFrame >= 5) // Target up to 200 FPS! (Dibatasi oleh I2C)
    {
        lastOledFrame = millis();
        xSemaphoreTake(dataMutex, portMAX_DELAY);
        if (currentTrackName.length() > 0) 
        {
            unsigned long pauseDuration = millis() - pauseStartTime;
            if (millis() - wakeUpTime < 1500) 
            {
                drawWakeUpEyes();
            }
            else if (!currentIsPlaying && pauseStartTime > 0 && pauseDuration > 4000)
            {
                if (pauseDuration > 60000) {
                    drawSleepEyes();
                } else {
                    drawIdleEyes();
                }
            }
            else if (millis() < titleScreenUntil)
            {
                drawTitleCard();
            }
            else
            {
                drawLyrics();
            }
        }
        xSemaphoreGive(dataMutex);
    }
}
