/* Arduino nano atmega328
 * A clock that displays the GPS time, as used by LIGO.
 * Uses u-blox NEO 6M (or later) receiver, and three 4-digit 7-segment displays driven by I2C.
 * All messages are turned off except for the UBX  NAV TIMEGPS message, which is sent every second.
 * v1.0 Graham Woan 2019
 */

#include <TimerOne.h>              // for the 10 ms interrupts
#include <SoftwareSerial.h>        // to talk to the gps module
#include <Wire.h>                  // to talk to the displays
#include "Adafruit_LEDBackpack.h"  // for the displays

const byte interruptPin = 2;             // 1 pps interrupt pin
static const int RXPin = 10, TXPin = 11; // serial connections to the gps module
static const uint32_t GPSBaud = 9600;    // gps module serial speed
float olight=15, light=15;  // light reading

// define the ubx messages to initialise the gps receiver.  ubx_cfg_prt sets the output protocol to ubx only, on UART1.  ubx_cfg_msg selects the NAV-TIME meassage.
// some modules will remember this, and can be set up offline, but some don't, so it's safer to reinitialise like this via the arduino whenever the module starts.
const byte reset_gps[]   {0xB5, 0x62, 0x06, 0x09, 0x0D, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x03, 0x1B, 0x9A};
const byte ubx_cfg_prt[] {0xB5, 0x62, 0x06, 0x00, 0x14, 0x00, 0x01, 0x00, 0x00, 0x00, 0xD0, 0x08, 0x00, 0x00, 0x80, 0x25, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x9C, 0x89};
const byte ubx_cfg_msg[] {0xB5, 0x62, 0x06, 0x01, 0x08, 0x00, 0x01, 0x20, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x31, 0x90};

// define the stucture of the UBX NAV-TIMEGPS message (24 bytes total)
typedef struct{
  uint16_t head;
  uint16_t id;
  uint16_t len;
  uint32_t itow;
  int32_t  ftow;
  uint16_t week;
  uint8_t  leap;
  uint8_t  valid;
  uint32_t tacc;
  uint8_t  check_A;
  uint8_t  check_B;
} UBX; // UBX is a datatype of this shape

typedef union{  // overlay a byte array on top of the message structure for ease of reading-in.
  UBX ubx;
  byte  c[24];
} u_gpsdata; // this is a union data type

volatile unsigned long gpstime = 0; // this will be used to hold the integer gps time
volatile byte ticks = 0; // this will hold the fractional gps time (00-99)

// The serial connection to the GPS device
SoftwareSerial ss(RXPin, TXPin);

// Initialise the three displays
Adafruit_7segment matrix1 = Adafruit_7segment();
Adafruit_7segment matrix2 = Adafruit_7segment();
Adafruit_7segment matrix3 = Adafruit_7segment();

void setup() {   // need to solder the jumpers to get these addresses. Matrix 1 is the least significant.
  matrix1.begin(0x70); // no jumper
  matrix2.begin(0x71); // just first jumper closed
  matrix3.begin(0x72); // just second jumper closed

  Timer1.initialize(10000); //10 ms
  Timer1.attachInterrupt(jiffy); //  to run every 0.01 seconds

  pinMode(interruptPin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(interruptPin), pps, RISING); 

  Serial.begin(115200); // for diagnostics
  ss.begin(GPSBaud);

// Send the initialsation messages to the gps module
  for(int i = 0; i < 28; i++)  ss.write(ubx_cfg_prt[i]); // set up for ubx only
  delay (100);
  for(int i = 0; i < 16; i++)  ss.write(ubx_cfg_msg[i]); // choose the TIMEGPS message
}

// *************interrupts routines****************
// this runs every 10 ms
void jiffy() {
    if (ticks>99) ticks = 0;
    if (ticks==99) gpstime = gpstime + 1;
    ticks = ticks + 1;
   }

// This runs when there is a gps 1 pps interrupt.
void pps() { // this is called on the rising edge of the GPS 1 pps signal and phases the jiffy counter correctly
    ticks = 0;
    Timer1.restart(); // reset the phasing of the jiffy counter so that these interrupts coincide with the 1pps interrupts as closely as possible
}


// *********** main loop *************
void loop() {
  float m1;
  unsigned long   m2, m3;
  u_gpsdata data;   // refer to data.ubx or data.c
  byte CK_A = 0, CK_B = 0; // checksums
  int i;

 // Try reading the data.  It will take a couple of seconds for this to get in sync witht the start byte   
  if ((ss.available() > 0) && (ticks>10)) {
      for (int i = 0; i<24; i++){
        data.c[i] = ss.read();
        }     
       ss.flush(); 

// determine the payload checksum
       CK_A = 0; CK_B = 0;
       for (i = 2; i < 22 ;i++) {
         CK_A = CK_A + data.c[i];
         CK_B = CK_B + CK_A;
        }

    if ((data.ubx.check_A == CK_A)  && (data.ubx.check_B == CK_B) ){ // does the checksum check OK?
        gpstime =  long(data.ubx.itow /1000) + long(data.ubx.week)*604800 ; // no need to use the leap seconds because these are gps itow and week
// some diagnostics
        Serial.print(gpstime);
        Serial.print(" itow: ");Serial.print(data.ubx.itow);
        Serial.print(" week: ");Serial.print(data.ubx.week);
        Serial.print(" leap: ");Serial.println(data.ubx.leap);
// adjust the display brightness, with a simple smoothing.
        light = 15*analogRead(A0)/40; 
        if (light>16) light = 16;
        if (light<1) light = 1;
        light  = (0.1*light + olight)/1.1;
        matrix1.setBrightness(int(light));
        matrix2.setBrightness(int(light));
        matrix3.setBrightness(int(light));
        olight = light;
        Serial.print(" light: ");Serial.println(light);
       }
      }
// the three display numbers
    m1 = (gpstime % 100) + (ticks % 100)/100.0;
    m2 =(gpstime / 100) % 10000;
    m3 = (gpstime / 1000000) % 10000;
    
// write to the least significant display, which need a decimal point in the middle.
    matrix1.println(m1);
    if (m1<10) matrix1.writeDigitNum(0, 0);
    matrix1.writeDisplay();
    
 // use the following rather convoluted procedure for the other digits to make sure leading zeros are present   
    matrix2.writeDigitNum(0, (m2 /1000) % 10,false);
    matrix2.writeDigitNum(1, (m2 /100) % 10,false);
    matrix2.writeDigitNum(3, (m2 /10) % 10,false);
    matrix2.writeDigitNum(4, m2 % 10,false);
    matrix2.writeDisplay();

    matrix3.writeDigitNum(0, (m3 /1000) % 10,false);
    matrix3.writeDigitNum(1, (m3 /100) % 10,false);
    matrix3.writeDigitNum(3, (m3 /10) % 10,false);
    matrix3.writeDigitNum(4, m3 % 10,false);
    matrix3.writeDisplay();
    matrix3.writeDisplay();
}

