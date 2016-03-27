 
/*--------------------------OPEN TURBIDIMETER PROJECT------------------------*/
/*
Copyright 2012-2014 Alex Krolick and Chris Kelley

     This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
/*------------------------------- ARDUINO CODE ------------------------------*/

// Flags
boolean debug = false;  // IMPORTANT: to update EEPROM, change this to true
boolean using_modem = false; // if not using a GSM modem, change to false
boolean using_bluetooth = false; //
long debug_rep_count = 0;

// Libraries
// NOTE: Be sure to use the libraries included with this sketch
// (by placing them in your Arduino or Sketchbook folder)
#include <PinChangeInt.h>
#include <EEPROM.h>
#include <EEPROMAnything.h>
boolean common_cathode_display = true; //if using a common-anode seven segment display, change to false
boolean button_resting_low = false;

/*if using common_cathode_display, uncomment the two lines below, and comment the two lines below those*/
#define COUNT_SEVEN_SEG_NUMBERS  13
#define COUNT_SEVEN_SEG_LETTERS   9
#define SEVEN_SEG_NUMBERS {B11000000,  B11111001,  B10100100,  B10110000, B10011001,  B10010010,  B10000010,  B11111000, B10000000,  B10010000,  B01111111,  B10111111, B11111111}
#define SEVEN_SEG_LETTERS {B10101111,  B10000110,  B10100001,  B10010001, B11000111,  B10010010,  B10000111,  B11000000, B11100011}

/*otherwise, use this*/
//#define SEVEN_SEG_NUMBERS {B00111111,  B00000110,  B01011011,  B01001111, B01100110,  B01101101,  B01111101,  B00000111, B01111111,  B01101111,  B10000000,  B01000000, B00000000}
//#define SEVEN_SEG_LETTERS {B01010000,  B01111001,  B01011110,  B01101110, B00111000,  B01101101,  B01111000,  B00111111, B00011100}

//Definitions
#define IR_LED           4
#define TSL_S1          12
#define TSL_S0          11
#define TSL_FREQ_PIN    13
#define TSL_OE           9
#define BPIN             2    // external button
#define VPIN            A4    // read voltage from this pin
#define SHIFT_CLOCK_PIN  8
#define SHIFT_DATA_PIN   5
#define SHIFT_LATCH_PIN  6
#define TPIN            A0 
#define DIV_R1       10000   // resistance for R1
#define DIV_R2        1000   // resistance for R2
#define VDIV      0.090909
#define HIGH_SENSITIVITY  100
#define MED_SENSITIVITY    10   
#define LOW_SENSITIVITY     1     
#define HIGH_SCALE          2
#define SAMPLING_WINDOW   500
#define READ_REPS           8

boolean button_pressed = false, sufficient_battery = true;
volatile unsigned long pulse_count, timer;
int scale_divider = HIGH_SCALE, sensitivity = HIGH_SENSITIVITY, btn_press = 0, btn_press_threshold = 50;
float div_fact = 1.0, system_voltage = 0.0, humidity, temperature;

boolean over_flag = false;

const int num_displays = 4;                                  
const int shift_latch = SHIFT_LATCH_PIN;  // RCLK                           
const int shift_clock = SHIFT_CLOCK_PIN;  // SRCLCK
const int shift_data = SHIFT_DATA_PIN;   // SER
const int dispPorts[num_displays] = {A3,A2,A1,10};
String language = "english";
const byte SevenSegNumbers[COUNT_SEVEN_SEG_NUMBERS] = SEVEN_SEG_NUMBERS;
const byte SevenSegLetters[COUNT_SEVEN_SEG_LETTERS] = SEVEN_SEG_LETTERS;

struct config_t{
  int foo;           
  long machine_id;   
  unsigned long last_calibration_timestamp; 
  float y0, y1, y2, y3, a0, b0, c0, a1, b1, c1, a2, b2, c2;
}
config;

void setup() { 
  analogReference(INTERNAL); 
  delay(50);
  pinMode(TSL_OE, OUTPUT);
  pinMode(TSL_FREQ_PIN, INPUT);
  pinMode(TSL_S0, OUTPUT);  
  pinMode(TSL_S1, OUTPUT);
  pinMode(IR_LED, OUTPUT);
  pinMode(TPIN, INPUT);
  pinMode(BPIN, INPUT);
  digitalWrite(TSL_OE, LOW);
  digitalWrite(TSL_S0, HIGH);
  digitalWrite(TSL_S1, HIGH);
  digitalWrite(IR_LED, LOW);
  pinMode(shift_latch, OUTPUT); // shift register
  pinMode(shift_clock, OUTPUT); // shift register
  pinMode(shift_data,  OUTPUT); // shift register
  for(int i = 0; i < num_displays; i++){ 
    pinMode(dispPorts[i], OUTPUT); 
  }

  if(debug){
    config.foo = 255; //EEPROMAnything seems to need the struct to start with a integer in [0,255]
    config.machine_id = 11111111; //example
    config.last_calibration_timestamp = 1390936721;  //example


  //PLEASE NOTE -- the coefficients below are merely reasonable examples. 
  //Your device WILL require a calibration to determine the proper coefficient values.
    config.y0 = 0;
    config.a0 = 0.00007391;
    config.b0 = 0.07836;
    config.c0 = -46.096;
    config.y1 = 660;
    config.a1 = 0.00000773;
    config.b1 = 0.2315;
    config.c1 = -226.3;
    config.y2 = 2700;
    config.a2 = -0.00000678;
    config.b2 = 0.2315;
    config.c2 = -226.3;
    config.y3 = 6522;
    
    EEPROM_writeAnything(0, config);
  }
  else{ EEPROM_readAnything(0, config); }


  if(debug){
    Serial.begin(9600);
    Serial.println("starting...");
  }
  temperature = readTemperature();
  btn_press_threshold = 1;
  pulse_count = 0;
  timer = millis();
  turnOffDisplay();
  setSensitivity(sensitivity); // set sensor sensitivity
  timer = millis();
  displayForInterval(-1, "dashes", 1000);
  turnOffDisplay();
  
  if(divisionFactor_TSL230R() < 0){
    sufficient_battery = false;
    displayForInterval(-1, "error", 2000);
  }else{
   displayForInterval(-1, "ready", 2000);
   turnOffDisplay();
  }

  Serial.println("entering loop...");
}

void loop() {
  if(sufficient_battery){
    delay(5);
    readTemperature();
    delay(5);
    //Serial.println(readTemperature());
    if(digitalRead(BPIN) != btn_press){
      btn_press = digitalRead(BPIN);
      Serial.print("new button value: ");
      Serial.println(btn_press);
    }
    if (btn_press == btn_press_threshold){  //digital logic (1 = button press)
      button_pressed = true;
      divisionFactor_TSL230R();                
      // read, but discard first reading
      div_fact = divisionFactor_TSL230R(); 
      Serial.print("division factor: "); 
      Serial.println(div_fact);
      if(div_fact < 0){sufficient_battery = false;}     
      else{
        // Given there is enough battery power for the  sensor,
        // - read sensor value for given number of times,
        // - display the resulting value for 4000 milliseconds,
        // - clear out register pins, for a clean display next 
        //   time device is powered off/on.
        // - build a text message and transmit, 
        //   if using_modem flag is set to true.     
        debug_rep_count++;
        //display_voltage();
        //display_rep_count(debug_rep_count);
        displayForInterval(-1, "cycle_dashes", 1000);
        float reading = takeReadings(READ_REPS);
        if(debug){
          Serial.print("output:");
          Serial.println(reading);
        }
        if(over_flag){
          for(int z = 0; z < 6; z++){
            displayForInterval(reading, "data",500);
            displayForInterval(-1, "clear", 300);            
          }
          over_flag = false;
        }
      else{displayForInterval(reading, "data",2000);}
        displayForInterval(-1, "clear", 100);
      }
    }
    if(button_pressed){
      button_pressed = false; // reset
      turnOffDisplay();
    }
  }
  else{
    divisionFactor_TSL230R();
    displayForInterval(-1, "error", 500);
    turnOffDisplay();
    delay(5000);
    display_rep_count(debug_rep_count);
    display_voltage();
  }

}
/*--------------------------Debug Functions---------------------------*/
void display_rep_count(int debug_rep_count){
  unsigned long timer2 = millis();
  while(millis() - timer2 < 500){
    DisplayADigit(dispPorts[0],byte(SevenSegLetters[0]));  //r
    delay(2);
  }
  displayForInterval(debug_rep_count, "data",500);
}

void display_voltage(){
  unsigned long timer2 = millis();
  while(millis() - timer2 < 500){
    DisplayADigit(dispPorts[0],byte(SevenSegLetters[8]));  //u
    delay(2);
  }
  displayForInterval(getVoltageLevel(), "data",500);
}
/*--------------------------Sensor Functions--------------------------*/
void add_pulse() {pulse_count++;}

float takeReadings(int num_rdgs){
  Serial.println("entered readings function");
  int rep_cnt = 0, b = 0;
  long sum = 0, low = 1000000, high = 0, rd = 0;
  PCintPort::attachInterrupt(TSL_FREQ_PIN, add_pulse, RISING);  
  Serial.println("attached interrupt");
  delay(200);
  pulse_count = 0; //reset frequency counter
  timer = millis();
  Serial.println("readings:");
  
  boolean take_dark_counts = true;
  int throwaway_rdng_cnt = 5;
  rep_cnt = 0;
  sum = 0;
  digitalWrite(IR_LED, HIGH);
  delay(10);
  while(rep_cnt < num_rdgs + throwaway_rdng_cnt){
    if(millis() - timer >= SAMPLING_WINDOW){    
      rd = pulse_count * scale_divider;
      if(rep_cnt < throwaway_rdng_cnt){
          Serial.print("throwaway reading: ");
          Serial.println(rd);
      }else{
        if(take_dark_counts){// take dark count reading
          digitalWrite(IR_LED, LOW);
          delay(10);
          timer = millis();     //update timer
          pulse_count = 0;
          while(millis() - timer < SAMPLING_WINDOW){;}
          rd -= pulse_count * scale_divider;
          Serial.println(rd);
          digitalWrite(IR_LED, HIGH);
          delay(10);
        }else{
          Serial.println(rd);
        }
        if(rd > high){high = rd;}                 
        if(rd < low){low = rd;}
        sum += rd;            //sum the readings
      }
      timer = millis();     //update timer
      rep_cnt++;
      pulse_count = 0;
    }  
  }  
  PCintPort::detachInterrupt(TSL_FREQ_PIN);            //turn off frequency-counting function
  digitalWrite(IR_LED, LOW);                           //turn off light source
  //chuck out highest and lowest readings and average the rest, if there are four or more readings
  if(num_rdgs > 3){                  
    sum -= (high + low);
    b = 2;
  }
  float d_f = divisionFactor_TSL230R();
  Serial.print("sensor division factor:");
  Serial.println(d_f);
  Serial.print("light count average: ");
  Serial.println(float(sum) / float(num_rdgs - b));
  Serial.print("factored count: ");
  
  float raw_value = float(sum) / float(num_rdgs - b) / d_f;
  float ntu_value = -1;
  Serial.println(raw_value);
  if(sensitivity == HIGH_SENSITIVITY){
    if(raw_value > config.y3)       {;}//use this to raise an alarm -- for example, if the value is beyond the range the device is calibrated to measure
    if(raw_value > config.y2)       { ntu_value = raw_value * (raw_value * config.a2 + config.b2) + config.c2; }  
    else if(raw_value > config.y1)  { ntu_value = raw_value * (raw_value * config.a1 + config.b1) + config.c1; }
    else                            { ntu_value = raw_value * (raw_value * config.a0 + config.b0) + config.c0; }
    
    if(ntu_value < 0.00){ntu_value = 0.00;}
    if(ntu_value > 9999){ntu_value = 9999;}
    return ntu_value;
  }
  else{return 9999;}
}

void setSensitivity(int sens){    //set sensor sensitivity
  if(sens == LOW_SENSITIVITY){  
    digitalWrite(TSL_S0, LOW);
    digitalWrite(TSL_S1, HIGH);
    sensitivity = LOW_SENSITIVITY;
  }
  else if(sens == MED_SENSITIVITY){
    digitalWrite(TSL_S0, HIGH);
    digitalWrite(TSL_S1, LOW);
    sensitivity = MED_SENSITIVITY;
  }
  else if(sens == HIGH_SENSITIVITY){
    digitalWrite(TSL_S0, HIGH);
    digitalWrite(TSL_S1, HIGH);
    sensitivity = HIGH_SENSITIVITY;
  }
  return;
}

/*--------------------------Internal Metering Functions--------------------------*/
float divisionFactor_TSL230R(){
  float m = .0052;   //slope of sensor's linear response curve
  float vmin = 3.0;  //min operating v of sensor
  float vmax = 5.5;  //max operating v of sensor
  float v100 = 4.9;  //voltage | normalized response of TSL230r = 1.0
  float v = getVoltageLevel();
  Serial.print("v*m: ");
  Serial.println(1000 * (4.9 - v) * m);
  if(v < vmin || v > vmax){ return -1; }
  else{ return 1.0 - (4.9 - v) * m; } 
}

float getVoltageLevel(){
  float vpin = analogRead(VPIN); //drop the first reading
  delay(100);
  vpin = float(analogRead(VPIN));
  system_voltage = vpin/ 1023.0 * 1.1 / VDIV;  
  if(debug){Serial.print("voltage: ");}
  if(debug){Serial.println(system_voltage);}
  return system_voltage;
}

float readTemperature(){ //Assumes using an LMT86: 10.9mV/C responsivity
  analogReference(DEFAULT);
  delay(5);
  float t = analogRead(TPIN);
  analogReference(INTERNAL);
  delay(5);
  float v = getVoltageLevel();
  
  //calculate temperature using a formula recommended in LMT86 datasheet
  t = v * t * 1000 / 1023;  
  t = pow(-10.888, 2) + 4.0 * 0.00347 * (1777.3 - t);
  t = pow(t,0.5);
  t = ( (10.888 - t)/(2.0 * -0.00347) ) + 30;
  
  //simpler alternative formula
  //float t = (2100 - t * getVoltageLevel() * 1000 / 1023) / 10.9;
  
  Serial.print("temperature: ");
  Serial.print(t);
  Serial.println("C");
  //displayForInterval(t,"data",2500);         
  return float(t);
}
/*------------------------------SevSeg Functions-----------------------------*/
void turnOffDisplay(){
  for(int i = 0; i < num_displays; i++){
    if(common_cathode_display){digitalWrite(dispPorts[i], LOW);}
    else{digitalWrite(dispPorts[i], HIGH);}
  }  
}

void DisplayADigit(int dispnum, byte digit2disp){
  digitalWrite(shift_latch, LOW);                     //turn shift register off
  turnOffDisplay();
  shiftOut(shift_data, shift_clock, MSBFIRST, digit2disp);     // perform shift
  digitalWrite(shift_latch, HIGH);                     // turn register back on
  if(common_cathode_display){digitalWrite(dispnum, HIGH);}
  else{digitalWrite(dispnum, LOW);}
  delay(2);                                        // for persistance of vision
}

void displayForInterval(float f, String msg, int ms){
  if(msg == "data"){
    long powers[6] = {10000, 1000, 100, 10, 1};
    if(f > 9999){
      f = 9999.0;    
    }  //bounds checks for display
    if(f < 0){
      f = 0.0;
    }
    int numeric_scale = 1, pt = -1, start = 0;  
    // determine where to put decimal point and leading blank digit, if needed
    if(f > 1000){
      ;
    }
    else if(f > 100){
      numeric_scale = 10;
      pt = 2;
    }
    else if(f > 10){
      numeric_scale = 100;
      pt = 1;
    }
    else if(f > 1){
      numeric_scale = 100;
      pt = 1;
      start = 1;
    }
    else{
      numeric_scale = 100;
      start = 2;
    } 
    long f2l = long(f * numeric_scale);
    unsigned long timer2 = millis();
    while(millis() - timer2 < ms){
      if(start == 1){DisplayADigit(dispPorts[0],SevenSegNumbers[0]);}//, false);}
      else if(start == 2){
        DisplayADigit(dispPorts[0],SevenSegNumbers[12]);//, false);
        if(common_cathode_display){DisplayADigit(dispPorts[1], byte(SevenSegNumbers[0] & SevenSegNumbers[10]));}
        else{DisplayADigit(dispPorts[1],byte(SevenSegNumbers[1] | SevenSegNumbers[10]));}//, false);}
      }
      for(int i = start; i < 4; i++){
        if(i == pt){
          if(common_cathode_display){
            DisplayADigit(dispPorts[i], byte(SevenSegNumbers[(f2l% powers[i]) / powers[i+1]] & SevenSegNumbers[10]));
          }
          else{
            DisplayADigit(dispPorts[i],byte(SevenSegNumbers[(f2l% powers[i]) / powers[i+1]] | SevenSegNumbers[10]));
          }
        }  
        else{
          DisplayADigit(dispPorts[i], SevenSegNumbers[(f2l% powers[i]) / powers[i+1]]);
        }                               
      }
    }
  }
  else if(msg == "dashes"){
    unsigned long timer2 = millis();
    while(millis() - timer2 < ms){
      for(int i = 0; i < num_displays; i++){
        DisplayADigit(dispPorts[i],SevenSegNumbers[11]);
      }
    }
  }
  else if(msg == "cycle_dashes"){
    unsigned long timer2 = millis();
    while(millis() - timer2 < ms){
      for(int i = 0; i < num_displays; i++){
        DisplayADigit(dispPorts[i],SevenSegNumbers[11]);
        delay(100);
        turnOffDisplay();
      }
    }
  }
  else if(msg == "ready"){    
    // display best available approximation of "ready" message, 
    // in Spanish or English, on seven-segment display
    unsigned long timer2 = millis();
    while(millis() - timer2 < ms){
      if(language == "english"){
        DisplayADigit(dispPorts[0],byte(SevenSegLetters[0]));  //r
        DisplayADigit(dispPorts[1],byte(SevenSegLetters[1]));  //E
        DisplayADigit(dispPorts[2],byte(SevenSegLetters[2]));  //d
        DisplayADigit(dispPorts[3],SevenSegLetters[3]);  //Y
      }
      else if(language == "espanol"){
        DisplayADigit(dispPorts[0],SevenSegLetters[4]);  //L
        DisplayADigit(dispPorts[1],SevenSegLetters[5]);  //S
        DisplayADigit(dispPorts[2],SevenSegLetters[6]);  //t
        DisplayADigit(dispPorts[3],SevenSegLetters[7]);  //O
      }
      delay(2);
    }
  }
  else if(msg == "error"){  
    unsigned long timer2 = millis();
    while(millis() - timer2 < ms){
      DisplayADigit(dispPorts[0],SevenSegLetters[1]);  //E
      DisplayADigit(dispPorts[1],SevenSegLetters[0]);  //r
      DisplayADigit(dispPorts[2],SevenSegLetters[0]);  //r
      DisplayADigit(dispPorts[3],SevenSegNumbers[12]);  //
    }
  }
  else if(msg == "clear"){
    unsigned long timer2 = millis();
    while(millis() - timer2 < ms){
      for(int i = 0; i < num_displays; i++){
        DisplayADigit(dispPorts[i],SevenSegNumbers[12]);
      }    
    }
  }
  turnOffDisplay();
}

/*---------------------------END OF PROGRAM---------------------------------*/
