/*
 * MIT License
 *
 * Copyright (c) 2024 esp32beans@gmail.com
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
/*
 * ESP32 S3 Box 3
 * Convert capacitive touch screen (x,y) to USB MIDI pitch bend.
 * On Box display use slider to control MIDI pitch bend.
 * On web server interface show two HTML5 sliders to control 14 bit MIDI CC
 * for modulation and volume.
*/

#if ARDUINO_USB_MODE
#warning This sketch should be used when USB is in OTG mode
#endif

#define DEBUG_ON  0
#if DEBUG_ON
#define DBG_begin(...)    Serial.begin(__VA_ARGS__)
#define DBG_print(...)    Serial.print(__VA_ARGS__)
#define DBG_println(...)  Serial.println(__VA_ARGS__)
#define DBG_printf(...)   Serial.printf(__VA_ARGS__)
#else
#define DBG_begin(...)
#define DBG_print(...)
#define DBG_println(...)
#define DBG_printf(...)
#endif

#include <WiFi.h>
#include <ArduinoOTA.h>
#include <WiFiManager.h>  // See README.md
#include <ArduinoJson.h>  // Install from IDE Library manager
#include <WebSocketsServer.h> // Install WebSockets by Markus Sattler from IDE Library manager
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ESP_Panel_Library.h>
#include <lvgl.h>
#include "lvgl_port_v8.h"
#include "index_html.h"
#include "USB.h"
#include "USBMIDI.h"
USBMIDI MIDI;

/* Display */
ESP_Panel *panel = nullptr;
ESP_PanelLcd *lcd = nullptr;
ESP_PanelTouch *touch = nullptr;
ESP_PanelBacklight *backlight = nullptr;

const int SCREEN_X_MAX = 319;
const int SCREEN_Y_MAX = 239;

/* WiFi Network */
MDNSResponder mdns;

WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

void CC14(uint8_t control, uint16_t cc14_value, uint8_t chan) {
  MIDI.controlChange(control, (uint8_t)((cc14_value >> 7) & 0x7F), chan);
  MIDI.controlChange(control+32, (uint8_t)(cc14_value & 0x7F), chan);
}

static lv_obj_t * label;

static void slider_event_cb(lv_event_t * e) {
  static int32_t pitch_last;
  int32_t pitch;
  lv_obj_t * slider = lv_event_get_target(e);
  lv_event_code_t code = lv_event_get_code(e);

  if (code == LV_EVENT_VALUE_CHANGED) {
    /* Get MIDI pitch bend */
    pitch = lv_slider_get_value(slider);
  } else {  /* LV_EVENT_RELEASED */
    pitch = 8192;
    lv_slider_set_value(slider, pitch, LV_ANIM_OFF);
  }
  if (pitch != pitch_last) {
    pitch_last = pitch;
    MIDI.pitchBend((uint16_t)pitch);
    /* Refresh the text */
    lv_label_set_text_fmt(label, "%" LV_PRId32, pitch);
    lv_obj_align_to(label, slider, LV_ALIGN_OUT_TOP_MID, 0, -15);    /*Align top of the slider*/
  }
}

/**
 * Create a slider and write its value on a label.
 */
void lv_create_slider(void) {
  /* Create a big slider in the center of the display */
  lv_obj_t * slider = lv_slider_create(lv_scr_act());
  lv_obj_set_size(slider, SCREEN_X_MAX+1, (SCREEN_Y_MAX+1)/2);
  lv_obj_center(slider);  /* Align to the center of the parent (screen) */
  lv_slider_set_range(slider, 0, 16383);
  lv_slider_set_value(slider, 8192, LV_ANIM_OFF);
  lv_obj_add_event_cb(slider, slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);     /*Assign an event function*/
  lv_obj_add_event_cb(slider, slider_event_cb, LV_EVENT_RELEASED, NULL);     /*Assign an event function*/

  /* Create a label above the slider */
  label = lv_label_create(lv_scr_act());
  lv_label_set_text(label, "8192");
  lv_obj_align_to(label, slider, LV_ALIGN_OUT_TOP_MID, 0, -15);    /*Align top of the slider*/
}

/* WiFi Network */

/* WiFiManager, global */
WiFiManager wm;

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length)
{
  switch(type) {
    case WStype_DISCONNECTED:
      DBG_printf("[%u] Disconnected!\r\n", num);
      break;
    case WStype_CONNECTED:
      {
        IPAddress ip = webSocket.remoteIP(num);
        DBG_printf("[%u] Connected from %d.%d.%d.%d url: %s\r\n", num, ip[0], ip[1], ip[2], ip[3], payload);
      }
      break;
    case WStype_TEXT:
      {
        DBG_printf("[%u] get Text: [%d] %s \r\n", num, length, payload);

        JsonDocument doc;

        DeserializationError error = deserializeJson(doc, payload, length);

        if (error) {
          DBG_print(F("deserializeJson() failed: "));
          DBG_println(error.f_str());
          return;
        }

        const char* name = doc["name"];
        int hrcc_value = doc["hrcc_value"];
        if (strcmp(name, "modulation") == 0) {
          CC14(1, hrcc_value, 1);
        } else if (strcmp(name, "volume") == 0) {
          CC14(7, hrcc_value, 1);
        } else if (strcmp(name, "pitchbend") == 0) {
          MIDI.pitchBend((uint16_t)hrcc_value, 1);
        }
      }
      break;
    case WStype_BIN:
      DBG_printf("[%u] get binary length: %u\r\n", num, length);
      //      hexdump(payload, length);

      // echo data back to browser
      // webSocket.sendBIN(num, payload, length);
      break;
    default:
      DBG_printf("Invalid WStype [%d]\r\n", type);
      break;
  }
}

void handleRoot() {
  server.send(200, "text/html", INDEX_HTML);
}

void handleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET)?"GET":"POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i=0; i<server.args(); i++){
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}

// OTA -- Over the Air (WiFi) upload
void OTA_setup() {
  // Port defaults to 3232
  // ArduinoOTA.setPort(3232);

  // Hostname defaults to esp3232-[MAC]
  ArduinoOTA.setHostname("hirescc");

  // No authentication by default
  // ArduinoOTA.setPassword("admin");

  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

  ArduinoOTA
    .onStart([]() {
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH)
        type = "sketch";
        else // U_SPIFFS
        type = "filesystem";

        // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
        Serial.println("Start updating " + type);
        })
  .onEnd([]() {
      Serial.println("\nEnd");
      })
  .onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
      })
  .onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
      });

  ArduinoOTA.begin();
}

/* WiFiManager feedback callback */
void configModeCallback (WiFiManager *myWiFiManager) {
  DBG_println("Entered config mode");
  DBG_println(WiFi.softAPIP());
  //if you used auto generated SSID, print it
  DBG_println(myWiFiManager->getConfigPortalSSID());
}


void setup() {
  WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP
#if DEBUG_ON
  DBG_begin(115200);
  while (!Serial && (millis() < 3000)) delay(10);
  DBG_println("HiRes CC Start");
#else
  Serial.end();
#endif

  panel = new ESP_Panel();

  /* Initialize bus and device of panel */
  panel->init();
#if LVGL_PORT_AVOID_TEAR
  // When avoid tearing function is enabled, configure the RGB bus according to the LVGL configuration
  ESP_PanelBus_RGB *rgb_bus = static_cast<ESP_PanelBus_RGB *>(panel->getLcd()->getBus());
  rgb_bus->configRgbFrameBufferNumber(LVGL_PORT_DISP_BUFFER_NUM);
  rgb_bus->configRgbBounceBufferSize(LVGL_PORT_RGB_BOUNCE_BUFFER_SIZE);
#endif
  /* Start panel */
  panel->begin();

  lcd = panel->getLcd();
  if (lcd == nullptr) {
    Serial.println("LCD is not available");
  }
  touch = panel->getTouch();
  if (touch == nullptr) {
    Serial.println("Touch is not available");
  }
  backlight = panel->getBacklight();
  if (backlight != nullptr) {
    Serial.println("Turn off the backlight");
    backlight->off();
  } else {
    Serial.println("Backlight is not available");
  }

  Serial.println("Initialize LVGL");
  lvgl_port_init(lcd, touch);

  Serial.println("Create UI");
  if (backlight != nullptr) {
    backlight->setBrightness(100);
  } else {
    Serial.println("Backlight is not available");
  }

  /* Lock the mutex due to the LVGL APIs are not thread-safe */
  lvgl_port_lock(-1);
  lv_create_slider();
  /* Release the mutex */
  lvgl_port_unlock();

  // Automatically connect using saved credentials,
  // if connection fails, it starts an access point with the specified name ( "AutoConnectAP"),
  // if empty will auto generate SSID, if password is blank it will be anonymous AP (wm.autoConnect())
  // then goes into a blocking loop awaiting configuration and will return success result

  wm.setAPCallback(configModeCallback);
  bool res;
  // res = wm.autoConnect(); // auto generated AP name from chipid
  res = wm.autoConnect("HiResCC");
  // res = wm.autoConnect("AutoConnectAP","password"); // password protected ap

  if(!res) {
      DBG_println(F("Failed to connect"));
      delay(1000);
      ESP.restart();
      delay(1000);
  }

  if (mdns.begin("hirescc")) {
    DBG_println(F("MDNS responder started"));
    mdns.addService("http", "tcp", 80);
    mdns.addService("ws", "tcp", 81);
  }
  else {
    DBG_println(F("MDNS.begin failed"));
  }
  DBG_print(F("Connect to http://hirescc.local or http://"));
  DBG_println(WiFi.localIP());

  OTA_setup();

  server.on("/", handleRoot);
  server.onNotFound(handleNotFound);

  server.begin();

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  MIDI.begin();
  USB.begin();
}

void loop() {
  midiEventPacket_t midi_packet_in = {0};

  if (MIDI.readPacket(&midi_packet_in)) {
    /* Ignore incoming MIDI messages */
  }
  ArduinoOTA.handle();
  webSocket.loop();
  server.handleClient();
}
