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
float currentBatteryVoltage = 0.0; 

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

// --- System Configuration Variables ---
String sysName = "Device";
String pageTitle = "Wireless Control";
bool batteryEnabled = true;
bool smartModeActive = false;

// Battery Tuning
float batDischarged = 12.2;
float batCharged = 13.0; 

// Configurable Power Levels
int pwmMin = 10;
int pwmLow = 50;
int pwmMed = 150;
int pwmHigh = 255;

// --- Web Server Setup ---
WiFiServer server(80);
String header;

// --- Button Debounce Variables ---
unsigned long lastButtonMillis = 0;
const long debounceTime = 250; 

// --- Device Constants & Variables ---
const int pwmPin = 4;       
const int bootButtonPin = 9; 

int currentPWM = 0;
int pwmOveride = 0;      
int powerState = 0;      
int lastPowerState = 4;  
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
    } else if (str[i] == '+') { decoded += ' '; } 
    else { decoded += str[i]; }
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
  sysName = preferences.getString("sys_name", "Device");
  pageTitle = preferences.getString("page_title", "Wireless Control");
  batteryEnabled = preferences.getBool("bat_en", true);
  smartModeActive = preferences.getBool("smart_mode", false);
  scheduleEnabled = preferences.getBool("sched_en", false);
  pwmMin = preferences.getInt("pwm_min", 10);
  pwmLow = preferences.getInt("pwm_low", 50);
  pwmMed = preferences.getInt("pwm_med", 150);
  pwmHigh = preferences.getInt("pwm_high", 255);
  batDischarged = preferences.getFloat("bat_dis", 12.2);
  batCharged = preferences.getFloat("bat_chg", 13.0);
  preferences.end();
}

void saveSettings() {
  preferences.begin("sys_settings", false);
  preferences.putString("sys_name", sysName);
  preferences.putString("page_title", pageTitle);
  preferences.putBool("bat_en", batteryEnabled);
  preferences.putBool("smart_mode", smartModeActive);
  preferences.putBool("sched_en", scheduleEnabled);
  preferences.putInt("pwm_min", pwmMin);
  preferences.putInt("pwm_low", pwmLow);
  preferences.putInt("pwm_med", pwmMed);
  preferences.putInt("pwm_high", pwmHigh);
  preferences.putFloat("bat_dis", batDischarged);
  preferences.putFloat("bat_chg", batCharged);
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

// --- Device Control Logic ---
void applyPowerState() {
  if (pwmOveride == 0) {
    if (powerState == 1) currentPWM = pwmMin;
    else if (powerState == 2) currentPWM = pwmLow;
    else if (powerState == 3) currentPWM = pwmMed;
    else if (powerState == 4) currentPWM = pwmHigh;
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
    for (int i = 0; i <= pwmHigh; i += 15) { analogWrite(pwmPin, i); delay(10); }
    for (int i = pwmHigh; i >= 0; i -= 15) { analogWrite(pwmPin, i); delay(10); }
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
    if (morseStr[i] == '.') { analogWrite(pwmPin, pwmMed); delay(dotDuration); } 
    else if (morseStr[i] == '-') { analogWrite(pwmPin, pwmMed); delay(dotDuration * 3); }
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
  String host = sysName;
  host.toLowerCase();
  host.replace(" ", "-");
  ArduinoOTA.setHostname(host.c_str());
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
    WiFi.softAP("Wireless Control Setup", "password123"); 
    apActive = true;
  } else {
    WiFi.mode(WIFI_STA);
    apActive = false;
  }
  
  if (batteryEnabled) {
    ads.setGain(GAIN_ONE); 
    delay(200);
    ads.begin();
    loadBatteryLogs();
  }
  
  setupOTA();
  server.begin(); 
}

void loop() {
  ArduinoOTA.handle();
  unsigned long now = millis();
  
  // 1. Handle Physical Button Input 
  int buttonState = digitalRead(bootButtonPin);
  if (buttonState == LOW && (now - lastButtonMillis > debounceTime)) {
    lastButtonMillis = now; 
    triggerManualOverride();
    powerState++;
    if (powerState > 4) powerState = 0;
    if (powerState != 0) lastPowerState = powerState;
    pwmOveride = 0; printNow = 1;
    applyPowerState();
  }

  // 2. Handle Schedule State Machine
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
            bool batterySafe = !batteryEnabled || (batteryEnabled && currentBatteryPercentage > 15.0);
            
            if(!batterySafe) {
                if(powerState != 0 || smartModeActive || pwmOveride != 0) {
                    smartModeActive = false;
                    powerState = 0; pwmOveride = 0; applyPowerState(); printNow = 1;
                }
            } else {
                if(targetAction == 5 && batteryEnabled) {
                    if(!smartModeActive) { smartModeActive = true; saveSettings(); printNow = 1; }
                } else if (targetAction >= 0 && targetAction <= 4) {
                    if(smartModeActive || powerState != targetAction || pwmOveride != 0) {
                        smartModeActive = false;
                        powerState = targetAction; pwmOveride = 0; applyPowerState(); printNow = 1;
                    }
                }
            }
        }
      }
    }
  }

  // 3. Handle ADC Reading & Smart Power Management
  if (batteryEnabled) {
    if (now - lastADCMillis >= adcInterval) {
      lastADCMillis = now;
      int16_t adc0 = ads.readADC_SingleEnded(0);
      float adcVolts = ads.computeVolts(adc0);
      
      currentBatteryVoltage = adcVolts * 5.0; 
      
      if (batCharged > batDischarged) {
        currentBatteryPercentage = ((currentBatteryVoltage - batDischarged) / (batCharged - batDischarged)) * 100.0;
      } else {
        currentBatteryPercentage = 0.0;
      }

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

        if (powerState != desiredState || pwmOveride != 0) {
          powerState = desiredState;
          pwmOveride = 0;
          applyPowerState();
          printNow = 1;
        }
      }
    }

    // 4. Handle Battery Logging
    if (now - lastShortTermLogMillis >= 300000) { 
      addBatteryLog(currentBatteryPercentage);
      lastShortTermLogMillis = now;
    }
    if (now - lastLongTermLogMillis >= 900000) { 
      saveBatteryLogs();
      lastLongTermLogMillis = now;
    }
  }

  // 5. Handle Serial Input
  if (Serial.available() > 0) {
    int input = Serial.parseInt();
    while (Serial.available() > 0) Serial.read(); 
    if (input >= 0 && input <= 255) {
      triggerManualOverride();
      pwmOveride = 1; currentPWM = input;
      analogWrite(pwmPin, currentPWM); printNow = 1;
    }
  }

  // 6. Handle Wi-Fi Connection
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
          WiFi.softAP("Wireless Control Setup", "password123"); 
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

  // 7. Handle Web Server Requests 
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
            
            // SYSTEM UPDATES
            if (header.indexOf("GET /update_sys?") >= 0) {
              int nStart = header.indexOf("n=") + 2; int nEnd = header.indexOf("&t=", nStart);
              int tStart = header.indexOf("&t=") + 3; int tEnd = header.indexOf("&b=", tStart);
              int bStart = header.indexOf("&b=") + 3; int bEnd = header.indexOf("&p1=", bStart);
              int p1Start = header.indexOf("&p1=") + 4; int p1End = header.indexOf("&p2=", p1Start);
              int p2Start = header.indexOf("&p2=") + 4; int p2End = header.indexOf("&p3=", p2Start);
              int p3Start = header.indexOf("&p3=") + 4; int p3End = header.indexOf("&p4=", p3Start);
              int p4Start = header.indexOf("&p4=") + 4; int p4End = header.indexOf("&bd=", p4Start);
              int bdStart = header.indexOf("&bd=") + 4; int bdEnd = header.indexOf("&bc=", bdStart);
              int bcStart = header.indexOf("&bc=") + 4; int bcEnd = header.indexOf(" HTTP", bcStart);
              
              sysName = urldecode(header.substring(nStart, nEnd));
              pageTitle = urldecode(header.substring(tStart, tEnd));
              batteryEnabled = header.substring(bStart, bEnd).toInt() == 1;
              pwmMin = header.substring(p1Start, p1End).toInt();
              pwmLow = header.substring(p2Start, p2End).toInt();
              pwmMed = header.substring(p3Start, p3End).toInt();
              pwmHigh = header.substring(p4Start, p4End).toInt();
              batDischarged = header.substring(bdStart, bdEnd).toFloat();
              batCharged = header.substring(bcStart, bcEnd).toFloat();
              
              saveSettings();
              setupOTA(); 
              client.print("HTTP/1.1 200 OK\r\nContent-type:application/json\r\nConnection: close\r\n\r\n{\"status\":\"ok\"}");
            }
            // WI-FI MANAGER
            else if (header.indexOf("GET /add_wifi") >= 0) {
              int sStart = header.indexOf("s=") + 2; int sEnd = header.indexOf("&", sStart);
              int pStart = header.indexOf("p=") + 2; int pEnd = header.indexOf(" HTTP", pStart);
              String newSsid = urldecode(header.substring(sStart, sEnd));
              String newPass = urldecode(header.substring(pStart, pEnd));
              savedNetworks.push_back({newSsid, newPass}); saveNetworks();
              client.print("HTTP/1.1 200 OK\r\nContent-type:application/json\r\nConnection: close\r\n\r\n{\"status\":\"ok\"}");
              previousWifiMillis = millis() - wifiInterval; 
            } 
            else if (header.indexOf("GET /delete_wifi") >= 0) {
              int iStart = header.indexOf("index=") + 6; int iEnd = header.indexOf(" HTTP", iStart);
              int idx = header.substring(iStart, iEnd).toInt();
              if (idx >= 0 && idx < savedNetworks.size()) { savedNetworks.erase(savedNetworks.begin() + idx); saveNetworks(); }
              client.print("HTTP/1.1 200 OK\r\nContent-type:application/json\r\nConnection: close\r\n\r\n{\"status\":\"ok\"}");
            } 
            // SCHEDULE MANAGER
            else if (header.indexOf("GET /add_schedule") >= 0) {
              int hStart = header.indexOf("h=") + 2; int hEnd = header.indexOf("&m", hStart);
              int mStart = header.indexOf("m=") + 2; int mEnd = header.indexOf("&a", mStart);
              int aStart = header.indexOf("a=") + 2; int aEnd = header.indexOf(" HTTP", aStart);
              uint8_t h = header.substring(hStart, hEnd).toInt(); uint8_t m = header.substring(mStart, mEnd).toInt(); uint8_t a = header.substring(aStart, aEnd).toInt();
              schedule.push_back({h, m, a}); saveSchedule();
              client.print("HTTP/1.1 200 OK\r\nContent-type:application/json\r\nConnection: close\r\n\r\n{\"status\":\"ok\"}");
            }
            else if (header.indexOf("GET /delete_schedule") >= 0) {
              int iStart = header.indexOf("index=") + 6; int iEnd = header.indexOf(" HTTP", iStart);
              int idx = header.substring(iStart, iEnd).toInt();
              if (idx >= 0 && idx < schedule.size()) { schedule.erase(schedule.begin() + idx); saveSchedule(); }
              client.print("HTTP/1.1 200 OK\r\nContent-type:application/json\r\nConnection: close\r\n\r\n{\"status\":\"ok\"}");
            }
            else if (header.indexOf("GET /toggle_schedule") >= 0) {
              scheduleEnabled = !scheduleEnabled; saveSettings();
              client.print("HTTP/1.1 200 OK\r\nContent-type:application/json\r\nConnection: close\r\n\r\n{\"status\":\"ok\"}");
            }
            // BATTERY DATA
            else if (header.indexOf("GET /data_history HTTP") >= 0) {
              String json = "[";
              for (size_t i = 0; i < shortTermLogs.size(); i++) {
                json += "{\"ts\":" + String((unsigned long)shortTermLogs[i].timestamp) + ",\"p\":" + String(shortTermLogs[i].percentage) + "}";
                if (i < shortTermLogs.size() - 1) json += ",";
              }
              json += "]";
              client.print("HTTP/1.1 200 OK\r\nContent-type:application/json\r\nConnection: close\r\n\r\n"); client.print(json);
            }
            // DEVICE CONTROLS
            else if (header.indexOf("GET /device/") >= 0) {
              triggerManualOverride(); 
              
              if (header.indexOf("/smart") >= 0 && batteryEnabled) {
                smartModeActive = !smartModeActive; saveSettings();
                if (smartModeActive) { 
                  int desiredState = 0;
                  if (currentBatteryPercentage > 80.0) desiredState = 4;
                  else if (currentBatteryPercentage > 50.0) desiredState = 3;
                  else if (currentBatteryPercentage > 30.0) desiredState = 2;
                  else if (currentBatteryPercentage > 15.0) desiredState = 1;
                  powerState = desiredState; pwmOveride = 0; applyPowerState();
                }
              }
              else if (header.indexOf("/on") >= 0) { powerState = lastPowerState; pwmOveride = 0; applyPowerState(); }
              else if (header.indexOf("/off") >= 0) { powerState = 0; pwmOveride = 0; applyPowerState(); }
              else if (header.indexOf("/min") >= 0) { powerState = 1; lastPowerState = 1; pwmOveride = 0; applyPowerState(); }
              else if (header.indexOf("/low") >= 0) { powerState = 2; lastPowerState = 2; pwmOveride = 0; applyPowerState(); }
              else if (header.indexOf("/med") >= 0) { powerState = 3; lastPowerState = 3; pwmOveride = 0; applyPowerState(); }
              else if (header.indexOf("/high") >= 0) { powerState = 4; lastPowerState = 4; pwmOveride = 0; applyPowerState(); }
              else if (header.indexOf("/animate") >= 0) { playWifiConnectSequence(); }
              else if (header.indexOf("/pwm?val=") >= 0) {
                int pwmStart = header.indexOf("val=") + 4; int pwmEnd = header.indexOf(" HTTP", pwmStart);
                int newVal = header.substring(pwmStart, pwmEnd).toInt();
                if (newVal < 0) newVal = 0; if (newVal > 255) newVal = 255;
                if (newVal == 0) { powerState = 0; pwmOveride = 0; } else { currentPWM = newVal; pwmOveride = 1; }
                analogWrite(pwmPin, currentPWM);
              }
              
              printNow = 1;
              String stateStr = (pwmOveride == 1) ? "MANUAL OVERRIDE" : (powerState == 0 ? "OFF" : (powerState == 1 ? "MIN" : (powerState == 2 ? "LOW" : (powerState == 3 ? "MED" : "HIGH"))));
              String smartStr = smartModeActive ? "true" : "false";
              client.print("HTTP/1.1 200 OK\r\nContent-type:application/json\r\nConnection: close\r\n\r\n{\"state\":\"" + stateStr + "\", \"pwm\":" + String(currentPWM) + ", \"smart\":" + smartStr + "}");
            } 
            
            // --- WEB PAGES ---
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
              if (batteryEnabled && currentBatteryPercentage <= 15.0 && scheduleEnabled) {
                  batteryBanner = "<div style='background-color:#FF9800; color:#000; padding:10px; margin-bottom:15px; border-radius:5px; font-weight:bold;'>⚠️ Battery low (" + String((int)currentBatteryPercentage) + "%). Schedule paused until charged.</div>";
              }
              String smartOption = batteryEnabled ? "<option value='5'>Battery Smart Mode</option>" : "";

              client.print("HTTP/1.1 200 OK\r\nContent-type:text/html\r\nCache-Control: no-cache, no-store, must-revalidate\r\nConnection: close\r\n\r\n");
              String schedTemplate = R"rawliteral(
<!DOCTYPE html><html>
<head>
  <meta charset='UTF-8'>
  <meta name='viewport' content='width=device-width, initial-scale=1'>
  <title>%PAGE_TITLE%</title>
  <style>
    body { font-family: Helvetica; text-align: center; background: #121212; color: #fff; padding-top: 20px;} 
    input, select { padding: 10px; margin: 5px; border-radius: 5px; border: none;} 
    .btn { background-color: #2196F3; color: white; padding: 12px 24px; font-size: 16px; border-radius: 8px; cursor: pointer; border: none;} 
    .btn-off { background-color: #f44336; } .btn-blue { background-color: #2196F3; } .btn-back { background-color: #555; margin-top: 30px;} 
    ul { list-style-type: none; padding: 0; }
  </style>
  <script>
    function addSched() { 
      let t = document.getElementById('time').value; let a = document.getElementById('action').value; 
      if(!t) return alert('Select a time!'); let pts = t.split(':');
      fetch('/add_schedule?h=' + pts[0] + '&m=' + pts[1] + '&a=' + a).then(res => res.json()).then(data => { location.reload(); }); 
    } 
    function deleteSched(index) { if(confirm('Delete event?')) fetch('/delete_schedule?index=' + index).then(res => res.json()).then(data => { location.reload(); }); }
    function toggleSched() { fetch('/toggle_schedule').then(res => res.json()).then(data => { location.reload(); }); }
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
    %SMART_OPT%
    <option value='4'>Power: HIGH</option>
    <option value='3'>Power: MED</option>
    <option value='2'>Power: LOW</option>
    <option value='1'>Power: MIN</option>
    <option value='0'>Turn OFF</option>
  </select><br><br>
  <button class='btn' onclick='addSched()'>Add Event</button><br>
  <a href='/'><button class='btn btn-back'>&#x1F519;&#xFE0E; Back</button></a>
</body>
</html>
)rawliteral";
              schedTemplate.replace("%PAGE_TITLE%", pageTitle);
              schedTemplate.replace("%LIST_PLACEHOLDER%", schedListHTML);
              schedTemplate.replace("%MASTER_CLASS%", masterBtnClass);
              schedTemplate.replace("%MASTER_TEXT%", masterBtnText);
              schedTemplate.replace("%BANNER_PLACEHOLDER%", batteryBanner);
              schedTemplate.replace("%SMART_OPT%", smartOption);
              client.print(schedTemplate);
            }
            else if (header.indexOf("GET /history HTTP") >= 0 && batteryEnabled) {
              client.print("HTTP/1.1 200 OK\r\nContent-type:text/html\r\nCache-Control: no-cache, no-store, must-revalidate\r\nConnection: close\r\n\r\n");
              String historyTemplate = R"rawliteral(
<!DOCTYPE html><html>
<head>
  <meta charset='UTF-8'>
  <meta name='viewport' content='width=device-width, initial-scale=1'>
  <title>%PAGE_TITLE%</title>
  <style>
    body { font-family: Helvetica; text-align: center; background: #121212; color: #fff; padding-top: 20px;} 
    .chart-container { width: 95%; max-width: 800px; margin: auto; background: #1e1e1e; padding: 10px; border-radius: 10px; overflow: hidden; position: relative;} 
    canvas { width: 100%; height: auto; display: block; cursor: grab;} 
    canvas:active { cursor: grabbing; }
    .btn-back { background-color: #555; color: white; padding: 12px 24px; font-size: 16px; border-radius: 20px; text-decoration: none; display: inline-block; margin-top: 20px; border: none; cursor: pointer;}
    .controls { margin-top: 15px; }
    .controls button { background: #333; border: none; color: #fff; padding: 8px 15px; border-radius: 5px; cursor: pointer; margin: 0 5px; font-size: 14px; }
  </style>
</head>
<body>
  <h2>Battery History</h2>
  <h4 style="margin-top: 0; color: #aaa;">Current: %CUR_PCT%% (%CUR_VOLT%V)</h4>
  <div class='chart-container'>
    <canvas id='batteryChart' width='800' height='400'></canvas>
    <div class='controls'>
        <button onclick='zoomBtn(1.5)'>Zoom Out</button>
        <button onclick='resetZoom()'>Reset View</button>
        <button onclick='zoomBtn(0.5)'>Zoom In</button>
    </div>
  </div><br>
  <a href='/' class='btn-back'>&#x1F519;&#xFE0E; Back</a>
  <script>
    let rawData = [];
    let viewMin = 0, viewMax = 0;
    const canvas = document.getElementById('batteryChart');
    const ctx = canvas.getContext('2d');

    function formatTime(ts) {
        return new Date(ts * 1000).toLocaleTimeString([],{hour:'2-digit',minute:'2-digit'});
    }

    function draw() {
        const w = canvas.width, h = canvas.height;
        const padX = 50, padY = 20, padB = 40;
        const gW = w - padX - 10, gH = h - padY - padB;

        ctx.fillStyle = '#1e1e1e'; ctx.fillRect(0,0,w,h);
        ctx.fillStyle = '#fff'; ctx.font = '14px Helvetica';

        if(!rawData.length){ ctx.textAlign='center'; ctx.fillText('Waiting for logs...', w/2, h/2); return; }

        const dataMin = rawData[0].ts; const dataMax = rawData[rawData.length-1].ts;
        const minRange = 600; 
        if (viewMax - viewMin < minRange) { let c = (viewMax+viewMin)/2; viewMin = c - minRange/2; viewMax = c + minRange/2; }
        
        if (viewMin < dataMin) { let d = dataMin - viewMin; viewMin += d; viewMax += d; }
        if (viewMax > dataMax) { let d = viewMax - dataMax; viewMin -= d; viewMax -= d; }
        if (viewMin < dataMin) viewMin = dataMin;

        ctx.textAlign = 'right'; ctx.textBaseline = 'middle';
        for(let p=0; p<=100; p+=25) {
            let y = padY + gH - (p/100)*gH;
            ctx.fillText(p+'%', padX-10, y);
            ctx.strokeStyle = '#333'; ctx.beginPath(); ctx.moveTo(padX, y); ctx.lineTo(padX+gW, y); ctx.stroke();
        }

        ctx.strokeStyle = '#555'; ctx.beginPath(); ctx.moveTo(padX, padY); ctx.lineTo(padX, padY+gH); ctx.lineTo(padX+gW, padY+gH); ctx.stroke();

        let timeRange = viewMax - viewMin;
        let numTicks = 6;
        let tickStep = timeRange / numTicks;
        ctx.textAlign = 'center'; ctx.textBaseline = 'top';
        for(let i=0; i<=numTicks; i++) {
            let t = viewMin + i * tickStep;
            let x = padX + (t - viewMin)/timeRange * gW;
            ctx.fillText(formatTime(t), x, padY+gH+10);
            ctx.beginPath(); ctx.moveTo(x, padY+gH); ctx.lineTo(x, padY+gH+5); ctx.stroke();
        }

        ctx.save();
        ctx.beginPath(); ctx.rect(padX, padY, gW, gH); ctx.clip();

        if (rawData.length === 1) {
            let d = rawData[0];
            let x = padX + gW/2;
            let y = padY + gH - (d.p/100.0) * gH;
            ctx.beginPath(); ctx.arc(x, y, 4, 0, 2*Math.PI); ctx.fillStyle = '#21d8f6'; ctx.fill();
        } else {
            ctx.strokeStyle = '#21d8f6'; ctx.lineWidth = 3; ctx.beginPath();
            let first = true;
            for(let d of rawData) {
                if(d.ts < viewMin - timeRange*0.1 || d.ts > viewMax + timeRange*0.1) continue; 
                let x = padX + ((d.ts - viewMin)/timeRange) * gW;
                let y = padY + gH - (d.p/100.0) * gH;
                if(first) { ctx.moveTo(x, y); first = false; } else { ctx.lineTo(x, y); }
            }
            ctx.stroke();
            ctx.lineTo(padX + gW, padY + gH); ctx.lineTo(padX, padY + gH);
            ctx.fillStyle = 'rgba(33, 216, 246, 0.1)'; ctx.fill();
        }
        ctx.restore();
    }

    let isDragging = false; let lastX = 0; let lastDist = 0;

    function getX(e) {
        if(e.touches && e.touches.length > 0) {
            let rect = canvas.getBoundingClientRect();
            return (e.touches[0].clientX - rect.left) * (canvas.width / rect.width);
        }
        return e.offsetX * (canvas.width / canvas.offsetWidth);
    }

    function getDist(e) {
        if(e.touches && e.touches.length === 2) {
            let dx = e.touches[0].clientX - e.touches[1].clientX;
            let dy = e.touches[0].clientY - e.touches[1].clientY;
            return Math.sqrt(dx*dx + dy*dy);
        }
        return 0;
    }

    canvas.addEventListener('mousedown', e => { isDragging = true; lastX = getX(e); });
    canvas.addEventListener('mousemove', e => {
        if(!isDragging) return;
        let x = getX(e); let dx = x - lastX;
        let dt = (dx / (canvas.width - 60)) * (viewMax - viewMin);
        viewMin -= dt; viewMax -= dt; lastX = x; draw();
    });
    canvas.addEventListener('mouseup', () => isDragging = false);
    canvas.addEventListener('mouseleave', () => isDragging = false);

    canvas.addEventListener('touchstart', e => {
        if(e.touches.length === 1) { isDragging = true; lastX = getX(e); }
        if(e.touches.length === 2) { isDragging = false; lastDist = getDist(e); }
    });
    canvas.addEventListener('touchmove', e => {
        e.preventDefault();
        if(e.touches.length === 1 && isDragging) {
            let x = getX(e); let dx = x - lastX;
            let dt = (dx / (canvas.width - 60)) * (viewMax - viewMin);
            viewMin -= dt; viewMax -= dt; lastX = x; draw();
        }
        if(e.touches.length === 2) {
            let dist = getDist(e); let zoom = lastDist / dist;
            zoomCenter(1.0, zoom); 
            lastDist = dist;
        }
    }, {passive: false});
    canvas.addEventListener('touchend', () => isDragging = false);

    canvas.addEventListener('wheel', e => {
        e.preventDefault();
        let rect = canvas.getBoundingClientRect(); let x = e.clientX - rect.left;
        let pct = (x - 50) / (rect.width - 60);
        if(pct < 0) pct = 0; if(pct > 1) pct = 1;
        zoomCenter(pct, e.deltaY > 0 ? 1.2 : 0.8);
    }, {passive: false});

    function zoomCenter(pct, factor) {
        let range = viewMax - viewMin; let tCenter = viewMin + pct * range; let newRange = range * factor;
        viewMin = tCenter - pct * newRange; viewMax = tCenter + (1 - pct) * newRange; draw();
    }

    function zoomBtn(factor) { zoomCenter(1.0, factor); }
    
    function resetZoom() {
        if(rawData.length > 0) { 
            let maxT = rawData[rawData.length-1].ts; let minT = rawData[0].ts;
            let buff = (maxT - minT) * 0.02;
            viewMin = minT - buff; viewMax = maxT + buff; draw(); 
        }
    }

    fetch('/data_history').then(r=>r.json()).then(data=>{
        rawData = data;
        if(rawData.length > 0) {
            let maxT = rawData[rawData.length-1].ts; let minT = rawData[0].ts;
            viewMax = maxT; viewMin = Math.max(minT, maxT - 14400); 
            draw();
        } else { draw(); }
    }).catch(e => { ctx.fillStyle = '#fff'; ctx.fillText('Error loading data', 50, 50); });
  </script>
</body>
</html>
)rawliteral";
              historyTemplate.replace("%PAGE_TITLE%", pageTitle);
              historyTemplate.replace("%CUR_PCT%", String((int)currentBatteryPercentage));
              historyTemplate.replace("%CUR_VOLT%", String(currentBatteryVoltage, 1));
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
  <title>%PAGE_TITLE% Setup</title>
  <style>
    body { font-family: Helvetica; text-align: center; background: #121212; color: #fff; padding-top: 20px;} 
    input[type='text'], input[type='password'], input[type='number'] { padding: 10px; margin: 5px; width: 80%; max-width: 300px; border-radius: 5px; border: none;} 
    .btn { background-color: #2196F3; color: white; padding: 12px 24px; font-size: 16px; border-radius: 8px; cursor: pointer; border: none;} 
    .btn-off { background-color: #f44336; } .btn-back { background-color: #555; margin-top: 30px;} 
    ul { list-style-type: none; padding: 0; }
    .card { background: #1e1e1e; padding: 15px; margin: 15px auto; border-radius: 10px; max-width: 350px; }
    .row { display: flex; justify-content: space-between; align-items: center; margin: 10px 25px; }
    .row input[type='number'] { width: 60px; text-align: center; }
  </style>
  <script>
    function addNetwork() { 
      let s = encodeURIComponent(document.getElementById('ssid').value); let p = encodeURIComponent(document.getElementById('pass').value); 
      if(!s || !p) return alert('Fill out both fields!'); 
      fetch('/add_wifi?s=' + s + '&p=' + p).then(res => res.json()).then(data => { location.reload(); }); 
    } 
    function deleteNetwork(index) { if(confirm('Are you sure?')) fetch('/delete_wifi?index=' + index).then(res => res.json()).then(data => { location.reload(); }); }
    
    function updateSys() {
      let n = encodeURIComponent(document.getElementById('sysname').value); let t = encodeURIComponent(document.getElementById('pagetitle').value);
      let b = document.getElementById('baten').checked ? 1 : 0;
      let p1 = document.getElementById('p1').value; let p2 = document.getElementById('p2').value;
      let p3 = document.getElementById('p3').value; let p4 = document.getElementById('p4').value;
      let bd = document.getElementById('batdis').value; let bc = document.getElementById('batchg').value;
      document.getElementById('sys-status').innerText = 'Saving...';
      fetch('/update_sys?n=' + n + '&t=' + t + '&b=' + b + '&p1=' + p1 + '&p2=' + p2 + '&p3=' + p3 + '&p4=' + p4 + '&bd=' + bd + '&bc=' + bc)
        .then(res => res.json()).then(data => { document.getElementById('sys-status').innerText = 'Saved!'; setTimeout(()=>location.reload(), 500); });
    }
  </script>
</head>
<body>
  <h2>System Configuration</h2>
  <div class='card'>
    <label>Device Name (OTA Name):</label><br>
    <input type='text' id='sysname' value="%SYS_NAME%"><br><br>
    <label>Web Page Title:</label><br>
    <input type='text' id='pagetitle' value="%PAGE_TITLE%"><br><br>
    <label><input type='checkbox' id='baten' %BAT_CHECKED%> Enable Battery Management</label><br><br>
    
    <hr style="border: 0; height: 1px; background-color: #555; margin: 15px 0;">
    <p style="margin-bottom:5px;">Battery Chemistry Curve</p>
    <div class='row'><span>0% Voltage:</span><input type='number' step='0.1' id='batdis' value="%BAT_DIS%"></div>
    <div class='row'><span>100% Voltage:</span><input type='number' step='0.1' id='batchg' value="%BAT_CHG%"></div>
    
    <hr style="border: 0; height: 1px; background-color: #555; margin: 15px 0;">
    <p style="margin-bottom:5px;">Power Tuning (0-255)</p>
    <div class='row'><span>MIN:</span><input type='number' id='p1' value="%P1%" min="0" max="255"></div>
    <div class='row'><span>LOW:</span><input type='number' id='p2' value="%P2%" min="0" max="255"></div>
    <div class='row'><span>MED:</span><input type='number' id='p3' value="%P3%" min="0" max="255"></div>
    <div class='row'><span>HIGH:</span><input type='number' id='p4' value="%P4%" min="0" max="255"></div>
    <button class='btn' onclick='updateSys()' style='margin-top:10px;'>Save Configuration</button>
    <p id='sys-status' style='color:#4CAF50; font-size:14px; margin-bottom:0;'></p>
  </div>

  <h2>Network Setup</h2>
  <div class='card'>
    <ul id='network-list'>%NW_LIST%</ul><hr style="border: 0; height: 1px; background-color: #555; margin: 15px 0;">
    <p>Add a new Wi-Fi network:</p>
    <input type='text' id='ssid' placeholder='SSID'><br>
    <input type='password' id='pass' placeholder='Password'><br>
    <button class='btn' onclick='addNetwork()'>Add Network</button>
  </div>
  
  <br>
  <a href='/'><button class='btn btn-back'>&#x1F519;&#xFE0E; Back to %SYS_NAME%</button></a><br><br>
</body>
</html>
)rawliteral";
              settingsTemplate.replace("%PAGE_TITLE%", pageTitle);
              settingsTemplate.replace("%SYS_NAME%", sysName);
              settingsTemplate.replace("%BAT_CHECKED%", batteryEnabled ? "checked" : "");
              settingsTemplate.replace("%BAT_DIS%", String(batDischarged, 1));
              settingsTemplate.replace("%BAT_CHG%", String(batCharged, 1));
              settingsTemplate.replace("%P1%", String(pwmMin));
              settingsTemplate.replace("%P2%", String(pwmLow));
              settingsTemplate.replace("%P3%", String(pwmMed));
              settingsTemplate.replace("%P4%", String(pwmHigh));
              settingsTemplate.replace("%NW_LIST%", networkListHTML);
              client.print(settingsTemplate);
            } 
            else if (header.indexOf("GET / HTTP") >= 0 || header.indexOf("GET /? HTTP") >= 0) {
              String stateStr = (pwmOveride == 1) ? "MANUAL OVERRIDE" : (powerState == 0 ? "OFF" : (powerState == 1 ? "MIN" : (powerState == 2 ? "LOW" : (powerState == 3 ? "MED" : "HIGH"))));
              String powerBtn = (powerState == 0 && currentPWM == 0) ? "<button class=\"btn btn-blue\" style=\"margin:0;\" onclick=\"sendCommand('/device/on')\">Turn ON</button>" : "<button class=\"btn btn-off\" style=\"margin:0;\" onclick=\"sendCommand('/device/off')\">Turn OFF</button>";
              String smartBtnClass = smartModeActive ? "btn-smart-active" : "btn-smart-inactive";
              String smartBtnText = smartModeActive ? "Smart Mode: ON" : "Smart Mode: OFF";
              
              String batteryUICSS = batteryEnabled ? "" : "display: none;";

              client.print("HTTP/1.1 200 OK\r\nContent-type:text/html\r\nCache-Control: no-cache, no-store, must-revalidate\r\nConnection: close\r\n\r\n");
              
              String htmlTemplate = R"rawliteral(
<!DOCTYPE html><html>
<head>
  <meta charset='UTF-8'>
  <meta name='viewport' content='width=device-width, initial-scale=1'>
  <title>%PAGE_TITLE%</title>
  <style>
    :root { --bg-color: #121212; --text-color: #ffffff; } 
    .light-mode { --bg-color: #f4f4f4; --text-color: #121212; } 
    body { font-family: Helvetica; text-align: center; background-color: var(--bg-color); color: var(--text-color); transition: background-color 0.4s, color 0.4s; width: 100%; padding-top: 20px;} 
    .btn { background-color: #4CAF50; border: none; color: white; padding: 16px 30px; font-size: 20px; margin: 4px; cursor: pointer; border-radius: 8px;} 
    .btn-off { background-color: #f44336; } .btn-blue { background-color: #2196F3; } .btn-purple { background-color: #9C27B0; } 
    .btn-smart-active { background-color: #FFC107; color: #000; font-weight: bold; margin: 0; } .btn-smart-inactive { background-color: #555; color: #ccc; margin: 0; }
    .theme-btn-top { position: absolute; top: 15px; left: 15px; background: none; border: none; font-size: 28px; cursor: pointer; padding: 10px; outline: none; color: var(--text-color); }
    .flex-row { display: flex; justify-content: center; gap: 10px; flex-wrap: wrap; align-items: center; margin-bottom: 10px; }
    .btn-group { display: flex; width: 95%; max-width: 400px; margin: 0 auto; gap: 5px; }
    .flex-btn { flex: 1; padding: 12px 5px; font-size: 16px; margin: 0; box-sizing: border-box; min-width: 60px; }
    .nav-btn { background-color: #333; padding: 12px 24px; font-size: 14px; border-radius: 20px; margin-top: 10px; margin-bottom: 30px; color: white; text-decoration: none; display: inline-block;} 
    
    .slider-container { background: #1e1e1e; padding: 20px; border-radius: 12px; width: 85%; max-width: 400px; margin: 20px auto; box-shadow: 0 4px 10px rgba(0,0,0,0.5); }
    .light-mode .slider-container { background: #fff; box-shadow: 0 4px 10px rgba(0,0,0,0.1); }
    .slider-header { display: flex; justify-content: space-between; font-size: 18px; margin-bottom: 15px; font-weight: bold; }
    .slider { -webkit-appearance: none; width: 100%; height: 8px; border-radius: 4px; background: #333; outline: none; }
    .light-mode .slider { background: #ddd; }
    .slider::-webkit-slider-thumb { -webkit-appearance: none; appearance: none; width: 28px; height: 28px; border-radius: 50%; background: #4CAF50; cursor: pointer; box-shadow: 0 2px 5px rgba(0,0,0,0.3); transition: transform 0.1s;}
    .slider::-webkit-slider-thumb:active { transform: scale(1.15); }
    
    hr { border: 0; height: 1px; background-color: #555; margin: 30px 10%; } 
    .battery-ui { %BATTERY_CSS% }
  </style>
  <script>
    function handleResponse(data) { 
      document.getElementById('state-display').innerText = data.state; 
      document.getElementById('pwmSlider').value = data.pwm; document.getElementById('sliderValue').innerText = data.pwm; 
      let pBtn = document.getElementById('power-container'); 
      if(data.state === 'OFF') pBtn.innerHTML = '<button class="btn btn-blue" style="margin:0;" onclick="sendCommand(\'/device/on\')">Turn ON</button>'; 
      else pBtn.innerHTML = '<button class="btn btn-off" style="margin:0;" onclick="sendCommand(\'/device/off\')">Turn OFF</button>'; 
      let sBtn = document.getElementById('smart-btn');
      if(data.smart) { sBtn.className = "btn btn-smart-active battery-ui"; sBtn.innerText = "Smart Mode: ON"; } 
      else { sBtn.className = "btn btn-smart-inactive battery-ui"; sBtn.innerText = "Smart Mode: OFF"; }
    } 
    function sendCommand(cmd) { fetch(cmd).then(res => res.json()).then(data => handleResponse(data)); } 
    function updateSliderUI(val) { document.getElementById('sliderValue').innerText = val; } 
    function sendPWM(val) { fetch('/device/pwm?val=' + val).then(res => res.json()).then(data => handleResponse(data)); } 
    function toggleTheme() { document.body.classList.toggle('light-mode'); let tb = document.getElementById('theme-btn'); 
      if (document.body.classList.contains('light-mode')) tb.innerHTML = '&#x263E;&#xFE0E;'; else tb.innerHTML = '&#x2600;&#xFE0E;'; }
  </script>
</head>
<body>
  <button id='theme-btn' class='theme-btn-top' onclick='toggleTheme()'>&#x2600;&#xFE0E;</button>
  <h1>%SYS_NAME%</h1>
  <h3>Current State: <strong id='state-display'>%STATE%</strong></h3>
  <div class='flex-row'>
    <span id='power-container'>%POWER%</span>
    <button id='smart-btn' class='btn %SMART_CLASS% battery-ui' onclick="sendCommand('/device/smart')">%SMART_TEXT%</button>
  </div>
  
  <div class='slider-container'>
    <div class='slider-header'>
      <span>Power</span>
      <span style='color: #4CAF50;' id='sliderValue'>%PWM%</span>
    </div>
    <input type='range' min='0' max='255' value='%PWM%' class='slider' id='pwmSlider' oninput='updateSliderUI(this.value)' onchange='sendPWM(this.value)'>
  </div>
  
  <div class='btn-group'>
    <button class='btn flex-btn' onclick="sendCommand('/device/min')">MIN</button>
    <button class='btn flex-btn' onclick="sendCommand('/device/low')">LOW</button>
    <button class='btn flex-btn' onclick="sendCommand('/device/med')">MED</button>
    <button class='btn flex-btn' onclick="sendCommand('/device/high')">HIGH</button>
  </div><br>
  <div><button class='btn btn-purple' onclick="sendCommand('/device/animate')">Play Animation</button></div>
  <hr>
  <a href='/schedule' class='nav-btn'>&#x1F4C5;&#xFE0E; Daily Schedule</a>
  <a href='/history' class='nav-btn battery-ui' style='margin-left:10px;'>&#x1F50B;&#xFE0E; Battery History</a>
  <a href='/settings' class='nav-btn' style='margin-left:10px;'>&#x2699;&#xFE0E; Config</a>
</body>
</html>
)rawliteral";
              htmlTemplate.replace("%PAGE_TITLE%", pageTitle);
              htmlTemplate.replace("%SYS_NAME%", sysName);
              htmlTemplate.replace("%STATE%", stateStr);
              htmlTemplate.replace("%POWER%", powerBtn);
              htmlTemplate.replace("%PWM%", String(currentPWM));
              htmlTemplate.replace("%SMART_CLASS%", smartBtnClass);
              htmlTemplate.replace("%SMART_TEXT%", smartBtnText);
              htmlTemplate.replace("%BATTERY_CSS%", batteryUICSS);
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
    Serial.print(" | powerState: "); Serial.print(powerState);
    if (batteryEnabled) { Serial.print(" | Batt %: "); Serial.print(currentBatteryPercentage); }
    if (pwmOveride == 1) Serial.print(" [SERIAL OVERRIDE]");
    if (smartModeActive) Serial.print(" [SMART MODE]");
    if (scheduleOverride) Serial.print(" [SCHED OVERRIDE]");
    Serial.println(""); printNow = 0;
  }
}