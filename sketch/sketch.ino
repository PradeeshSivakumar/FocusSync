/* ==========================================================================
   FOCUSSYNC — Real-Time IoT-Based Smart Study System
   Embedded Programming Project — Zeroth Review
   ESP32 + DHT22 + LDR + Sound Sensor(sim) + MQ135 + PIR + Relay/Fan +
   LED + Buzzer + SSD1306 OLED (128x64, I2C)

   Libraries required (install via Wokwi Library Manager / libraries.txt,
   or Arduino Library Manager if flashing real hardware):
     - DHT sensor library      (Adafruit)
     - Adafruit Unified Sensor (Adafruit)
     - Adafruit GFX Library    (Adafruit)
     - Adafruit SSD1306        (Adafruit)
   ========================================================================== */

#include <DHT.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

/* ---------------------- PIN MAP -------------------------------------- */
#define DHTPIN        4      // DHT22 data pin
#define DHTTYPE       DHT22

#define LDR_PIN       34     // Light intensity   (analog, ADC1_CH6)
#define SOUND_PIN     35     // Noise level        (analog, ADC1_CH7) — potentiometer simulates a sound-sensor module's AO
#define MQ135_PIN     32     // Air quality        (analog, ADC1_CH4)
#define PIR_PIN       27     // Occupancy / motion (digital in)

#define RELAY_PIN     26     // Relay -> DC Fan
#define LED_PIN       25     // Study-lamp / room LED
#define BUZZER_PIN    13     // Alert buzzer

#define OLED_SDA      21
#define OLED_SCL      22
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define OLED_ADDR     0x3C

/* ---------------------- THRESHOLDS (dynamically adjustable) ----------- */
/* Change these at runtime from the Serial Monitor (115200 baud, line
   ending = "Newline"). Type HELP to see commands. */
float   TEMP_HIGH_C      = 28.0;   // above this -> fan ON
float   TEMP_LOW_C       = 24.0;   // below this -> fan OFF (hysteresis)
int     DARK_LUX_RAW     = 2200;   // LDR analog raw value below which room counts as "dark" (LED ON)
int     LIGHT_LUX_RAW    = 1400;   // raw value above which room is "bright enough" (LED OFF)
int     NOISE_THRESHOLD  = 2600;   // sound sensor raw ADC threshold -> noisy
int     AIR_THRESHOLD    = 1800;   // MQ135 raw ADC threshold -> poor air quality

/* ---------------------- GLOBALS ---------------------------------------- */
DHT dht(DHTPIN, DHTTYPE);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

bool fanState   = false;
bool lightState = false;
unsigned long lastRead = 0;
const unsigned long READ_INTERVAL = 2000;

/* ------------------------------------------------------------------------
   Runtime threshold control via Serial Monitor.
   Open Serial Monitor at 115200 baud, line ending "Newline", and type:

     SET TEMPHIGH 30      -> fan turns ON at/above 30.0 C
     SET TEMPLOW  24      -> fan turns OFF at/below 24.0 C
     SET DARK     2200    -> lamp turns ON when LDR raw >= 2200 (darker)
     SET LIGHT    1400    -> lamp turns OFF when LDR raw <= 1400 (brighter)
     SET NOISE    2600    -> buzzer alerts when sound raw > 2600
     SET AIR      1800    -> buzzer alerts when MQ135 raw > 1800
     GET                  -> print all current threshold values
     HELP                 -> print this command list
   ------------------------------------------------------------------------ */
void printThresholds() {
  Serial.println(F("----- Current Thresholds -----"));
  Serial.printf("TEMPHIGH = %.1f C\n", TEMP_HIGH_C);
  Serial.printf("TEMPLOW  = %.1f C\n", TEMP_LOW_C);
  Serial.printf("DARK     = %d (LDR raw)\n", DARK_LUX_RAW);
  Serial.printf("LIGHT    = %d (LDR raw)\n", LIGHT_LUX_RAW);
  Serial.printf("NOISE    = %d (sound raw)\n", NOISE_THRESHOLD);
  Serial.printf("AIR      = %d (MQ135 raw)\n", AIR_THRESHOLD);
  Serial.println(F("-------------------------------"));
}

void printHelp() {
  Serial.println(F("Commands:"));
  Serial.println(F("  SET TEMPHIGH <val>   SET TEMPLOW <val>"));
  Serial.println(F("  SET DARK <val>       SET LIGHT <val>"));
  Serial.println(F("  SET NOISE <val>      SET AIR <val>"));
  Serial.println(F("  GET   -> show current thresholds"));
  Serial.println(F("  HELP  -> show this list"));
}

void handleSerialCommands() {
  if (!Serial.available()) return;

  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line.length() == 0) return;

  line.toUpperCase();

  if (line == "HELP") {
    printHelp();
    return;
  }
  if (line == "GET") {
    printThresholds();
    return;
  }

  if (!line.startsWith("SET ")) {
    Serial.println(F("Unknown command. Type HELP for the command list."));
    return;
  }

  String rest = line.substring(4);
  rest.trim();
  int sep = rest.indexOf(' ');
  if (sep == -1) {
    Serial.println(F("Missing value. Example: SET TEMPHIGH 30"));
    return;
  }

  String name = rest.substring(0, sep);
  String valStr = rest.substring(sep + 1);
  valStr.trim();
  float val = valStr.toFloat();

  if (name == "TEMPHIGH") {
    TEMP_HIGH_C = val;
    Serial.printf("OK: TEMPHIGH set to %.1f C\n", TEMP_HIGH_C);
  } else if (name == "TEMPLOW") {
    TEMP_LOW_C = val;
    Serial.printf("OK: TEMPLOW set to %.1f C\n", TEMP_LOW_C);
  } else if (name == "DARK") {
    DARK_LUX_RAW = (int)val;
    Serial.printf("OK: DARK set to %d\n", DARK_LUX_RAW);
  } else if (name == "LIGHT") {
    LIGHT_LUX_RAW = (int)val;
    Serial.printf("OK: LIGHT set to %d\n", LIGHT_LUX_RAW);
  } else if (name == "NOISE") {
    NOISE_THRESHOLD = (int)val;
    Serial.printf("OK: NOISE set to %d\n", NOISE_THRESHOLD);
  } else if (name == "AIR") {
    AIR_THRESHOLD = (int)val;
    Serial.printf("OK: AIR set to %d\n", AIR_THRESHOLD);
  } else {
    Serial.println(F("Unknown threshold name. Type HELP for the command list."));
  }
}

/* ------------------------------------------------------------------------ */
void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(PIR_PIN, INPUT);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  digitalWrite(RELAY_PIN, LOW);
  digitalWrite(LED_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);

  dht.begin();

  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println(F("SSD1306 allocation failed"));
  } else {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println(F("FocusSync"));
    display.println(F("Smart Study System"));
    display.println(F("Booting sensors..."));
    display.display();
    delay(1500);
  }

  Serial.println(F("=== FocusSync Smart Study System Initialized ==="));
  printHelp();
  printThresholds();
}

/* ------------------------------------------------------------------------ */
void loop() {
  handleSerialCommands();   // check every iteration, independent of the sensor-read timer

  if (millis() - lastRead < READ_INTERVAL) return;
  lastRead = millis();

  /* ---- Read all sensors ---- */
  float temperature = dht.readTemperature();
  float humidity    = dht.readHumidity();

  int   lightRaw = analogRead(LDR_PIN);      // higher raw = darker (LDR module AO)
  int   soundRaw = analogRead(SOUND_PIN);    // higher raw = louder
  int   airRaw   = analogRead(MQ135_PIN);    // higher raw = worse air quality
  bool  occupied = digitalRead(PIR_PIN);     // HIGH = motion / student present

  if (isnan(temperature) || isnan(humidity)) {
    Serial.println(F("Failed to read DHT22 sensor!"));
    temperature = 0;
    humidity = 0;
  }

  /* ---- Automatic fan control (temperature, with hysteresis) ---- */
  if (temperature >= TEMP_HIGH_C) fanState = true;
  else if (temperature <= TEMP_LOW_C) fanState = false;
  digitalWrite(RELAY_PIN, fanState ? HIGH : LOW);

  /* ---- Automatic lighting control (only when room is occupied) ---- */
  if (occupied) {
    if (lightRaw >= DARK_LUX_RAW) lightState = true;
    else if (lightRaw <= LIGHT_LUX_RAW) lightState = false;
  } else {
    lightState = false;   // save energy when nobody is studying
  }
  digitalWrite(LED_PIN, lightState ? HIGH : LOW);

  /* ---- Alerts: noise or poor air quality ---- */
  bool noisy   = soundRaw > NOISE_THRESHOLD;
  bool badAir  = airRaw   > AIR_THRESHOLD;
  digitalWrite(BUZZER_PIN, (noisy || badAir) ? HIGH : LOW);

  /* ---- Serial debug / would be pushed to Blynk/ThingSpeak/Firebase ---- */
  Serial.println(F("---------------------------------------------"));
  Serial.printf("Temp: %.1f C  | Humidity: %.1f %%\n", temperature, humidity);
  Serial.printf("Light(raw): %d  | Sound(raw): %d | Air(raw): %d\n", lightRaw, soundRaw, airRaw);
  Serial.printf("Occupancy: %s | Fan: %s | Light: %s | Buzzer: %s\n",
                occupied ? "PRESENT" : "EMPTY",
                fanState ? "ON" : "OFF",
                lightState ? "ON" : "OFF",
                (noisy || badAir) ? "ALERT" : "OFF");

  /* ---- OLED dashboard ---- */
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);

  // Row 1: Temperature & Humidity
  display.print("T:"); display.print(temperature, 1);
  display.print("C  H:"); display.print((int)humidity); display.println("%");

  // Row 2: Light & Sound
  display.print("Lgt:"); display.print(lightRaw);
  display.print(" Snd:"); display.println(soundRaw);

  // Row 3: Air & Occupancy
  display.print("Air:"); display.print(airRaw);
  display.print("  Occ:"); display.println(occupied ? "Y" : "N");

  // Divider
  display.println("-----------------");

  // Row 4: Fan & Lamp
  display.print("Fan:"); display.print(fanState ? "ON " : "OFF");
  display.print(" Lmp:"); display.println(lightState ? "ON" : "OFF");

  // Row 5: Alert
  display.print("Alert:"); display.println((noisy || badAir) ? "YES" : "NO ");

  display.display();
}
