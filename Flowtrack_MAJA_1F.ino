#include <WiFi.h>
#include <WiFiManager.h> // Tambahkan library WiFiManager
#include <ESPAsyncWebServer.h>
#include <PubSubClient.h>
#include <WiFiClient.h>
#include <Preferences.h>
#include <AsyncElegantOTA.h>

Preferences preferences;

volatile int flow_frequency = 0;
volatile int total_pulse = 0;
double vol = 0.0, flowrate;
double volume_per_pulse = 0.00342;
const uint8_t flowsensor = 34;
const uint8_t resetButton = 25;
const uint8_t ledPin = 32;
unsigned long currentTime;
unsigned long cloopTime;
bool wifiConnected = false;
unsigned long sendInterval = 1000;
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 25200;
const int   daylightOffset_sec = 0;

AsyncWebServer server(80);
WiFiClient espClient;
PubSubClient mqttClient(espClient);

char broker_url[50];
char mqtt_user[20] = "";
char mqtt_pass[20] = "";
const int mqtt_port = 1883;

// Hapus kredensial Wi-Fi yang di-hardcode
// const char* ssid = "HabibiGarden";
// const char* password = "Prodigy123";

const char* kebunId = "66f25fc7474c1c3ca90ec63e";
const char* deviceId = "66f26016474c1c3ca90ec642";

String dataTopic = "kebun/" + String(kebunId) + "/device/" + String(deviceId) + "/data";
String intervalTopic = "kebun/" + String(kebunId) + "/device/" + String(deviceId) + "/interval";
String flowKalTopic = "kebun/" + String(kebunId) + "/device/" + String(deviceId) + "/flowkal";
String firmVer = "1.0.0";
String deviceName = "Flowtrack Maja 1";
IPAddress local_IP(192, 168, 1, 66);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(8, 8, 8, 8);
IPAddress secondaryDNS(8, 8, 4, 4);

unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;

void IRAM_ATTR flow() {
  flow_frequency++;
  total_pulse++;
}

void setup() {
  Serial.begin(115200);

  preferences.begin("kalibrasi", false);

  // Mengambil nilai kalibrasi dari Preferences dengan key baru
  uint32_t calibrationMicro = preferences.getUInt("vol_pulse_int", 0);
  if (calibrationMicro == 0) {
    // Jika tidak ada nilai yang disimpan, gunakan nilai default dan simpan
    volume_per_pulse = 0.00342;
    calibrationMicro = (uint32_t)(volume_per_pulse * 1e8); // Konversi ke integer
    preferences.putUInt("vol_pulse_int", calibrationMicro);
    Serial.println("No calibration value found. Using default and saving.");
  } else {
    volume_per_pulse = calibrationMicro / 1e8; // Konversi kembali ke double
  }
  Serial.printf("Loaded calibration value: %.8f\n", volume_per_pulse);

  if (preferences.isKey("broker_url")) {
    preferences.getString("broker_url", broker_url, sizeof(broker_url));
  } else {
    strcpy(broker_url, "192.168.1.55");
    preferences.putString("broker_url", broker_url);
  }
  preferences.getString("mqtt_user", mqtt_user, sizeof(mqtt_user));
  preferences.getString("mqtt_pass", mqtt_pass, sizeof(mqtt_pass));

  Serial.print("Loaded MQTT Broker URL: ");
  Serial.println(broker_url);
  Serial.print("Loaded MQTT Username: ");
  Serial.println(mqtt_user);
  Serial.print("Loaded MQTT Password: ");
  Serial.println(mqtt_pass);

  // Konfigurasi Static IP jika diperlukan
  if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
    Serial.println("Failed to configure static IP");
  }

  // Inisialisasi WiFiManager
  WiFiManager wifiManager;
  //wifiManager.resetSettings(); //uncomennt if u will rmv wifi conf

  // AutoConnect akan mencoba terhubung menggunakan kredensial yang tersimpan.
  // Jika gagal, akan memulai portal konfigurasi Wi-Fi.
  wifiManager.autoConnect("Flowtrack_MAJA_1", "HGPro123");
  wifiConnected = WiFi.status() == WL_CONNECTED;

  if (wifiConnected) {
    Serial.println("Connected to WiFi!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Failed to connect to WiFi.");
  }

  mqttClient.setServer(broker_url, mqtt_port);
  mqttClient.setCallback(mqttCallback);
  if (wifiConnected) {
    connectToMqtt();
  }
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  //  printLocalTime();
  setupServer();

  pinMode(flowsensor, INPUT_PULLUP);
  pinMode(resetButton, INPUT_PULLUP);
  pinMode(ledPin, OUTPUT);

  attachInterrupt(digitalPinToInterrupt(flowsensor), flow, RISING);
  currentTime = millis();
  cloopTime = currentTime;
}

void setupServer() {
  // Endpoint untuk halaman dashboard
  server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
    String html = "<!DOCTYPE html><html><head><title>Flowtrack Dashboard</title>";
    html += "<style>";
    html += "body { font-family: Arial, sans-serif; background: linear-gradient(to bottom, #6dd5fa, #2980b9); color: #ffffff; margin: 0; padding: 0; display: flex; flex-direction: column; align-items: center; justify-content: center; height: 100vh; }";
    html += ".container { max-width: 90%; width: 400px; background: rgba(255, 255, 255, 0.9); color: #2980b9; padding: 20px; border-radius: 10px; box-shadow: 0 4px 10px rgba(0, 0, 0, 0.2); }";
    html += "h1 { text-align: center; margin: 0 0 20px; }";
    html += ".status { display: flex; justify-content: space-between; font-weight: bold; margin-bottom: 15px; }";
    html += ".data-section, .calibration, .mqtt-settings { margin-top: 15px; }";
    html += "input, button { width: calc(100% - 20px); padding: 10px; margin-top: 10px; border: none; border-radius: 5px; box-shadow: 0px 3px 6px rgba(0, 0, 0, 0.1); }";
    html += "button { background-color: #2980b9; color: white; cursor: pointer; transition: background-color 0.3s; }";
    html += "button:hover { background-color: #1f6391; }";
    html += "@media screen and (max-width: 600px) { .container { width: 90%; } }";
    html += "</style></head><body>";
    html += "<div class='container'>";
    html += "<h1>Flowtrack Dashboard</h1>";
    html += "<div class='status'>";
    html += "<p>WiFi: <span id='wifi-status'>Checking...</span></p>";
    html += "<p>MQTT: <span id='mqtt-status'>Checking...</span></p>";
    html += "</div>";
    html += "<div class='data-section'>";
    html += "<h3>Flow Data</h3>";
    html += "<p>Flow Rate: <span id='flowrate'>0</span> L/min</p>";
    html += "<p>Volume: <span id='volume'>0</span> L</p>";
    html += "<p>Total Pulse: <span id='total_pulse'>0</span></p>";
    html += "<p>Calibration (Current): <span id='calibrationValue'>0.000000</span> L/pulse</p>";
    html += "</div>";
    html += "<div class='calibration'>";
    html += "<h3>Calibration</h3>";
    html += "<input type='number' id='realVolume' placeholder='Real Volume (L)' />";
    html += "<button onclick='calculateCalibration()'>Calculate Calibration</button>";
    html += "<p>Result: <span id='calibrationResult'>-</span> L/pulse</p>";
    html += "<button onclick='saveCalibration()'>Save Calibration</button>";
    html += "</div>";
    html += "<div class='mqtt-settings'>";
    html += "<h3>MQTT Settings</h3>";
    html += "<p>Broker URL: <span id='broker_url'></span></p>";
    html += "<input type='text' id='newBroker' placeholder='New Broker URL' />";
    html += "<input type='text' id='mqttUser' placeholder='Username' />";
    html += "<input type='password' id='mqttPass' placeholder='Password' />";
    html += "<button onclick='updateBroker()'>Update MQTT Settings</button>";
    html += "</div>";
    html += "<div class='wifi-settings'>";
    html += "<h3>WiFi Settings</h3>";
    html += "<button onclick='resetWiFi()'>Reset WiFi Configuration</button>";
    html += "</div>";
    html += "<script>";
    html += "function fetchData() {";
    html += "fetch('/status').then(response => response.json()).then(data => {";
    html += "document.getElementById('wifi-status').innerText = data.wifi ? 'Connected' : 'Disconnected';";
    html += "document.getElementById('mqtt-status').innerText = data.mqtt ? 'Connected' : 'Disconnected';";
    html += "document.getElementById('flowrate').innerText = data.flowrate;";
    html += "document.getElementById('volume').innerText = data.volume;";
    html += "document.getElementById('total_pulse').innerText = data.total_pulse;";
    html += "document.getElementById('calibrationValue').innerText = data.calibrationValue;";
    html += "document.getElementById('broker_url').innerText = data.broker_url;";
    html += "});";
    html += "}";
    html += "function calculateCalibration() {";
    html += "const realVolume = parseFloat(document.getElementById('realVolume').value);";
    html += "const totalPulse = parseFloat(document.getElementById('total_pulse').innerText);";
    html += "if (realVolume > 0 && totalPulse > 0) {";
    html += "const result = (realVolume / totalPulse).toFixed(6);";
    html += "document.getElementById('calibrationResult').innerText = result + ' L/pulse';";
    html += "} else { document.getElementById('calibrationResult').innerText = 'Invalid input'; }";
    html += "}";
    html += "function saveCalibration() {";
    html += "const result = document.getElementById('calibrationResult').innerText.split(' ')[0];";
    html += "if (result && !isNaN(result)) {";
    html += "fetch(`/save-calibration?value=${result}`)";
    html += "  .then(response => response.text())";
    html += "  .then(data => {";
    html += "    alert('Calibration saved: ' + result + ' L/pulse');";
    html += "  })";
    html += "  .catch(error => console.error('Error saving calibration:', error));";
    html += "} else {";
    html += "  alert('Invalid calibration value.');";
    html += "}";
    html += "}";
    html += "function updateBroker() {";
    html += "const broker = document.getElementById('newBroker').value;";
    html += "const user = document.getElementById('mqttUser').value;";
    html += "const pass = document.getElementById('mqttPass').value;";
    html += "fetch(`/update-broker?url=${broker}&user=${user}&pass=${pass}`);";
    html += "alert('Broker updated');";
    html += "}";
    html += "function resetWiFi() {";
    html += "if (confirm('Are you sure you want to reset WiFi configuration?')) {";
    html += "fetch('/reset_wifi', { method: 'POST' }).then(response => {";
    html += "if (response.ok) {";
    html += "alert('WiFi configuration reset successfully. Device will restart.');";
    html += "} else {";
    html += "alert('Failed to reset WiFi configuration.');";
    html += "}";
    html += "}).catch(error => console.error('Error:', error));";
    html += "}";
    html += "}";
    html += "setInterval(fetchData, 2000);";
    html += "</script>";
    html += "</body></html>";
    request->send(200, "text/html", html);
  });

  // Endpoint untuk reset WiFi menggunakan POST
  server.on("/reset_wifi", HTTP_POST, [](AsyncWebServerRequest * request) {
    WiFiManager wifiManager;
    wifiManager.resetSettings();
    request->send(200, "text/plain", "WiFi settings have been reset. The device will restart.");
    delay(1000);
    ESP.restart();
  });


  server.on("/save-calibration", HTTP_GET, [](AsyncWebServerRequest * request) {
    if (request->hasParam("value")) {
      String value = request->getParam("value")->value();
      Serial.print("Calibration value received: ");
      Serial.println(value);

      // Konversi nilai ke double
      double newCalibration = value.toDouble();
      if (newCalibration > 0 && newCalibration <= 1.0) {
        volume_per_pulse = newCalibration;

        // Konversi nilai kalibrasi ke integer micro units
        uint32_t calibrationMicro = (uint32_t)(volume_per_pulse * 1e8); // Kalikan dengan 100,000,000

        // Simpan nilai ke Preferences dengan key baru
        preferences.putUInt("vol_pulse_int", calibrationMicro);
        Serial.printf("Saving calibration value: %.8f (stored as %u)\n", volume_per_pulse, calibrationMicro);

        // Verifikasi nilai yang disimpan
        uint32_t storedCalibrationMicro = preferences.getUInt("vol_pulse_int", 0xFFFFFFFF);
        if (storedCalibrationMicro == calibrationMicro) {
          request->send(200, "text/plain", "Calibration saved successfully");
          Serial.printf("Verified calibration value saved: %u\n", storedCalibrationMicro);
        } else {
          request->send(500, "text/plain", "Failed to save calibration");
          Serial.printf("Error: Stored value %u does not match input value %u!\n", storedCalibrationMicro, calibrationMicro);
        }
      } else {
        request->send(400, "text/plain", "Invalid calibration value.");
        Serial.println("Calibration value must be greater than 0 and less than or equal to 1.");
      }
    } else {
      request->send(400, "text/plain", "Missing calibration value");
      Serial.println("Missing calibration value in the request.");
    }
  });

  server.on("/update-broker", HTTP_GET, [](AsyncWebServerRequest * request) {
    if (request->hasParam("url") && request->hasParam("user") && request->hasParam("pass")) {
      String url = request->getParam("url")->value();
      String user = request->getParam("user")->value();
      String pass = request->getParam("pass")->value();
      if (url.length() < sizeof(broker_url)) {
        url.toCharArray(broker_url, sizeof(broker_url));
        user.toCharArray(mqtt_user, sizeof(mqtt_user));
        pass.toCharArray(mqtt_pass, sizeof(mqtt_pass));
        preferences.putString("broker_url", broker_url);
        preferences.putString("mqtt_user", mqtt_user);
        preferences.putString("mqtt_pass", mqtt_pass);
        mqttClient.setServer(broker_url, mqtt_port);
        connectToMqtt();
        request->send(200, "text/plain", "Broker updated");
      } else {
        request->send(400, "text/plain", "Broker URL is too long");
      }
    } else {
      request->send(400, "text/plain", "Missing parameters");
    }
  });

  server.on("/status", HTTP_GET, [](AsyncWebServerRequest * request) {
    String calibrationValueStr = String(volume_per_pulse, 8);
    String json = "{\"wifi\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") +
                  ",\"mqtt\":" + String(mqttClient.connected() ? "true" : "false") +
                  ",\"flowrate\":" + String(flowrate) +
                  ",\"volume\":" + String(vol) +
                  ",\"total_pulse\":" + String(total_pulse) +
                  ",\"calibrationValue\":" + calibrationValueStr +
                  ",\"broker_url\":\"" + String(broker_url) + "\"}";
    request->send(200, "application/json", json);
  });




  server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(204); // Tidak ada konten
  });

  AsyncElegantOTA.begin(&server, "admin", "izinupdate");

  server.begin();
}

void loop() {
  checkWiFiAndReconnect();
  checkMqttAndReconnect();
  // mqttClient.loop(); // Dipanggil dalam checkMqttAndReconnect()

  if (wifiConnected && mqttClient.connected()) {
    digitalWrite(ledPin, HIGH);
  } else {
    digitalWrite(ledPin, millis() % 400 < 200 ? HIGH : LOW);
  }

  // Penanganan tombol reset dengan debounce
  static bool lastButtonState = HIGH;
  bool currentButtonState = digitalRead(resetButton);

  if (currentButtonState != lastButtonState) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (currentButtonState == LOW && lastButtonState == HIGH) {
      vol = 0.0;
      noInterrupts();
      total_pulse = 0;
      interrupts();
      Serial.println("Volume and Total Pulse reset to 0.");
    }
  }
  lastButtonState = currentButtonState;

  currentTime = millis();
  if ((unsigned long)(currentTime - cloopTime) >= sendInterval) {
    cloopTime = currentTime;

    // Akses variabel bersama dengan aman
    noInterrupts();
    int freq = flow_frequency;
    flow_frequency = 0;
    int total = total_pulse;
    interrupts();

    if (freq != 0) {
      flowrate = (freq * volume_per_pulse * 60) / (sendInterval / 1000.0);
      vol += (freq * volume_per_pulse);
    } else {
      flowrate = 0;
    }


    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
      Serial.println("Failed to obtain time");
      return;
    }
    char timestamp[30];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S WIB", &timeinfo);
    Serial.println(timestamp);
    //    String dataPayload = "{\"flowrate\":" + String(flowrate) + ",\"volume\":" + String(vol) + ",\"pulses\":" + String(freq) + ",\"total_pulse\":" + String(total) + "}";
    String dataPayload = "{"
                         "\"info\": {"
                         "\"device_name\": \"" + deviceName + "\","
                         //                                                      "\"firm_ver\": \"" + firmVer + "\","
                         //                             "\"kode_kebun\": \"" + kebunId + "\","
                         //                             "\"mqtt\": \"" + broker_url + "\","
                         "\"wifi_name\": \"" + WiFi.SSID() + "\","
                         //                             "\"wifi_pass\": \"" + password + "\","
                         "\"wifi_signal\": " + String(WiFi.RSSI()) + ","
                         //                         "\"ip\": \"" + WiFi.localIP().toString() + "\","
                         //                         "\"mac\": \"" + WiFi.macAddress() + "\","
                         //                             "\"GMT\": \"" + gmt + "\","
                         //                             "\"date\": \"" + dayStamp + "\","
                         //                             "\"time\": \"" + timeStamp + "\""
                         "},"
                         "\"sensor\": {"
                         "\"volume\": " + String(vol) + ","
                         "\"flowRate\": " + String(flowrate) + ","
                         "\"timestamp\": \"" + String(timestamp) + "\""
                         "}"
                         "}";
    if (mqttClient.connected()) {
      mqttClient.publish(dataTopic.c_str(), dataPayload.c_str());
      Serial.print("Published to MQTT - ");
    } else {
      Serial.print("MQTT not connected - ");
    }

    Serial.print("Flow Rate: ");
    Serial.print(flowrate);
    Serial.print(" L/M, Volume: ");
    Serial.print(vol);
    Serial.print(" L, Pulses: ");
    Serial.print(freq);
    Serial.print(", Total Pulse: ");
    Serial.println(total);
  }
  delay(10);
}

void checkWiFiAndReconnect() {
  if (WiFi.status() != WL_CONNECTED) {
    WiFiManager wifiManager;
    wifiManager.autoConnect("Flowtrack_AP", "12345678");
    wifiConnected = WiFi.status() == WL_CONNECTED;

    if (wifiConnected) {
      Serial.println("Reconnected to WiFi!");
      Serial.print("IP Address: ");
      Serial.println(WiFi.localIP());
    } else {
      Serial.println("Failed to reconnect to WiFi.");
    }
  }
}

void checkMqttAndReconnect() {
  static unsigned long lastMqttReconnectAttempt = 0;
  unsigned long now = millis();
  if (!mqttClient.connected() && wifiConnected) {
    if (now - lastMqttReconnectAttempt > 5000) { // Coba setiap 5 detik
      lastMqttReconnectAttempt = now;
      connectToMqtt();
    }
  } else if (mqttClient.connected()) {
    mqttClient.loop();
  }
}

bool connectToMqtt() {
  Serial.print("Connecting to MQTT...");
  bool connected = strlen(mqtt_user) > 0 && strlen(mqtt_pass) > 0
                   ? mqttClient.connect("ESP32Client", mqtt_user, mqtt_pass)
                   : mqttClient.connect("ESP32Client");
  if (connected) {
    Serial.println("connected");
    mqttClient.subscribe(intervalTopic.c_str());
    mqttClient.subscribe(flowKalTopic.c_str());
  } else {
    Serial.print("failed, rc=");
    Serial.print(mqttClient.state());
    Serial.println(" Will try again in 5 seconds");
  }
  return connected;
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String messageTemp;
  for (unsigned int i = 0; i < length; i++) {
    messageTemp += (char)payload[i];
  }
  Serial.print("Message arrived on topic: ");
  Serial.print(topic);
  Serial.print(". Message: ");
  Serial.println(messageTemp);

  if (String(topic) == intervalTopic) {
    unsigned long newInterval = messageTemp.toInt() * 1000;
    if (newInterval > 0) {
      sendInterval = newInterval;
      Serial.print("Updated send interval to: ");
      Serial.print(sendInterval / 1000);
      Serial.println(" seconds");
    }
  } else if (String(topic) == flowKalTopic) {
    double newCalibration = messageTemp.toDouble();
    if (newCalibration > 0 && newCalibration <= 1.0) {
      volume_per_pulse = newCalibration;
      uint32_t calibrationMicro = (uint32_t)(volume_per_pulse * 1e8);
      preferences.putUInt("vol_pulse_int", calibrationMicro);
      Serial.print("Updated volume_per_pulse for calibration and saved to flash: ");
      Serial.println(volume_per_pulse, 8);
    } else {
      Serial.println("Received invalid calibration value.");
    }
  }
}
