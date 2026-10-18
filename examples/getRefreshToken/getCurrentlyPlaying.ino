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

LyricLine lyricLines[80];
uint8_t lyricCount = 0;
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

const uint8_t OLED_CHARS_PER_LINE = 21;
const uint8_t LYRIC_MAX_ROWS = 2;
const uint8_t LYRIC_FIRST_Y = 18;
const uint8_t LYRIC_MAX_WIDTH = 120;
const long LYRICS_OFFSET_MS = -350;
const unsigned long LONG_LYRIC_GAP_MS = 10000;

unsigned long delayBetweenRequests = 250; // Resync Spotify four times per second
unsigned long requestDueTime;               //time when request due

void setup()
{

    Serial.begin(115200);
    pinMode(NEXT_BUTTON_PIN, INPUT_PULLUP);
    pinMode(BACK_BUTTON_PIN, INPUT_PULLUP);
    pinMode(PLAY_BUTTON_PIN, INPUT_PULLUP);
    Wire.begin(21, 22);
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
    //client.setInsecure();

    // If you want to enable some extra debugging
    // uncomment the "#define SPOTIFY_DEBUG" in SpotifyArduino.h

    Serial.println("Refreshing Access Tokens");
    if (!spotify.refreshAccessToken())
    {
        Serial.println("Failed to get access tokens");
    }
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

unsigned long adjustedLyricTime(uint8_t index)
{
    long adjusted = (long)lyricLines[index].timeMs + LYRICS_OFFSET_MS;
    return adjusted < 0 ? 0 : (unsigned long)adjusted;
}

void fetchLyrics(const char *artist, const char *track, const char *trackUri)
{
    clearLyrics();
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

    DynamicJsonDocument document(16384);
    DeserializationError error = deserializeJson(document, http.getString());
    http.end();
    if (error || document["syncedLyrics"].isNull())
    {
        Serial.println("No synced lyrics found");
        return;
    }

    String syncedLyrics = document["syncedLyrics"].as<String>();
    int lineStart = 0;
    while (lineStart < syncedLyrics.length() && lyricCount < 80)
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
            lyricLines[lyricCount].text = line.substring(closeBracket + 1);
            lyricCount++;
        }
        lineStart = lineEnd + 1;
    }
    lyricTrackUri = trackUri;
    Serial.print("Loaded lyric lines: ");
    Serial.println(lyricCount);
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

void drawPlaybackControls();

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
    display.display();
}

void drawDotAnimation()
{
    uint8_t dotCount = (millis() / 350) % 4;
    display.setCursor(52, 24);
    display.print("...");
    for (uint8_t index = 0; index < dotCount; index++)
    {
        display.print('.');
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
    if (millis() - lastButtonTime > 300)
    {
        if (lastNextState == HIGH && nextState == LOW)
        {
            spotify.nextTrack();
            lastButtonTime = millis();
        }
        else if (lastBackState == HIGH && backState == LOW)
        {
            spotify.previousTrack();
            lastButtonTime = millis();
        }
        else if (lastPlayState == HIGH && playState == LOW)
        {
            if (currentIsPlaying)
            {
                spotify.pause();
            }
            else
            {
                spotify.play();
            }
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
        return gapLength >= LONG_LYRIC_GAP_MS && progress > adjustedLyricTime(activeLine) + 1000 && progress < nextTime;
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
        display.display();
        return;
    }

    int activeLine = -1;
    for (uint8_t index = 0; index < lyricCount; index++)
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
        uint8_t lyricY = lyricLines[activeLine].text.length() > OLED_CHARS_PER_LINE * LYRIC_MAX_ROWS
                             ? 8
                             : LYRIC_FIRST_Y;
        drawWrappedCentered(lyricLines[activeLine].text, lyricY, LYRIC_MAX_ROWS, lyricLines[activeLine].text.length());
    }
    drawProgressBar(progress);
    display.display();
}

void printCurrentlyPlayingToSerial(CurrentlyPlaying currentlyPlaying)
{
    String newTrack = currentlyPlaying.trackUri == NULL ? "" : currentlyPlaying.trackUri;
    if (lyricTrackUri != newTrack)
    {
        fetchLyrics(currentlyPlaying.numArtists > 0 ? currentlyPlaying.artists[0].artistName : NULL, currentlyPlaying.trackName, currentlyPlaying.trackUri);
        titleScreenUntil = millis() + 2000;
        displayedLyricIndex = -1;
    }
    currentTrackName = currentlyPlaying.trackName == NULL ? "" : currentlyPlaying.trackName;
    currentArtistName = currentlyPlaying.numArtists > 0 && currentlyPlaying.artists[0].artistName != NULL
                            ? currentlyPlaying.artists[0].artistName
                            : "";
    currentProgressMs = max(0L, currentlyPlaying.progressMs);
    currentDurationMs = max(0L, currentlyPlaying.durationMs);
    currentIsPlaying = currentlyPlaying.isPlaying;
    progressSampledAt = millis();

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
    if (millis() - lastOledFrame >= 40 && currentTrackName.length() > 0)
    {
        lastOledFrame = millis();
        if (millis() < titleScreenUntil)
        {
            drawTitleCard();
        }
        else
        {
            drawLyrics();
        }
    }
    if (millis() > requestDueTime)
    {
        Serial.print("Free Heap: ");
        Serial.println(ESP.getFreeHeap());

        Serial.println("getting currently playing song:");
        // Market can be excluded if you want e.g. spotify.getCurrentlyPlaying()
        int status = spotify.getCurrentlyPlaying(printCurrentlyPlayingToSerial, SPOTIFY_MARKET);
        if (status == 200)
        {
            Serial.println("Successfully got currently playing");
        }
        else if (status == 204)
        {
            Serial.println("Doesn't seem to be anything playing");
        }
        else
        {
            Serial.print("Error: ");
            Serial.println(status);
        }
        requestDueTime = millis() + delayBetweenRequests;
    }
}
