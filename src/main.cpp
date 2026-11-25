#include <Arduino.h>
#include <esp_arduino_version.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#if defined(ESP32_ETH01)
#include <ETH.h>
#endif
#include "driver/uart.h"

static constexpr const char *FW_VERSION = "1.1.0";
static constexpr uint16_t ARTNET_PORT = 6454;
static constexpr uint16_t DMX_SLOTS = 512;
static constexpr uint8_t DMX_UART = UART_NUM_1;
#if defined(ESP32_ETH01)
static constexpr int DMX_TX_PIN = 4;
static constexpr int RS485_DIR_PIN = 33;
#else
static constexpr int DMX_TX_PIN = 4;
static constexpr int RS485_DIR_PIN = 5;
#endif
static constexpr uint32_t DMX_BAUD = 250000;
static constexpr uint32_t DMX_INTERFRAME_MS = 2;
static constexpr int DEFAULT_UNIVERSE = 0;

#if defined(ESP32_ETH01)
static constexpr const char *HW_TARGET = "esp32-eth01";
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
static constexpr const char *HW_TARGET = "esp32-c3";
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
static constexpr const char *HW_TARGET = "esp32-s3";
#else
static constexpr const char *HW_TARGET = "esp32";
#endif

#if defined(ESP32_ETH01)
#ifndef ETH01_PHY_ADDR
#define ETH01_PHY_ADDR 1
#endif
static constexpr int ETH01_PHY_POWER = 16;
static constexpr int ETH01_PHY_MDC = 23;
static constexpr int ETH01_PHY_MDIO = 18;
static constexpr eth_phy_type_t ETH01_PHY_TYPE = ETH_PHY_LAN8720;
static constexpr eth_clock_mode_t ETH01_CLK_MODE = ETH_CLOCK_GPIO0_IN;
#endif

struct Config {
  String hostname = "esp32-c3-dmx";
  bool uniqueNames = true;
  bool apEnabled = true;
  String apSsid = "DMX-Node-C3";
  String apPass = "dmx12345";
  String apIp = "192.168.44.1";
  String staSsid = "";
  String staPass = "";
  bool staEnabled = false;
  bool ethEnabled = true;
  bool ethDhcp = true;
  String ethIp = "192.168.88.50";
  String ethGateway = "192.168.88.1";
  String ethSubnet = "255.255.255.0";
  String ethDns = "192.168.88.1";
  int universe = DEFAULT_UNIVERSE;
};

struct LogLine {
  uint32_t ms = 0;
  String text;
};

Config cfg;
Preferences prefs;
WebServer server(80);
DNSServer dnsServer;
WiFiUDP artnetUdp;
SemaphoreHandle_t dmxMutex;

uint8_t dmx[DMX_SLOTS + 1] = {0};
LogLine logs[60];
uint8_t logHead = 0;
uint32_t framesSent = 0;
uint32_t artnetPackets = 0;
uint32_t lastArtnetMs = 0;
uint32_t lastDemoMs = 0;
String activeDemo = "off";
uint16_t demoStep = 0;
bool blackout = false;
uint32_t staConnectStartMs = 0;
bool staConnectAttempt = false;
bool staConnectReported = false;
bool staFallbackDone = false;
bool wifiRuntimeEnabled = false;
bool dnsRuntimeEnabled = false;
bool autoWifiSuppressedByEth = false;
#if defined(ESP32_ETH01) && defined(AUTO_ETH_WIFI_FAILOVER)
bool autoEthLinkState = false;
uint32_t autoEthChangeMs = 0;
#endif

static String ipToString(IPAddress ip);
static void setupWifi();
static void stopWifiForEth();

static IPAddress parseIpOrDefault(const String &value, IPAddress fallback) {
  IPAddress ip;
  if (ip.fromString(value)) return ip;
  return fallback;
}

static bool isValidIpString(const String &value) {
  IPAddress ip;
  return ip.fromString(value);
}

static String macSuffix() {
  char buf[5];
  snprintf(buf, sizeof(buf), "%04X", (uint16_t)(ESP.getEfuseMac() & 0xFFFF));
  return String(buf);
}

static String withMacSuffix(String base) {
  if (!cfg.uniqueNames) return base;
  String suffix = "-" + macSuffix();
  if (base.endsWith(suffix)) return base;
  return base + suffix;
}

static String effectiveHostname() {
  return withMacSuffix(cfg.hostname);
}

static String effectiveApSsid() {
  return withMacSuffix(cfg.apSsid);
}

#if defined(ESP32_ETH01)
static void onNetEvent(WiFiEvent_t event) {
  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      Serial.println("ETH event: start");
      ETH.setHostname(effectiveHostname().c_str());
      break;
    case ARDUINO_EVENT_ETH_CONNECTED:
      Serial.println("ETH event: link connected");
      break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      Serial.println("ETH event: got IP " + ipToString(ETH.localIP()));
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      Serial.println("ETH event: link disconnected");
      break;
    case ARDUINO_EVENT_ETH_STOP:
      Serial.println("ETH event: stop");
      break;
    default:
      break;
  }
}
#endif

static void addLog(const String &line) {
  logs[logHead].ms = millis();
  logs[logHead].text = line;
  logHead = (logHead + 1) % 60;
}

static String htmlEscape(String value) {
  value.replace("&", "&amp;");
  value.replace("<", "&lt;");
  value.replace(">", "&gt;");
  value.replace("\"", "&quot;");
  return value;
}

static String jsonEscape(String value) {
  value.replace("\\", "\\\\");
  value.replace("\"", "\\\"");
  value.replace("\n", "\\n");
  value.replace("\r", "");
  return value;
}

static void loadConfig() {
  prefs.begin("dmx-node", true);
  cfg.hostname = prefs.getString("hostname", cfg.hostname);
  cfg.uniqueNames = prefs.getBool("uniqueNames", cfg.uniqueNames);
  cfg.apEnabled = prefs.getBool("apEnabled", cfg.apEnabled);
  cfg.apSsid = prefs.getString("apSsid", cfg.apSsid);
  cfg.apPass = prefs.getString("apPass", cfg.apPass);
  cfg.apIp = prefs.getString("apIp", cfg.apIp);
  cfg.staSsid = prefs.getString("staSsid", cfg.staSsid);
  cfg.staPass = prefs.getString("staPass", cfg.staPass);
  cfg.staEnabled = prefs.getBool("staEnabled", cfg.staEnabled);
  cfg.ethEnabled = prefs.getBool("ethEnabled", cfg.ethEnabled);
  cfg.ethDhcp = prefs.getBool("ethDhcp", cfg.ethDhcp);
  cfg.ethIp = prefs.getString("ethIp", cfg.ethIp);
  cfg.ethGateway = prefs.getString("ethGateway", cfg.ethGateway);
  cfg.ethSubnet = prefs.getString("ethSubnet", cfg.ethSubnet);
  cfg.ethDns = prefs.getString("ethDns", cfg.ethDns);
  cfg.universe = prefs.getInt("universe", cfg.universe);
  prefs.end();
#if defined(ETH_ONLY_DEFAULT)
  cfg.apEnabled = false;
  cfg.staEnabled = false;
  cfg.ethEnabled = true;
  cfg.ethDhcp = true;
#endif
  if (!cfg.apEnabled && !cfg.staEnabled && !cfg.ethEnabled) cfg.apEnabled = true;
  if (cfg.apPass.length() > 0 && cfg.apPass.length() < 8) cfg.apPass = "dmx12345";
  if (!isValidIpString(cfg.apIp)) cfg.apIp = "192.168.44.1";
  if (cfg.universe < 0 || cfg.universe > 32767) cfg.universe = DEFAULT_UNIVERSE;
}

static void saveConfig() {
  prefs.begin("dmx-node", false);
  prefs.putString("hostname", cfg.hostname);
  prefs.putBool("uniqueNames", cfg.uniqueNames);
  prefs.putBool("apEnabled", cfg.apEnabled);
  prefs.putString("apSsid", cfg.apSsid);
  prefs.putString("apPass", cfg.apPass);
  prefs.putString("apIp", cfg.apIp);
  prefs.putString("staSsid", cfg.staSsid);
  prefs.putString("staPass", cfg.staPass);
  prefs.putBool("staEnabled", cfg.staEnabled);
  prefs.putBool("ethEnabled", cfg.ethEnabled);
  prefs.putBool("ethDhcp", cfg.ethDhcp);
  prefs.putString("ethIp", cfg.ethIp);
  prefs.putString("ethGateway", cfg.ethGateway);
  prefs.putString("ethSubnet", cfg.ethSubnet);
  prefs.putString("ethDns", cfg.ethDns);
  prefs.putInt("universe", cfg.universe);
  prefs.end();
}

static String ipToString(IPAddress ip) {
  return String(ip[0]) + "." + ip[1] + "." + ip[2] + "." + ip[3];
}

static void setupDmxUart() {
  pinMode(RS485_DIR_PIN, OUTPUT);
  digitalWrite(RS485_DIR_PIN, HIGH);

  uart_config_t uartConfig = {};
  uartConfig.baud_rate = DMX_BAUD;
  uartConfig.data_bits = UART_DATA_8_BITS;
  uartConfig.parity = UART_PARITY_DISABLE;
  uartConfig.stop_bits = UART_STOP_BITS_2;
  uartConfig.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  uartConfig.source_clk = UART_SCLK_APB;
  uart_driver_install((uart_port_t)DMX_UART, 1024, 0, 0, nullptr, 0);
  uart_param_config((uart_port_t)DMX_UART, &uartConfig);
  uart_set_pin((uart_port_t)DMX_UART, DMX_TX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

static void sendDmxBreak() {
  uart_wait_tx_done((uart_port_t)DMX_UART, pdMS_TO_TICKS(20));
  pinMode(DMX_TX_PIN, OUTPUT);
  digitalWrite(DMX_TX_PIN, LOW);
  delayMicroseconds(120);
  digitalWrite(DMX_TX_PIN, HIGH);
  delayMicroseconds(16);
  uart_set_pin((uart_port_t)DMX_UART, DMX_TX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

static void dmxTask(void *) {
  uint8_t frame[DMX_SLOTS + 1];
  frame[0] = 0;
  for (;;) {
    if (xSemaphoreTake(dmxMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      memcpy(frame, dmx, sizeof(frame));
      xSemaphoreGive(dmxMutex);
    }
    digitalWrite(RS485_DIR_PIN, HIGH);
    sendDmxBreak();
    uart_write_bytes((uart_port_t)DMX_UART, (const char *)frame, sizeof(frame));
    uart_wait_tx_done((uart_port_t)DMX_UART, pdMS_TO_TICKS(30));
    framesSent++;
    vTaskDelay(pdMS_TO_TICKS(DMX_INTERFRAME_MS));
  }
}

static void setDmxSlot(uint16_t channel, uint8_t value) {
  if (channel < 1 || channel > DMX_SLOTS) return;
  if (xSemaphoreTake(dmxMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
    dmx[channel] = value;
    xSemaphoreGive(dmxMutex);
  }
}

static void setDmxRange(uint16_t start, const uint8_t *values, uint16_t count) {
  if (xSemaphoreTake(dmxMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
    for (uint16_t i = 0; i < count; i++) {
      uint16_t ch = start + i;
      if (ch >= 1 && ch <= DMX_SLOTS) dmx[ch] = values[i];
    }
    xSemaphoreGive(dmxMutex);
  }
}

static void clearDmx() {
  if (xSemaphoreTake(dmxMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
    memset(dmx, 0, sizeof(dmx));
    xSemaphoreGive(dmxMutex);
  }
}

static int artnetUniverse(const uint8_t *packet) {
  return packet[14] | (packet[15] << 8);
}

static IPAddress activeNodeIp() {
#if defined(ESP32_ETH01)
  if (cfg.ethEnabled && ETH.linkUp() && ETH.localIP() != IPAddress(0, 0, 0, 0)) return ETH.localIP();
#endif
  if (WiFi.status() == WL_CONNECTED) return WiFi.localIP();
  return WiFi.softAPIP();
}

static void sendArtPollReply(IPAddress targetIp, uint16_t targetPort) {
  uint8_t reply[239] = {0};
  memcpy(reply, "Art-Net\0", 8);
  reply[8] = 0x00;
  reply[9] = 0x21;
  IPAddress ip = activeNodeIp();
  reply[10] = ip[0];
  reply[11] = ip[1];
  reply[12] = ip[2];
  reply[13] = ip[3];
  reply[14] = 0x36;
  reply[15] = 0x19;
  reply[16] = 1;
  reply[17] = 0;
  reply[23] = 0xd0;
  reply[24] = 0x00;
  reply[25] = 0x08;
  String shortName = "ESP32-C3 DMX";
  String longName = cfg.hostname + " Art-Net DMX512 node";
  String nodeReport = "#0001 [0000] OK";
  strncpy((char *)&reply[26], shortName.c_str(), 17);
  strncpy((char *)&reply[44], longName.c_str(), 63);
  strncpy((char *)&reply[108], nodeReport.c_str(), 63);
  reply[172] = 0;
  reply[173] = 1;
  reply[174] = 0x80;
  reply[182] = cfg.universe & 0xff;
  reply[190] = cfg.universe & 0xff;
  reply[200] = 0x00;
  uint8_t mac[6];
  WiFi.macAddress(mac);
  memcpy(&reply[201], mac, 6);
  reply[207] = ip[0];
  reply[208] = ip[1];
  reply[209] = ip[2];
  reply[210] = ip[3];
  reply[211] = 1;
  reply[212] = 0x0e;
  reply[217] = 0x08;
  artnetUdp.beginPacket(targetIp, targetPort);
  artnetUdp.write(reply, sizeof(reply));
  artnetUdp.endPacket();
}

static void handleArtnet() {
  int packetSize = artnetUdp.parsePacket();
  if (packetSize <= 0) return;
  uint8_t packet[600];
  int len = artnetUdp.read(packet, sizeof(packet));
  if (len < 10) return;
  if (memcmp(packet, "Art-Net\0", 8) != 0) return;
  uint16_t opCode = packet[8] | (packet[9] << 8);
  if (opCode == 0x2000) {
    sendArtPollReply(artnetUdp.remoteIP(), artnetUdp.remotePort());
    return;
  }
  if (opCode != 0x5000) return;
  if (len < 18) return;
  int universe = artnetUniverse(packet);
  if (universe != cfg.universe) return;
  uint16_t dmxLen = (packet[16] << 8) | packet[17];
  if (dmxLen > DMX_SLOTS) dmxLen = DMX_SLOTS;
  if (len < 18 + dmxLen) return;

  if (xSemaphoreTake(dmxMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
    dmx[0] = 0;
    memcpy(&dmx[1], &packet[18], dmxLen);
    xSemaphoreGive(dmxMutex);
  }
  activeDemo = "off";
  blackout = false;
  artnetPackets++;
  lastArtnetMs = millis();
}

static void applyShehds8Ch(uint8_t dimmer, uint8_t red, uint8_t green, uint8_t blue, uint8_t white, uint8_t strobe) {
  uint8_t values[8] = {dimmer, red, green, blue, white, strobe, 0, 0};
  setDmxRange(1, values, 8);
}

static void applyFixture7Ch(uint8_t dimmer, uint8_t red, uint8_t green, uint8_t blue, uint8_t white,
                            uint8_t mode, uint8_t strobeSpeed) {
  uint8_t values[7] = {dimmer, red, green, blue, white, mode, strobeSpeed};
  setDmxRange(1, values, 7);
}

static void runDemo() {
  if (activeDemo == "off" || blackout) return;
  uint32_t now = millis();
  uint32_t interval = 700;
  if (activeDemo == "fade-rgbw" || activeDemo == "dimmer-ramp") interval = 45;
  if (activeDemo == "fast-chase") interval = 180;
  if (activeDemo == "channel-scan" || activeDemo == "mode-scan") interval = 1200;
  if (now - lastDemoMs < interval) return;
  lastDemoMs = now;
  demoStep++;

  if (activeDemo == "red") {
    applyFixture7Ch(255, 255, 0, 0, 0, 0, 0);
  } else if (activeDemo == "green") {
    applyFixture7Ch(255, 0, 255, 0, 0, 0, 0);
  } else if (activeDemo == "blue") {
    applyFixture7Ch(255, 0, 0, 255, 0, 0, 0);
  } else if (activeDemo == "white") {
    applyFixture7Ch(255, 0, 0, 0, 255, 0, 0);
  } else if (activeDemo == "all-on") {
    applyFixture7Ch(255, 255, 255, 255, 255, 0, 0);
  } else if (activeDemo == "shehds-color") {
    const uint8_t colors[][4] = {
      {255, 0, 0, 0}, {0, 255, 0, 0}, {0, 0, 255, 0},
      {0, 0, 0, 255}, {255, 120, 0, 0}, {180, 0, 255, 0}
    };
    const uint8_t *c = colors[demoStep % 6];
    applyFixture7Ch(255, c[0], c[1], c[2], c[3], 0, 0);
  } else if (activeDemo == "fast-chase") {
    const uint8_t colors[][4] = {
      {255, 0, 0, 0}, {0, 255, 0, 0}, {0, 0, 255, 0}, {0, 0, 0, 255}
    };
    const uint8_t *c = colors[demoStep % 4];
    applyFixture7Ch(255, c[0], c[1], c[2], c[3], 0, 0);
  } else if (activeDemo == "fade-rgbw") {
    uint8_t phase = demoStep % 255;
    uint8_t segment = (demoStep / 255) % 4;
    uint8_t c[4] = {0, 0, 0, 0};
    c[segment] = 255 - phase;
    c[(segment + 1) % 4] = phase;
    applyFixture7Ch(255, c[0], c[1], c[2], c[3], 0, 0);
  } else if (activeDemo == "dimmer-ramp") {
    uint8_t level = demoStep % 510;
    if (level > 255) level = 510 - level;
    applyFixture7Ch(level, 255, 255, 255, 255, 0, 0);
  } else if (activeDemo == "shehds-strobe") {
    applyFixture7Ch(255, 255, 255, 255, 255, 0, (demoStep % 2) ? 80 : 180);
  } else if (activeDemo == "channel-scan") {
    clearDmx();
    uint8_t channel = (demoStep % 8) + 1;
    if (channel != 1) setDmxSlot(1, 255);
    setDmxSlot(channel, 255);
  } else if (activeDemo == "mode-scan") {
    uint8_t mode = (demoStep % 16) * 16;
    applyFixture7Ch(255, 255, 80, 80, 80, mode, 160);
  }
}

static String statusJson() {
  String json = "{";
  json += "\"fw\":\"" + String(FW_VERSION) + "\",";
  json += "\"hw\":\"" + String(HW_TARGET) + "\",";
  json += "\"hostname\":\"" + jsonEscape(cfg.hostname) + "\",";
  json += "\"effectiveHostname\":\"" + jsonEscape(effectiveHostname()) + "\",";
  json += "\"macSuffix\":\"" + macSuffix() + "\",";
  json += "\"uniqueNames\":" + String(cfg.uniqueNames ? "true" : "false") + ",";
  json += "\"apEnabled\":" + String(cfg.apEnabled ? "true" : "false") + ",";
  json += "\"wifiRuntime\":" + String(wifiRuntimeEnabled ? "true" : "false") + ",";
  json += "\"wifiSuppressedByEth\":" + String(autoWifiSuppressedByEth ? "true" : "false") + ",";
  json += "\"staEnabled\":" + String(cfg.staEnabled ? "true" : "false") + ",";
  json += "\"ethEnabled\":" + String(cfg.ethEnabled ? "true" : "false") + ",";
  json += "\"ethDhcp\":" + String(cfg.ethDhcp ? "true" : "false") + ",";
  json += "\"apIp\":\"" + (cfg.apEnabled ? ipToString(WiFi.softAPIP()) : String("")) + "\",";
  json += "\"staIp\":\"" + (WiFi.status() == WL_CONNECTED ? ipToString(WiFi.localIP()) : String("")) + "\",";
#if defined(ESP32_ETH01)
  json += "\"ethIp\":\"" + (cfg.ethEnabled && ETH.linkUp() ? ipToString(ETH.localIP()) : String("")) + "\",";
  json += "\"ethLink\":" + String(cfg.ethEnabled && ETH.linkUp() ? "true" : "false") + ",";
#else
  json += "\"ethIp\":\"\",";
  json += "\"ethLink\":false,";
#endif
  json += "\"wifiMode\":\"" + String(!wifiRuntimeEnabled ? "OFF" : (cfg.apEnabled && cfg.staEnabled ? "AP+STA" : (cfg.apEnabled ? "AP" : (cfg.staEnabled ? "STA" : "OFF")))) + "\",";
  json += "\"staConnected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
  json += "\"universe\":" + String(cfg.universe) + ",";
  json += "\"framesSent\":" + String(framesSent) + ",";
  json += "\"artnetPackets\":" + String(artnetPackets) + ",";
  json += "\"lastArtnetAgeMs\":" + String(lastArtnetMs ? millis() - lastArtnetMs : 0) + ",";
  json += "\"activeDemo\":\"" + activeDemo + "\",";
  json += "\"freeHeap\":" + String(ESP.getFreeHeap());
  json += "}";
  return json;
}

static String configJson() {
  String json = "{";
  json += "\"hostname\":\"" + jsonEscape(cfg.hostname) + "\",";
  json += "\"uniqueNames\":" + String(cfg.uniqueNames ? "true" : "false") + ",";
  json += "\"apEnabled\":" + String(cfg.apEnabled ? "true" : "false") + ",";
  json += "\"apSsid\":\"" + jsonEscape(cfg.apSsid) + "\",";
  json += "\"apPass\":\"" + jsonEscape(cfg.apPass) + "\",";
  json += "\"apIp\":\"" + jsonEscape(cfg.apIp) + "\",";
  json += "\"staSsid\":\"" + jsonEscape(cfg.staSsid) + "\",";
  json += "\"staPass\":\"" + jsonEscape(cfg.staPass) + "\",";
  json += "\"staEnabled\":" + String(cfg.staEnabled ? "true" : "false") + ",";
  json += "\"ethEnabled\":" + String(cfg.ethEnabled ? "true" : "false") + ",";
  json += "\"ethDhcp\":" + String(cfg.ethDhcp ? "true" : "false") + ",";
  json += "\"ethIp\":\"" + jsonEscape(cfg.ethIp) + "\",";
  json += "\"ethGateway\":\"" + jsonEscape(cfg.ethGateway) + "\",";
  json += "\"ethSubnet\":\"" + jsonEscape(cfg.ethSubnet) + "\",";
  json += "\"ethDns\":\"" + jsonEscape(cfg.ethDns) + "\",";
  json += "\"universe\":" + String(cfg.universe);
  json += "}";
  return json;
}

static const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html><html lang="pl"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32-C3 Art-Net DMX</title>
<style>
[hidden]{display:none!important}
:root{color-scheme:dark;--bg:#111316;--panel:#1b2026;--panel2:#242b33;--text:#eef3f8;--muted:#9aa8b5;--line:#3a4652;--accent:#2fd17c;--warn:#ffb84d;--bad:#ff5f6d}
*{box-sizing:border-box}html{overflow-x:hidden}body{margin:0;background:var(--bg);color:var(--text);font:15px/1.45 system-ui,-apple-system,Segoe UI,sans-serif;overflow-x:hidden}
header{padding:16px;border-bottom:1px solid var(--line);background:#171b20;display:flex;gap:12px;align-items:center;justify-content:space-between;flex-wrap:wrap}
h1{font-size:20px;margin:0}main{width:100%;padding:16px;max-width:1680px;margin:auto;display:grid;grid-template-columns:1fr;gap:14px;align-items:start}
.tabs{display:flex;gap:8px;padding:10px 16px;background:#12161b;border-bottom:1px solid var(--line);position:sticky;top:0;z-index:2;overflow-x:auto;scrollbar-width:thin}.tabs button{flex:0 0 auto;white-space:nowrap}.tabs button.active{background:#126b42;border-color:#20985f}
section{min-width:0;background:var(--panel);border:1px solid var(--line);border-radius:8px;padding:14px}.hidden,section.hidden{display:none!important}h2{font-size:16px;margin:0 0 12px}.grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px}
.metric{background:var(--panel2);border-radius:6px;padding:10px}.label{color:var(--muted);font-size:12px}.value{font-size:16px;font-weight:700;overflow-wrap:anywhere}
button,input,select{font:inherit;border-radius:6px;border:1px solid var(--line);background:#12161b;color:var(--text);padding:9px 10px;min-height:42px;max-width:100%}
button{cursor:pointer;background:#26313a}button.primary{background:#126b42;border-color:#20985f}button.warn{background:#6b4112;border-color:#9a641f}button.bad{background:#6b1b25;border-color:#973140}
button:active{transform:translateY(1px)}label{display:grid;gap:6px;color:var(--muted);font-size:13px;margin-bottom:10px}input,select{width:100%}
input[type=checkbox]{width:auto;min-height:auto}.checks{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:8px;margin:0 0 10px}.check{display:flex;gap:8px;align-items:center;background:var(--panel2);border:1px solid var(--line);border-radius:6px;padding:9px 10px;color:var(--text);margin:0}.profileBar{display:grid;grid-template-columns:2fr 1fr;gap:8px;margin-bottom:10px}.subhead{color:var(--muted);font-size:13px;margin:-6px 0 12px}
.row{display:flex;gap:8px;flex-wrap:wrap}.row>*{flex:1 1 140px;min-width:0}.channels{display:grid;gap:10px}.ch{display:grid;grid-template-columns:46px minmax(0,1fr) 42px;gap:8px;align-items:center}.ch span{overflow-wrap:anywhere}.ch input{padding:0;min-width:0}
.wheelWrap{display:grid;grid-template-columns:220px minmax(0,1fr);gap:14px;align-items:center;margin-bottom:12px}.wheelWrap canvas{width:100%;max-width:240px;aspect-ratio:1;border-radius:50%;touch-action:none;background:#111;border:1px solid var(--line);cursor:crosshair}.swatches{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:8px}.swatch{min-height:42px;border-radius:6px;border:1px solid var(--line);cursor:pointer}
.preview{display:grid;grid-template-columns:1fr;gap:12px}.stage{position:relative;min-height:260px;background:radial-gradient(circle at 50% 82%,#29313a 0,#11161b 54%,#090b0e 100%);border:1px solid var(--line);border-radius:8px;overflow:hidden}.beam{position:absolute;left:50%;top:10px;width:76%;max-width:260px;height:205px;transform:translateX(-50%);clip-path:polygon(42% 0,58% 0,100% 100%,0 100%);background:radial-gradient(ellipse at 50% 100%,var(--beam,#ff0000) 0,rgba(255,255,255,.08) 65%,transparent 100%);opacity:var(--beamOpacity,.8);filter:blur(1px)}.fixtureBody{position:absolute;left:50%;bottom:18px;width:118px;height:72px;transform:translateX(-50%);background:#0c0f12;border:1px solid #4a5662;border-radius:12px;box-shadow:0 12px 28px rgba(0,0,0,.55)}.lens{position:absolute;left:50%;top:10px;width:52px;height:52px;transform:translateX(-50%);border-radius:50%;background:var(--beam,#ff0000);box-shadow:0 0 22px var(--beam,#ff0000)}.previewGrid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px}.previewCard{background:var(--panel2);border:1px solid var(--line);border-radius:6px;padding:10px;min-width:0}.previewCard b{display:block;font-size:18px;overflow-wrap:anywhere}.colorChip{width:100%;height:46px;border-radius:6px;border:1px solid var(--line);background:var(--beam,#ff0000)}
.steps{display:grid;gap:8px;max-height:260px;overflow:auto}.step{display:grid;grid-template-columns:34px minmax(0,1fr) auto;gap:8px;align-items:center;background:var(--panel2);border:1px solid var(--line);border-radius:6px;padding:8px}.step.active{border-color:var(--accent);box-shadow:0 0 0 1px var(--accent) inset}.stepSwatch{width:30px;height:30px;border-radius:6px;border:1px solid var(--line)}.step small{color:var(--muted);display:block;overflow-wrap:anywhere}.step b{overflow-wrap:anywhere}.compact{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:8px}.programStatus{color:var(--muted);font-size:13px;min-height:20px}
pre{white-space:pre-wrap;word-break:break-word;background:#0c0f12;border-radius:6px;padding:10px;max-height:260px;overflow:auto;color:#d8e2eb}
.ok{color:var(--accent)}.warnText{color:var(--warn)}
@media(min-width:760px){main{grid-template-columns:repeat(auto-fit,minmax(320px,1fr))}}
@media(max-width:800px){header{padding:12px}h1{font-size:18px}main{grid-template-columns:1fr;padding:10px;gap:10px}section{padding:12px}.wheelWrap{grid-template-columns:1fr}.wheelWrap canvas{justify-self:center;max-width:260px;width:82vw}.swatches{grid-template-columns:repeat(4,1fr)}.compact{grid-template-columns:1fr}.previewGrid{grid-template-columns:repeat(2,minmax(0,1fr))}.stage{min-height:220px}.row>*{flex-basis:calc(50% - 8px)}}
@media(max-width:520px){body{font-size:14px}.tabs{padding:8px 10px}.tabs button{padding:8px 10px;min-height:38px}button,input,select{min-height:44px}.row{display:grid;grid-template-columns:1fr 1fr}.row>*{width:100%;min-width:0}.ch{grid-template-columns:42px minmax(0,1fr) 36px;gap:6px}.previewGrid{grid-template-columns:1fr}.stage{min-height:190px}.beam{height:165px}.fixtureBody{width:104px;height:62px}.lens{width:44px;height:44px}.step{grid-template-columns:30px minmax(0,1fr);align-items:start}.step button{grid-column:1 / -1}.stepSwatch{width:28px;height:28px}}
@media(max-width:520px){.checks,.profileBar{grid-template-columns:1fr}}
@media(max-width:360px){main{padding:8px}section{padding:10px}.row{grid-template-columns:1fr}.swatches{grid-template-columns:repeat(2,1fr)}}
</style></head><body>
<header><h1>ESP32-C3 Art-Net DMX</h1><div id="badge" class="warnText">łączenie...</div></header>
<nav class="tabs">
<button class="active" onclick="setTab('home',this)">Start</button>
<button onclick="setTab('live',this)">Live</button>
<button onclick="setTab('program',this)">Programy</button>
<button onclick="setTab('monitor',this)">Monitor</button>
<button onclick="setTab('config',this)">Konfiguracja</button>
</nav>
<main>
<section data-tab="home"><h2>Usługi urządzenia</h2>
<div class="grid" id="homeServices"></div>
<h2>Adresy i porty</h2>
<div class="grid" id="homeIps"></div>
</section>
<section data-tab="home"><h2>Art-Net / DMX</h2>
<div class="subhead">Node odbiera pakiety ArtDMX po UDP i stale nadaje pełną ramkę DMX512 na MAX3485.</div>
<div class="grid">
<div class="metric"><div class="label">Art-Net UDP</div><div class="value">port 6454</div></div>
<div class="metric"><div class="label">Universe</div><div class="value" id="homeUniverse">0</div></div>
<div class="metric"><div class="label">DMX wyjście</div><div class="value">250000 baud, 8N2</div></div>
<div class="metric"><div class="label">MAX3485</div><div class="value">DI/RX z ESP, EN jako TX enable</div></div>
</div>
</section>
<section data-tab="home"><h2>Aktualizacja i profile</h2>
<div class="grid">
<div class="metric"><div class="label">OTA WWW</div><div class="value">Konfiguracja -> Aktualizacja OTA</div></div>
<div class="metric"><div class="label">USB/Serial</div><div class="value">PlatformIO / esptool</div></div>
<div class="metric"><div class="label">Profile lamp</div><div class="value">Live i Programy używają wybranego profilu</div></div>
<div class="metric"><div class="label">Domyślny profil</div><div class="value">SHE-FS403A 7CH</div></div>
</div>
</section>
<section data-tab="monitor"><h2>Status</h2><div class="grid" id="status"></div></section>
<section data-tab="live"><h2>Live: urządzenie / profil</h2>
<div class="profileBar">
<label>Profil urządzenia<select id="liveProfile" onchange="syncProfileSelects('live');updateProfileUi();updatePreview()"></select></label>
<label>Adres DMX<input id="liveAddress" type="number" min="1" max="512" value="1" oninput="syncAddress('live');updatePreview()"></label>
</div>
<div class="subhead" id="profileInfo">Sterowanie aktywnym profilem.</div>
<div class="row">
<button class="primary" onclick="demo('red')">Red</button>
<button class="primary" onclick="demo('green')">Green</button>
<button class="primary" onclick="demo('blue')">Blue</button>
<button class="primary" onclick="demo('white')">White</button>
<button onclick="demo('all-on')">All on</button>
</div>
<div class="row">
<button onclick="demo('shehds-color')">Color chase</button>
<button onclick="demo('fast-chase')">Fast chase</button>
<button onclick="demo('fade-rgbw')">Fade RGBW</button>
<button onclick="demo('dimmer-ramp')">Dimmer ramp</button>
</div>
<div class="row">
<button onclick="demo('shehds-strobe')">Strobe 7CH</button>
<button onclick="demo('channel-scan')">CH1-CH7 scan</button>
<button onclick="demo('mode-scan')">CH6 mode scan</button>
<button class="bad" onclick="post('/api/blackout')">Blackout</button>
</div></section>
<section data-tab="live"><h2 id="liveControlTitle">Sterowanie profilem</h2>
<label>Kolor RGB<input id="pickColor" type="color" value="#ff0000" oninput="colorChanged()"></label>
<div class="wheelWrap">
<canvas id="colorWheel" width="260" height="260" aria-label="Koło kolorów"></canvas>
<div class="swatches">
<button class="swatch" style="background:#ff0000" onclick="presetColor(255,0,0,0)" title="Red"></button>
<button class="swatch" style="background:#00ff00" onclick="presetColor(0,255,0,0)" title="Green"></button>
<button class="swatch" style="background:#0000ff" onclick="presetColor(0,0,255,0)" title="Blue"></button>
<button class="swatch" style="background:#ffffff" onclick="presetColor(0,0,0,255)" title="White"></button>
<button class="swatch" style="background:#ffff00" onclick="presetColor(255,255,0,0)" title="Yellow"></button>
<button class="swatch" style="background:#00ffff" onclick="presetColor(0,255,255,0)" title="Cyan"></button>
<button class="swatch" style="background:#ff00ff" onclick="presetColor(255,0,255,0)" title="Magenta"></button>
<button class="swatch" style="background:#ff7800" onclick="presetColor(255,120,0,0)" title="Amber"></button>
</div>
</div>
<div class="channels">
<div class="ch"><span>Dim</span><input id="fxDim" type="range" min="0" max="255" value="255" oninput="syncFxLabel(this)"><b>255</b></div>
<div class="ch"><span>R</span><input id="fxR" type="range" min="0" max="255" value="255" oninput="syncFxLabel(this)"><b>255</b></div>
<div class="ch"><span>G</span><input id="fxG" type="range" min="0" max="255" value="0" oninput="syncFxLabel(this)"><b>0</b></div>
<div class="ch"><span>B</span><input id="fxB" type="range" min="0" max="255" value="0" oninput="syncFxLabel(this)"><b>0</b></div>
<div class="ch"><span>W</span><input id="fxW" type="range" min="0" max="255" value="0" oninput="syncFxLabel(this)"><b>0</b></div>
<div class="ch"><span>CH6</span><input id="fxMode" type="range" min="0" max="255" value="0" oninput="syncFxLabel(this)"><b>0</b></div>
<div class="ch"><span>CH7</span><input id="fxStrobe" type="range" min="0" max="255" value="0" oninput="syncFxLabel(this)"><b>0</b></div>
<div class="ch hidden"><span>CH8</span><input id="fxSpeed" type="range" min="0" max="255" value="0" oninput="syncFxLabel(this)"><b>0</b></div>
</div>
<div class="row">
<button class="primary" onclick="sendFixture()">Ustaw</button>
<button onclick="presetColor(255,255,255,255)">Wszystkie LED</button>
<button onclick="setStrobe(40)">Strobe wolny</button>
<button onclick="setStrobe(180)">Strobe szybki</button>
<button onclick="setStrobe(0)">Strobe off</button>
</div>
<h2>Tryby autonomiczne CH6</h2>
<div class="row">
<button onclick="setAutoMode(0,0)">Manual RGBW</button>
<button onclick="setAutoMode(75,120)">Program jump</button>
<button onclick="setAutoMode(125,120)">Program gradient</button>
<button onclick="setAutoMode(175,120)">Program pulse</button>
<button class="warn" onclick="setAutoMode(225,160)">Sound action</button>
</div></section>
<section data-tab="live"><h2>Wizualizacja</h2>
<div class="preview">
<div class="stage" id="stagePreview">
<div class="beam"></div><div class="fixtureBody"><div class="lens"></div></div>
</div>
<div class="previewGrid">
<div class="previewCard"><span class="label">Kolor wynikowy</span><div class="colorChip" id="previewChip"></div></div>
<div class="previewCard"><span class="label">Adres / model</span><b id="previewFixture">1 / SHE</b></div>
<div class="previewCard"><span class="label">Jasność</span><b id="previewDim">255</b></div>
<div class="previewCard"><span class="label">CH6 mode</span><b id="previewMode">0</b></div>
<div class="previewCard"><span class="label">CH7 strobe/speed</span><b id="previewStrobe">0</b></div>
<div class="previewCard hidden"><span class="label">CH8 speed</span><b id="previewSpeed">0</b></div>
</div>
</div></section>
<section data-tab="live"><h2>Ręczne kanały DMX</h2><div class="channels" id="channels"></div>
<div class="row"><button class="primary" onclick="sendChannels()">Wyślij</button><button onclick="zeroLocal()">Wyzeruj suwaki</button></div></section>
<section data-tab="program"><h2>Programator scen</h2>
<div class="compact">
<label>Nazwa programu<input id="progName" value="Program 1"></label>
<label>Lampa / adres DMX<input id="fixtureAddress" type="number" min="1" max="512" value="1" oninput="syncAddress('program');updatePreview()"></label>
<label>Urządzenie / profil<select id="fixtureModel" onchange="syncProfileSelects('program');updateProfileUi();updatePreview()"></select></label>
</div>
<div class="compact">
<label>Czas kroku ms<input id="stepMs" type="number" min="50" max="60000" value="1000"></label>
<label>Powtórzenia<input id="playCount" type="number" min="0" max="999" value="0"></label>
<label>Auto podgląd<select id="autoPreview"><option value="1">tak</option><option value="0">nie</option></select></label>
</div>
<div class="row">
<button class="primary" onclick="recordStep()">Dodaj krok z aktualnych suwaków</button>
<button class="primary" onclick="replaceSelectedStep()">Podmień wybrany krok</button>
<button onclick="clearSelectedStep()">Odznacz krok</button>
<button onclick="playProgram()">Play</button>
<button onclick="stopProgram()">Stop</button>
<button onclick="saveProgram()">Zapisz</button>
<button onclick="loadProgram()">Wczytaj</button>
<button class="bad" onclick="clearProgram()">Wyczyść</button>
</div>
<div class="row">
<button onclick="loadDemoProgram('softRainbow')">Demo: soft rainbow</button>
<button onclick="loadDemoProgram('partyChase')">Demo: party chase</button>
<button onclick="loadDemoProgram('pulseWhite')">Demo: white pulse</button>
<button onclick="loadDemoProgram('strobeBurst')">Demo: strobe burst</button>
<button onclick="loadDemoProgram('modeExplore')">Demo: CH6 explore</button>
</div>
<div class="row">
<button onclick="loadDemoProgram('colorSelect')">Demo: program jump</button>
<button onclick="loadDemoProgram('builtinJump')">Demo: built-in jump</button>
<button onclick="loadDemoProgram('builtinGradient')">Demo: built-in gradient</button>
<button onclick="loadDemoProgram('builtinPulse')">Demo: built-in pulse</button>
<button onclick="loadDemoProgram('soundMode')">Demo: sound mode</button>
</div>
<div class="row">
<button onclick="loadDemoProgram('warmFade')">Demo: warm fade</button>
<button onclick="loadDemoProgram('police')">Demo: blue/red alert</button>
<button onclick="loadDemoProgram('breathRgb')">Demo: RGB breath</button>
<button onclick="loadDemoProgram('randomPop')">Demo: color pops</button>
</div>
<div class="row">
<button onclick="loadDemoProgram('fire')">Demo: fire</button>
<button onclick="loadDemoProgram('ocean')">Demo: ocean</button>
<button onclick="loadDemoProgram('blueShades')">Demo: blue shades</button>
<button onclick="loadDemoProgram('orangeShades')">Demo: orange shades</button>
<button onclick="loadDemoProgram('clubSweep')">Demo: club sweep</button>
<button onclick="loadDemoProgram('sunrise')">Demo: sunrise</button>
<button onclick="loadDemoProgram('blackoutBeat')">Demo: blackout beat</button>
</div>
<div class="row">
<button onclick="loadDemoProgram('twoMirror')">Demo: 2-lamp mirror</button>
<button onclick="loadDemoProgram('twoAlternate')">Demo: 2-lamp alternate</button>
<button onclick="loadDemoProgram('twoWave')">Demo: 2-lamp wave</button>
</div>
<div class="programStatus" id="programStatus"></div>
<div class="steps" id="steps"></div>
</section>
<section data-tab="config"><h2>Konfiguracja sieci / Art-Net</h2>
<label>Prefix hostname<input id="hostname" value="esp32-c3-dmx"></label>
<label>Unikalne nazwy<select id="uniqueNames"><option value="1">dopisz ostatnie 4 znaki MAC</option><option value="0">użyj dokładnie wpisanych nazw</option></select></label>
<label>Universe<input id="universe" type="number" min="0" max="32767" value="0"></label>
<h2>Interfejsy</h2>
<div class="checks">
<label class="check"><input id="apEnabled" type="checkbox" checked> AP</label>
<label class="check"><input id="staEnabled" type="checkbox" checked> STA Wi-Fi</label>
<label class="check"><input id="ethEnabled" type="checkbox" checked> Ethernet</label>
</div>
<label>Adres Ethernet<select id="ethDhcp"><option value="1">DHCP</option><option value="0">statyczny IP</option></select></label>
<label>Prefix SSID AP<input id="apSsid" value="DMX-Node-C3"></label>
<label>AP hasło<input id="apPass" type="password" value="dmx12345"></label>
<label>AP IP<input id="apIp" placeholder="192.168.44.1"></label>
<label>SSID sieci domowej<input id="staSsid"></label>
<label>Hasło sieci domowej<input id="staPass" type="password"></label>
<div class="grid">
<label>ETH IP<input id="ethIp" placeholder="192.168.88.50"></label>
<label>ETH brama<input id="ethGateway" placeholder="192.168.88.1"></label>
<label>ETH maska<input id="ethSubnet" placeholder="255.255.255.0"></label>
<label>ETH DNS<input id="ethDns" placeholder="192.168.88.1"></label>
</div>
<div class="row"><button class="primary" onclick="saveConfig()">Zapisz i restartuj</button><button onclick="scanWifi()">Skanuj Wi-Fi</button></div>
<pre id="wifiScan">Kliknij Skanuj Wi-Fi, aby zobaczyć sieci widoczne dla ESP.</pre>
<h2>Profile urządzeń</h2>
<div class="row"><button onclick="exportProfiles()">Eksportuj profile JSON</button><label>Import profili JSON<input id="profileFile" type="file" accept=".json,application/json" onchange="importProfiles()"></label></div>
<pre id="profileStatus">Profile są używane przez Live i Programator.</pre>
<h2>Aktualizacja OTA</h2>
<label>Firmware .bin<input id="otaFile" type="file" accept=".bin,application/octet-stream"></label>
<div class="row"><button class="warn" onclick="uploadOta()">Wgraj firmware przez WWW</button></div>
<pre id="otaStatus">Użyj pliku firmware.bin zbudowanego dla tego samego hardware.</pre></section>
<section data-tab="monitor"><h2>Logi</h2><pre id="logs"></pre><div class="row"><button onclick="loadLogs()">Odśwież</button></div></section>
</main>
<script>
const chNames=['Dim','Red','Green','Blue','White','Mode','Strobe/Speed'];
let fixtureProfiles={
  'she-fs403a-7ch':{type:'she7',label:'SHE-FS403A / SHE-FS403A 7CH',short:'SHE-FS403A 7CH',channels:7,info:'CH1 dimmer, CH2 red, CH3 green, CH4 blue, CH5 white, CH6 auto program, CH7 strobe/speed.',labels:['Dim','R','G','B','W','CH6 mode','CH7 strobe/speed']},
  'rgbw-4ch':{type:'rgbw4',label:'Generic RGBW 4CH',short:'RGBW 4CH',channels:4,info:'CH1 red, CH2 green, CH3 blue, CH4 white. Dimmer realizowany programowo w wizualizacji.',labels:['Dim','R','G','B','W','-','-']},
  'raw-8ch':{type:'raw8',label:'Generic RAW 8CH',short:'RAW 8CH',channels:8,info:'Prosty profil testowy: wartości suwaków trafiają kolejno w kanały DMX.',labels:['CH1','CH2','CH3','CH4','CH5','CH6','CH7','CH8']}
};
function setTab(name,btn){document.querySelectorAll('section[data-tab]').forEach(s=>s.classList.toggle('hidden',s.dataset.tab!==name));document.querySelectorAll('.tabs button').forEach(b=>b.classList.remove('active'));if(btn)btn.classList.add('active')}
function metric(k,v){return `<div class="metric"><div class="label">${k}</div><div class="value">${v||'-'}</div></div>`}
function post(url,body){return fetch(url,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body}).then(r=>r.text()).then(()=>{refresh();loadLogs();})}
function postQuiet(url,body){return fetch(url,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body})}
function activeModel(){return document.getElementById('liveProfile').value||document.getElementById('fixtureModel').value||'she-fs403a-7ch'}
function activeAddress(){return Math.max(1,Math.min(512,+document.getElementById('liveAddress').value||1))}
function profile(id){return fixtureProfiles[id]||fixtureProfiles['she-fs403a-7ch']}
function profileMap(p,v){if(p.type==='rgbw4')return [v[1],v[2],v[3],v[4]];if(p.type==='raw8')return v.slice(0,8);return [v[0],v[1],v[2],v[3],v[4],v[6],v[5]]}
function profilePreview(p,v){if(p.type==='rgbw4')return [255,v[0]||0,v[1]||0,v[2]||0,v[3]||0,0,0,0];if(p.type==='raw8')return [v[0]||0,v[1]||0,v[2]||0,v[3]||0,v[4]||0,v[5]||0,v[6]||0,v[7]||0];return [v[0]||0,v[1]||0,v[2]||0,v[3]||0,v[4]||0,v[6]||0,v[5]||0,0]}
function fillProfileSelects(){['liveProfile','fixtureModel'].forEach(id=>{let el=document.getElementById(id);el.innerHTML=Object.keys(fixtureProfiles).map(k=>`<option value="${k}">${fixtureProfiles[k].label}</option>`).join('');el.value='she-fs403a-7ch'})}
function syncProfileSelects(source){let from=source==='live'?liveProfile:fixtureModel,to=source==='live'?fixtureModel:liveProfile;to.value=from.value}
function syncAddress(source){let from=source==='live'?liveAddress:fixtureAddress,to=source==='live'?fixtureAddress:liveAddress;to.value=from.value}
function updateProfileUi(){let p=profile(activeModel());document.getElementById('profileInfo').textContent=p.info;document.getElementById('liveControlTitle').textContent='Sterowanie: '+p.short;['fxDim','fxR','fxG','fxB','fxW','fxMode','fxStrobe','fxSpeed'].forEach((id,i)=>{let row=document.getElementById(id).closest('.ch'),label=row.querySelector('span');label.textContent=p.labels[i]||('CH'+(i+1));let hide=(p.type==='rgbw4'&&['fxMode','fxStrobe','fxSpeed'].includes(id))||(p.type==='she7'&&id==='fxSpeed');row.classList.toggle('hidden',hide)})}
function demo(name){const demos={red:[255,255,0,0,0,0,0,0],green:[255,0,255,0,0,0,0,0],blue:[255,0,0,255,0,0,0,0],white:[255,0,0,0,255,0,0,0],'all-on':[255,255,255,255,255,0,0,0],'shehds-color':[255,255,120,0,0,0,0,0],'fast-chase':[255,255,0,255,0,0,0,0],'fade-rgbw':[255,0,120,255,0,0,0,0],'dimmer-ramp':[140,255,255,255,255,0,0,0],'shehds-strobe':[255,255,255,255,255,180,0,0],'channel-scan':[255,255,255,255,255,0,0,0],'mode-scan':[255,255,80,80,80,0,160,0]};let vals=demos[name]||[0,0,0,0,0,0,0,0];setSlidersFromPreviewValues(vals);sendRange(activeAddress(),modelValues(activeModel(),vals));updatePreview(vals,activeModel(),activeAddress());loadLogs()}
function zeroLocal(){document.querySelectorAll('.ch input').forEach(i=>{i.value=0;i.nextElementSibling.textContent='0'})}
function sendChannels(){let p=[];document.querySelectorAll('.ch input').forEach((i,n)=>p.push('c'+(n+1)+'='+i.value));post('/api/dmx',p.join('&'))}
function syncFxLabel(el){el.nextElementSibling.textContent=el.value;updatePreview()}
function hex(v){return Math.max(0,Math.min(255,v)).toString(16).padStart(2,'0')}
function setFx(id,v){let el=document.getElementById(id);el.value=v;el.nextElementSibling.textContent=v;updatePreview()}
function colorChanged(){let c=document.getElementById('pickColor').value;setFx('fxR',parseInt(c.slice(1,3),16));setFx('fxG',parseInt(c.slice(3,5),16));setFx('fxB',parseInt(c.slice(5,7),16));updatePreview()}
function syncPicker(){document.getElementById('pickColor').value='#'+hex(+fxR.value)+hex(+fxG.value)+hex(+fxB.value)}
function presetColor(r,g,b,w){setFx('fxDim',255);setFx('fxR',r);setFx('fxG',g);setFx('fxB',b);setFx('fxW',w);setFx('fxMode',0);setFx('fxStrobe',0);setFx('fxSpeed',0);syncPicker();sendFixture()}
function setStrobe(v){setFx('fxStrobe',v);sendFixture()}
function setAutoMode(mode,speed){setFx('fxDim',255);setFx('fxMode',mode);setFx('fxStrobe',speed);if(mode>0&&(+fxR.value+ +fxG.value+ +fxB.value+ +fxW.value)===0){setFx('fxR',255);setFx('fxG',255);setFx('fxB',255);setFx('fxW',255)}sendFixture()}
function sendFixture(){syncPicker();let vals=modelValues(activeModel(),currentFixtureValues());sendRange(activeAddress(),vals).then(()=>{refresh();loadLogs();});updatePreview(vals,activeModel(),activeAddress())}
function currentFixtureValues(){return [+fxDim.value,+fxR.value,+fxG.value,+fxB.value,+fxW.value,+fxStrobe.value,+fxMode.value,+fxSpeed.value]}
function modelValues(model,vals){return profileMap(profile(model),vals)}
function previewValues(model,vals){return profilePreview(profile(model),vals)}
function valuesColor(vals,model){let p=previewValues(model||activeModel(),vals),dim=p[0]/255,w=p[4];return [Math.min(255,Math.round((p[1]+w*.65)*dim)),Math.min(255,Math.round((p[2]+w*.65)*dim)),Math.min(255,Math.round((p[3]+w*.65)*dim))]}
function updatePreview(vals,model,addr){let m=model||activeModel(),p=previewValues(m,vals||modelValues(m,currentFixtureValues())),rgb=valuesColor(vals||modelValues(m,currentFixtureValues()),m),color='#'+hex(rgb[0])+hex(rgb[1])+hex(rgb[2]),dim=p[0],opacity=Math.max(.08,dim/255);document.getElementById('stagePreview').style.setProperty('--beam',color);document.getElementById('stagePreview').style.setProperty('--beamOpacity',opacity);document.getElementById('previewChip').style.background=color;document.getElementById('previewFixture').textContent=(addr||activeAddress())+' / '+profile(m).short;document.getElementById('previewDim').textContent=dim;document.getElementById('previewStrobe').textContent=p[5];document.getElementById('previewMode').textContent=p[6];document.getElementById('previewSpeed').textContent=p[7]}
function sendRange(addr,vals){return postQuiet('/api/range','addr='+addr+'&values='+vals.join(','))}
function hsvToRgb(h,s,v){let c=v*s,x=c*(1-Math.abs((h/60)%2-1)),m=v-c,r=0,g=0,b=0;if(h<60){r=c;g=x}else if(h<120){r=x;g=c}else if(h<180){g=c;b=x}else if(h<240){g=x;b=c}else if(h<300){r=x;b=c}else{r=c;b=x}return [Math.round((r+m)*255),Math.round((g+m)*255),Math.round((b+m)*255)]}
function drawWheel(){let canvas=document.getElementById('colorWheel'),ctx=canvas.getContext('2d'),w=canvas.width,h=canvas.height,cx=w/2,cy=h/2,img=ctx.createImageData(w,h);for(let y=0;y<h;y++){for(let x=0;x<w;x++){let dx=x-cx,dy=y-cy,dist=Math.sqrt(dx*dx+dy*dy),idx=(y*w+x)*4;if(dist>cx){img.data[idx+3]=0;continue}let hue=(Math.atan2(dy,dx)*180/Math.PI+360)%360,sat=dist/cx,rgb=hsvToRgb(hue,sat,1);img.data[idx]=rgb[0];img.data[idx+1]=rgb[1];img.data[idx+2]=rgb[2];img.data[idx+3]=255}}ctx.putImageData(img,0,0);ctx.beginPath();ctx.arc(cx,cy,cx-1,0,Math.PI*2);ctx.strokeStyle='#3a4652';ctx.stroke()}
let wheelDragging=false,lastWheelSend=0;
function pickWheel(e){let canvas=document.getElementById('colorWheel'),rect=canvas.getBoundingClientRect(),x=(e.clientX-rect.left)*canvas.width/rect.width,y=(e.clientY-rect.top)*canvas.height/rect.height,cx=canvas.width/2,cy=canvas.height/2,dx=x-cx,dy=y-cy,dist=Math.sqrt(dx*dx+dy*dy);if(dist>cx)return;let hue=(Math.atan2(dy,dx)*180/Math.PI+360)%360,rgb=hsvToRgb(hue,dist/cx,1);setFx('fxR',rgb[0]);setFx('fxG',rgb[1]);setFx('fxB',rgb[2]);setFx('fxW',0);syncPicker();let now=Date.now();if(now-lastWheelSend>90){lastWheelSend=now;sendFixture()}}
function setupWheel(){let canvas=document.getElementById('colorWheel');drawWheel();canvas.addEventListener('pointerdown',e=>{wheelDragging=true;canvas.setPointerCapture(e.pointerId);pickWheel(e)});canvas.addEventListener('pointermove',e=>{if(wheelDragging)pickWheel(e)});canvas.addEventListener('pointerup',e=>{wheelDragging=false;pickWheel(e);sendFixture()});canvas.addEventListener('pointercancel',()=>wheelDragging=false)}
let programSteps=[],programTimer=null,programPlaying=false,programIndex=0,programLoopsDone=0,selectedStep=-1;
function programKey(){return 'dmxProgram:'+document.getElementById('progName').value}
function setProgramStatus(t){document.getElementById('programStatus').textContent=t}
function renderSteps(active=-1){let box=document.getElementById('steps');box.innerHTML=programSteps.map((s,i)=>{let c=valuesColor(s.values,s.model),bg='#'+hex(c[0])+hex(c[1])+hex(c[2]);return `<div class="step ${i===active?'active':''}"><div class="stepSwatch" style="background:${bg}"></div><div><b>${i+1}. ${s.model} @${s.addr}</b><small>${s.ms} ms | ${s.values.join(', ')}</small></div><button onclick="deleteStep(${i})">Usuń</button></div>`}).join('')||'<div class="programStatus">Brak kroków</div>';setProgramStatus(programSteps.length+' kroków w programie')}
function recordStep(){let addr=Math.max(1,Math.min(512,+document.getElementById('fixtureAddress').value||1)),model=document.getElementById('fixtureModel').value,ms=Math.max(50,+document.getElementById('stepMs').value||1000),values=modelValues(model,currentFixtureValues());programSteps.push({addr,model,ms,values});renderSteps(programSteps.length-1);updatePreview(values,model,addr);if(document.getElementById('autoPreview').value==='1')sendRange(addr,values)}
function makeStep(addr,model,ms,vals){return {addr,model,ms,values:modelValues(model,vals)}}
function loadDemoProgram(name){stopProgram();let addr=Math.max(1,Math.min(512,+document.getElementById('fixtureAddress').value||1)),model=document.getElementById('fixtureModel').value,s=[];if(name==='softRainbow'){document.getElementById('progName').value='Soft Rainbow @'+addr;s=[[255,255,0,0,0,0,0,0],[255,255,120,0,0,0,0,0],[255,0,255,0,0,0,0,0],[255,0,180,255,0,0,0,0],[255,0,0,255,0,0,0,0],[255,180,0,255,0,0,0,0]].map(v=>makeStep(addr,model,900,v))}else if(name==='partyChase'){document.getElementById('progName').value='Party Chase @'+addr;s=[[255,255,0,0,0,0,0,0],[255,0,255,0,0,0,0,0],[255,0,0,255,0,0,0,0],[255,0,0,0,255,0,0,0],[255,255,255,255,255,90,0,0]].map(v=>makeStep(addr,model,180,v))}else if(name==='pulseWhite'){document.getElementById('progName').value='White Pulse @'+addr;s=[20,60,120,200,255,200,120,60].map(d=>makeStep(addr,model,160,[d,255,255,255,255,0,0,0]))}else if(name==='strobeBurst'){document.getElementById('progName').value='Strobe Burst @'+addr;s=[[255,255,255,255,255,0,0,0],[255,255,255,255,255,80,0,0],[255,255,255,255,255,170,0,0],[255,0,0,0,0,0,0,0]].map((v,i)=>makeStep(addr,model,i===3?600:350,v))}else if(name==='modeExplore'){document.getElementById('progName').value='CH7 Explore @'+addr;s=[0,32,64,96,128,160,192,224].map(m=>makeStep(addr,model,1000,[255,255,80,80,80,0,m,160]))}else if(name==='colorSelect'){document.getElementById('progName').value='Built-in Color Select @'+addr;s=[0,32,64,96,128,160,192,224,255].map(c=>makeStep(addr,model,650,[255,0,0,0,0,0,75,c]))}else if(name==='builtinJump'){document.getElementById('progName').value='Built-in Jump @'+addr;s=[30,90,150,220].map(sp=>makeStep(addr,model,1400,[255,0,0,0,0,0,125,sp]))}else if(name==='builtinGradient'){document.getElementById('progName').value='Built-in Gradient @'+addr;s=[25,80,140,210].map(sp=>makeStep(addr,model,1800,[255,0,0,0,0,0,175,sp]))}else if(name==='builtinPulse'){document.getElementById('progName').value='Built-in Pulse @'+addr;s=[30,90,150,230].map(sp=>makeStep(addr,model,1500,[255,255,80,120,0,0,225,sp]))}else if(name==='soundMode'){document.getElementById('progName').value='Sound Reactive @'+addr;s=[[255,255,255,255,255,0,252,80],[255,255,0,0,0,0,252,160],[255,0,0,255,0,0,252,230]].map(v=>makeStep(addr,model,1800,v))}else if(name==='warmFade'){document.getElementById('progName').value='Warm Fade @'+addr;s=[[80,255,80,0,20,0,0,0],[140,255,120,0,60,0,0,0],[220,255,170,20,120,0,0,0],[255,255,220,80,200,0,0,0],[140,255,120,0,60,0,0,0]].map(v=>makeStep(addr,model,900,v))}else if(name==='police'){document.getElementById('progName').value='Blue Red Alert @'+addr;s=[[255,255,0,0,0,0,0,0],[0,0,0,0,0,0,0,0],[255,0,0,255,0,0,0,0],[0,0,0,0,0,0,0,0]].map(v=>makeStep(addr,model,140,v))}else if(name==='breathRgb'){document.getElementById('progName').value='RGB Breath @'+addr;let colors=[[255,0,0,0],[0,255,0,0],[0,0,255,0]];colors.forEach(c=>[30,90,170,255,170,90].forEach(d=>s.push(makeStep(addr,model,180,[d,c[0],c[1],c[2],c[3],0,0,0]))))}else if(name==='randomPop'){document.getElementById('progName').value='Color Pops @'+addr;[[255,40,0,0],[0,255,80,0],[0,60,255,0],[255,0,220,0],[255,180,0,40],[0,255,255,0],[255,255,255,255]].forEach(c=>s.push(makeStep(addr,model,230,[255,c[0],c[1],c[2],c[3],0,0,0])))}programSteps=s;renderSteps(0);if(programSteps[0])updatePreview(programSteps[0].values,programSteps[0].model,programSteps[0].addr);setProgramStatus('Załadowano demo: '+document.getElementById('progName').value)}
function loadDemoProgram(name){
  stopProgram();
  selectedStep=-1;
  let addr=Math.max(1,Math.min(512,+document.getElementById('fixtureAddress').value||1));
  let model=document.getElementById('fixtureModel').value;
  let s=[];
  let setName=t=>document.getElementById('progName').value=t+' @'+addr;
  let one=(ms,vals)=>s.push(makeStep(addr,model,ms,vals));
  let a2=Math.min(505,addr+8);
  let two=(ms1,v1,ms2,v2)=>{s.push(makeStep(addr,model,ms1,v1));s.push(makeStep(a2,model,ms2,v2))};
  if(name==='softRainbow'){setName('Soft Rainbow');[[255,255,0,0,0,0,0,0],[255,255,120,0,0,0,0,0],[255,0,255,0,0,0,0,0],[255,0,180,255,0,0,0,0],[255,0,0,255,0,0,0,0],[255,180,0,255,0,0,0,0]].forEach(v=>one(900,v))}
  else if(name==='partyChase'){setName('Party Chase');[[255,255,0,0,0,0,0,0],[255,0,255,0,0,0,0,0],[255,0,0,255,0,0,0,0],[255,0,0,0,255,0,0,0],[255,255,255,255,255,90,0,0]].forEach(v=>one(180,v))}
  else if(name==='pulseWhite'){setName('White Pulse');[20,60,120,200,255,200,120,60].forEach(d=>one(160,[d,255,255,255,255,0,0,0]))}
  else if(name==='strobeBurst'){setName('Strobe Burst');[[255,255,255,255,255,0,0,0],[255,255,255,255,255,80,0,0],[255,255,255,255,255,170,0,0],[255,0,0,0,0,0,0,0]].forEach((v,i)=>one(i===3?600:350,v))}
  else if(name==='modeExplore'){setName('CH6 Explore');[0,32,64,96,128,160,192,224].forEach(m=>one(1000,[255,255,80,80,80,0,m,160]))}
  else if(name==='colorSelect'){setName('Built-in Color Select');[0,32,64,96,128,160,192,224,255].forEach(c=>one(650,[255,0,0,0,0,0,75,c]))}
  else if(name==='builtinJump'){setName('Built-in Jump');[30,90,150,220].forEach(sp=>one(1400,[255,0,0,0,0,0,125,sp]))}
  else if(name==='builtinGradient'){setName('Built-in Gradient');[25,80,140,210].forEach(sp=>one(1800,[255,0,0,0,0,0,175,sp]))}
  else if(name==='builtinPulse'){setName('Built-in Pulse');[30,90,150,230].forEach(sp=>one(1500,[255,255,80,120,0,0,225,sp]))}
  else if(name==='soundMode'){setName('Sound Reactive');[[255,255,255,255,255,0,252,80],[255,255,0,0,0,0,252,160],[255,0,0,255,0,0,252,230]].forEach(v=>one(1800,v))}
  else if(name==='warmFade'){setName('Warm Fade');[[80,255,80,0,20,0,0,0],[140,255,120,0,60,0,0,0],[220,255,170,20,120,0,0,0],[255,255,220,80,200,0,0,0],[140,255,120,0,60,0,0,0]].forEach(v=>one(900,v))}
  else if(name==='police'){setName('Blue Red Alert');[[255,255,0,0,0,0,0,0],[0,0,0,0,0,0,0,0],[255,0,0,255,0,0,0,0],[0,0,0,0,0,0,0,0]].forEach(v=>one(140,v))}
  else if(name==='breathRgb'){setName('RGB Breath');[[255,0,0,0],[0,255,0,0],[0,0,255,0]].forEach(c=>[30,90,170,255,170,90].forEach(d=>one(180,[d,c[0],c[1],c[2],c[3],0,0,0])))}
  else if(name==='randomPop'){setName('Color Pops');[[255,40,0,0],[0,255,80,0],[0,60,255,0],[255,0,220,0],[255,180,0,40],[0,255,255,0],[255,255,255,255]].forEach(c=>one(230,[255,c[0],c[1],c[2],c[3],0,0,0]))}
  else if(name==='fire'){setName('Fire');[[255,255,40,0,0],[220,255,90,0,0],[180,255,20,0,20],[255,255,130,0,0],[90,255,30,0,0],[230,255,70,0,30]].forEach(v=>one(140,[v[0],v[1],v[2],v[3],v[4],0,0,0]))}
  else if(name==='ocean'){setName('Ocean');[[180,0,90,255,0],[230,0,180,255,20],[120,0,40,180,80],[255,0,220,255,0],[160,0,120,220,30]].forEach(v=>one(780,[v[0],v[1],v[2],v[3],v[4],0,0,0]))}
  else if(name==='blueShades'){setName('Blue Shades Smooth');[[70,0,10,80,0],[110,0,25,130,0],[155,0,55,190,0],[210,0,95,255,0],[255,20,150,255,20],[230,0,220,255,35],[190,0,140,255,10],[140,0,70,210,0],[95,0,35,150,0],[70,0,10,80,0]].forEach(v=>one(220,[v[0],v[1],v[2],v[3],v[4],0,0,0]))}
  else if(name==='orangeShades'){setName('Orange Shades Smooth');[[70,180,35,0,0],[110,220,55,0,0],[155,255,80,0,0],[210,255,115,0,10],[255,255,150,20,25],[230,255,95,0,5],[185,235,65,0,0],[130,210,45,0,0],[90,170,25,0,0],[70,180,35,0,0]].forEach(v=>one(220,[v[0],v[1],v[2],v[3],v[4],0,0,0]))}
  else if(name==='clubSweep'){setName('Club Sweep');[[255,255,0,180,0,0,0,0],[255,0,255,255,0,0,0,0],[255,255,255,0,0,90,0,0],[255,0,0,255,0,0,0,0],[255,255,80,0,0,140,0,0]].forEach(v=>one(120,v))}
  else if(name==='sunrise'){setName('Sunrise');[[20,255,20,0,0],[60,255,50,0,10],[120,255,90,0,30],[180,255,140,20,80],[240,255,200,80,150],[255,255,255,160,220]].forEach(v=>one(1200,[v[0],v[1],v[2],v[3],v[4],0,0,0]))}
  else if(name==='blackoutBeat'){setName('Blackout Beat');[[255,255,255,255,255],[0,0,0,0,0],[255,255,0,0,0],[0,0,0,0,0],[255,0,0,255,0],[0,0,0,0,0]].forEach(v=>one(120,[v[0],v[1],v[2],v[3],v[4],0,0,0]))}
  else if(name==='twoMirror'){setName('2-Lamp Mirror');[[[255,255,0,0,0],[255,0,0,255,0]],[[255,0,255,0,0],[255,255,0,180,0]],[[255,0,0,255,0],[255,255,255,0,0]]].forEach(p=>two(50,[p[0][0],p[0][1],p[0][2],p[0][3],p[0][4],0,0,0],520,[p[1][0],p[1][1],p[1][2],p[1][3],p[1][4],0,0,0]))}
  else if(name==='twoAlternate'){setName('2-Lamp Alternate');[[255,0,0,0],[0,0,255,0],[0,255,0,0],[255,255,255,255]].forEach(c=>{two(60,[255,c[0],c[1],c[2],c[3],0,0,0],260,[0,0,0,0,0,0,0,0]);two(60,[0,0,0,0,0,0,0,0],260,[255,c[0],c[1],c[2],c[3],0,0,0])})}
  else if(name==='twoWave'){setName('2-Lamp Wave');let seq=[[255,255,0,0,0],[180,255,120,0,0],[120,0,255,80,0],[180,0,80,255,0],[255,180,0,255,0]];seq.forEach((v,i)=>two(50,v,430,seq[(i+2)%seq.length]))}
  programSteps=s;renderSteps(0);if(programSteps[0])updatePreview(programSteps[0].values,programSteps[0].model,programSteps[0].addr);setProgramStatus('Załadowano demo: '+document.getElementById('progName').value)
}
function setSlidersFromPreviewValues(p){setFx('fxDim',p[0]);setFx('fxR',p[1]);setFx('fxG',p[2]);setFx('fxB',p[3]);setFx('fxW',p[4]);setFx('fxStrobe',p[5]);setFx('fxMode',p[6]);setFx('fxSpeed',p[7]);syncPicker()}
function stepDisplayValues(step){return previewValues(step.model,step.values)}
function selectStep(i){if(!programSteps[i])return;selectedStep=i;let s=programSteps[i];document.getElementById('fixtureAddress').value=s.addr;document.getElementById('fixtureModel').value=s.model;document.getElementById('stepMs').value=s.ms;setSlidersFromPreviewValues(stepDisplayValues(s));renderSteps(i);updatePreview(s.values,s.model,s.addr);setProgramStatus('Edytujesz krok '+(i+1)+'. Zmień parametry i kliknij Podmień wybrany krok.')}
function clearSelectedStep(){selectedStep=-1;renderSteps();setProgramStatus(programSteps.length+' kroków w programie')}
function currentStepFromForm(){let addr=Math.max(1,Math.min(512,+document.getElementById('fixtureAddress').value||1)),model=document.getElementById('fixtureModel').value,ms=Math.max(50,+document.getElementById('stepMs').value||1000),values=modelValues(model,currentFixtureValues());return {addr,model,ms,values}}
function replaceSelectedStep(){if(selectedStep<0||!programSteps[selectedStep]){setProgramStatus('Najpierw kliknij krok, który chcesz edytować');return}let step=currentStepFromForm();programSteps[selectedStep]=step;renderSteps(selectedStep);updatePreview(step.values,step.model,step.addr);if(document.getElementById('autoPreview').value==='1')sendRange(step.addr,step.values);setProgramStatus('Podmieniono krok '+(selectedStep+1))}
function renderSteps(active=-1){let box=document.getElementById('steps');box.innerHTML=programSteps.map((s,i)=>{let c=valuesColor(s.values,s.model),bg='#'+hex(c[0])+hex(c[1])+hex(c[2]),sel=i===active||i===selectedStep;return `<div class="step ${sel?'active':''}" onclick="selectStep(${i})"><div class="stepSwatch" style="background:${bg}"></div><div><b>${i+1}. ${s.model} @${s.addr}</b><small>${s.ms} ms | ${s.values.join(', ')}</small></div><button onclick="event.stopPropagation();deleteStep(${i})">Usuń</button></div>`}).join('')||'<div class="programStatus">Brak kroków</div>';setProgramStatus(selectedStep>=0?'Edytujesz krok '+(selectedStep+1):programSteps.length+' kroków w programie')}
function deleteStep(i){programSteps.splice(i,1);if(selectedStep===i)selectedStep=-1;else if(selectedStep>i)selectedStep--;renderSteps()}
function saveProgram(){localStorage.setItem(programKey(),JSON.stringify({steps:programSteps}));setProgramStatus('Zapisano: '+document.getElementById('progName').value)}
function loadProgram(){let raw=localStorage.getItem(programKey());selectedStep=-1;programSteps=raw?JSON.parse(raw).steps||[]:[];renderSteps()}
function clearProgram(){stopProgram();selectedStep=-1;programSteps=[];renderSteps()}
function playProgram(){if(!programSteps.length){setProgramStatus('Brak kroków do odtworzenia');return}stopProgram();programPlaying=true;programIndex=0;programLoopsDone=0;playNextStep()}
function playNextStep(){if(!programPlaying)return;let total=+document.getElementById('playCount').value||0;if(programIndex>=programSteps.length){programIndex=0;programLoopsDone++;if(total>0&&programLoopsDone>=total){stopProgram();setProgramStatus('Koniec programu');return}}let active=programIndex,s=programSteps[programIndex++];renderSteps(active);updatePreview(s.values,s.model,s.addr);setProgramStatus('Play krok '+programIndex+'/'+programSteps.length+' pętla '+(programLoopsDone+1));sendRange(s.addr,s.values);programTimer=setTimeout(playNextStep,s.ms)}
function stopProgram(){programPlaying=false;if(programTimer)clearTimeout(programTimer);programTimer=null;setProgramStatus('Stop')}
function fieldValue(id){let el=document.getElementById(id);return el.type==='checkbox'?(el.checked?'1':'0'):el.value}
function saveConfig(){const ids=['hostname','uniqueNames','universe','apEnabled','apSsid','apPass','apIp','staEnabled','staSsid','staPass','ethEnabled','ethDhcp','ethIp','ethGateway','ethSubnet','ethDns'];post('/api/config',ids.map(id=>id+'='+encodeURIComponent(fieldValue(id))).join('&'))}
async function scanWifi(){let box=document.getElementById('wifiScan');box.textContent='Skanowanie...';let r=await fetch('/api/wifi/scan');box.textContent=await r.text();loadLogs()}
async function uploadOta(){let f=document.getElementById('otaFile').files[0],box=document.getElementById('otaStatus');if(!f){box.textContent='Wybierz plik .bin';return}box.textContent='Wgrywanie '+f.name+' ...';let fd=new FormData();fd.append('update',f,f.name);try{let r=await fetch('/api/update',{method:'POST',body:fd});box.textContent=await r.text()}catch(e){box.textContent='Błąd OTA: '+e}}
function exportProfiles(){let data=JSON.stringify(fixtureProfiles,null,2),a=document.createElement('a');a.href=URL.createObjectURL(new Blob([data],{type:'application/json'}));a.download='dmx-fixture-profiles.json';a.click();URL.revokeObjectURL(a.href)}
async function importProfiles(){let f=document.getElementById('profileFile').files[0],box=document.getElementById('profileStatus');if(!f)return;try{let raw=await f.text(),obj=JSON.parse(raw);Object.keys(obj).forEach(k=>{if(obj[k].label&&obj[k].channels&&obj[k].type)fixtureProfiles[k]=obj[k]});fillProfileSelects();updateProfileUi();box.textContent='Załadowano profile: '+Object.keys(fixtureProfiles).length}catch(e){box.textContent='Błąd profili JSON: '+e}}
async function refresh(){let s=await (await fetch('/api/status')).json();document.getElementById('badge').textContent=s.ethLink?'ETH '+s.ethIp:(s.staConnected?'STA '+s.staIp:(s.apEnabled?'AP '+s.apIp:'brak sieci'));document.getElementById('badge').className=(s.ethLink||s.staConnected||s.apEnabled)?'ok':'warnText';document.getElementById('status').innerHTML=metric('Hardware',s.hw)+metric('Firmware',s.fw)+metric('Hostname',s.effectiveHostname)+metric('MAC suffix',s.macSuffix)+metric('Tryb Wi-Fi',s.wifiMode)+metric('ETH',s.ethEnabled?(s.ethDhcp?'DHCP':'Static'):'off')+metric('AP IP',s.apIp)+metric('STA IP',s.staIp)+metric('ETH IP',s.ethIp)+metric('Universe',s.universe)+metric('Art-Net',s.artnetPackets)+metric('Ostatni pakiet',s.lastArtnetAgeMs?Math.round(s.lastArtnetAgeMs/1000)+' s temu':'brak')+metric('Demo',s.activeDemo)+metric('DMX frames',s.framesSent)+metric('Heap',s.freeHeap);}
function renderHome(s){document.getElementById('homeServices').innerHTML=metric('Panel WWW','HTTP port 80')+metric('HTTP API','/api/status, /api/config, /api/range, /api/update')+metric('Art-Net','UDP 6454 / ArtDMX')+metric('DMX512','ciągła transmisja ramek');document.getElementById('homeIps').innerHTML=metric('AP',s.apEnabled?('http://'+s.apIp+'/'):'off')+metric('STA',s.staConnected?('http://'+s.staIp+'/'):'off / brak połączenia')+metric('ETH',s.ethLink?('http://'+s.ethIp+'/'):'off / brak linku')+metric('Hostname',s.effectiveHostname);document.getElementById('homeUniverse').textContent=s.universe}
async function refresh(){let s=await (await fetch('/api/status')).json();document.getElementById('badge').textContent=s.ethLink?'ETH '+s.ethIp:(s.staConnected?'STA '+s.staIp:(s.apEnabled&&s.wifiRuntime?'AP '+s.apIp:'brak sieci'));document.getElementById('badge').className=(s.ethLink||s.staConnected||(s.apEnabled&&s.wifiRuntime))?'ok':'warnText';document.getElementById('status').innerHTML=metric('Hardware',s.hw)+metric('Firmware',s.fw)+metric('Hostname',s.effectiveHostname)+metric('MAC suffix',s.macSuffix)+metric('Tryb Wi-Fi',s.wifiMode+(s.wifiSuppressedByEth?' / wyłączone przez ETH':''))+metric('ETH',s.ethEnabled?(s.ethDhcp?'DHCP':'Static'):'off')+metric('AP IP',s.apIp)+metric('STA IP',s.staIp)+metric('ETH IP',s.ethIp)+metric('Universe',s.universe)+metric('Art-Net',s.artnetPackets)+metric('Ostatni pakiet',s.lastArtnetAgeMs?Math.round(s.lastArtnetAgeMs/1000)+' s temu':'brak')+metric('Demo',s.activeDemo)+metric('DMX frames',s.framesSent)+metric('Heap',s.freeHeap);renderHome(s)}
async function loadConfig(){let c=await (await fetch('/api/config')).json();['hostname','universe','apSsid','apPass','apIp','staSsid','staPass','ethIp','ethGateway','ethSubnet','ethDns'].forEach(id=>document.getElementById(id).value=c[id]||'');['apEnabled','staEnabled','ethEnabled'].forEach(id=>document.getElementById(id).checked=!!c[id]);['uniqueNames','ethDhcp'].forEach(id=>document.getElementById(id).value=c[id]?'1':'0')}
async function loadLogs(){document.getElementById('logs').textContent=await (await fetch('/api/logs')).text()}
document.getElementById('channels').innerHTML=chNames.map((n,i)=>`<div class="ch"><span>${i+1}</span><input type="range" min="0" max="255" value="0" oninput="this.nextElementSibling.textContent=this.value" title="${n}"><b>0</b></div>`).join('');
)HTML"
#include "profiles_ui.h"
R"HTML(
fillProfileSelects();updateProfileUi();setupWheel();setTab('home',document.querySelector('.tabs button'));renderSteps();updatePreview();loadConfig();refresh();loadLogs();loadStoredProfiles();setInterval(refresh,2000);
</script></body></html>
)HTML";

static void handleIndex() {
  server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

static void handleStatus() {
  server.send(200, "application/json", statusJson());
}

static void handleLogs() {
  String out;
  for (uint8_t i = 0; i < 60; i++) {
    uint8_t idx = (logHead + i) % 60;
    if (logs[idx].text.length() == 0) continue;
    out += "[" + String(logs[idx].ms / 1000) + "s] " + logs[idx].text + "\n";
  }
  server.send(200, "text/plain; charset=utf-8", out);
}

static String scanWifiText(bool logToSerial) {
  String out;
  addLog("Skan Wi-Fi start");
  if (logToSerial) Serial.println("WiFi scan start");
  int n = WiFi.scanNetworks(false, true);
  if (n <= 0) {
    out = "Nie znaleziono sieci Wi-Fi 2.4 GHz.\n";
    addLog("Skan Wi-Fi: brak sieci");
    if (logToSerial) Serial.println(out);
  } else {
    out += "Znaleziono sieci: " + String(n) + "\n";
    for (int i = 0; i < n; i++) {
      String line = String(i + 1) + ". " + WiFi.SSID(i) +
                    " | RSSI " + String(WiFi.RSSI(i)) + " dBm" +
                    " | CH " + String(WiFi.channel(i)) +
                    " | " + (WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "open" : "secured") + "\n";
      out += line;
      if (logToSerial) Serial.print(line);
    }
    int target = -999;
    for (int i = 0; i < n; i++) {
      if (WiFi.SSID(i) == cfg.staSsid) target = WiFi.RSSI(i);
    }
    if (target == -999) {
      out += "UWAGA: nie widzę skonfigurowanej sieci: " + cfg.staSsid + "\n";
      addLog("Skan: nie widzę " + cfg.staSsid);
    } else {
      out += "OK: widzę " + cfg.staSsid + " RSSI " + String(target) + " dBm\n";
      addLog("Skan: " + cfg.staSsid + " RSSI " + String(target) + " dBm");
    }
  }
  WiFi.scanDelete();
  return out;
}

static void handleWifiScan() {
  String out = scanWifiText(true);
  server.send(200, "text/plain; charset=utf-8", out);
}

static void handleConfigGet() {
  server.send(200, "application/json", configJson());
}

static void handleConfigPost() {
  cfg.hostname = server.arg("hostname");
  cfg.uniqueNames = server.arg("uniqueNames") == "1";
  cfg.apEnabled = server.arg("apEnabled") == "1";
  cfg.apSsid = server.arg("apSsid");
  cfg.apPass = server.arg("apPass");
  cfg.apIp = server.arg("apIp");
  cfg.staSsid = server.arg("staSsid");
  cfg.staPass = server.arg("staPass");
  cfg.staEnabled = server.arg("staEnabled") == "1";
  cfg.ethEnabled = server.arg("ethEnabled") == "1";
  cfg.ethDhcp = server.arg("ethDhcp") == "1";
  cfg.ethIp = server.arg("ethIp");
  cfg.ethGateway = server.arg("ethGateway");
  cfg.ethSubnet = server.arg("ethSubnet");
  cfg.ethDns = server.arg("ethDns");
  cfg.universe = server.arg("universe").toInt();
  if (cfg.hostname.length() == 0) cfg.hostname = "esp32-c3-dmx";
  if (!cfg.apEnabled && !cfg.staEnabled && !cfg.ethEnabled) cfg.apEnabled = true;
  if (cfg.apSsid.length() == 0) cfg.apSsid = "DMX-Node-C3";
  if (cfg.apPass.length() > 0 && cfg.apPass.length() < 8) cfg.apPass = "dmx12345";
  if (!isValidIpString(cfg.apIp)) cfg.apIp = "192.168.44.1";
  if (!isValidIpString(cfg.ethIp)) cfg.ethIp = "192.168.88.50";
  if (!isValidIpString(cfg.ethGateway)) cfg.ethGateway = "192.168.88.1";
  if (!isValidIpString(cfg.ethSubnet)) cfg.ethSubnet = "255.255.255.0";
  if (!isValidIpString(cfg.ethDns)) cfg.ethDns = cfg.ethGateway;
  if (cfg.universe < 0 || cfg.universe > 32767) cfg.universe = DEFAULT_UNIVERSE;
  saveConfig();
  addLog("Zapisano konfigurację, restart za 1 s");
  server.send(200, "text/plain", "OK");
  delay(1000);
  ESP.restart();
}

static void handleUpdateFinished() {
  bool ok = !Update.hasError();
  server.send(ok ? 200 : 500, "text/plain", ok ? "OK, restart" : "Update failed");
  if (ok) {
    addLog("OTA OK, restart");
    delay(700);
    ESP.restart();
  }
}

static void handleUpdateUpload() {
  HTTPUpload &upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    Serial.println("OTA start: " + upload.filename);
    addLog("OTA start: " + upload.filename);
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      Serial.println("OTA begin failed");
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Serial.println("OTA write failed");
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      Serial.println("OTA written: " + String(upload.totalSize));
    } else {
      Serial.println("OTA end failed");
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    Serial.println("OTA aborted");
  }
}

static void handleDmx() {
  activeDemo = "off";
  blackout = false;
  for (uint8_t i = 1; i <= 8; i++) {
    String key = "c" + String(i);
    if (server.hasArg(key)) setDmxSlot(i, constrain(server.arg(key).toInt(), 0, 255));
  }
  addLog("Ustawiono ręcznie kanały DMX 1-8");
  server.send(200, "text/plain", "OK");
}

static void handleFixture() {
  activeDemo = "off";
  blackout = false;
  uint8_t dimmer = constrain(server.arg("dim").toInt(), 0, 255);
  uint8_t red = constrain(server.arg("r").toInt(), 0, 255);
  uint8_t green = constrain(server.arg("g").toInt(), 0, 255);
  uint8_t blue = constrain(server.arg("b").toInt(), 0, 255);
  uint8_t white = constrain(server.arg("w").toInt(), 0, 255);
  uint8_t strobe = constrain(server.arg("strobe").toInt(), 0, 255);
  uint8_t mode = constrain(server.arg("mode").toInt(), 0, 255);
  applyFixture7Ch(dimmer, red, green, blue, white, mode, strobe);
  addLog("Fixture RGBW dim=" + String(dimmer) + " rgbw=" + String(red) + "," + String(green) + "," + String(blue) + "," + String(white) +
         " mode=" + String(mode) + " strobe/speed=" + String(strobe));
  server.send(200, "text/plain", "OK");
}

static void handleRange() {
  activeDemo = "off";
  blackout = false;
  uint16_t addr = constrain(server.arg("addr").toInt(), 1, (int)DMX_SLOTS);
  String values = server.arg("values");
  uint8_t parsed[DMX_SLOTS];
  uint16_t count = 0;
  int start = 0;
  while (start <= values.length() && count < sizeof(parsed)) {
    int comma = values.indexOf(',', start);
    String part = comma >= 0 ? values.substring(start, comma) : values.substring(start);
    part.trim();
    if (part.length()) parsed[count++] = constrain(part.toInt(), 0, 255);
    if (comma < 0) break;
    start = comma + 1;
  }
  if (count == 0) {
    server.send(400, "text/plain", "No values");
    return;
  }
  if (addr + count - 1 > DMX_SLOTS) count = DMX_SLOTS - addr + 1;
  setDmxRange(addr, parsed, count);
  addLog("Range addr=" + String(addr) + " count=" + String(count));
  server.send(200, "text/plain", "OK");
}

static void handleDemo() {
  String name = server.arg("name");
  if (name != "red" && name != "green" && name != "blue" && name != "white" && name != "all-on" &&
      name != "shehds-color" && name != "fast-chase" && name != "fade-rgbw" && name != "dimmer-ramp" &&
      name != "shehds-strobe" && name != "channel-scan" && name != "mode-scan") {
    name = "off";
  }
  activeDemo = name;
  blackout = false;
  demoStep = 0;
  addLog("Demo: " + activeDemo);
  server.send(200, "text/plain", "OK");
}

static void handleBlackout() {
  activeDemo = "off";
  blackout = true;
  clearDmx();
  addLog("Blackout");
  server.send(200, "text/plain", "OK");
}

static void setupWifi() {
  Serial.println("wifi: begin");
  wifiRuntimeEnabled = false;
  dnsRuntimeEnabled = false;
  if (!cfg.apEnabled && !cfg.staEnabled) {
    staConnectAttempt = false;
    addLog("Wi-Fi off, ETH-only");
    Serial.println("Wi-Fi off, ETH-only");
    return;
  }
  WiFi.persistent(false);
  WiFi.setSleep(false);
  String hostname = effectiveHostname();
  WiFi.setHostname(hostname.c_str());
  Serial.println("wifi: mode");
  if (cfg.apEnabled && cfg.staEnabled && cfg.staSsid.length()) {
    WiFi.mode(WIFI_AP_STA);
  } else if (cfg.apEnabled) {
    WiFi.mode(WIFI_AP);
  } else if (cfg.staEnabled && cfg.staSsid.length()) {
    WiFi.mode(WIFI_STA);
  } else {
    WiFi.mode(WIFI_OFF);
  }
  if (cfg.apEnabled) {
    Serial.println("wifi: softAP");
    String apName = effectiveApSsid();
    IPAddress apIp = parseIpOrDefault(cfg.apIp, IPAddress(192, 168, 44, 1));
    IPAddress apGw = apIp;
    IPAddress apMask(255, 255, 255, 0);
    WiFi.softAPConfig(apIp, apGw, apMask);
    WiFi.softAP(apName.c_str(), cfg.apPass.c_str());
    Serial.println("wifi: dns");
    dnsServer.start(53, "*", WiFi.softAPIP());
    dnsRuntimeEnabled = true;
    addLog("AP: " + apName + " IP " + ipToString(WiFi.softAPIP()));
  } else {
    addLog("AP off");
    Serial.println("AP off");
  }

  if (cfg.staEnabled && cfg.staSsid.length()) {
    Serial.println("STA scan dostepny z panelu WWW, pominiety przy starcie");
    WiFi.begin(cfg.staSsid.c_str(), cfg.staPass.c_str());
    Serial.println("STA target: " + cfg.staSsid);
    staConnectStartMs = millis();
    staConnectAttempt = true;
    staConnectReported = false;
    staFallbackDone = false;
    addLog("STA start w tle: " + cfg.staSsid);
  } else {
    staConnectAttempt = false;
    addLog("STA off");
    Serial.println("STA off");
  }
  wifiRuntimeEnabled = true;
}

static void stopWifiForEth() {
  if (!wifiRuntimeEnabled && autoWifiSuppressedByEth) return;
  dnsRuntimeEnabled = false;
  staConnectAttempt = false;
  staConnectReported = false;
  staFallbackDone = false;
  WiFi.disconnect(true, false);
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  wifiRuntimeEnabled = false;
  autoWifiSuppressedByEth = true;
  addLog("ETH link up: Wi-Fi/AP off");
  Serial.println("ETH link up: Wi-Fi/AP off");
}

static void monitorSta() {
  if (!staConnectAttempt || staFallbackDone) return;
  if (WiFi.status() == WL_CONNECTED) {
    if (!staConnectReported) {
      addLog("STA IP: " + ipToString(WiFi.localIP()));
      Serial.println("STA connected IP: " + ipToString(WiFi.localIP()));
      staConnectReported = true;
    }
    return;
  }
  if (millis() - staConnectStartMs > 20000) {
    staFallbackDone = true;
    if (cfg.apEnabled) {
      WiFi.disconnect(false);
      WiFi.mode(WIFI_AP);
      addLog("STA timeout, zostaje stabilny AP-only");
      Serial.println("STA timeout, fallback AP-only");
    } else {
      addLog("STA timeout, AP jest wylaczony");
      Serial.println("STA timeout, AP disabled");
    }
  }
}

static void setupEthernet() {
#if defined(ESP32_ETH01)
  if (!cfg.ethEnabled) {
    Serial.println("ETH off");
    addLog("ETH off");
    return;
  }
  Serial.println("ETH init LAN8720 addr=" + String(ETH01_PHY_ADDR) +
                 " power=" + String(ETH01_PHY_POWER) +
                 " mdc=" + String(ETH01_PHY_MDC) +
                 " mdio=" + String(ETH01_PHY_MDIO) +
                 " clk=GPIO0_IN");
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ETH.begin(ETH01_PHY_TYPE, ETH01_PHY_ADDR, ETH01_PHY_MDC, ETH01_PHY_MDIO, ETH01_PHY_POWER, ETH01_CLK_MODE);
#else
  ETH.begin(ETH01_PHY_ADDR, ETH01_PHY_POWER, ETH01_PHY_MDC, ETH01_PHY_MDIO, ETH01_PHY_TYPE, ETH01_CLK_MODE);
#endif
  if (!cfg.ethDhcp) {
    IPAddress ip = parseIpOrDefault(cfg.ethIp, IPAddress(192, 168, 88, 50));
    IPAddress gw = parseIpOrDefault(cfg.ethGateway, IPAddress(192, 168, 88, 1));
    IPAddress mask = parseIpOrDefault(cfg.ethSubnet, IPAddress(255, 255, 255, 0));
    IPAddress dns = parseIpOrDefault(cfg.ethDns, gw);
    ETH.config(ip, gw, mask, dns);
    Serial.println("ETH static IP: " + ipToString(ip));
    addLog("ETH static IP: " + ipToString(ip));
  }
  addLog("ETH init LAN8720");
#endif
}

static void monitorEthernet() {
#if defined(ESP32_ETH01)
  if (!cfg.ethEnabled) return;
  static bool ethReported = false;
  static uint32_t lastEthStatusMs = 0;
  bool linkNow = ETH.linkUp();
#if defined(AUTO_ETH_WIFI_FAILOVER)
  if (linkNow != autoEthLinkState) {
    if (autoEthChangeMs == 0) autoEthChangeMs = millis();
    if (millis() - autoEthChangeMs > 1500) {
      autoEthLinkState = linkNow;
      autoEthChangeMs = 0;
      if (linkNow) {
        stopWifiForEth();
      } else if (cfg.apEnabled || cfg.staEnabled) {
        autoWifiSuppressedByEth = false;
        addLog("ETH link down: wlaczam awaryjne Wi-Fi/AP");
        Serial.println("ETH link down: Wi-Fi/AP fallback on");
        setupWifi();
      }
    }
  } else {
    autoEthChangeMs = 0;
  }
#endif
  if (linkNow) {
    if (!ethReported && ETH.localIP() != IPAddress(0, 0, 0, 0)) {
      String ip = ipToString(ETH.localIP());
      addLog("ETH IP: " + ip);
      Serial.println("ETH connected IP: " + ip);
      ethReported = true;
    }
  } else if (ethReported) {
    addLog("ETH link down");
    Serial.println("ETH link down");
    ethReported = false;
  }
  if (millis() - lastEthStatusMs > 5000) {
    lastEthStatusMs = millis();
    Serial.println("ETH status link=" + String(linkNow ? "up" : "down") +
                   " ip=" + ipToString(ETH.localIP()));
  }
#endif
}

static bool waitForEthLink(uint32_t timeoutMs) {
#if defined(ESP32_ETH01)
  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    if (ETH.linkUp()) return true;
    delay(100);
  }
#endif
  return false;
}

static void setupWeb() {
  server.on("/api/profiles", HTTP_GET, []() {
    Preferences storage;
    storage.begin("profiles", false);
    String data = storage.getString("json", "{}");
    storage.end();
    server.send(200, "application/json", data);
  });
  server.on("/api/profiles", HTTP_POST, []() {
    String data = server.arg("plain");
    if (data.length() < 2 || data.length() > 16000 || data[0] != '{') {
      server.send(400, "text/plain", "Invalid profiles / limit 16000 bytes");
      return;
    }
    Preferences storage;
    storage.begin("profiles", false);
    size_t saved = storage.putString("json", data);
    storage.end();
    server.send(saved ? 200 : 507, "text/plain", saved ? "OK" : "Storage full");
  });
  server.on("/", HTTP_GET, handleIndex);
  server.on("/favicon.ico", HTTP_GET, []() { server.send(204, "text/plain", ""); });
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/logs", HTTP_GET, handleLogs);
  server.on("/api/config", HTTP_GET, handleConfigGet);
  server.on("/api/config", HTTP_POST, handleConfigPost);
  server.on("/api/update", HTTP_POST, handleUpdateFinished, handleUpdateUpload);
  server.on("/api/wifi/scan", HTTP_GET, handleWifiScan);
  server.on("/api/dmx", HTTP_POST, handleDmx);
  server.on("/api/fixture", HTTP_POST, handleFixture);
  server.on("/api/range", HTTP_POST, handleRange);
  server.on("/api/demo", HTTP_POST, handleDemo);
  server.on("/api/blackout", HTTP_POST, handleBlackout);
  server.onNotFound(handleIndex);
  server.begin();
}

void setup() {
  delay(500);
  Serial.begin(115200);
  Serial.println();
  Serial.println("DMX node boot");
#if defined(ESP32_ETH01)
  WiFi.onEvent(onNetEvent);
#endif
  loadConfig();
  Serial.println("config loaded");
  dmxMutex = xSemaphoreCreateMutex();
  clearDmx();
  Serial.println("dmx buffer clear");
  setupDmxUart();
  Serial.println("dmx uart ok");
#if defined(ESP32_ETH01) && defined(AUTO_ETH_WIFI_FAILOVER)
  setupEthernet();
  Serial.println("ethernet ok");
  if (waitForEthLink(3500)) {
    autoEthLinkState = true;
    stopWifiForEth();
    Serial.println("wifi skipped because ETH link is up");
  } else {
    Serial.println("ETH link missing, starting Wi-Fi/AP fallback");
    setupWifi();
    Serial.println("wifi ok");
  }
#else
  setupWifi();
  Serial.println("wifi ok");
  setupEthernet();
  Serial.println("ethernet ok");
#endif
  artnetUdp.begin(ARTNET_PORT);
  Serial.println("artnet udp ok");
  setupWeb();
  Serial.println("web server ok");
  addLog("Art-Net UDP port 6454, universe " + String(cfg.universe));
  xTaskCreatePinnedToCore(dmxTask, "dmx", 4096, nullptr, 3, nullptr, 0);
  Serial.println("dmx task ok");
}

void loop() {
  if (dnsRuntimeEnabled) dnsServer.processNextRequest();
  server.handleClient();
  monitorSta();
  monitorEthernet();
  handleArtnet();
  runDemo();
}
