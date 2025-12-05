#include <WiFi.h>
#include <NTPClient.h>
#include <AsyncUDP_RP2040W.h>
#include <Adafruit_BME280.h>

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

#include <Wire.h>

#include "src/DateTimeNTP/DateTimeNTP.h"
#include "src/windcal.h"

#define VERSION_STRLEN 9
char version[] = "202512051";

// Backlight update = 133 MHz/(255*2360) = 221 Hz
#define BACKLIGHT_DIV 255
#define BACKLIGHT_TOP 2360
TFT_eSPI tft = TFT_eSPI();
uint8_t backlight_pwm_slice;

// here "physical pin" means pins 1-40 of the Pico W board
#define DEBUG_DIR_PWM_PIN D6 // physical pin 9, black wire, also motor PWM
#define DEBUG_SPEED_PWM_PIN D4 // physical pin 6
#define DEBUG_SPEED_ISR_PIN D3 // physical pin 5, white wire
// 133/(522*255) ==> 1.0008 ms per interrupt
#define DEBUG_SPEED_ISR_TOP 522 // clock cycles (possibly pre-divided) to generate IRQ
#define DEBUG_SPEED_ISR_CLK_DIV 255 // pre-divide 133 MHz clock by this
#define DEBUG_DAC_TOP 1024 // clock cycles between pulses
#define DEBUG_DAC_CLK_DIV 1

#define PICOW_CLK_FREQ 133000000
#define MOTOR_TOP 10431 // with 255 clk div is 50 Hz (20 ms period)
#define MOTOR_CLK_DIV 255

// pwm counters used in debug/motor control
uint8_t dirSlice;
uint8_t speedSlice;

// pins for actual sensor
#define WIND_DIR_PIN D27 // A1, physical pin 32
#define WIND_SPEED_PIN D26 // A0, physical pin 31
#define ADC_GROUND 33 // physical pin 33; just listed for reference

// NTP time stuff
WiFiUDP ntpUDP;
NTPClient theNTPUDPClient(ntpUDP);
DateTimeNTP dtntp(&theNTPUDPClient);
int wifi_status = WL_IDLE_STATUS;     // the Wifi radio's status

// UDP stuff
AsyncUDP udp;
#define UDP_LISTEN_PORT 8225
#define INCOMING_UDP_PACKET_SZ 64
unsigned char incoming_packet_buf[INCOMING_UDP_PACKET_SZ];
#define OUTGOING_UDP_PACKET_SZ 64
unsigned char outgoing_packet_buf[OUTGOING_UDP_PACKET_SZ];
#define INCOMING_UDP_PACKET_DATA_SZ 32

elapsedMillis rotor_millis;
elapsedMillis update_millis;
elapsedMillis bme_update_millis;

uint32_t update_delay = 1*1000; // ms, for raw sensor data
uint32_t bme_update_delay = 60*1000; // ms, for BME T/H/P sensor 

enum GIZMO_STATES {
  STATE_UPDATE,
  STATE_DO_LEFT_BUTTON,
  STATE_DO_RIGHT_BUTTON,
  STATE_MOTOR_MOVING,
  STATE_WAIT
};
uint8_t gizmo_state;

enum BITMAP_NAMES {
  DATE_CANVAS,
  T_CANVAS,
  H_CANVAS,
  P_CANVAS,
  WINDV_CANVAS,
  WINDA_CANVAS,
  DATA_CANVAS, // general purpose
  NUM_BITMAPS
};

GFXcanvas1 *label_canvases[NUM_BITMAPS]; 
GFXcanvas1 *data_canvases[NUM_BITMAPS]; 
TFT_eSPI_Button left_button = TFT_eSPI_Button();
TFT_eSPI_Button right_button = TFT_eSPI_Button();

#define LABEL_WIDTH 150
#define LABEL_OFFSET 35

int data_pos[][4] = {
  {0,0,320,50}, // x, y, w, h; date
  {LABEL_WIDTH,70,320-LABEL_WIDTH,50}, // temp
  {LABEL_WIDTH,120,320-LABEL_WIDTH,50}, // hum
  {LABEL_WIDTH,170,320-LABEL_WIDTH,50}, // pressure
  {LABEL_WIDTH,220,320-LABEL_WIDTH,50}, // wind v
  {LABEL_WIDTH,270,320-LABEL_WIDTH,50}, // wind angle
  {0,320,320,50}, // data
};

int label_pos[][4] = {
  {0,0,320,50}, // x, y, w, h; date
  {0,70,LABEL_WIDTH,50}, // temp
  {0,120,LABEL_WIDTH,50}, // hum
  {0,170,LABEL_WIDTH,50}, // pressure
  {0,220,LABEL_WIDTH,50}, // wind v
  {0,270,LABEL_WIDTH,50}, // wind angle
  {0,320,320,50}, // data
};

// text color, background color
int label_canvas_colors[][2] = {
  {TFT_SKYBLUE,TFT_BLACK},
  {TFT_GREEN,TFT_DARKGREY},
  {TFT_GREEN,TFT_BLACK},
  {TFT_GREEN,TFT_DARKGREY},
  {TFT_GREEN,TFT_BLACK},
  {TFT_GREEN,TFT_DARKGREY},
  {TFT_GREEN,TFT_BLACK},
};

int data_canvas_colors[][2] = {
  {TFT_SKYBLUE,TFT_BLACK},
  {TFT_WHITE,TFT_DARKGREY},
  {TFT_WHITE,TFT_BLACK},
  {TFT_WHITE,TFT_DARKGREY},
  {TFT_WHITE,TFT_BLACK},
  {TFT_WHITE,TFT_DARKGREY},
  {TFT_WHITE,TFT_BLACK},
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

static uint16_t isr_high_counts = 2;
static uint16_t isr_low_counts = 48;
static uint16_t isr_high;
static uint16_t isr_low;

uint16_t motor_pulses = 50; // 1 s default with 20 ms period

uint16_t pulse_no = 0;
uint16_t last_motor_angle=0;

void pwmIrqHandler() {
  if (pwm_get_irq_status_mask()&(1<<speedSlice)) {
    pwm_clear_irq(speedSlice);
    if (isr_high!=0 && isr_low==0){
      isr_high--;
      digitalWrite(DEBUG_SPEED_ISR_PIN, HIGH); 
      if (isr_high==0) {
        isr_low=isr_low_counts;
      }
    }
    else if (isr_high==0 && isr_low!=0) {
      digitalWrite(DEBUG_SPEED_ISR_PIN, LOW); 
      isr_low--;
      if(isr_low==0) {
        isr_high=isr_high_counts;
      }
    }
    else if ( (isr_high==0 && isr_low==0) || (isr_high!=0 && isr_low !=0)) {
      isr_high=isr_high_counts;
      isr_low=0;
    }
  }
  if (pwm_get_irq_status_mask()&(1<<dirSlice)) {
    pwm_clear_irq(dirSlice);
    if(pulse_no == 0 || pulse_no > motor_pulses) {
      // time to stop
      pwm_set_enabled(dirSlice, false);
      pwm_set_irq_enabled(dirSlice, false);
      digitalWrite(DEBUG_DIR_PWM_PIN, HIGH);
    }
    else pulse_no--;
  }

}


static float last_rotor_interrupt_delta = 0; // elapsed millisecs
static int last_vane_reading = 0;
static float last_wind_velocity = 0;

static float last_board_T = -999;

Adafruit_BME280 theBME280; // = Adafruit_BME280();
static float last_bme280_temperature=0;
static float last_bme280_humidity=0;
static float last_bme_280_pressure=0;


void read_bme280() 
{
  // The BME280 will read the sensors once (storing in registers)
  // then go back to sleep in "forced" mode.
  // Otherwise, it is continuously updating the registers with t_sb delay
  // the Adafruit lib defaults to 250 ms delay, which is enough to heat 
  // the sensor
  if(theBME280.takeForcedMeasurement()) {
    last_bme280_temperature = theBME280.readTemperature();
    last_bme280_humidity = theBME280.readHumidity();
    last_bme_280_pressure = theBME280.readPressure();
  }

}


void read_board_T() {
  last_board_T = analogReadTemp();
}

float vane_offset_angle = 32; // degrees from North
float last_wind_angle = 0;
float wind_angle_breaks[][2] = {
  {0, 22.5}, // N
  {22.5, 67.5}, // NE
  {67.5, 112.5}, // E
  {112.5, 157.5}, // SE
  {157.5, 202.5}, // S
  {202.5, 247.5}, // SW
  {247.5, 292.5}, // W
  {292.5, 337.5}, // NW
  {337.5, 360} // N
};
char wind_angle_labels[][3] = {
  " N",
  "NE",
  " E",
  "SE",
  " S",
  "SW",
  " W",
  "NW",
  " N"
};

char angle_dir[] = "XX";

// measure analog voltage
void read_vane() {
  // should be 0-1024 i.e. 10 bit
  last_vane_reading = analogRead(WIND_DIR_PIN); 

  last_wind_angle = ((float)last_vane_reading/1024)*360 + vane_offset_angle;
  if (last_wind_angle > 359.99) {
    last_wind_angle = last_wind_angle - 360;
  }
  else if (last_wind_angle < 0) {
    last_wind_angle = last_wind_angle + 360;
  }

 int widx = -1; 
  for (int i=0; i < 9; ++i) {
    if (last_wind_angle >= wind_angle_breaks[i][0] && last_wind_angle < wind_angle_breaks[i][1]) {
       widx = i;
       break;
     }
  }

   if (widx >= 0) {
     strncpy(angle_dir,wind_angle_labels[widx],2);
   }

}

// this stuff removes outlier values from a small buffer
#define ROTOR_DEBOUNCE_BUFLEN 5
float rotor_debounce_buffer[ROTOR_DEBOUNCE_BUFLEN];
float trim_buffer[ROTOR_DEBOUNCE_BUFLEN];
int debounce_buf_idx=0;

float rotor_mad_frac = 0.0; // maximum abs dev fraction
float last_rotor_trimmed_mean=0;
float last_rotor_trimmed_mad=0;
float rotor_mad_threshold = 0.3;


// iterative outlier removal
// will converge on median
// assumes positive-definte values 
void trimmed_mean(float *buf,uint8_t n, float *trimmed_mean, float *trimmed_mad, int max_iter=2) {


  // trim highest, lowest and average remaining
  float lowest = buf[0];
  float highest = buf[0];
  int low_idx=0;
  int high_idx=0;

  memcpy(trim_buffer,buf,n*sizeof(float));
  int buflen = n;

  for (int j=0; j < max_iter; ++j) {
    *trimmed_mean = 0;
    *trimmed_mad = 0;
    for (int i=1; i < buflen; ++i) {
      if (trim_buffer[i] < lowest) {
        low_idx=i;
        lowest = trim_buffer[i];
      }
      if (trim_buffer[i] > highest) {
        high_idx=i;
        highest = trim_buffer[i];
      }
    } 
    // all values the same
    if(low_idx==high_idx) {
      *trimmed_mean=trim_buffer[0];
      *trimmed_mad = 0;
      break;
    }
    // compute mean for mad and update trim buffer
    int trim_idx=0;
    for (int i=0; i < buflen; ++i) {
      if (i!=low_idx && i!=high_idx) {
        *trimmed_mean += (float)trim_buffer[i];
        ++trim_idx;
      }
    }
    *trimmed_mean = *trimmed_mean/(buflen-2);

    // compute trimmed MAD
    for (int i=0; i < buflen; ++i) {
      if (i!=low_idx && i!=high_idx) {
        *trimmed_mad += abs((float)trim_buffer[i] - *trimmed_mean);
      }
    } 
    *trimmed_mad = *trimmed_mad/(buflen-2);

    // check to see if we need more iterations
    if (*trimmed_mean > 0) {
      if(*trimmed_mad/ (*trimmed_mean) < rotor_mad_threshold) {
        break;
      }
    }
    else if (*trimmed_mean == 0) {
      break; // only way to get in pos-def values
    }
    // another round of trimming
    // remove low/high vals from trim_buffer
    for(int k=low_idx; k < buflen-1; ++k) {
      trim_buffer[k]=trim_buffer[k+1];
    }
    for(int k=high_idx; k < buflen-1; ++k) {
      trim_buffer[k]=trim_buffer[k+1];
    }

    lowest = trim_buffer[0];
    highest = trim_buffer[0];
    buflen = buflen - 2;
    low_idx = 0;
    high_idx = 0;
  } // j

}

static float last_rotor_millis = millis();
// after rotor_timeout seconds, just set wind velocity to zero
static float rotor_timeout = 5000;
// measure time
void rotorIsr() {
//    digitalWrite(DEBUG_DIR_PWM_PIN, LOW);

  float millis_now = millis();
  // handle roll-over by not updating value that one time...
  if (millis_now > last_rotor_millis) {
    last_rotor_interrupt_delta = millis_now - last_rotor_millis; //rotor_millis;
  }
  last_rotor_millis = millis();

  rotor_debounce_buffer[debounce_buf_idx] = last_rotor_interrupt_delta;
  debounce_buf_idx+=1;
  if (debounce_buf_idx >= ROTOR_DEBOUNCE_BUFLEN) {
    debounce_buf_idx = 0; //reset
  }

  // the untrimmed latest value
  if (last_rotor_interrupt_delta > 0) {
    last_wind_velocity = ((float)2250)/last_rotor_interrupt_delta;
  }
    
  trimmed_mean(rotor_debounce_buffer, ROTOR_DEBOUNCE_BUFLEN, &last_rotor_trimmed_mean, &last_rotor_trimmed_mad);

//    digitalWrite(DEBUG_DIR_PWM_PIN, HIGH);
}

// initialize I2C
TwoWire theWire(i2c0,D0,D1);

void setup() {
  // put your setup code here, to run once:

//  Serial.begin(9600);

  digitalWrite(PIN_LED, HIGH);
  delay(300);
  digitalWrite(PIN_LED, LOW);
  delay(100);

  // set up sensor pins for wind measurement
  // gpio_set_dir(WIND_DIR_PIN, INPUT);
  pinMode(WIND_DIR_PIN, INPUT);

  pinMode(WIND_SPEED_PIN,INPUT_PULLUP);
  attachInterrupt(WIND_SPEED_PIN,rotorIsr,FALLING);

  // set up PWM outputs for debugging - these simulate the anemometer signals
  // "slice" is a weird name for "Counter Number" - there are 8 16 bit counters (0-7), 
  // each having two channels (A=0,B=1) supporting two outputs with different CC values
  pinMode(DEBUG_DIR_PWM_PIN,OUTPUT);
  digitalWrite(DEBUG_DIR_PWM_PIN, HIGH);
//  delay(100);


  // set up output pin for simualated reed switch pulse
  pinMode(DEBUG_SPEED_ISR_PIN,OUTPUT);
  digitalWrite(DEBUG_SPEED_ISR_PIN, LOW);

  pinMode(DEBUG_SPEED_PWM_PIN,OUTPUT);
  digitalWrite(DEBUG_SPEED_PWM_PIN, HIGH);
  delay(400);
  digitalWrite(DEBUG_SPEED_PWM_PIN, LOW);
  delay(100);

  gpio_set_function(DEBUG_DIR_PWM_PIN, GPIO_FUNC_PWM);
  dirSlice = pwm_gpio_to_slice_num(DEBUG_DIR_PWM_PIN);
  pwm_config dirConfig = pwm_get_default_config();
  // 133 MHz/256 = 519.5 kHz with 256 different levels
  pwm_config_set_wrap(&dirConfig, MOTOR_TOP);
  pwm_init(dirSlice, &dirConfig, true);
  pwm_set_clkdiv_int_frac(dirSlice, MOTOR_CLK_DIV, 0);
  pwm_set_enabled(dirSlice,false);
//  pwm_set_chan_level(dirSlice, 0, DEBUG_DAC_TOP/3);

//  irq_set_enabled(PWM_IRQ_WRAP, true);


  gpio_set_function(DEBUG_SPEED_PWM_PIN, GPIO_FUNC_PWM);
  speedSlice = pwm_gpio_to_slice_num(DEBUG_SPEED_PWM_PIN);

  // Note: 133 MHz/255 = 561.6 kHz
  // at 65535 top = 7.96 Hz
  pwm_config speedConfig = pwm_get_default_config();
  pwm_config_set_wrap(&speedConfig, DEBUG_SPEED_ISR_TOP);
  pwm_init(speedSlice, &speedConfig, true);
  pwm_set_enabled(speedSlice,true);
  pwm_set_chan_level(speedSlice, 0, 25);
  // reduce base clock to ~500 kHz
  pwm_set_clkdiv_int_frac(speedSlice, DEBUG_SPEED_ISR_CLK_DIV, 0);
  isr_high = isr_high_counts;
  isr_low=0;


  irq_add_shared_handler(PWM_IRQ_WRAP, pwmIrqHandler,PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
  irq_set_enabled(PWM_IRQ_WRAP, true);
  pwm_set_irq_enabled(speedSlice, true);
  pwm_clear_irq(speedSlice);

  pwm_set_irq_enabled(dirSlice, true);
  pwm_clear_irq(dirSlice);

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

//  WiFi.config(static_ip,static_dns, static_gateway,static_subnet);
  // Connect to WPA/WPA2 network
  // Just calling begin once and checking status doesn't seem to work
  // repeatedly calling begin after a delay does work though...
  while (wifi_status != WL_CONNECTED) {
    WiFi.config(static_ip,static_dns, static_gateway,static_subnet);
    wifi_status = WiFi.begin(local_ssid,local_pass);
    tft.print('.');
    // wait for connection:
    delay(1000);
  }

  // start the date time NTP updates
  #define NTP_RETRIES 3 
  tft.println("");
  tft.println("Starting NTP...");
  uint8_t retries = 0;
  //   tft.println("NTP update failed " + String(theNTPUDPClient.getEpochTime()));
  while (!dtntp.start() && retries < NTP_RETRIES) {
    tft.print('.');
    wifi_status = WiFi.disconnect();
    while (wifi_status != WL_CONNECTED) {
      WiFi.config(static_ip,static_dns, static_gateway,static_subnet);
      wifi_status = WiFi.begin(local_ssid,local_pass);
      tft.print('#');
      // wait for connection:
      delay(1000);
    }
    delay(1000);
    retries+=1;
  }
  if (retries==NTP_RETRIES) {
    tft.print("Cannot start NTP service");
  }

  delay(1000);

  tft.println("");
  tft.println("Init BME280 THP dev...");

  unsigned bme_status = theBME280.begin(0x76,&theWire);
  tft.println("BME280 status = "+String(bme_status));
  delay(1000);
  if (!bme_status) {
    bme_status = theBME280.begin(0x77,&theWire);
    tft.println("BME280 status = "+String(bme_status));
    delay(1000);
  }
  if(!bme_status) {
    bme_status = theBME280.begin(0x76,&theWire);
    tft.println("BME280 status = "+String(bme_status));
    delay(1000);
  }

  // set to forced mode
  if (bme_status) {
    theBME280.setSampling(Adafruit_BME280::sensor_mode::MODE_FORCED);
  }


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
    label_canvases[i] = new GFXcanvas1(label_pos[i][2],label_pos[i][3]);
  }
  // allocate canvases for rendering
  for (int i=0; i < NUM_BITMAPS; ++i) {
    data_canvases[i] = new GFXcanvas1(data_pos[i][2],data_pos[i][3]);
  }
  
  // print intial labels

  label_canvases[DATE_CANVAS]->fillScreen(TFT_BLACK);

  label_canvases[T_CANVAS]->fillScreen(TFT_BLACK);
  label_canvases[T_CANVAS]->setFont(&FreeMonoBold18pt7b);
  label_canvases[T_CANVAS]->setCursor(5, LABEL_OFFSET);
  label_canvases[T_CANVAS]->printf("T(F)");

  label_canvases[H_CANVAS]->fillScreen(TFT_BLACK);
  label_canvases[H_CANVAS]->setFont(&FreeMonoBold18pt7b);
  label_canvases[H_CANVAS]->setCursor(5, LABEL_OFFSET);
  label_canvases[H_CANVAS]->printf("H(%%)");

  label_canvases[P_CANVAS]->fillScreen(TFT_BLACK);
  label_canvases[P_CANVAS]->setFont(&FreeMonoBold18pt7b);
  label_canvases[P_CANVAS]->setCursor(5, LABEL_OFFSET);
  label_canvases[P_CANVAS]->printf("P(inHg)");

  label_canvases[WINDA_CANVAS]->fillScreen(TFT_BLACK);
  label_canvases[WINDA_CANVAS]->setFont(&FreeMonoBold18pt7b);
  label_canvases[WINDA_CANVAS]->setCursor(5, LABEL_OFFSET);
  label_canvases[WINDA_CANVAS]->printf("D(deg)");

  label_canvases[WINDV_CANVAS]->fillScreen(TFT_BLACK);
  label_canvases[WINDV_CANVAS]->setFont(&FreeMonoBold18pt7b);
  label_canvases[WINDV_CANVAS]->setCursor(5, LABEL_OFFSET);
  label_canvases[WINDV_CANVAS]->printf("V(mph)");
  for (int i=0; i < NUM_BITMAPS; ++i) {
      tft.drawBitmap(label_pos[i][0],label_pos[i][1],label_canvases[i]->getBuffer(),label_pos[i][2],label_pos[i][3],label_canvas_colors[i][0],label_canvas_colors[i][1]);
  }

  char llabel[] = "E-W"; 
  left_button.initButton(&tft,250,450,120,50,TFT_SKYBLUE,TFT_BLACK,TFT_SKYBLUE,llabel,2);
  left_button.drawButton();

  char rlabel[] = "N-S"; 
  right_button.initButton(&tft,70,450,120,50,TFT_SKYBLUE,TFT_BLACK,TFT_SKYBLUE,rlabel,2);
  right_button.drawButton();

  // reset loop update clock
  update_millis = 0;
  rotor_millis  = 0;
  bme_update_millis = bme_update_delay;


  // set up UDP 
  if(udp.listen(UDP_LISTEN_PORT)) {
    udp.onPacket([](AsyncUDPPacket packet) {
      parsePacket(packet);
    });
  }
}

void set_motor_position(uint16_t tcount) {
  pulse_no = motor_pulses;
  last_motor_angle = tcount;
  gpio_set_function(DEBUG_DIR_PWM_PIN, GPIO_FUNC_PWM);
  pwm_set_enabled(dirSlice, false);
  pwm_config dirConfig = pwm_get_default_config();
  pwm_config_set_wrap(&dirConfig, MOTOR_TOP);
  pwm_config_set_clkdiv(&dirConfig, MOTOR_CLK_DIV);
  pwm_init(dirSlice, &dirConfig, false);   
  // map 0-180 degrees to 0.5-2.5 ms level
  float time_to_top = ((float)MOTOR_TOP*(float)MOTOR_CLK_DIV)/(float)PICOW_CLK_FREQ;
  float half_ms_level = (0.0005/time_to_top)*MOTOR_TOP; // pwm_level for for half ms pulse
  float frac = (float)(tcount)/180.0; // fraction of half-rotation
  float motor_width = 4*half_ms_level*frac; // add up to 2 ms to base 0.5 ms pulse 
  uint16_t motor_level = (uint16_t)(MOTOR_TOP - (motor_width + half_ms_level));
  pwm_set_chan_level(dirSlice, 0, motor_level);
  pwm_set_irq_enabled(dirSlice, true);
  pwm_clear_irq(dirSlice);
  pwm_set_enabled(dirSlice, true);
}

enum PACKET_COMMANDS {
  PCOMMAND_RESERVED,
  PCOMMAND_STATUS,
  PCOMMAND_UPTIME,
  PCOMMAND_READ_DIR_AD_RAW,
  PCOMMAND_READ_SPEED_TIMER_RAW,
  PCOMMAND_READ_BME_VALS,
  PCOMMAND_READ_WIND_VALS,
  PCOMMAND_READ_BOARD_T, 
  PCOMMAND_NUM_READ_COMMANDS,
  PCOMMAND_SET_ISR_LOW_COUNT,
  PCOMMAND_SET_ISR_HIGH_COUNT,
  PCOMMAND_SET_DAC_LEVEL, 
  PCOMMAND_SET_MOTOR_POSITION,
  PCOMMAND_SET_BACKLIGHT_LEVEL,
  PCOMMAND_SET_THP_UPDATE_TIME
};

enum PACKET_ERRORS {
  PERR_NONE,
  PERR_UNK_COMMAND,
  PERR_CHECKSUM,
  PERR_NO_ACK,
  PERR_COUNT_RANGE
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
      outgoing_packet_buf[0]=ACK_BYTE;
      outgoing_packet_buf[1]=incoming_packet_buf[1];
      switch(incoming_packet_buf[1]) { // command byte
        case PCOMMAND_STATUS:
        {
          uint32_t uptime_secs = dtntp.last_secs-dtntp.init_secs;
          outgoing_packet_buf[2]=uptime_secs&255;
          outgoing_packet_buf[3]=(uptime_secs>>8)&255;
          outgoing_packet_buf[4]=(uptime_secs>>16)&255;
          outgoing_packet_buf[5]=(uptime_secs>>24)&255;
          outgoing_packet_buf[6]=((uint8_t)last_motor_angle)&255;
          int16_t scaled_T = (int16_t)(last_board_T*10);
          outgoing_packet_buf[7]=(uint8_t)(scaled_T&255);
          outgoing_packet_buf[8]=(uint8_t)((scaled_T>>8)&255);
          outgoing_packet_buf[9]=(uint8_t)((received_packet_count)&255);
          outgoing_packet_buf[10]=(uint8_t)((received_packet_count>>8)&255);
          outgoing_packet_buf[11]=(uint8_t)((received_packet_count>>16)&255);
          outgoing_packet_buf[12]=(uint8_t)((received_packet_count>>24)&255);
          outgoing_packet_buf[13]=(uint8_t)(bme_update_delay&255);
          outgoing_packet_buf[14]=(uint8_t)((bme_update_delay>>8)&255);
          for (int i=0; i < VERSION_STRLEN; ++i) {
            outgoing_packet_buf[i+15]=version[i];
          }
          outgoing_data_len=17+VERSION_STRLEN;
          checksum_packet(outgoing_packet_buf, outgoing_data_len);
          break;
        }
        case PCOMMAND_UPTIME:
        {
          uint32_t uptime_secs = dtntp.last_secs-dtntp.init_secs;
          outgoing_packet_buf[2]=(uint8_t)(uptime_secs&255);
          outgoing_packet_buf[3]=(uint8_t)((uptime_secs>>8)&255);
          outgoing_packet_buf[4]=(uint8_t)((uptime_secs>>16)&255);
          outgoing_packet_buf[5]=(uint8_t)((uptime_secs>>24)&255);
          outgoing_data_len=8;
          checksum_packet(outgoing_packet_buf, outgoing_data_len);
          break;
        }
        case PCOMMAND_READ_DIR_AD_RAW:
        {
          outgoing_packet_buf[2]=(uint8_t)(last_vane_reading&255);
          outgoing_packet_buf[3]=(uint8_t)((last_vane_reading>>8)&255);
          outgoing_data_len=6;
          checksum_packet(outgoing_packet_buf, outgoing_data_len);
          break;
        }
        case PCOMMAND_READ_SPEED_TIMER_RAW:
        {
          uint16_t tlri = (uint16_t)last_rotor_interrupt_delta;
          outgoing_packet_buf[2]=(uint8_t)(tlri&255);
          outgoing_packet_buf[3]=(uint8_t)((tlri>>8)&255);
          outgoing_data_len=6;
          checksum_packet(outgoing_packet_buf, outgoing_data_len);
          break;
        }
        case PCOMMAND_READ_BOARD_T:
        {
          int16_t scaled_T = (int16_t)(last_board_T*10);
          outgoing_packet_buf[2]=(uint8_t)(scaled_T&255);
          outgoing_packet_buf[3]=(uint8_t)((scaled_T>>8)&255);
          outgoing_data_len=6;
          checksum_packet(outgoing_packet_buf, outgoing_data_len);
          break;
        }
        case PCOMMAND_READ_BME_VALS:
        {
          int16_t scaled_T = (int16_t)(last_bme280_temperature*10);
          outgoing_packet_buf[2]=(uint8_t)(scaled_T&255);
          outgoing_packet_buf[3]=(uint8_t)((scaled_T>>8)&255);
          int16_t scaled_H = (int16_t)(last_bme280_humidity*10);
          outgoing_packet_buf[4]=(uint8_t)(scaled_H&255);
          outgoing_packet_buf[5]=(uint8_t)((scaled_H>>8)&255);
          int16_t scaled_P = (int16_t)(0.1*last_bme_280_pressure/3.387); // 100x inHg vals
          outgoing_packet_buf[6]=(uint8_t)(scaled_P&255);
          outgoing_packet_buf[7]=(uint8_t)((scaled_P>>8)&255);
          outgoing_data_len=10;
          checksum_packet(outgoing_packet_buf, outgoing_data_len);
          break;
        }
        case PCOMMAND_READ_WIND_VALS:
        {
          int16_t scaled_A = (int16_t)(last_wind_angle*10);
          outgoing_packet_buf[2]=(uint8_t)(scaled_A&255);
          outgoing_packet_buf[3]=(uint8_t)((scaled_A>>8)&255);
          int16_t scaled_V = (int16_t)(last_wind_velocity*10);
          outgoing_packet_buf[4]=(uint8_t)(scaled_V&255);
          outgoing_packet_buf[5]=(uint8_t)((scaled_V>>8)&255);
          if (last_rotor_trimmed_mean > 0) {
            scaled_V = (int16_t)((2250.0/last_rotor_trimmed_mean)*10);
          }
          else scaled_V = 0;
          outgoing_packet_buf[6]=(uint8_t)(scaled_V&255);
          outgoing_packet_buf[7]=(uint8_t)((scaled_V>>8)&255);          
          scaled_V = (int16_t)(last_rotor_trimmed_mad*10);
          outgoing_packet_buf[8]=(uint8_t)(scaled_V&255);
          outgoing_packet_buf[9]=(uint8_t)((scaled_V>>8)&255); 
          outgoing_data_len=12;
          checksum_packet(outgoing_packet_buf, outgoing_data_len);
          break;
        }
        case PCOMMAND_SET_ISR_LOW_COUNT:
        {
          uint16_t tcount = (uint16_t)incoming_packet_buf[2]+(((uint16_t)incoming_packet_buf[3])<<8);
          outgoing_packet_buf[2]=(uint8_t)(tcount&255);
          outgoing_packet_buf[3]=(uint8_t)((tcount>>8)&255);
          outgoing_data_len=6;
          checksum_packet(outgoing_packet_buf, outgoing_data_len);
          if (tcount > 0 && tcount < 5000) { // 5 s max
            isr_low_counts = tcount;
          }
          else last_packet_error = PERR_COUNT_RANGE;
          break;
        }
        case PCOMMAND_SET_ISR_HIGH_COUNT:
        {
          uint16_t tcount = (uint16_t)incoming_packet_buf[2]+(((uint16_t)incoming_packet_buf[3])<<8);
          outgoing_packet_buf[2]=(uint8_t)(tcount&255);
          outgoing_packet_buf[3]=(uint8_t)((tcount>>8)&255);
          outgoing_data_len=6;
          checksum_packet(outgoing_packet_buf, outgoing_data_len);
          if (tcount > 0 && tcount < 5000) { // 5 s max
            isr_high_counts = tcount;
          }
          else last_packet_error = PERR_COUNT_RANGE; 

          break;
        }
        case PCOMMAND_SET_DAC_LEVEL:
        {
          uint16_t tcount = (uint16_t)incoming_packet_buf[2]+(((uint16_t)incoming_packet_buf[3])<<8);
          outgoing_packet_buf[2]=(uint8_t)(tcount&255);
          outgoing_packet_buf[3]=(uint8_t)((tcount>>8)&255);
          outgoing_data_len=6;
          checksum_packet(outgoing_packet_buf, outgoing_data_len);
          pwm_set_irq_enabled(dirSlice, false);
          if (tcount < DEBUG_DAC_TOP) { 
              if (tcount == 0) { 
                // stop PWM
                pwm_set_enabled(dirSlice,false);
              }
              else {
                gpio_set_function(DEBUG_DIR_PWM_PIN, GPIO_FUNC_PWM);
                pwm_config dirConfig = pwm_get_default_config();
                pwm_config_set_wrap(&dirConfig, DEBUG_DAC_TOP);
                pwm_config_set_clkdiv(&dirConfig, DEBUG_DAC_CLK_DIV);
                pwm_init(dirSlice, &dirConfig, false);   
                pwm_set_chan_level(dirSlice, 0, tcount);
                pwm_set_enabled(dirSlice, true);
              }
          }
          else last_packet_error = PERR_COUNT_RANGE; 

          break;
        }
        case PCOMMAND_SET_MOTOR_POSITION:
        {
          uint16_t tcount = (uint16_t)incoming_packet_buf[2]+(((uint16_t)incoming_packet_buf[3])<<8);
          outgoing_packet_buf[2]=(uint8_t)(tcount&255);
          outgoing_packet_buf[3]=(uint8_t)((tcount>>8)&255);
          outgoing_data_len=6;
          checksum_packet(outgoing_packet_buf, outgoing_data_len);
          if (tcount <= 180) {
              set_motor_position(tcount);
          }
          else last_packet_error = PERR_COUNT_RANGE; 

          break;
        }
        case PCOMMAND_SET_BACKLIGHT_LEVEL:
        {
          uint16_t tcount = (uint16_t)incoming_packet_buf[2]+(((uint16_t)incoming_packet_buf[3])<<8);
          outgoing_packet_buf[2]=(uint8_t)(tcount&255);
          outgoing_packet_buf[3]=(uint8_t)((tcount>>8)&255);
          outgoing_data_len=6;
          checksum_packet(outgoing_packet_buf, outgoing_data_len);
          pwm_set_irq_enabled(dirSlice, false);
          uint16_t level = (uint16_t)(((float)tcount/100)*BACKLIGHT_TOP);
          if (level < BACKLIGHT_TOP) { 
            pwm_set_chan_level(backlight_pwm_slice, 1, level); 
          }
          else last_packet_error = PERR_COUNT_RANGE; 
          break;
        }
        case PCOMMAND_SET_THP_UPDATE_TIME:
        {
          uint16_t tcount = (uint16_t)incoming_packet_buf[2]+(((uint16_t)incoming_packet_buf[3])<<8);
          outgoing_packet_buf[2]=(uint8_t)(tcount&255);
          outgoing_packet_buf[3]=(uint8_t)((tcount>>8)&255);
          outgoing_data_len=6;
          checksum_packet(outgoing_packet_buf, outgoing_data_len);
          if (tcount < 3600) { // max 1 hour
            bme_update_delay = tcount*1000; // ms 
          }
          else last_packet_error = PERR_COUNT_RANGE; 
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

    // there was an error so send a NACK 
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

  uint16_t x,y;
  if (tft.getTouch(&x, &y)) {
    if (left_button.contains(x,y)) {
      if (!left_button.isPressed()) {
        left_button.press(true); 
        left_button.drawButton(true);
        gizmo_state = STATE_DO_LEFT_BUTTON;
      }
    }
    else if (right_button.contains(x,y)) {
      if (!right_button.isPressed()) {
        right_button.press(true); 
        right_button.drawButton(true);
        gizmo_state = STATE_DO_RIGHT_BUTTON;
      }
    }
    else {
        gizmo_state = STATE_UPDATE;
      }
  }
  else if (pulse_no > 0 && update_millis < update_delay) {
    gizmo_state = STATE_MOTOR_MOVING;
  }
  else if (update_millis > update_delay) {
      gizmo_state = STATE_UPDATE;
      update_millis = 0;
  }
  else {
    gizmo_state = STATE_WAIT;
  }

  switch (gizmo_state) {
    case STATE_UPDATE:
    {
      // update sensor values
      read_board_T();
      read_vane();
      if (bme_update_millis >= bme_update_delay) {
        read_bme280();
        bme_update_millis=0;
      }

       // update date/time first
      dtntp.get_date();
      data_canvases[DATE_CANVAS]->fillScreen(TFT_BLACK);
      data_canvases[DATE_CANVAS]->setFont(&FreeMonoBold12pt7b);
      data_canvases[DATE_CANVAS]->setCursor(30, 16);
      data_canvases[DATE_CANVAS]->printf("%s\n",dtntp.date_cstring);
      int16_t ycur = data_canvases[DATE_CANVAS]->getCursorY();
      data_canvases[DATE_CANVAS]->setCursor(0, ycur+5);
      data_canvases[DATE_CANVAS]->setFont(&FreeMonoBold18pt7b);
      data_canvases[DATE_CANVAS]->printf("%s",dtntp.time_cstring);

      data_canvases[T_CANVAS]->fillScreen(TFT_BLACK);
      data_canvases[T_CANVAS]->setFont(&FreeMonoBold18pt7b);
      data_canvases[T_CANVAS]->setCursor(15, LABEL_OFFSET);
      data_canvases[T_CANVAS]->printf("%.1f" ,(9*last_bme280_temperature/5)+32);

      data_canvases[H_CANVAS]->fillScreen(TFT_BLACK);
      data_canvases[H_CANVAS]->setFont(&FreeMonoBold18pt7b);
      data_canvases[H_CANVAS]->setCursor(15, LABEL_OFFSET);
      data_canvases[H_CANVAS]->printf("%.1f" ,last_bme280_humidity);

      data_canvases[P_CANVAS]->fillScreen(TFT_BLACK);
      data_canvases[P_CANVAS]->setFont(&FreeMonoBold18pt7b);
      data_canvases[P_CANVAS]->setCursor(15, LABEL_OFFSET);
      data_canvases[P_CANVAS]->printf("%.2f",0.001*last_bme_280_pressure/3.387);

      data_canvases[WINDA_CANVAS]->fillScreen(TFT_BLACK);
      data_canvases[WINDA_CANVAS]->setFont(&FreeMonoBold18pt7b);
      data_canvases[WINDA_CANVAS]->setCursor(5, LABEL_OFFSET);
      data_canvases[WINDA_CANVAS]->printf("%.0f %s",last_wind_angle, angle_dir);


      data_canvases[WINDV_CANVAS]->fillScreen(TFT_BLACK);
      data_canvases[WINDV_CANVAS]->setFont(&FreeMonoBold18pt7b);
      data_canvases[WINDV_CANVAS]->setCursor(5, LABEL_OFFSET);
      // nominal speed is S = 2250/T where T is in ms and S is in mph
      // See e.g.
      // https://www.digitalconcepts.net.au/arduino/index.php?op=DavisWind
      // There is a correction table vs. angle available too: see
      //  https://github.com/kobuki/weewx-meteoRX/tree/master
      float rotor_now = millis();
      if (rotor_now - last_rotor_millis > rotor_timeout) {
        // briefly disable interrupts
          noInterrupts();
          last_rotor_millis = rotor_now;
          last_rotor_trimmed_mean = 0;
          last_rotor_trimmed_mad = 0;
          memset(rotor_debounce_buffer,0,sizeof(float)*ROTOR_DEBOUNCE_BUFLEN);
          interrupts();
          data_canvases[WINDV_CANVAS]->printf("0.0 *");
      }
      else if (last_rotor_trimmed_mean > 0) {
          if ((last_rotor_trimmed_mad/last_rotor_trimmed_mean) < rotor_mad_threshold) {       
            data_canvases[WINDV_CANVAS]->printf("%.1f", 2250.0/last_rotor_trimmed_mean);
          }
      }
      else {
        data_canvases[WINDV_CANVAS]->printf("0.0");
      }

//      data_canvases[DATA_CANVAS]->fillScreen(TFT_BLACK);
//      data_canvases[DATA_CANVAS]->setFont(&FreeMonoBold12pt7b);
//      data_canvases[DATA_CANVAS]->setCursor(5, 40);
//      data_canvases[DATA_CANVAS]->printf("%.1f %.3f",last_rotor_trimmed_mean, last_rotor_trimmed_mad);

      for (int i=0; i < NUM_BITMAPS; ++i) {
          tft.drawBitmap(data_pos[i][0],data_pos[i][1],data_canvases[i]->getBuffer(),data_pos[i][2],data_pos[i][3],data_canvas_colors[i][0],data_canvas_colors[i][1]);
      }

      if (left_button.isPressed()) {
        left_button.press(false);
        left_button.drawButton();
      }
      if (right_button.isPressed()) {
        right_button.press(false);
        right_button.drawButton();
      }

      break;
    }
    case STATE_DO_LEFT_BUTTON: 
    {
      set_motor_position(95);
      break;
    }
    case STATE_DO_RIGHT_BUTTON: 
    {
      set_motor_position(5);
      break;
    }
    case STATE_MOTOR_MOVING: 
    {
      break;
    }
    case STATE_WAIT:
    {
      break;
    }
  }

}
