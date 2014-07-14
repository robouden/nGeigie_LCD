/*
  nGeigie.ino

Connection:
 * An Arduino Ethernet Shield
 * D2: The output pin of the Geiger counter (active low)
 * D3: The output pin of the Geiger counter (active low)
 based on NetRad 1.1.2

 created 18 Nov 2013
 updated 22 Nov 2013
 updated 10 Dec 2013 added LCD display
 by Allan Lind: alind@joho.com
 Code from Lionel and Kalin of Safecast
 This code is in the public domain.
 */
 
 

#include <SPI.h>         // needed for Arduino versions later than 0018
#include <Ethernet.h>
#include <limits.h>
#include <avr/wdt.h>
#include "board_specific_settings.h"
#include <SdFat.h>
#include <LiquidCrystal.h>


static char VERSION[] = "V2.1.5";
static char checksum(char *s, int N);

// initialize the library with the numbers of the interface pins
	LiquidCrystal lcd(A0,A1,8,5,6,7);

// screen variables
	int brightness;
	unsigned long dimmerTimer;
	int dimmed;
	int tilt_pre;

// this holds the info for the device
// static device_t dev;
	#define LINE_SZ 80

static char json_buf[LINE_SZ];
static char buf[LINE_SZ];

// Sampling interval (e.g. 60,000ms = 1min)
	unsigned long updateIntervalInMillis = 0;

// The next time to feed
	unsigned long nextExecuteMillis = 0;

// Event flag signals when a geiger event has occurred
	volatile unsigned char eventFlag = 0;		// FIXME: Can we get rid of eventFlag and use counts>0?
	int counts_per_sample;

// The last connection time to disconnect from the server
// after uploaded feeds
	long lastConnectionTime = 0;

// The conversion coefficient from cpm to µSv/h
	float conversionCoefficient = 0;
	
void onPulse()
	{
		counts_per_sample++;
		eventFlag = 1;
	}

unsigned long elapsedTime(unsigned long startTime);
unsigned long int  nSv ;

// holds the control info for the device
	static devctrl_t ctrl;

static FILE uartout = {0};		// needed for printf



int freeRAM ()
	{
		extern int __heap_start, *__brkval;
		int v;
		return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval);
	}


//SD read config file NGEIGIE.CFG

	  SdFat sd;
	  char *logfile_name= "DATA.CSV";
	  const uint8_t chipSelect = 4;
	  ArduinoOutStream cout(Serial);
	  #define error(s) sd.errorHalt_P(PSTR(s)) 
			  typedef struct tprofiles
			{
			  char *packetBuffer;  
			}tprofiles;
			tprofiles temp[3]; 
		
	void sdread() {
		  char line_SD[22];
		  int n,i=0;
		  // open test file
		  SdFile rdfile("NGEIGIE.CFG", O_READ);

	// check for open error
		  if (!rdfile.isOpen()) error("sdtest");

	// read lines from the file
		   while ((n = rdfile.fgets(line_SD, sizeof(line_SD))) > 0) {
		   	if (line_SD[n - 1] == '\n') {
      		line_SD[n-1] = 0;
      		}
		if(i<4)temp[i].packetBuffer = strdup(line_SD);
		i++;
  		}
  		apiKey = temp[0].packetBuffer;
  		ID = temp[1].packetBuffer;
  		lat = temp[2].packetBuffer;
  		lon = temp[3].packetBuffer;  			
  		sprintf_P(logfile_name, PSTR("%s.log"),ID);
  	    Serial.print(F("Logfile name:\t"));
  	    Serial.println(logfile_name);
  	    
	}
	// generate checksum for log format
		byte len, chk;     
		char checksum(char *s, int N)
		{
		  int i = 0;
		  char chk = s[0];

		  for (i=1 ; i < N ; i++)
			chk ^= s[i];

		  return chk;
		}
    
void setup() {  
	
	// The uart is the standard output device STDOUT.
	stdout = &uartout ;

	// init command line parser
        Serial.begin(9600);

	// init the control info
	memset(&ctrl, 0, sizeof(devctrl_t));

	// enable watchdog to allow reset if anything goes wrong			
	wdt_enable(WDTO_8S);                                                
	// comment out to disable interrupts max 8S = 8 seconds

	Serial.println();
	// Set the conversion coefficient from cpm to µSv/h
		// LND_7318:
				Serial.println(F("Sensor Model: LND 7317"));
				Serial.println(F("Conversion factor: 344 cpm = 1 uSv/Hr"));
				conversionCoefficient = 0.0029;

		// LND_712:
				//conversionCoefficient = 0.0083;
			    //	Serial.println(F("Sensor model:   LND 712"));
   

  
	// set brightness
	  dimmed = 0;
  
	//set up the LCD's number of columns and rows: 
		  lcd.begin(8, 2);

	//switch off ethernet
		  pinMode(10,OUTPUT);
		  digitalWrite(10,HIGH);
	//start SD card
		  if (!sd.begin(chipSelect, SPI_HALF_SPEED)) sd.initErrorHalt(); 
		  sdread();
	//switch off SD card
		  pinMode(4,OUTPUT);
		  digitalWrite(4,HIGH);

	// Print a message to the LCD.
		  lcd.clear();
		  lcd.print(F("nGeigie"));
		  delay(1000);
		  lcd.setCursor(0, 1);
		  lcd.print(VERSION);
	//Free ram print
		  Serial.print(F("Free RAM:\t"));
		  Serial.println(freeRAM());


/**************************************************************************/
// Print out the current device ID
/**************************************************************************/

	// Initiate a DHCP session
	//Serial.println(F("Getting an IP address..."));
        if (Ethernet.begin(macAddress) == 0)
	{
       		Serial.println(F("Failed DHCP"));
  // DHCP failed, so use a fixed IP address:
        	Ethernet.begin(macAddress, localIP);
	}
	Serial.print(F("local_IP:\t"));
	Serial.println(Ethernet.localIP());

	// Attach an interrupt to the digital pin and start counting
	//
	// Note:
	// Most Arduino boards have two external interrupts:
	// numbers 0 (on digital pin 2) and 1 (on digital pin 3)
			attachInterrupt(0, onPulse, interruptMode);                                // comment out to disable the GM Tube
  	        //attachInterrupt(1, onPulse, interruptMode);                                // comment out to disable the GM Tube
		updateIntervalInMillis = updateIntervalInMinutes * 300000;                  // update time in ms

	unsigned long now = millis();
	nextExecuteMillis = now + updateIntervalInMillis;

	// Walk the dog
	wdt_reset();
	
	Serial.print(F("ID:\t\t")) ;
	Serial.println(ID) ;
	Serial.println(F("setup OK."));	
	lcd.setCursor(0, 1);
	lcd.print(F("setup OK"));
	
}

	
//**************************************************************************/
/*!
//  On each falling edge of the Geiger counter's output,
//  increment the counter and signal an event. The event
//  can be used to do things like pulse a buzzer or flash an LED
*/
/**************************************************************************/


void SendDataToServer(float CPM) {

	// Convert from cpm to µSv/h with the pre-defined coefficient
	float uSv = CPM * conversionCoefficient;                   // convert CPM to Micro Sieverts Per Hour
	float f_nSv = CPM * conversionCoefficient * 1000;          // convert CPM to Nano Sieverts Per Hour


    nSv = (long) (f_nSv+0.5) ;                                 // Convert Floating Point to 32 Bit Integer

	//display geiger info
	lcd.clear();
	lcd.setCursor(0, 0);
    lcd.print(uSv);
    lcd.print(F("uS/H"));
  
	if (client.connected())
	{
		Serial.println(F("Disconnecting"));
		client.stop();
	}

	// Try to connect to the server
	Serial.println(F("Connecting"));
	if (client.connect(serverIP, 80))
	{
		Serial.println(F("Connected"));
		lastConnectionTime = millis();

		// clear the connection fail count if we have at least one successful connection
		ctrl.conn_fail_cnt = 0;
	}
	else
	{
		ctrl.conn_fail_cnt++;
		if (ctrl.conn_fail_cnt >= MAX_FAILED_CONNS)
		{
				ctrl.state = RESET;
		}
		lastConnectionTime = millis();
		return;
	}

	// Convert from cpm to µSv/h with the pre-defined coefficient
        char CPM_string[16];
        dtostrf(CPM, 0, 0, CPM_string);

    // prepare the log entry
	memset(json_buf, 0, LINE_SZ);
	sprintf_P(json_buf, PSTR("{\"longitude\":\"%s\",\"latitude\":\"%s\",\"device_id\":\"%s\",\"value\":\"%s\",\"unit\":\"cpm\"}"),  \
	              lon, \
	              lat, \
	              ID,  \
	              CPM_string);

	int len = strlen(json_buf);
	json_buf[len] = '\0';
	Serial.println(json_buf);

	client.print("POST /scripts/index.php?api_key=");
	client.print(apiKey);
	client.println(F(" HTTP/1.1"));
	client.println(F("Accept: application/json"));
	client.println(F("Host: 107.161.164.166"));
	client.print(F("Content-Length: "));
	client.println(strlen(json_buf));
	client.println(F("Content-Type: application/json"));
	client.println();
	client.println(json_buf);
	Serial.println(F("Disconnecting"));
	
	
	//Create and write SD logfile
	memset(buf, 0, LINE_SZ);
	sprintf_P(buf, PSTR("$BNRDD,%s,,,,%s,,,,%s,,%s,,,,,,"),  \
              ID, \
              CPM_string, \
              lat,\
              lon);
	              
	 len = strlen(buf);
	  buf[len] = '\0';

	  // generate checksum
	  chk = checksum(buf+1, len);

	  // add checksum to end of line before sending
	  if (chk < 16)
		sprintf_P(buf + len, PSTR("*0%X"), (int)chk);
	  else
		sprintf_P(buf + len, PSTR("*%X"), (int)chk);

	//Serial.println(buf);
	SdFile wrfile;
	  if (!wrfile.open(logfile_name, O_RDWR | O_CREAT | O_AT_END)) {
    sd.errorHalt("opening file for write failed");
	  }

    wrfile.println(buf);
    //Serial.print(logfile_name);
    Serial.println(F("SDcard data written"));
    // close the file:
    wrfile.close();
    
    
    // report to LCD 
	lcd.setCursor(0, 1);
	lcd.print(F("Send OK"));
	client.stop();
}

int controlBrightness()
{
  int dim_coeff;
  dim_coeff = 0;
}

/**************************************************************************/
// Main Loop
/**************************************************************************/


void loop() {                                             
	// Main Loop
	// Walk the dog only if we're not in RESET state. if we're in RESET,
	// then that means something happened to mess up our connection and
	// we will just let the device gracefully reset
	if (ctrl.state != RESET)
	{
		wdt_reset();
	}


	// maintain the DHCP lease, if needed
	Ethernet.maintain();

	// Add any geiger event handling code here

	// check if its time to update server.
	// elapsedTime function will take into account counter rollover.
	if (elapsedTime(lastConnectionTime) < updateIntervalInMillis)
	{
		return;
	}

	float CPM = (float)counts_per_sample / (float)updateIntervalInMinutes/5;
	//int CPM = (int)counts_per_sample / (float)updateIntervalInMinutes/5;
	counts_per_sample = 0;

        SendDataToServer(CPM);

}


/**************************************************************************/
// calculate elapsed time, taking into account rollovers
/**************************************************************************/
unsigned long elapsedTime(unsigned long startTime)
{
	unsigned long stopTime = millis();

	if (startTime >= stopTime)
	{
		return startTime - stopTime;
	}
	else
	{
		return (ULONG_MAX - (startTime - stopTime));
	}
}


void GetFirmwareVersion()
{
	printf_P(PSTR("Firmware_ver:\t%s\n"), VERSION);
}


 
