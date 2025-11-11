#include <WiFi.h>
#include <NTPClient.h>
#include <AsyncUDP_RP2040W.h>

// secret, contains definition of local_ssid, local_pass, and data_url in three lines, literally: 
// char local_ssid[] = "NNN";  //  your network SSID (name)
// char local_pass[] = "PPP";  // your network password
#include "/home/jmoon/Arduino/libraries/local/ssid_harvest.h"
IPAddress static_ip(192,168,1,10);
IPAddress static_dns(192,168,1,2);
IPAddress static_gateway(192,168,1,1);
IPAddress static_subnet(255,255,255,0);

#include <SPI.h>
#include <Adafruit_GFX.h>
#include "Fonts/FreeSerif9pt7b.h"
#include "Fonts/FreeMonoBold9pt7b.h"
#include "Fonts/FreeMonoBold24pt7b.h"
#include "Fonts/FreeMonoBold18pt7b.h"
#include "Fonts/FreeMonoBold12pt7b.h"
#include <TFT_eSPI.h>
#include <hardware/pwm.h>
#include <elapsedMillis.h>

#include "src/DateTimeNTP/DateTimeNTP.h"

// Backlight update = 133 MHz/(255*2360) = 221 Hz
#define BACKLIGHT_DIV 255
#define BACKLIGHT_TOP 2360
TFT_eSPI tft = TFT_eSPI();
uint8_t backlight_pwm_slice;

// NTP time stuff
WiFiUDP ntpUDP;
NTPClient theClient(ntpUDP);
DateTimeNTP dtntp(&theClient);
int wifi_status = WL_IDLE_STATUS;     // the Wifi radio's status

// UDP stuff
AsyncUDP udp;
#define UDP_LISTEN_PORT 8225
#define INCOMING_UDP_PACKET_SZ 64
unsigned char incoming_packet_buf[INCOMING_UDP_PACKET_SZ];
#define OUTGOING_UDP_PACKET_SZ 64
unsigned char outgoing_packet_buf[OUTGOING_UDP_PACKET_SZ];
#define INCOMING_UDP_PACKET_DATA_SZ 32

elapsedMillis performance_millis;
elapsedMillis update_millis;
uint32_t update_delay = 2*1000; // ms, for raw sensor data

enum GIZMO_STATES {
  STATE_UPDATE,
  STATE_WAIT
};
uint8_t gizmo_state;

enum BITMAP_NAMES {
  DATE_CANVAS,
  T_CANVAS,
  H_CANVAS,
  WINDV_CANVAS,
  WINDA_CANVAS,
  NUM_BITMAPS
};

GFXcanvas1 *canvases[NUM_BITMAPS]; 

int bpos[][4] = {
  {0,0,320,50}, // x, y, w, h; date
  {0,70,320,50}, // temp
  {0,120,320,50}, // hum
  {0,200,320,55}, // wind v
  {0,255,320,50}, // wind angle
};

int canvas_colors[][2] = {
  {TFT_SKYBLUE,TFT_BLACK},
  {TFT_GREEN,TFT_DARKGREY},
  {TFT_GREEN,TFT_BLACK},
  {TFT_GREEN,0x01},
  {TFT_GREEN,0x01}
};

void initial_screen() {

  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_GREEN);
  tft.setTextWrap(true);
  tft.setTextSize(1);
  tft.setCursor(0,0);
  tft.println(""); // cursor is at bottom of font
  tft.println("Connected!");
  tft.println("");

  IPAddress ip = WiFi.localIP();
  tft.setTextColor(TFT_WHITE);
  tft.print("IP Address: ");
  tft.setTextColor(TFT_YELLOW);
  tft.println(ip);

  // print MAC address:
  byte mac[6];
  WiFi.macAddress(mac);
  tft.setTextColor(TFT_WHITE);
  tft.print("MAC: ");
  char mac_str[18];
  sprintf(mac_str, "%02X:%02X:%02X:%02X:%02X:%02X", mac[5],mac[4],mac[3],mac[2],mac[1],mac[0]);
  tft.setTextColor(TFT_YELLOW);
  tft.println(mac_str);

}



void setup() {
  // put your setup code here, to run once:

   Serial.begin(9600);
  
// blink once when setup begins
  digitalWrite(PIN_LED, HIGH);
  delay(300);
  digitalWrite(PIN_LED, LOW);
  delay(100);

// nothing was coming out of pins from scope so had to do this manually
  gpio_set_function(TFT_CS, GPIO_FUNC_SPI);
  gpio_set_function(TFT_SCLK, GPIO_FUNC_SPI);
  gpio_set_function(TFT_MOSI, GPIO_FUNC_SPI);
  gpio_set_function(TFT_MISO, GPIO_FUNC_SPI);


  tft.init();

  // can use PWM on this pin to dim screen - TODO
  pinMode(TFT_BL, OUTPUT); // GPIO13 = PWM 6B 
  gpio_set_function(TFT_BL, GPIO_FUNC_PWM);
  backlight_pwm_slice = pwm_gpio_to_slice_num(TFT_BL);
  pwm_config backlightConfig = pwm_get_default_config();
  pwm_config_set_wrap(&backlightConfig, BACKLIGHT_TOP); // with 255 prescaling, gets to 220 Hz  
  pwm_init(backlight_pwm_slice, &backlightConfig, true);
  pwm_set_chan_level(backlight_pwm_slice, 1, BACKLIGHT_TOP-1); // initial value
  pwm_set_clkdiv_int_frac(backlight_pwm_slice, BACKLIGHT_DIV, 0); // 133 MHz/255 = 521.6 kHz clock freq

  tft.setRotation(2);
  tft.fillScreen((TFT_BLACK));


// DO NOT use the Adafruit_GFX fonts! 
//  tft.setFreeFont(&FreeSerif9pt7b); 
  tft.setTextFont(2);
  tft.setTextSize(2);

//  tft.setCursor(20, 0, 2);
  tft.setTextColor(TFT_SKYBLUE);
  tft.println(" ");
  tft.println("Hello!");

  tft.setTextColor(TFT_GREEN); 
  tft.println("Hello!");

  tft.setTextColor(TFT_RED); 
  tft.println("Hello!");
  
  tft.setTextColor(TFT_WHITE);
  tft.println("Connecting"); 




  // WiFi stuff

// configure static IP



  WiFi.config(static_ip,static_dns, static_gateway,static_subnet);
  // Connect to WPA/WPA2 network
  // Just calling begin once and checking status doesn't seem to work
  // repeatedly calling begin after a delay does work though...
  while (wifi_status != WL_CONNECTED) {
    wifi_status = WiFi.begin(local_ssid,local_pass);
    tft.print('.');
    // wait for connection:
    delay(1000);
  }

  // start the date time NTP updates
  tft.println("");
  tft.println("Starting DateTime NTP updates...");
  uint8_t retries = 0;
  if (!dtntp.start()) {
    tft.println("NTP update failed " + String(theClient.getEpochTime()));
    while (!theClient.forceUpdate() && retries < 3) {
      tft.print('.');
      delay(1000);
      retries+=1;
    }
  }
  delay(1000);

  // x0,x1,y0,y1,ctl = [bits from LSB: 1=rotate,2=invertx,3=inverty]
  // NOTE: Calibration values are RAW extent values - which are between ~300-3600 in both X and Y
  uint16_t calibrationData[5] = {300,3600,300,3600,0};
  tft.setTouch(calibrationData);

  initial_screen();
  delay(5000);
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(0,0);
  tft.println("");

  // allocate canvases for rendering
  for (int i=0; i < NUM_BITMAPS; ++i) {
    canvases[i] = new GFXcanvas1(bpos[i][2],bpos[i][3]);
  }
  
  // reset loop update clock
  update_millis = 0;

  // set up UDP 
  if(udp.listen(UDP_LISTEN_PORT)) {
    udp.onPacket([](AsyncUDPPacket packet) {
      parsePacket(packet);
    });
  }


}


// DEBUG STUFF - FIXME
static int debug_counter=0;
char debug_buf[30];

enum PACKET_COMMANDS {
  PCOMMAND_RESERVED,
  PCOMMAND_STATUS,
  PCOMMAND_UPTIME
};

enum PACKET_ERRORS {
  PERR_NONE,
  PERR_UNK_COMMAND,
  PERR_CHECKSUM,
  PERR_NO_ACK
};

#define ACK_BYTE 0x06
#define NACK_BYTE 0x15

static uint32_t last_packet_length = 0;
static uint16_t last_remote_port = 0;
static uint8_t  last_packet_error = PERR_NONE;
static uint16_t outgoing_data_len = 0;
static uint32_t received_packet_count = 0;

void checksum_packet(unsigned char *buf, uint16_t buflen) {
  uint16_t checksum=0;
  for (int i=0; i < buflen-2; ++i) {
    checksum += buf[i];
  }
  // last two bytes are checksum
  buf[buflen-2]=(uint8_t)(checksum&255);
  buf[buflen-1]=(uint8_t)(checksum>>8);
}

void parsePacket(AsyncUDPPacket packet) {

    received_packet_count+=1;
    IPAddress ip = packet.remoteIP();
    last_remote_port = packet.remotePort();
    last_packet_length = packet.length();
    last_packet_error = PERR_NONE;

    memcpy((uint8_t *)incoming_packet_buf, (const uint8_t *)packet.data(), packet.length());

    outgoing_data_len = 0;
    // first byte = 0x06 (ACK); last two bytes are checksum
    if (incoming_packet_buf[0]==0x06 && last_packet_length > 2) { 
      switch(incoming_packet_buf[1]) { // command byte
        case PCOMMAND_STATUS:
        {
          outgoing_packet_buf[0]=ACK_BYTE;
          outgoing_packet_buf[1]=PCOMMAND_STATUS;
          // TODO: fill in dummy vals
          outgoing_packet_buf[2]=0x00;
          outgoing_packet_buf[3]=0x01;
          outgoing_packet_buf[4]=0x02;
          outgoing_packet_buf[5]=0x03;
          outgoing_data_len=8;
          checksum_packet(outgoing_packet_buf, outgoing_data_len);
          break;
        }
        case PCOMMAND_UPTIME:
        {
          uint32_t uptime_secs = dtntp.last_secs-dtntp.init_secs;
          outgoing_packet_buf[0]=ACK_BYTE;
          outgoing_packet_buf[1]=PCOMMAND_UPTIME;
          outgoing_packet_buf[2]=(uint8_t)(uptime_secs&255);
          outgoing_packet_buf[3]=(uint8_t)((uptime_secs>>8)&255);
          outgoing_packet_buf[4]=(uint8_t)((uptime_secs>>16)&255);
          outgoing_packet_buf[5]=(uint8_t)((uptime_secs>>24)&255);
          outgoing_data_len=8;
          checksum_packet(outgoing_packet_buf, outgoing_data_len);
          break;
        }
        default:
        {
          last_packet_error = PERR_UNK_COMMAND;
          break;
        }
      }

    }
    else {
      last_packet_error = PERR_NO_ACK;
    }
//    Serial.println("Packet " + String(received_packet_count));
//    for(int i=0; i < packet.length();++i) {
//      Serial.printf("%x ",incoming_packet_buf[i]);
//    }

    if (last_packet_error!=PERR_NONE) {
      outgoing_packet_buf[0]=NACK_BYTE;  
      outgoing_packet_buf[1]=last_packet_error;
      outgoing_packet_buf[2]=0; // reserved
      outgoing_packet_buf[3]=0; // reserved
      outgoing_data_len = 4;
    }

    // alsways send response packet
    packet.write((uint8_t*) outgoing_packet_buf, outgoing_data_len);
}


void loop() {
  // put your main code here, to run repeatedly:

  if (update_millis > update_delay) {
      gizmo_state = STATE_UPDATE;
      update_millis = 0;
      debug_counter+=1;
  }
  else {
    gizmo_state = STATE_WAIT;
  }

  switch (gizmo_state) {
    case STATE_UPDATE:
    {
       // update date/time first
      dtntp.get_date();
      canvases[DATE_CANVAS]->fillScreen(TFT_BLACK);
      canvases[DATE_CANVAS]->setFont(&FreeMonoBold12pt7b);
      canvases[DATE_CANVAS]->setCursor(30, 16);
      canvases[DATE_CANVAS]->printf("%s\n",dtntp.date_cstring);
      int16_t ycur = canvases[DATE_CANVAS]->getCursorY();
      canvases[DATE_CANVAS]->setCursor(0, ycur+5);
      canvases[DATE_CANVAS]->setFont(&FreeMonoBold18pt7b);
      canvases[DATE_CANVAS]->printf("%s",dtntp.time_cstring);

      canvases[T_CANVAS]->fillScreen(TFT_BLACK);
      canvases[T_CANVAS]->setFont(&FreeMonoBold12pt7b);
      canvases[T_CANVAS]->setCursor(5, 40);
      canvases[T_CANVAS]->printf("P=%d, L=%d, C=%d" ,last_remote_port,last_packet_length,received_packet_count);

      for (int i=0; i < NUM_BITMAPS; ++i) {
          tft.drawBitmap(bpos[i][0],bpos[i][1],canvases[i]->getBuffer(),bpos[i][2],bpos[i][3],canvas_colors[i][0],canvas_colors[i][1]);
      }
//      Serial.println("Update " + String(debug_counter));
//      tft.setTextColor(TFT_GREEN); 
//      tft.setCursor(0,0);
//      tft.println("                    ");
//      tft.println("                    ");
//      tft.setCursor(0,0);
//      dtntp.get_date();
//      tft.println(dtntp.date_cstring);
//      tft.println(dtntp.time_cstring);
//      sprintf(debug_buf,"%d",debug_counter);
//      tft.println(debug_buf);
      break;
    }
    case STATE_WAIT:
    {

      break;
    }
  }

}
