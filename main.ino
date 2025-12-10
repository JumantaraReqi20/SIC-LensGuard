
/*********************************************************
 * IoT Smart Camera Box Monitor
 * ESP32 + DHT11 + Edge Impulse + MQTT + Telegram Alert
 *********************************************************/

#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <HTTPClient.h>

// ===== Edge Impulse Header =====
#include <SIC-LensGuard_inferencing.h>

// ====== WiFi & MQTT Config =======
const char* ssid = "bismillaah_dulu";
const char* password = "bismillaah";

const char* mqtt_server = "broker.hivemq.com";
const char* mqtt_topic = "sic/funtasticfour/lensguard/status";

WiFiClient espClient;
PubSubClient client(espClient);

// ====== Telegram Bot Config ======
String botToken = "8225694717:AAFy7IzwVEbzqCzOM4WmIGMGT239FIbIc1A";
String chatID   = "-4627709856";

// ====== Sensor Config ======
#define DHTPIN 4
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// #define BUZZER_PIN 13
#define LED_PIN 2

// Anti-spam alert
bool telegramSent = false;

// =========================================================
// WiFi Setup
// =========================================================
void setup_wifi() {
    delay(10);
    WiFi.begin(ssid, password);

    Serial.println("Menghubungkan WiFi...");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    Serial.println("\nWiFi Tersambung!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
}

// =========================================================
// MQTT Reconnect
// =========================================================
void reconnect() {
    while (!client.connected()) {
        Serial.print("Menghubungkan ke MQTT...");
        if (client.connect("ESP32_AI_Client")) {
            Serial.println("Berhasil!");
        } else {
            Serial.print("Gagal. rc=");
            Serial.print(client.state());
            Serial.println(" retry 5 detik...");
            delay(5000);
        }
    }
}

// =========================================================
// SETUP
// =========================================================
void setup() {
    Serial.begin(115200);

    // pinMode(BUZZER_PIN, OUTPUT);
    pinMode(LED_PIN, OUTPUT);
    // digitalWrite(BUZZER_PIN, LOW);
    digitalWrite(LED_PIN, LOW);

    dht.begin();

    Serial.print("Input width: ");
    Serial.println(EI_CLASSIFIER_INPUT_WIDTH);

    Serial.print("Input frame size: ");
    Serial.println(EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE);

    Serial.print("Axes: ");
    Serial.println(EI_CLASSIFIER_RAW_SAMPLES_PER_FRAME);

    setup_wifi();
    client.setServer(mqtt_server, 1883);
}

// =========================================================
// LOOP
// =========================================================
void loop() {
    if (!client.connected()) reconnect();
    client.loop();

    float temp = dht.readTemperature();
    float hum = dht.readHumidity();

    if (isnan(temp) || isnan(hum)) {
        Serial.println("Sensor tidak terbaca!");
        delay(3000);
        return;
    }

    Serial.println("=========================");
    Serial.print("Temp: ");
    Serial.print(temp);
    Serial.print(" | Hum: ");
    Serial.println(hum);

    // --------------- Edge Impulse Input ----------------
    float features[2] = { temp, hum };

    signal_t signal;
    signal.total_length = 2;
    signal.get_data = [&](size_t offset, size_t length, float* out_ptr) {
        memcpy(out_ptr, features + offset, length * sizeof(float));
        return 0;
    };

    ei_impulse_result_t result;
    EI_IMPULSE_ERROR ei_res = run_classifier(&signal, &result, false);

    if (ei_res != EI_IMPULSE_OK) {
        Serial.print("Inference error: ");
        Serial.println(ei_res);
        delay(2000);
        return;
    }

    // --------------- Ambil Label Terbaik ---------------
    Serial.println("=== Hasil AI ===");
    size_t best_i = 0;
    float best_score = result.classification[0].value;

    for (size_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
        Serial.print(result.classification[i].label);
        Serial.print(" : ");
        Serial.println(result.classification[i].value, 4);

        if (result.classification[i].value > best_score) {
            best_score = result.classification[i].value;
            best_i = i;
        }
    }

    const char* status_label = result.classification[best_i].label;

    // --------------- Logika Alarm ----------------
    bool alarmOn = false;

    alarmOn = (strcmp(status_label, "Bahaya") == 0 || strcmp(status_label, "Kritis") == 0);


    // digitalWrite(BUZZER_PIN, alarmOn ? HIGH : LOW);
    digitalWrite(LED_PIN, alarmOn ? HIGH : LOW);

    Serial.print("Status: ");
    Serial.println(status_label);


    // --------------- MQTT Publish ----------------
    char msg[250];
    sprintf(msg,
        "{\"temperature\":%.2f, \"humidity\":%.2f, \"status\":\"%s\", \"confidence\":%.3f}",
        temp, hum, status_label, best_score
    );

    client.publish(mqtt_topic, msg);

    delay(10000);
}
