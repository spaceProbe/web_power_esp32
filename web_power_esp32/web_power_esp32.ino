// Wifi and OTA setup
#include <WiFi.h>
#include <Preferences.h>
#include <vector>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <time.h> 

// I2C Reading
#include <Wire.h>
#include <Adafruit_ADS1X15.h>

// --- NVS Storage ---
Preferences preferences;

// --- I2C Reading ---
Adafruit_ADS1115 ads; 
const float maxVoltage = 3.5;
const float refVoltage = 3.3;

// --- Wi-Fi Reconnection Timer Variables ---
unsigned long previousWifiMillis = 0; 
const long wifiInterval = 12000; 
bool wifiWasConnected = false;   
int currentWifiIndex = -1;
int attemptsInCurrentCycle = 0;  
bool apActive = false;           

// --- Saved Networks Structure ---
struct WiFiCred {
  String ssid;
  String pass;
};
std::vector<WiFiCred> savedNetworks;

// --- Battery Logging Structures ---
struct BatteryLogEntry {
  time_t timestamp;
  float percentage;
};
std::vector<BatteryLogEntry> shortTermLogs;
unsigned long lastShortTermLogMillis = 0;
unsigned long lastLongTermLogMillis = 0;
const size_t MAX_LOGS = 60; 
bool firstLogAdded = false;

// --- ADC Variables ---
unsigned long lastADCMillis = 0;
const long adcInterval = 5000; 
float currentBatteryPercentage = 0.0;

// --- Schedule & Smart Mode Variables ---
struct SchedEvent {
  uint8_t h;
  uint8_t m;
  uint8_t action; // 0=OFF, 1=MIN, 2=LOW, 3=MED, 4=HIGH, 5=SMART
};
std::vector<SchedEvent> schedule;
bool scheduleEnabled = false;
bool scheduleOverride = false;
int lastScheduledAction = -1;
unsigned long lastSchedCheckMillis = 0;

bool smartModeActive = false;

// --- Web Server Setup ---
WiFiServer server(80);
String header;

// --- Button Debounce Variables ---
unsigned long lastButtonMillis = 0;
const long debounceTime = 250; 

// --- Lamp Constants & Variables ---
const int pwmPin = 4;       
const int bootButtonPin = 9; 
const int highPWM = 255;
const int medPWM = 150;
const int lowPWM = 50;
const int minPWM = 10;

int currentPWM = 0;
int pwmOveride = 0;      
int lightState = 0;      
int lastLightState = 4;  
int printNow = 0;
unsigned long currentTime = millis();
unsigned long previousTime = 0; 
const long timeoutTime = 2000;

// --- Helper Functions ---

unsigned char h2int(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return 0;
}

String urldecode(String str) {
  String decoded = "";
  for (int i = 0; i < str.length(); i++) {
    if (str[i] == '%') {
      if (i + 2 < str.length()) {
        decoded += (char)((h2int(str[i+1]) << 4) | h2int(str[i+2]));
        i += 2;
      }
    } else if (str[i] == '+') {
      decoded += ' ';
    } else {
      decoded += str[i];
    }
  }
  return decoded;
}

String getActionName(int a) {
  switch(a) {
    case 0: return "Turn OFF";
    case 1: return "Power: MIN";
    case 2: return "Power: LOW";
    case 3: return "Power: MED";
    case 4: return "Power: HIGH";
    case 5: return "Battery Smart Mode";
    default: return "Unknown";
  }
}

// --- NVS Storage Functions ---
void loadNetworks() {
  savedNetworks.clear();
  preferences.begin("wifi_creds", true); 
  int count = preferences.getInt("count", 0);
  for (int i = 0; i < count; i++) {
    String sKey = "s_" + String(i);
    String pKey = "p_" + String(i);
    String s = preferences.getString(sKey.c_str(), "");
    String p = preferences.getString(pKey.c_str(), "");
    if (s != "") savedNetworks.push_back({s, p});
  }
  preferences.end();
}

void saveNetworks() {
  preferences.begin("wifi_creds", false); 
  preferences.clear(); 
  preferences.putInt("count", savedNetworks.size());
  for (int i = 0; i < savedNetworks.size(); i++) {
    String sKey = "s_" + String(i);
    String pKey = "p_" + String(i);
    preferences.putString(sKey.c_str(), savedNetworks[i].ssid);
    preferences.putString(pKey.c_str(), savedNetworks[i].pass);
  }
  preferences.end();
}

void loadSettings() {
  preferences.begin("sys_settings", true);
  smartModeActive = preferences.getBool("smart_mode", false);
  scheduleEnabled = preferences.getBool("sched_en", false);
  preferences.end();
}

void saveSettings() {
  preferences.begin("sys_settings", false);
  preferences.putBool("smart_mode", smartModeActive);
  preferences.putBool("sched_en", scheduleEnabled);
  preferences.end();
}

void loadSchedule() {
  preferences.begin("sched_data", true);
  size_t len = preferences.getBytesLength("events");
  if(len > 0 && len % sizeof(SchedEvent) == 0) {
    schedule.resize(len / sizeof(SchedEvent));
    preferences.getBytes("events", schedule.data(), len);
  }
  preferences.end();
}

void saveSchedule() {
  preferences.begin("sched_data", false);
  preferences.putBytes("events", schedule.data(), schedule.size() * sizeof(SchedEvent));
  preferences.end();
}

// --- Fountain Control Logic ---
void applyLightState() {
  if (pwmOveride == 0) {
    if (lightState == 1) currentPWM = minPWM;
    else if (lightState == 2) currentPWM = lowPWM;
    else if (lightState == 3) currentPWM = medPWM;
    else if (lightState == 4) currentPWM = highPWM;
    else currentPWM = 0; 
    analogWrite(pwmPin, currentPWM);
  }
}

void triggerManualOverride() {
  scheduleOverride = true; 
  if (smartModeActive) {
    smartModeActive = false;
    saveSettings();
  }
  Serial.println("Manual override triggered.");
}

void playWifiConnectSequence() {
  for (int pulse = 0; pulse < 2; pulse++) {
    for (int i = 0; i <= 255; i += 15) { analogWrite(pwmPin, i); delay(10); }
    for (int i = 255; i >= 0; i -= 15) { analogWrite(pwmPin, i); delay(10); }
    analogWrite(pwmPin, 0); delay(100); 
  }
  delay(200); 
  for (int i = 0; i <= currentPWM; i += 5) { analogWrite(pwmPin, i); delay(15); }
  analogWrite(pwmPin, currentPWM); 
}

// --- Morse Code Helpers ---
String digitToMorse(char digit) {
  switch (digit) {
    case '0': return "-----"; case '1': return ".----"; case '2': return "..---";
    case '3': return "...--"; case '4': return "....-"; case '5': return ".....";
    case '6': return "-...."; case '7': return "--..."; case '8': return "---..";
    case '9': return "----."; default: return "";
  }
}

void blinkMorse(String morseStr) {
  int dotDuration = 200; 
  analogWrite(pwmPin, 0);
  delay(dotDuration);
  for (int i = 0; i < morseStr.length(); i++) {
    if (morseStr[i] == '.') { analogWrite(pwmPin, medPWM); delay(dotDuration); } 
    else if (morseStr[i] == '-') { analogWrite(pwmPin, medPWM); delay(dotDuration * 3); }
    analogWrite(pwmPin, 0); delay(dotDuration); 
  }
}

void playIPMorse() {
  IPAddress ip = WiFi.localIP();
  String lastOctet = String(ip[3]);
  delay(2000); 
  for (int i = 0; i < lastOctet.length(); i++) {
    blinkMorse(digitToMorse(lastOctet[i]));
    if (i < lastOctet.length() - 1) delay(1000); 
  }
  for (int i = 0; i <= currentPWM; i += 5) { analogWrite(pwmPin, i); delay(15); }
  analogWrite(pwmPin, currentPWM);
}

// --- OTA Setup ---
void setupOTA() {
  ArduinoOTA.setHostname("fountain");
  ArduinoOTA.setPassword("admin"); 
  ArduinoOTA.onStart([]() { Serial.println("Start updating"); });
  ArduinoOTA.onEnd([]() { Serial.println("\nEnd"); });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) { Serial.printf("Progress: %u%%\r", (progress / (total / 100))); });
  ArduinoOTA.begin();
}

// --- Battery Logging Helpers ---
bool stringParse(String data, std::vector<BatteryLogEntry>& logs) {
  if (data == "") return false;
  logs.clear();
  int start = 0;
  int end = data.indexOf(';');
  while (end != -1) {
    String segment = data.substring(start, end);
    int comma = segment.indexOf(',');
    if (comma != -1) {
      time_t ts = (time_t)segment.substring(0, comma).toInt();
      float p = segment.substring(comma + 1).toFloat();
      logs.push_back({ts, p});
    }
    start = end + 1;
    end = data.indexOf(';', start);
  }
  return true;
}

void addBatteryLog(float percentage) {
  time_t now;
  time(&now); 
  shortTermLogs.push_back({now, percentage});
  while (shortTermLogs.size() > MAX_LOGS) { shortTermLogs.erase(shortTermLogs.begin()); }
}

void saveBatteryLogs() {
  preferences.begin("bat_logs", false);
  String stStr = "";
  for (auto const& entry : shortTermLogs) {
    stStr += String((unsigned long)entry.timestamp) + "," + String(entry.percentage) + ";";
  }
  preferences.putString("st_logs", stStr);
  preferences.end();
}

void loadBatteryLogs() {
  preferences.begin("bat_logs", true);
  String stStr = preferences.getString("st_logs", "");
  stringParse(stStr, shortTermLogs);
  preferences.end();
}

void setup() {
  Serial.begin(115200);
  delay(100);
  pinMode(bootButtonPin, INPUT_PULLUP);
  pinMode(pwmPin, OUTPUT);
  analogWrite(pwmPin, currentPWM); 
  
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  setenv("TZ", "CST6CDT,M3.2.0,M11.1.0", 1);
  tzset();
  
  loadNetworks();
  loadSettings();
  loadSchedule();
  
  previousWifiMillis = millis() - wifiInterval; 
  
  if (savedNetworks.size() == 0) {
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP("Fountain Setup", "password123"); 
    apActive = true;
  } else {
    WiFi.mode(WIFI_STA);
    apActive = false;
  }
  
  ads.setGain(GAIN_ONE); 
  delay(200);
  ads.begin();
  
  setupOTA();
  server.begin(); 
  loadBatteryLogs();
}

void loop() {
  ArduinoOTA.handle();
  unsigned long now = millis();
  
  // ---------------------------------------------------------
  // 1. Handle Physical Button Input 
  // ---------------------------------------------------------
  int buttonState = digitalRead(bootButtonPin);
  if (buttonState == LOW && (now - lastButtonMillis > debounceTime)) {
    lastButtonMillis = now; 
    triggerManualOverride();
    lightState++;
    if (lightState > 4) lightState = 0;
    if (lightState != 0) lastLightState = lightState;
    pwmOveride = 0; printNow = 1;
    applyLightState();
  }

  // ---------------------------------------------------------
  // 2. Handle Schedule State Machine (Catch-Up Logic)
  // ---------------------------------------------------------
  if (now - lastSchedCheckMillis >= 5000) {
    lastSchedCheckMillis = now;
    
    if (scheduleEnabled && !schedule.empty()) {
      time_t t; time(&t);
      struct tm* ti = localtime(&t);
      
      if (ti->tm_year > 100) { 
        int curMins = ti->tm_hour * 60 + ti->tm_min;
        int targetAction = -1;
        int maxMins = -1;
        int wrapAction = -1;
        int wrapMins = -1;

        for(auto& ev : schedule) {
            int evMins = ev.h * 60 + ev.m;
            if(evMins <= curMins && evMins > maxMins) { maxMins = evMins; targetAction = ev.action; }
            if(evMins > wrapMins) { wrapMins = evMins; wrapAction = ev.action; }
        }
        if(targetAction == -1) targetAction = wrapAction;

        if(targetAction != lastScheduledAction) {
            lastScheduledAction = targetAction;
            scheduleOverride = false; 
            Serial.print("New schedule period triggered: Action "); Serial.println(targetAction);
        }

        if(!scheduleOverride) {
            if(currentBatteryPercentage <= 15.0) {
                if(lightState != 0 || smartModeActive || pwmOveride != 0) {
                    smartModeActive = false;
                    lightState = 0; pwmOveride = 0; applyLightState(); printNow = 1;
                    Serial.println("Schedule paused: Battery too low. Forced OFF.");
                }
            } else {
                if(targetAction == 5) {
                    if(!smartModeActive) { smartModeActive = true; saveSettings(); printNow = 1; }
                } else if (targetAction >= 0 && targetAction <= 4) {
                    if(smartModeActive || lightState != targetAction || pwmOveride != 0) {
                        smartModeActive = false;
                        lightState = targetAction; pwmOveride = 0; applyLightState(); printNow = 1;
                    }
                }
            }
        }
      }
    }
  }

  // ---------------------------------------------------------
  // 3. Handle ADC Reading & Smart Power Management
  // ---------------------------------------------------------
  if (now - lastADCMillis >= adcInterval) {
    lastADCMillis = now;
    int16_t adc0 = ads.readADC_SingleEnded(0);
    float voltage = ads.computeVolts(adc0);
    currentBatteryPercentage = (voltage / maxVoltage) * 100.0;
    if (currentBatteryPercentage > 100.0) currentBatteryPercentage = 100.0;
    if (currentBatteryPercentage < 0) currentBatteryPercentage = 0;
    
    if (!firstLogAdded) {
      addBatteryLog(currentBatteryPercentage);
      firstLogAdded = true;
    }

    if (smartModeActive && currentBatteryPercentage > 0) {
      int desiredState = 0;
      if (currentBatteryPercentage > 80.0) desiredState = 4;
      else if (currentBatteryPercentage > 50.0) desiredState = 3;
      else if (currentBatteryPercentage > 30.0) desiredState = 2;
      else if (currentBatteryPercentage > 15.0) desiredState = 1;
      else desiredState = 0; 

      if (lightState != desiredState || pwmOveride != 0) {
        lightState = desiredState;
        pwmOveride = 0;
        applyLightState();
        printNow = 1;
      }
    }
  }

  // ---------------------------------------------------------
  // 4. Handle Battery Logging
  // ---------------------------------------------------------
  if (now - lastShortTermLogMillis >= 300000) { 
    addBatteryLog(currentBatteryPercentage);
    lastShortTermLogMillis = now;
  }
  if (now - lastLongTermLogMillis >= 900000) { 
    saveBatteryLogs();
    lastLongTermLogMillis = now;
  }

  // ---------------------------------------------------------
  // 5. Handle Serial Input
  // ---------------------------------------------------------
  if (Serial.available() > 0) {
    int input = Serial.parseInt();
    while (Serial.available() > 0) Serial.read(); 
    if (input >= 0 && input <= 255) {
      triggerManualOverride();
      pwmOveride = 1; currentPWM = input;
      analogWrite(pwmPin, currentPWM); printNow = 1;
    }
  }

  // ---------------------------------------------------------
  // 6. Handle Wi-Fi Connection
  // ---------------------------------------------------------
  if (WiFi.status() != WL_CONNECTED) {
    wifiWasConnected = false;
    if (now - previousWifiMillis >= wifiInterval) {
      previousWifiMillis = now; 
      if (savedNetworks.size() > 0) {
        currentWifiIndex++;
        if (currentWifiIndex >= savedNetworks.size()) currentWifiIndex = 0;
        
        attemptsInCurrentCycle++;
        if (attemptsInCurrentCycle > savedNetworks.size() && !apActive) {
          WiFi.mode(WIFI_AP_STA);
          WiFi.softAP("Fountain Setup", "password123"); 
          apActive = true;
        }
        String trySsid = savedNetworks[currentWifiIndex].ssid;
        String tryPass = savedNetworks[currentWifiIndex].pass;
        WiFi.disconnect(); WiFi.begin(trySsid.c_str(), tryPass.c_str()); 
      }
    }
  } else if (!wifiWasConnected) {
    wifiWasConnected = true;
    attemptsInCurrentCycle = 0;
    playWifiConnectSequence(); playIPMorse();
    if (apActive) { WiFi.mode(WIFI_STA); apActive = false; }
  }

  // ---------------------------------------------------------
  // 7. Handle Web Server Requests 
  // ---------------------------------------------------------
  WiFiClient client = server.available();
  if (client) {
    currentTime = millis(); previousTime = currentTime;
    String currentLine = ""; 
    while (client.connected() && currentTime - previousTime <= timeoutTime) {
      currentTime = millis();
      if (client.available()) {
        char c = client.read();
        header += c;
        if (c == '\n') {
          if (currentLine.length() == 0) {
            
            if (header.indexOf("GET /add_wifi") >= 0) {
              int sStart = header.indexOf("s=") + 2; int sEnd = header.indexOf("&", sStart);
              int pStart = header.indexOf("p=") + 2; int pEnd = header.indexOf(" HTTP", pStart);
              String newSsid = urldecode(header.substring(sStart, sEnd));
              String newPass = urldecode(header.substring(pStart, pEnd));
              savedNetworks.push_back({newSsid, newPass});
              saveNetworks();
              client.print("HTTP/1.1 200 OK\r\nContent-type:application/json\r\nConnection: close\r\n\r\n{\"status\":\"ok\"}");
              previousWifiMillis = millis() - wifiInterval; 
            } 
            else if (header.indexOf("GET /delete_wifi") >= 0) {
              int iStart = header.indexOf("index=") + 6; int iEnd = header.indexOf(" HTTP", iStart);
              int idx = header.substring(iStart, iEnd).toInt();
              if (idx >= 0 && idx < savedNetworks.size()) { savedNetworks.erase(savedNetworks.begin() + idx); saveNetworks(); }
              client.print("HTTP/1.1 200 OK\r\nContent-type:application/json\r\nConnection: close\r\n\r\n{\"status\":\"ok\"}");
            } 
            else if (header.indexOf("GET /add_schedule") >= 0) {
              int hStart = header.indexOf("h=") + 2; int hEnd = header.indexOf("&m", hStart);
              int mStart = header.indexOf("m=") + 2; int mEnd = header.indexOf("&a", mStart);
              int aStart = header.indexOf("a=") + 2; int aEnd = header.indexOf(" HTTP", aStart);
              uint8_t h = header.substring(hStart, hEnd).toInt();
              uint8_t m = header.substring(mStart, mEnd).toInt();
              uint8_t a = header.substring(aStart, aEnd).toInt();
              schedule.push_back({h, m, a});
              saveSchedule();
              client.print("HTTP/1.1 200 OK\r\nContent-type:application/json\r\nConnection: close\r\n\r\n{\"status\":\"ok\"}");
            }
            else if (header.indexOf("GET /delete_schedule") >= 0) {
              int iStart = header.indexOf("index=") + 6; int iEnd = header.indexOf(" HTTP", iStart);
              int idx = header.substring(iStart, iEnd).toInt();
              if (idx >= 0 && idx < schedule.size()) { schedule.erase(schedule.begin() + idx); saveSchedule(); }
              client.print("HTTP/1.1 200 OK\r\nContent-type:application/json\r\nConnection: close\r\n\r\n{\"status\":\"ok\"}");
            }
            else if (header.indexOf("GET /toggle_schedule") >= 0) {
              scheduleEnabled = !scheduleEnabled;
              saveSettings();
              client.print("HTTP/1.1 200 OK\r\nContent-type:application/json\r\nConnection: close\r\n\r\n{\"status\":\"ok\"}");
            }
            else if (header.indexOf("GET /data_history HTTP") >= 0) {
              String json = "[";
              for (size_t i = 0; i < shortTermLogs.size(); i++) {
                json += "{\"ts\":" + String((unsigned long)shortTermLogs[i].timestamp) + ",\"p\":" + String(shortTermLogs[i].percentage) + "}";
                if (i < shortTermLogs.size() - 1) json += ",";
              }
              json += "]";
              client.print("HTTP/1.1 200 OK\r\nContent-type:application/json\r\nConnection: close\r\n\r\n");
              client.print(json);
            }
            else if (header.indexOf("GET /lamp/") >= 0) {
              triggerManualOverride(); 
              
              if (header.indexOf("/smart") >= 0) {
                smartModeActive = !smartModeActive;
                saveSettings();
                if (smartModeActive) { 
                  int desiredState = 0;
                  if (currentBatteryPercentage > 80.0) desiredState = 4;
                  else if (currentBatteryPercentage > 50.0) desiredState = 3;
                  else if (currentBatteryPercentage > 30.0) desiredState = 2;
                  else if (currentBatteryPercentage > 15.0) desiredState = 1;
                  lightState = desiredState; pwmOveride = 0; applyLightState();
                }
              }
              else if (header.indexOf("/on") >= 0) { lightState = lastLightState; pwmOveride = 0; applyLightState(); }
              else if (header.indexOf("/off") >= 0) { lightState = 0; pwmOveride = 0; applyLightState(); }
              else if (header.indexOf("/min") >= 0) { lightState = 1; lastLightState = 1; pwmOveride = 0; applyLightState(); }
              else if (header.indexOf("/low") >= 0) { lightState = 2; lastLightState = 2; pwmOveride = 0; applyLightState(); }
              else if (header.indexOf("/med") >= 0) { lightState = 3; lastLightState = 3; pwmOveride = 0; applyLightState(); }
              else if (header.indexOf("/high") >= 0) { lightState = 4; lastLightState = 4; pwmOveride = 0; applyLightState(); }
              else if (header.indexOf("/animate") >= 0) { playWifiConnectSequence(); }
              else if (header.indexOf("/pwm?val=") >= 0) {
                int pwmStart = header.indexOf("val=") + 4; int pwmEnd = header.indexOf(" HTTP", pwmStart);
                int newVal = header.substring(pwmStart, pwmEnd).toInt();
                if (newVal < 0) newVal = 0; if (newVal > 255) newVal = 255;
                if (newVal == 0) { lightState = 0; pwmOveride = 0; } else { currentPWM = newVal; pwmOveride = 1; }
                analogWrite(pwmPin, currentPWM);
              }
              
              printNow = 1;
              String stateStr = (pwmOveride == 1) ? "MANUAL OVERRIDE" : (lightState == 0 ? "OFF" : (lightState == 1 ? "MIN" : (lightState == 2 ? "LOW" : (lightState == 3 ? "MED" : "HIGH"))));
              String smartStr = smartModeActive ? "true" : "false";
              client.print("HTTP/1.1 200 OK\r\nContent-type:application/json\r\nConnection: close\r\n\r\n{\"state\":\"" + stateStr + "\", \"pwm\":" + String(currentPWM) + ", \"smart\":" + smartStr + "}");
            } 
            else if (header.indexOf("GET /schedule HTTP") >= 0) {
              String schedListHTML = "";
              for (int i = 0; i < schedule.size(); i++) {
                String timeStr = (schedule[i].h < 10 ? "0" : "") + String(schedule[i].h) + ":" + (schedule[i].m < 10 ? "0" : "") + String(schedule[i].m);
                schedListHTML += "<li style='background:#333; margin:8px auto; padding:10px 15px; border-radius:5px; display:flex; justify-content:space-between; align-items:center; max-width:300px;'>";
                schedListHTML += "<span><strong>" + timeStr + "</strong> - " + getActionName(schedule[i].action) + "</span>";
                schedListHTML += "<button class='btn btn-off' style='padding:6px 12px; font-size:14px; margin:0;' onclick='deleteSched(" + String(i) + ")'>X</button></li>";
              }
              if (schedule.empty()) schedListHTML = "<p style='color:#aaa;'>No schedules configured.</p>";
              
              String masterBtnClass = scheduleEnabled ? "btn-blue" : "btn-off";
              String masterBtnText = scheduleEnabled ? "Disable Schedule" : "Enable Schedule";
              
              String batteryBanner = "";
              if (currentBatteryPercentage <= 15.0 && scheduleEnabled) {
                  batteryBanner = "<div style='background-color:#FF9800; color:#000; padding:10px; margin-bottom:15px; border-radius:5px; font-weight:bold;'>⚠️ Battery low (" + String((int)currentBatteryPercentage) + "%). Schedule paused until charged.</div>";
              }

              client.print("HTTP/1.1 200 OK\r\nContent-type:text/html\r\nCache-Control: no-cache, no-store, must-revalidate\r\nConnection: close\r\n\r\n");
              String schedTemplate = R"rawliteral(
<!DOCTYPE html><html>
<head>
  <meta charset='UTF-8'>
  <meta name='viewport' content='width=device-width, initial-scale=1'>
  <title>Schedule</title>
  <style>
    body { font-family: Helvetica; text-align: center; background: #121212; color: #fff; padding-top: 20px;} 
    input, select { padding: 10px; margin: 5px; border-radius: 5px; border: none;} 
    .btn { background-color: #2196F3; color: white; padding: 12px 24px; font-size: 16px; border-radius: 8px; cursor: pointer; border: none;} 
    .btn-off { background-color: #f44336; } 
    .btn-blue { background-color: #2196F3; } 
    .btn-back { background-color: #555; margin-top: 30px;} 
    ul { list-style-type: none; padding: 0; }
  </style>
  <script>
    function addSched() { 
      let t = document.getElementById('time').value; 
      let a = document.getElementById('action').value; 
      if(!t) return alert('Select a time!'); 
      let pts = t.split(':');
      fetch('/add_schedule?h=' + pts[0] + '&m=' + pts[1] + '&a=' + a).then(res => res.json()).then(data => { location.reload(); }); 
    } 
    function deleteSched(index) { 
      if(confirm('Delete event?')) fetch('/delete_schedule?index=' + index).then(res => res.json()).then(data => { location.reload(); }); 
    }
    function toggleSched() {
      fetch('/toggle_schedule').then(res => res.json()).then(data => { location.reload(); }); 
    }
  </script>
</head>
<body>
  <h2>Daily Routine</h2>
  %BANNER_PLACEHOLDER%
  
  <button class='btn %MASTER_CLASS%' onclick='toggleSched()'>%MASTER_TEXT%</button>
  <hr style="border: 0; height: 1px; background-color: #555; margin: 30px 10%; max-width: 350px; margin-left: auto; margin-right: auto;">
  
  <ul id='sched-list'>%LIST_PLACEHOLDER%</ul>
  <hr style="border: 0; height: 1px; background-color: #555; margin: 30px 10%; max-width: 350px; margin-left: auto; margin-right: auto;">

  <p>Add a new event:</p>
  <input type='time' id='time' required>
  <select id='action'>
    <option value='5'>Battery Smart Mode</option>
    <option value='4'>Power: HIGH</option>
    <option value='3'>Power: MED</option>
    <option value='2'>Power: LOW</option>
    <option value='1'>Power: MIN</option>
    <option value='0'>Turn OFF</option>
  </select><br><br>
  <button class='btn' onclick='addSched()'>Add Event</button>
  <br>
  <a href='/'><button class='btn btn-back'>&#x1F519;&#xFE0E; Back to Fountain</button></a>
</body>
</html>
)rawliteral";
              schedTemplate.replace("%LIST_PLACEHOLDER%", schedListHTML);
              schedTemplate.replace("%MASTER_CLASS%", masterBtnClass);
              schedTemplate.replace("%MASTER_TEXT%", masterBtnText);
              schedTemplate.replace("%BANNER_PLACEHOLDER%", batteryBanner);
              client.print(schedTemplate);
            }
            else if (header.indexOf("GET /history HTTP") >= 0) {
              client.print("HTTP/1.1 200 OK\r\nContent-type:text/html\r\nCache-Control: no-cache, no-store, must-revalidate\r\nConnection: close\r\n\r\n");
              String historyTemplate = R"rawliteral(
<!DOCTYPE html><html>
<head>
  <meta charset='UTF-8'>
  <meta name='viewport' content='width=device-width, initial-scale=1'>
  <title>Battery History</title>
  <style>
    body { font-family: Helvetica; text-align: center; background: #121212; color: #fff; padding-top: 20px;} 
    .chart-container { width: 95%; max-width: 600px; margin: auto; background: #1e1e1e; padding: 10px; border-radius: 10px; overflow: hidden;} 
    canvas { width: 100%; height: auto; display: block;} 
    .btn-back { background-color: #555; color: white; padding: 12px 24px; font-size: 16px; border-radius: 20px; text-decoration: none; display: inline-block; margin-top: 20px; border: none; cursor: pointer;}
  </style>
</head>
<body>
  <h2>Battery History</h2>
  <div class='chart-container'><canvas id='batteryChart' width='600' height='350'></canvas></div><br>
  <a href='/' class='btn-back'>&#x1F519;&#xFE0E; Back to Fountain</a>
  <script>
    fetch('/data_history').then(r => r.json()).then(data => {
        const canvas = document.getElementById('batteryChart'); const ctx = canvas.getContext('2d');
        const w = canvas.width; const h = canvas.height; const padX = 45; const padY = 20;
        const gW = w - padX*2; const gH = h - padY*2;
        ctx.fillStyle = '#1e1e1e'; ctx.fillRect(0,0,w,h); ctx.fillStyle = '#fff'; ctx.font = '14px Helvetica';
        if(!data || data.length === 0){ ctx.fillText('Waiting for first log...', w/2-60, h/2); return; }
        ctx.textAlign = 'right'; ctx.fillText('100%', padX-5, padY+5); ctx.fillText('50%', padX-5, padY+gH/2+5); ctx.fillText('0%', padX-5, padY+gH);
        ctx.strokeStyle = '#555'; ctx.beginPath(); ctx.moveTo(padX, padY); ctx.lineTo(padX, padY+gH); ctx.lineTo(padX+gW, padY+gH); ctx.stroke();
        if(data.length === 1){
          ctx.beginPath(); ctx.arc(padX+gW/2, padY+gH-(data[0].p/100.0)*gH, 4, 0, 2*Math.PI); ctx.fillStyle = '#21d8f6'; ctx.fill();
          ctx.textAlign = 'center'; ctx.fillText(new Date(data[0].ts*1000).toLocaleTimeString([],{hour:'2-digit',minute:'2-digit'}), padX+gW/2, padY+gH+18); return;
        }
        const minT = data[0].ts; const maxT = data[data.length-1].ts; const timeRange = Math.max(maxT-minT, 1);
        ctx.strokeStyle = '#21d8f6'; ctx.lineWidth = 3; ctx.beginPath();
        data.forEach((d,i) => {
          const x = padX + ((d.ts-minT)/timeRange)*gW; const y = padY + gH - (d.p/100.0)*gH;
          if(i === 0) ctx.moveTo(x,y); else ctx.lineTo(x,y);
        });
        ctx.stroke(); ctx.textAlign = 'left'; ctx.fillText(new Date(minT*1000).toLocaleTimeString([],{hour:'2-digit',minute:'2-digit'}), padX, padY+gH+18);
        ctx.textAlign = 'right'; ctx.fillText(new Date(maxT*1000).toLocaleTimeString([],{hour:'2-digit',minute:'2-digit'}), padX+gW, padY+gH+18);
      }).catch(err => { const ctx = document.getElementById('batteryChart').getContext('2d'); ctx.fillStyle = '#fff'; ctx.fillText('Error loading data', 50, 50); });
  </script>
</body>
</html>
)rawliteral";
              client.print(historyTemplate);
            } 
            else if (header.indexOf("GET /settings HTTP") >= 0) {
              String networkListHTML = "";
              for (int i = 0; i < savedNetworks.size(); i++) {
                networkListHTML += "<li style='background:#333; margin:8px auto; padding:10px 15px; border-radius:5px; display:flex; justify-content:space-between; align-items:center; max-width:300px;'>";
                networkListHTML += "<span>" + savedNetworks[i].ssid + "</span>";
                networkListHTML += "<button class='btn btn-off' style='padding:6px 12px; font-size:14px; margin:0;' onclick='deleteNetwork(" + String(i) + ")'>X</button></li>";
              }
              if (savedNetworks.empty()) networkListHTML = "<p style='color:#aaa;'>No networks saved yet.</p>";
              client.print("HTTP/1.1 200 OK\r\nContent-type:text/html\r\nCache-Control: no-cache, no-store, must-revalidate\r\nConnection: close\r\n\r\n");
              String settingsTemplate = R"rawliteral(
<!DOCTYPE html><html>
<head>
  <meta charset='UTF-8'>
  <meta name='viewport' content='width=device-width, initial-scale=1'>
  <title>Settings</title>
  <style>
    body { font-family: Helvetica; text-align: center; background: #121212; color: #fff; padding-top: 20px;} 
    input { padding: 10px; margin: 10px; width: 80%; max-width: 300px; border-radius: 5px; border: none;} 
    .btn { background-color: #2196F3; color: white; padding: 12px 24px; font-size: 16px; border-radius: 8px; cursor: pointer; border: none;} 
    .btn-off { background-color: #f44336; } 
    .btn-back { background-color: #555; margin-top: 30px;} 
    ul { list-style-type: none; padding: 0; }
  </style>
  <script>
    function addNetwork() { 
      let s = encodeURIComponent(document.getElementById('ssid').value); 
      let p = encodeURIComponent(document.getElementById('pass').value); 
      if(!s || !p) return alert('Fill out both fields!'); 
      fetch('/add_wifi?s=' + s + '&p=' + p).then(res => res.json()).then(data => { location.reload(); }); 
    } 
    function deleteNetwork(index) { 
      if(confirm('Are you sure?')) { fetch('/delete_wifi?index=' + index).then(res => res.json()).then(data => { location.reload(); }); } 
    }
  </script>
</head>
<body>
  <h2>Network Settings</h2>
  <ul id='network-list'>%PLACEHOLDER%</ul>
  <hr>
  <p>Add a new Wi-Fi network:</p>
  <input type='text' id='ssid' placeholder='SSID'><br>
  <input type='password' id='pass' placeholder='Password'><br>
  <button class='btn' onclick='addNetwork()'>Add Network</button>
  <p id='status'></p>
  <br>
  <a href='/'><button class='btn btn-back'>&#x1F519;&#xFE0E; Back to Fountain</button></a>
</body>
</html>
)rawliteral";
              settingsTemplate.replace("%PLACEHOLDER%", networkListHTML);
              client.print(settingsTemplate);
            } 
            else if (header.indexOf("GET / HTTP") >= 0 || header.indexOf("GET /? HTTP") >= 0) {
              String stateStr = (pwmOveride == 1) ? "MANUAL OVERRIDE" : (lightState == 0 ? "OFF" : (lightState == 1 ? "MIN" : (lightState == 2 ? "LOW" : (lightState == 3 ? "MED" : "HIGH"))));
              String powerBtn = (lightState == 0 && currentPWM == 0) ? "<button class=\"btn btn-blue\" style=\"margin:0;\" onclick=\"sendCommand('/lamp/on')\">Turn ON</button>" : "<button class=\"btn btn-off\" style=\"margin:0;\" onclick=\"sendCommand('/lamp/off')\">Turn OFF</button>";
              String smartBtnClass = smartModeActive ? "btn-smart-active" : "btn-smart-inactive";
              String smartBtnText = smartModeActive ? "Smart Mode: ON" : "Smart Mode: OFF";

              client.print("HTTP/1.1 200 OK\r\nContent-type:text/html\r\nCache-Control: no-cache, no-store, must-revalidate\r\nConnection: close\r\n\r\n");
              
              String htmlTemplate = R"rawliteral(
<!DOCTYPE html><html>
<head>
  <meta charset='UTF-8'>
  <meta name='viewport' content='width=device-width, initial-scale=1'>
  <title>Fountain</title>
  <style>
    :root { --bg-color: #121212; --text-color: #ffffff; } 
    .light-mode { --bg-color: #f4f4f4; --text-color: #121212; } 
    body { font-family: Helvetica; text-align: center; background-color: var(--bg-color); color: var(--text-color); transition: background-color 0.4s, color 0.4s; width: 100%; padding-top: 20px;} 
    
    .btn { background-color: #4CAF50; border: none; color: white; padding: 16px 30px; font-size: 20px; margin: 4px; cursor: pointer; border-radius: 8px;} 
    .btn-off { background-color: #f44336; } 
    .btn-blue { background-color: #2196F3; } 
    .btn-purple { background-color: #9C27B0; } 
    .btn-smart-active { background-color: #FFC107; color: #000; font-weight: bold; margin: 0; } 
    .btn-smart-inactive { background-color: #555; color: #ccc; margin: 0; }
    
    .theme-btn-top { position: absolute; top: 15px; left: 15px; background: none; border: none; font-size: 28px; cursor: pointer; padding: 10px; outline: none; color: var(--text-color); }
    
    /* Layout Classes */
    .flex-row { display: flex; justify-content: center; gap: 10px; flex-wrap: wrap; align-items: center; margin-bottom: 10px; }
    .btn-group { display: flex; width: 95%; max-width: 400px; margin: 0 auto; gap: 5px; }
    .flex-btn { flex: 1; padding: 12px 5px; font-size: 16px; margin: 0; box-sizing: border-box; min-width: 60px; }
    
    .nav-btn { background-color: #333; padding: 12px 24px; font-size: 14px; border-radius: 20px; margin-top: 10px; margin-bottom: 30px; color: white; text-decoration: none; display: inline-block;} 
    hr { border: 0; height: 1px; background-color: #555; margin: 30px 10%; } 
    .slider { -webkit-appearance: none; width: 80%; height: 15px; border-radius: 5px; background: #d3d3d3; outline: none; margin-bottom: 10px; } 
    .slider::-webkit-slider-thumb { -webkit-appearance: none; appearance: none; width: 30px; height: 30px; border-radius: 50%; background: #4CAF50; cursor: pointer; }
  </style>
  <script>
    function handleResponse(data) { 
      document.getElementById('state-display').innerText = data.state; 
      document.getElementById('pwmSlider').value = data.pwm; 
      document.getElementById('sliderValue').innerText = data.pwm; 
      
      let pBtn = document.getElementById('power-container'); 
      if(data.state === 'OFF') {
        pBtn.innerHTML = '<button class="btn btn-blue" style="margin:0;" onclick="sendCommand(\'/lamp/on\')">Turn ON</button>'; 
      } else {
        pBtn.innerHTML = '<button class="btn btn-off" style="margin:0;" onclick="sendCommand(\'/lamp/off\')">Turn OFF</button>'; 
      }
      
      let sBtn = document.getElementById('smart-btn');
      if(data.smart) {
        sBtn.className = "btn btn-smart-active";
        sBtn.innerText = "Smart Mode: ON";
      } else {
        sBtn.className = "btn btn-smart-inactive";
        sBtn.innerText = "Smart Mode: OFF";
      }
    } 
    function sendCommand(cmd) { fetch(cmd).then(res => res.json()).then(data => handleResponse(data)); } 
    function updateSliderUI(val) { document.getElementById('sliderValue').innerText = val; } 
    function sendPWM(val) { fetch('/lamp/pwm?val=' + val).then(res => res.json()).then(data => handleResponse(data)); } 
    function toggleTheme() { 
      document.body.classList.toggle('light-mode'); 
      let tb = document.getElementById('theme-btn'); 
      if (document.body.classList.contains('light-mode')) tb.innerHTML = '&#x263E;&#xFE0E;'; 
      else tb.innerHTML = '&#x2600;&#xFE0E;'; 
    }
  </script>
</head>
<body>
  <button id='theme-btn' class='theme-btn-top' onclick='toggleTheme()'>&#x2600;&#xFE0E;</button>
  <h1>Fountain</h1>
  <h3>Current State: <strong id='state-display'>%STATE%</strong></h3>
  
  <div class='flex-row'>
    <span id='power-container'>%POWER%</span>
    <button id='smart-btn' class='btn %SMART_CLASS%' onclick="sendCommand('/lamp/smart')">%SMART_TEXT%</button>
  </div>
  
  <hr>
  <div>
    <input type='range' min='0' max='255' value='%PWM%' class='slider' id='pwmSlider' oninput='updateSliderUI(this.value)' onchange='sendPWM(this.value)'>
    <p>Custom Brightness: <strong id='sliderValue'>%PWM%</strong></p>
  </div>
  <hr>
  
  <div class='btn-group'>
    <button class='btn flex-btn' onclick="sendCommand('/lamp/min')">MIN</button>
    <button class='btn flex-btn' onclick="sendCommand('/lamp/low')">LOW</button>
    <button class='btn flex-btn' onclick="sendCommand('/lamp/med')">MED</button>
    <button class='btn flex-btn' onclick="sendCommand('/lamp/high')">HIGH</button>
  </div>
  
  <br>
  <div>
    <button class='btn btn-purple' onclick="sendCommand('/lamp/animate')">Play Animation</button>
  </div>
  <hr>
  
  <a href='/schedule' class='nav-btn'>&#x1F4C5;&#xFE0E; Daily Schedule</a>
  <a href='/history' class='nav-btn' style='margin-left:10px;'>&#x1F50B;&#xFE0E; Battery History</a>
  <a href='/settings' class='nav-btn' style='margin-left:10px;'>&#x2699;&#xFE0E; Network Setup</a>
</body>
</html>
)rawliteral";
              
              htmlTemplate.replace("%STATE%", stateStr);
              htmlTemplate.replace("%POWER%", powerBtn);
              htmlTemplate.replace("%PWM%", String(currentPWM));
              htmlTemplate.replace("%SMART_CLASS%", smartBtnClass);
              htmlTemplate.replace("%SMART_TEXT%", smartBtnText);
              client.print(htmlTemplate);
            }
            
            break; 
          } else { currentLine = ""; }
        } else if (c != '\r') { currentLine += c; }
      }
    }
    header = ""; 
    client.stop();
  }
  
  if (printNow == 1) {
    Serial.print("PWM Set: "); Serial.print(currentPWM);
    Serial.print(" | lightState: "); Serial.print(lightState);
    Serial.print(" | Batt %: "); Serial.print(currentBatteryPercentage);
    if (pwmOveride == 1) Serial.print(" [SERIAL OVERRIDE]");
    if (smartModeActive) Serial.print(" [SMART MODE]");
    if (scheduleOverride) Serial.print(" [SCHED OVERRIDE]");
    Serial.println(""); printNow = 0;
  }
}
