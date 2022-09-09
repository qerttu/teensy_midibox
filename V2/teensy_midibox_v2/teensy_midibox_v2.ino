/*

Next version to-do:
*****

2.0
- new mapping of input fields based on new hardware
- set octave with rotary encoder


1.5
- fixed project display for tempo change when rotating
- midi note mapping from MPC JJOS Free -> JJOS2XL (midi channel 13)

1.4
- project mode: allow transpose editing with rotary edit
- new playmode "L&B" which plays both lead and bass at the same time
- CC support for play modes (110&111 momentary buttons for selecting modes; pressing two both will select the new mode)


1.3
- fixes to scene tempo

1.2
- midi handlers for USBMidi -> 5 pin (no need to connect launchpad with 5pin anymore)


1.1
- project mode: allow tempo editing with rotary encoder
- bottom 4 knobs in main menu screen edits

1.0 (7.5.2022)
- intial import from storage  
- added merging for second midi in/out port

*/

const char version_number[] = "v1.5";

#include <MIDI.h>
#include <ResponsiveAnalogRead.h>
#include <USBHost_t36.h>
#include <SD.h>
#include <SPI.h>
#include <LiquidCrystal.h>
#include <Bounce.h>

#include <Encoder.h>

#define DEBUG
//#define DEBUG2

#define PROJECT 0
#define OCTAVE 1
#define TEMPO_TP 2
#define KEYS 3
#define LOAD 4
#define MIDICC 5
#define SAVE 6
#define CLEAN 7
#define BACK 8
#define numberOfModes 8
#define NUMBER_SCENES 6

#define MAIN 0
#define SUB 1

#define ACTIVE 1
#define DEACTIVE 0

#define BUTTON_ON 0
#define BUTTON_OFF 1

// rotary edit contstants
#define R_TEMPO 1
#define R_TRANSPOSE 2

#define R_B1 1
#define R_B2 2
#define R_L1 3
#define R_L2 4

#define R_MAX = 2;

#define MIDI_START 0xFA
#define MIDI_STOP 0xFC
#define MIDI_TIMING_CLOCK 0xF8

#define MIDI_CC_BASS 110
#define MIDI_CC_LEAD 111

MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, MIDI);
MIDI_CREATE_INSTANCE(HardwareSerial, Serial2, MIDI2);

/******************** MIDI *****************************/

/* CONSTANTS */

/* PINS */

//pins
const int ledPin = 9;
const int sw_pin = 4;
const int bs_pin = 10;

//lcd
const int rs = 37, en = 36, d4 = 41, d5 = 40, d6 = 39, d7 = 38;

//rotary enc
const int dt = 5, clk = 6;

// pots initials
ResponsiveAnalogRead pot1(A9, true);
ResponsiveAnalogRead pot2(A8, true);
ResponsiveAnalogRead pot3(A7, true);
ResponsiveAnalogRead pot4(A6, true);
ResponsiveAnalogRead pot5(A5, true);
ResponsiveAnalogRead pot6(A4, true);

//pots states
int pot1State = DEACTIVE;
int pot2State = DEACTIVE;
int pot3State = DEACTIVE;
int pot4State = DEACTIVE;
int pot5State = DEACTIVE;
int pot6State = DEACTIVE;

// pot bools
bool pot1Ok = false;
bool pot2Ok = false;
bool pot3Ok = false;
bool pot4Ok = false;
bool pot5Ok = false;
bool pot6Ok = false;

const int ledHigh = 255;
const int ledLow = 10;

//midi channels 
const int bass_channel = 2;
const int lead_channel = 1;
const int sample_channel = 11;
const int mpc_channel = 13;

// instrument names and channels
const int meeb = 5;
const int brute = 2;
const int keys = 6;
const int de = 7;

//max min values
const int maxTempo = 254;
const int minTempo = 50;

//max min transpose
const int maxTrans = 4;
const int minTrans = -4;

//midi button CC stuff
bool bassPressed = false;
bool leadPressed = false;
bool bothPressed = false;

typedef struct{
  int transpose;
  int transpose_temp;
  
  byte tempo;
  byte tempo_temp;

  byte scene_tempo[NUMBER_SCENES];
  byte scene;
  
  String name;
  byte id;
  
  int b1_oct;
  int b2_oct;
  int l1_oct;
  int l2_oct;
  
  int b1_channel;
  int b2_channel;
  int l1_channel;
  int l2_channel;
}Project; 


/* OTHER VARIABLES*/


// initalise project 
Project project = {0,0,150,150,{150,150,150,150,150,150},0,"EMPTY MRG",255,0,0,0,0,meeb,brute,keys,de}; 

// project.tempo count
int tempoCount;
boolean emptyProject = true;

// project name characters
char nameChars[37] = "ABCDEFGHIJKLMNOPQRSTUVXYZ0123456789- ";
String tempName = "";
int cursorLocation = 0;
boolean editChar = false;
int charLocation = 0;

//inital playmode
int bassMode=0;
int leadMode=0;

// keyboard offset
int keyOffset=-12;

// notes being played
int notesPlaying = 0;

//midi play stuff
elapsedMicros delay_time;
boolean playing;

// menu variables
byte menuMode = PROJECT; // 0 - main, 1 - octaves, 2 - save/load, 3 - performance (cc)
int menuState = SUB;  

//name variables
char b1[] = "ME";
char b2[] = "MB";
char l1[] = "VK";
char l2[] = "DE  ";


//time for delay
elapsedMillis msec = 0;

long resetTime = 1000;
elapsedMillis pressedTime = 0;
bool rotPressed = false;
bool resetShown = false;


//LCD

LiquidCrystal lcd(rs, en, d4, d5, d6, d7);

//ROTARY ENCODER
Encoder knob(dt, clk);
long positionEnc  = -999;
int encCount = 0;
int stateChange = 0;
static boolean rotating=false;
byte rotEditValue = 0;


Bounce swButton = Bounce(sw_pin, 10);


// BASS SWITCH
Bounce bsButton = Bounce(bs_pin, 10);


int print_count = 0;


/************************* USB AND LAUNCHPAD ***********************************/

// USB host stuff
USBHost myusb;
USBHub hub1(myusb);
USBHub hub2(myusb);
MIDIDevice midi1(myusb);

const int chipSelect = BUILTIN_SDCARD; 
const int NUMBER_OF_TRACKS = 32;
const int NUMBER_OF_PROJECTS = 32;

// Variables That will change:
int count = 0;  // counter for the number of button presses
int currentUpState = 1;         // current state of the up button
int lastUpButtonState = 1;     // previous state of the up button
int write_count = 0;

// BEFORE OFFSET -- track sysex: 32 tracks, 112 (32 steps) each + global settings + 6 mute scenes --> 39 tracks in total
// track sysex: 32 tracks, 144 (32 steps + offset 32) each + global settings + 6 mute scenes --> 39 tracks in total
uint8_t sysex_data[NUMBER_OF_TRACKS+7][144];

// project files sysex
uint8_t sysex_files[10];
uint8_t sysex_fileExist[8];

//project list
Project projectList[NUMBER_OF_PROJECTS];
byte off_set = 0;
int selectedProject=0;
byte filelist_count = 0;

bool isReceiving = false;

File dataFile;
File projectFile;
String buf;



// SETUP -----------------------------------------------------------------------------

void setup()
{

    pinMode(sw_pin, INPUT_PULLUP);
    pinMode(bs_pin, INPUT_PULLUP);
    Serial.begin(57600);

    // SD CARD setups
    #ifdef DEBUG
      Serial.print("Initializing SD card...");  
      // see if the card is present and can be initialized:
    #endif

    if (!SD.begin(chipSelect)) {
    #ifdef DEBUG    
      Serial.println("Card failed, or not present");
      // don't do anything more:
    #endif   
    //  return;
    }
    
    #ifdef DEBUG
      Serial.println("card initialized.");
    #endif


      // set up the LCD's number of columns and rows:
     lcd.begin(16, 2);
      
    //PRINT version while finishing setup
     lcd.print("Teensy_midibox");
     lcd.setCursor(0,1);
     lcd.print(version_number);
     
    // load default dirlist
      updateProjectList();
      
    // MIDI HANDLERS
    midi1.setHandleSystemExclusive(mySystemExclusive); 
    
    MIDI.setHandleNoteOn(handleNoteOn);  // Put only the name of the function
    MIDI2.setHandleNoteOn(handleNoteOn);
    midi1.setHandleNoteOn(handleNoteOn);
    

    // Do the same for NoteOffs
    MIDI.setHandleNoteOff(handleNoteOff);
    MIDI2.setHandleNoteOff(handleNoteOff);
    midi1.setHandleNoteOff(handleNoteOff);

    // handle midi cc
    MIDI.setHandleControlChange(handleCC);
    MIDI2.setHandleControlChange(handleCC);
    midi1.setHandleControlChange(handleCC);

    // handle midi cloc
    MIDI.setHandleClock(myClock);
    MIDI2.setHandleClock(myClock);
    midi1.setHandleClock(myClock);


    // handle midi start
    MIDI.setHandleStart(myStart);
    MIDI2.setHandleStart(myStart);
    midi1.setHandleStart(myStart);


    // handle stop
    MIDI.setHandleStop(myStop);
    MIDI2.setHandleStop(myStop);
    midi1.setHandleStop(myStop);


    // handle continue
    MIDI.setHandleContinue(myContinue);
    MIDI2.setHandleContinue(myContinue);
    midi1.setHandleContinue(myContinue);


    // Initiate MIDI communications, listen to all channels
    myusb.begin();
    MIDI.begin(MIDI_CHANNEL_OMNI);
    MIDI2.begin(MIDI_CHANNEL_OMNI);

    // turn midi thru off
    MIDI.turnThruOff();
    MIDI2.turnThruOff();

    // Initiate led for output
    pinMode(ledPin, OUTPUT);

    //turn on led
    analogWrite(ledPin, ledHigh);


    //set reponse times for pots

/*
    pot1.setActivityThreshold(10);
    pot2.setActivityThreshold(10);
    pot3.setActivityThreshold(10);
    pot4.setActivityThreshold(10);
    pot5.setActivityThreshold(10);
    pot6.setActivityThreshold(10);
*/


// Attach Interrupts
  attachInterrupt(digitalPinToInterrupt(dt), rotEncoder, CHANGE);  // ISR for rotary encoder

  
  // Print a message to the LCD.
  lcdSubMenu(99);

  // delaytime for midi sync
  playing = false;
    
}


/* ------MAIN LOOP ------------------------------------------------- */


void loop()
{
    // Call serial midi
    if(MIDI.read()){ 
    }  

    // Call serial midi2
    if(MIDI2.read()){ 
    }  

    // Call USB midi
    if (midi1.read()) {      
    }

    //update encoder
    updateEnc();

    // update encodebutton
    updateEncButton();

   //update other buttons and switches
   updateButtons();
    
    //update pots
   // updatePots();

  // updateScreen    
    updateLcdScreen();

  // reset all input states to DEACTIVE
 // resetPotStates();

  // send midi clock to USB
    if (playing) {
    
       if (delay_time >= (60000000 / project.scene_tempo[project.scene]) / 24) {
                
        delay_time = 0;
        midi1.sendRealTime(midi1.Clock);
        
       }
  }

    // if resetbutton is pressed long enough, display someting on the scren
  if ((pressedTime >= resetTime) && rotPressed && !resetShown) {
      lcdResetMessage();
      resetShown = true;
    }
    

 }


// MIDI CALLUPS-----------------------------------------------------------------------------

void myStart() {
  tempoCount = 0;

  //send start to 5 pin midi ports
  MIDI.sendRealTime(MIDI_START);
  MIDI2.sendRealTime(MIDI_START);
  
  #ifdef DEBUG
    Serial.println("START"); 
  #endif
}

void myStop() {
  analogWrite(ledPin, ledLow); 

 // send stop to 5 pin midi ports
 MIDI.sendRealTime(MIDI_STOP);
 MIDI2.sendRealTime(MIDI_STOP);
   
 #ifdef DEBUG
  Serial.println("STOP"); 
 #endif
}

void myContinue() {
  tempoCount = 0; 
   
 #ifdef DEBUG
   Serial.println("CONINUE"); 
 #endif
}

void myClock() {

  // send midi clock to 5 pin midi ports
    MIDI.sendRealTime(MIDI_TIMING_CLOCK);
   MIDI2.sendRealTime(MIDI_TIMING_CLOCK);
  
  // LED stuff
  if ((tempoCount >= 0) && (tempoCount < 12)) {
    analogWrite(ledPin,ledLow);
  }
  if ((tempoCount > 12) && (tempoCount < 23)) {
    analogWrite(ledPin,ledHigh);
    } 
  
  tempoCount++; 
  
  if(tempoCount > 23) {
    tempoCount = 0;
  }


  
}

void handleNoteOn(byte channel, byte note, byte velocity)
{

  //set led
  analogWrite(ledPin, ledLow);

  byte temp_note;
  
  //INDIDIDUAL CHANNELS
  if (channel==project.b1_channel || channel==project.b2_channel || channel==project.l1_channel || channel==project.l2_channel) {
 
    if (channel==project.b1_channel) {
      temp_note = note + project.b1_oct*12;
    }
    if (channel==project.b2_channel) {
      temp_note = note + project.b2_oct*12;
    }
    if (channel==project.l1_channel) {
      temp_note = note + project.l1_oct*12;
    }
    if (channel==project.l2_channel) {
      temp_note = note + project.l2_oct*12;
    }
    
   MIDI.sendNoteOn(temp_note + project.transpose, velocity, channel);    
   MIDI2.sendNoteOn(temp_note + project.transpose, velocity, channel);   
   notesPlaying=notesPlaying+1; 
   
    #ifdef DEBUG
        Serial.print("Note send: ");
        Serial.print(temp_note + project.transpose);
        Serial.print(" Channel: ");
        Serial.print(channel);
        Serial.println();
    #endif          
 
  }
  
      
 
  //BASSCHANNEL (Not in use anymore since everything comes with lead channel
  /*
  if (channel==bass_channel)
  {
    // send to both  
    if (bassMode==0)
     {    
      MIDI.sendNoteOn(note + (project.b1_oct*12) + project.transpose, velocity, project.b1_channel);    
      MIDI.sendNoteOn(note + (project.b2_oct*12) + project.transpose, velocity, project.b2_channel);
      MIDI2.sendNoteOn(note + (project.b1_oct*12) + project.transpose, velocity, project.b1_channel);    
      MIDI2.sendNoteOn(note + (project.b2_oct*12) + project.transpose, velocity, project.b2_channel);
      notesPlaying=notesPlaying+2; 

      #ifdef DEBUG
        Serial.print("Note send: ");
        Serial.print(note + (project.b1_oct*12) + project.transpose);
        Serial.print(" Channel: ");
        Serial.print(project.b1_channel);
        Serial.println();
              
     
        Serial.print("Note send: ");
        Serial.print(note + (project.b2_oct*12) + project.transpose);
        Serial.print(" Channel: ");
        Serial.print(project.b2_channel);
        Serial.println();
      #endif
     }
     
    // only left
    if (bassMode==-1)
     { 
      MIDI.sendNoteOn(note + (project.b1_oct*12) + project.transpose, velocity, project.b1_channel);
      MIDI2.sendNoteOn(note + (project.b1_oct*12) + project.transpose, velocity, project.b1_channel);
      notesPlaying=notesPlaying+1; 

       #ifdef DEBUG     
        Serial.print("Note send: ");
        Serial.print(note + (project.b1_oct*12) + project.transpose);
        Serial.print(" Channel: ");
        Serial.print(project.b1_channel);
        Serial.println();
      #endif
     }

    // only right
    if (bassMode==1)
     { 
      
      MIDI.sendNoteOn(note + (project.b2_oct*12) + project.transpose, velocity, project.b2_channel);
      MIDI2.sendNoteOn(note + (project.b2_oct*12) + project.transpose, velocity, project.b2_channel);
      notesPlaying=notesPlaying+1; 
  
      #ifdef DEBUG
        Serial.print("Note send: ");
        Serial.print(note + (project.b2_oct*12) + project.transpose);
        Serial.print(" Channel: ");
        Serial.print(project.b2_channel);
        Serial.println();
      #endif
       
     }
  }
  */

  // if channel is lead AND every first two octaves

  if ((channel==lead_channel && ((note>=0 && note <(24+keyOffset)) || (note>=(48+keyOffset) && note <(72+keyOffset)) || (note >=(96+keyOffset) && note<(120+keyOffset))))) {
      
      if ((bassMode==0) || (bassMode==2)) {
        // play only to L1 channel
        MIDI.sendNoteOn(note + (project.l1_oct*12) + project.transpose, velocity, project.l1_channel);
        MIDI2.sendNoteOn(note + (project.l1_oct*12) + project.transpose, velocity, project.l1_channel);
        notesPlaying=notesPlaying+1; 
       
       #ifdef DEBUG
        Serial.print("Note ON: ");
        Serial.print(note + (project.l1_oct*12) + project.transpose);
        Serial.print("Channel: ");
        Serial.print(project.l1_channel);
        Serial.println();   
      #endif 
      }
      
      if ((bassMode==1) || (bassMode==2)) {
       // play to both bass channels
       MIDI.sendNoteOn(note + (project.b1_oct*12) + project.transpose, velocity, project.b1_channel);
       MIDI.sendNoteOn(note + (project.b2_oct*12) + project.transpose, velocity, project.b2_channel);

       MIDI2.sendNoteOn(note + (project.b1_oct*12) + project.transpose, velocity, project.b1_channel);
       MIDI2.sendNoteOn(note + (project.b2_oct*12) + project.transpose, velocity, project.b2_channel);

       notesPlaying=notesPlaying+2; 
      
      #ifdef DEBUG
        Serial.print("Note ON: ");
        Serial.print(note + (project.b1_oct*12) + project.transpose);
        Serial.print("Channel: ");
        Serial.print(project.b1_channel);
        Serial.println();   
      #endif 
      }

      
    }

  // if channel is lead AND every last two octaves

  if ((channel==lead_channel && ((note>=(24+keyOffset) && note <(48+keyOffset)) || (note>=(72+keyOffset) && note <(96+keyOffset)) || (note >=(120+keyOffset) && note<(128+keyOffset))))) {
      MIDI.sendNoteOn(note + (project.l2_oct*12) + project.transpose, velocity, project.l2_channel);            
      MIDI2.sendNoteOn(note + (project.l2_oct*12) + project.transpose, velocity, project.l2_channel); 
      notesPlaying=notesPlaying+1; 
      
     #ifdef DEBUG         
      Serial.print("Note ON: ");
      Serial.print(note + (project.l2_oct*12) + project.transpose);
      Serial.print(" Channel: ");
      Serial.print(project.l2_channel);
      Serial.println();     
    #endif
    }



  /*
  // if incoming is lead note
  if ((channel==project.l1_channel) || (channel==project.l2_channel))
  {
    // apply transpose and / or octave to the relevant output

    if (channel==project.l1_channel) {
      MIDI.sendNoteOn(note + (project.l1_oct*12) + project.transpose, velocity, channel);
      MIDI2.sendNoteOn(note + (project.l1_oct*12) + project.transpose, velocity, channel);

    #ifdef DEBUG
      Serial.print("Note send: ");
      Serial.print(note + (project.l1_oct*12) + project.transpose);
      Serial.print("Channel: ");
      Serial.print(project.l1_channel);
      Serial.println();   
    #endif
    }

    if (channel==project.l2_channel) {
      MIDI.sendNoteOn(note + (project.l2_oct*12) + project.transpose, velocity, channel);            
      MIDI2.sendNoteOn(note + (project.l2_oct*12) + project.transpose, velocity, channel); 

     #ifdef DEBUG         
      Serial.print("Note send: ");
      Serial.print(note + (project.l2_oct*12) + project.transpose);
      Serial.print(" Channel: ");
      Serial.print(project.l2_channel);
      Serial.println();     
    #endif
    }

    notesPlaying=notesPlaying+1; 
   
  }
  */

  // if incoming is for sample, do mapping
  if ((channel==sample_channel) || (channel==mpc_channel))
  {
    // do mapping ...
      byte mappedNote;
      
      switch (note) {       
       case 37:
       mappedNote = 36;
        break;
      case 36:
        mappedNote = 37;
        break;
      case 42:
        mappedNote = 38;
        break;
      case 82:
        mappedNote = 39;
        break;
       case 40:
        mappedNote = 40;
        break;
      case 38:
        mappedNote = 41;
        break;
      case 46:
        mappedNote = 42;
        break;
      case 44:
        mappedNote = 43;
        break;
      case 48:
        mappedNote = 44;
        break;
      case 47:
        mappedNote = 45;
        break;           
      case 45:
        mappedNote = 46;
        break;           
      case 43:
        mappedNote = 47;
        break;           
      case 49:
        mappedNote = 48;
        break;           
      case 55:
        mappedNote = 49;
        break;           
      case 51:
        mappedNote = 50;
        break;           
      case 53:
        mappedNote = 51;
        break;           

       case 54:
       mappedNote = 52;
        break;
      case 69:
        mappedNote = 53;
        break;
      case 81:
        mappedNote = 54;
        break;
      case 80:
        mappedNote = 55;
        break;
       case 65:
        mappedNote = 56;
        break;
      case 66:
        mappedNote = 57;
        break;
      case 76:
        mappedNote = 58;
        break;
      case 77:
        mappedNote = 59;
        break;
      case 56:
        mappedNote = 60;
        break;
      case 62:
        mappedNote = 61;
        break;           
      case 63:
        mappedNote = 62;
        break;           
      case 64:
        mappedNote = 63;
        break;           
      case 73:
        mappedNote = 64;
        break;           
      case 74:
        mappedNote = 65;
        break;           
      case 71:
        mappedNote = 66;
        break;           
      case 39:
        mappedNote = 67;
        break;           
//      default:
//        mappedNote = 37;
//        break;
      }

      MIDI.sendNoteOn(mappedNote, velocity, channel);
      MIDI2.sendNoteOn(mappedNote, velocity, channel);

    #ifdef DEBUG  
      Serial.print("Note receive: ");
      Serial.print(note);
      Serial.print(" Note send: ");
      Serial.print(mappedNote);
      Serial.print(" Channel: ");
      Serial.print(channel);
      Serial.println();  
   #endif
    }

    
  // if incoming is none of above, just pass through without octave, project.transpose etc
  if ((channel!=bass_channel) && (channel!=lead_channel) && (channel!=project.l1_channel) && (channel!=project.l2_channel) && (channel!=project.b1_channel) && (channel!=project.b2_channel) &&(channel!=sample_channel) &&(channel!=mpc_channel))
  {

    MIDI.sendNoteOn(note, velocity, channel);
    MIDI2.sendNoteOn(note, velocity, channel);

    #ifdef DEBUG
      Serial.print("Note send: ");
      Serial.print(note);
      Serial.print(" Channel: ");
      Serial.print(channel);
      Serial.println();
    #endif     
   }

  #ifdef DEBUG
   //print modes and octaves and notes laying
   Serial.print("Bass mode: ");
   Serial.print(bassMode);
   Serial.print(" Lead mode: ");
   Serial.print(leadMode);
   Serial.print(" Notesplaying : ");
   Serial.print(notesPlaying);
   Serial.println();
  #endif
     
}

void handleNoteOff(byte channel, byte note, byte velocity)
{

  //set led
  analogWrite(ledPin, ledLow);

  byte temp_note;
  
  //INDIDIDUAL CHANNELS
  if (channel==project.b1_channel || channel==project.b2_channel || channel==project.l1_channel || channel==project.l2_channel) {
 
 
    if (channel==project.b1_channel) {
      temp_note = note + project.b1_oct*12;
    }
    if (channel==project.b2_channel) {
      temp_note = note + project.b2_oct*12;
    }
    if (channel==project.l1_channel) {
      temp_note = note + project.l1_oct*12;
    }
    if (channel==project.l2_channel) {
      temp_note = note + project.l2_oct*12;
    }
 
     MIDI.sendNoteOff(temp_note + project.transpose, velocity, channel);    
     MIDI2.sendNoteOff(temp_note + project.transpose, velocity, channel);   
     notesPlaying=notesPlaying-1; 
   
    #ifdef DEBUG
        Serial.print("Note send: ");
        Serial.print(temp_note + project.transpose);
        Serial.print(" Channel: ");
        Serial.print(channel);
        Serial.println();
    #endif          
 
  }
  
      
 
  //BASSCHANNEL
  /*
  if (channel==bass_channel)
  {
    // send to both  
    if (bassMode==0)
     {    
      MIDI.sendNoteOff(note + (project.b1_oct*12) + project.transpose, velocity, project.b1_channel);    
      MIDI.sendNoteOff(note + (project.b2_oct*12) + project.transpose, velocity, project.b2_channel);
      MIDI2.sendNoteOff(note + (project.b1_oct*12) + project.transpose, velocity, project.b1_channel);    
      MIDI2.sendNoteOff(note + (project.b2_oct*12) + project.transpose, velocity, project.b2_channel);
      notesPlaying=notesPlaying-2; 

      #ifdef DEBUG
        Serial.print("Note send: ");
        Serial.print(note + (project.b1_oct*12) + project.transpose);
        Serial.print(" Channel: ");
        Serial.print(project.b1_channel);
        Serial.println();
              
     
        Serial.print("Note send: ");
        Serial.print(note + (project.b2_oct*12) + project.transpose);
        Serial.print(" Channel: ");
        Serial.print(project.b2_channel);
        Serial.println();
      #endif
     }
     
    // only left
    if (bassMode==-1)
     { 
      MIDI.sendNoteOff(note + (project.b1_oct*12) + project.transpose, velocity, project.b1_channel);
      MIDI2.sendNoteOff(note + (project.b1_oct*12) + project.transpose, velocity, project.b1_channel);
      notesPlaying=notesPlaying-1; 

       #ifdef DEBUG     
        Serial.print("Note send: ");
        Serial.print(note + (project.b1_oct*12) + project.transpose);
        Serial.print(" Channel: ");
        Serial.print(project.b1_channel);
        Serial.println();
      #endif
     }

    // only right
    if (bassMode==1)
     { 
      
      MIDI.sendNoteOff(note + (project.b2_oct*12) + project.transpose, velocity, project.b2_channel);
      MIDI2.sendNoteOff(note + (project.b2_oct*12) + project.transpose, velocity, project.b2_channel);
      notesPlaying=notesPlaying-1; 
  
      #ifdef DEBUG
        Serial.print("Note send: ");
        Serial.print(note + (project.b2_oct*12) + project.transpose);
        Serial.print(" Channel: ");
        Serial.print(project.b2_channel);
        Serial.println();
      #endif
       
     }
  }
  */

  // if channel is lead AND every first two octaves

  if ((channel==lead_channel && ((note>=0 && note <(24+keyOffset)) || (note>=(48+keyOffset) && note <(72+keyOffset)) || (note >=(96+keyOffset) && note<(120+keyOffset))))) {
      
      if ((bassMode==0) || (bassMode==2)) {
        // play only to L1 channel
        MIDI.sendNoteOff(note + (project.l1_oct*12) + project.transpose, velocity, project.l1_channel);
        MIDI2.sendNoteOff(note + (project.l1_oct*12) + project.transpose, velocity, project.l1_channel);
        notesPlaying=notesPlaying-1; 
       
       #ifdef DEBUG
        Serial.print("Note ON: ");
        Serial.print(note + (project.l1_oct*12) + project.transpose);
        Serial.print("Channel: ");
        Serial.print(project.l1_channel);
        Serial.println();   
      #endif 
      }
      
      if ((bassMode==1) || (bassMode==2)) {
       // play to bass1 channel
       MIDI.sendNoteOff(note + (project.b1_oct*12) + project.transpose, velocity, project.b1_channel);
       MIDI.sendNoteOff(note + (project.b2_oct*12) + project.transpose, velocity, project.b2_channel);

       MIDI2.sendNoteOff(note + (project.b1_oct*12) + project.transpose, velocity, project.b1_channel);
       MIDI2.sendNoteOff(note + (project.b2_oct*12) + project.transpose, velocity, project.b2_channel);

       notesPlaying=notesPlaying-2; 
      
      #ifdef DEBUG
        Serial.print("Note ON: ");
        Serial.print(note + (project.b1_oct*12) + project.transpose);
        Serial.print("Channel: ");
        Serial.print(project.b1_channel);
        Serial.println();   
      #endif 
      }
      

      
    }

  // if channel is lead AND every last two octaves

  if ((channel==lead_channel && ((note>=(24+keyOffset) && note <(48+keyOffset)) || (note>=(72+keyOffset) && note <(96+keyOffset)) || (note >=(120+keyOffset) && note<(128+keyOffset))))) {
      MIDI.sendNoteOff(note + (project.l2_oct*12) + project.transpose, velocity, project.l2_channel);            
      MIDI2.sendNoteOff(note + (project.l2_oct*12) + project.transpose, velocity, project.l2_channel); 
      notesPlaying=notesPlaying-1; 
      
     #ifdef DEBUG         
      Serial.print("Note ON: ");
      Serial.print(note + (project.l2_oct*12) + project.transpose);
      Serial.print(" Channel: ");
      Serial.print(project.l2_channel);
      Serial.println();     
    #endif
    }



  /*
  // if incoming is lead note
  if ((channel==project.l1_channel) || (channel==project.l2_channel))
  {
    // apply transpose and / or octave to the relevant output

    if (channel==project.l1_channel) {
      MIDI.sendNoteOn(note + (project.l1_oct*12) + project.transpose, velocity, channel);
      MIDI2.sendNoteOn(note + (project.l1_oct*12) + project.transpose, velocity, channel);

    #ifdef DEBUG
      Serial.print("Note send: ");
      Serial.print(note + (project.l1_oct*12) + project.transpose);
      Serial.print("Channel: ");
      Serial.print(project.l1_channel);
      Serial.println();   
    #endif
    }

    if (channel==project.l2_channel) {
      MIDI.sendNoteOn(note + (project.l2_oct*12) + project.transpose, velocity, channel);            
      MIDI2.sendNoteOn(note + (project.l2_oct*12) + project.transpose, velocity, channel); 

     #ifdef DEBUG         
      Serial.print("Note send: ");
      Serial.print(note + (project.l2_oct*12) + project.transpose);
      Serial.print(" Channel: ");
      Serial.print(project.l2_channel);
      Serial.println();     
    #endif
    }

    notesPlaying=notesPlaying+1; 
   
  }
  */

  // if incoming is for sample, do mapping
  if ((channel==sample_channel) || (channel==mpc_channel))
  {
    // do mapping ...
      byte mappedNote;
      
      switch (note) {       
       case 37:
       mappedNote = 36;
        break;
      case 36:
        mappedNote = 37;
        break;
      case 42:
        mappedNote = 38;
        break;
      case 82:
        mappedNote = 39;
        break;
       case 40:
        mappedNote = 40;
        break;
      case 38:
        mappedNote = 41;
        break;
      case 46:
        mappedNote = 42;
        break;
      case 44:
        mappedNote = 43;
        break;
      case 48:
        mappedNote = 44;
        break;
      case 47:
        mappedNote = 45;
        break;           
      case 45:
        mappedNote = 46;
        break;           
      case 43:
        mappedNote = 47;
        break;           
      case 49:
        mappedNote = 48;
        break;           
      case 55:
        mappedNote = 49;
        break;           
      case 51:
        mappedNote = 50;
        break;           
      case 53:
        mappedNote = 51;
        break;           

      case 54:
       mappedNote = 52;
        break;
      case 69:
        mappedNote = 53;
        break;
      case 81:
        mappedNote = 54;
        break;
      case 80:
        mappedNote = 55;
        break;
       case 65:
        mappedNote = 56;
        break;
      case 66:
        mappedNote = 57;
        break;
      case 76:
        mappedNote = 58;
        break;
      case 77:
        mappedNote = 59;
        break;
      case 56:
        mappedNote = 60;
        break;
      case 62:
        mappedNote = 61;
        break;           
      case 63:
        mappedNote = 62;
        break;           
      case 64:
        mappedNote = 63;
        break;           
      case 73:
        mappedNote = 64;
        break;           
      case 74:
        mappedNote = 65;
        break;           
      case 71:
        mappedNote = 66;
        break;           
      case 39:
        mappedNote = 67;
        break;           
//      default:
//        mappedNote = 37;
//        break;
      }

      MIDI.sendNoteOff(mappedNote, velocity, channel);
      MIDI2.sendNoteOff(mappedNote, velocity, channel);

    #ifdef DEBUG  
      Serial.print("Note receive: ");
      Serial.print(note);
      Serial.print(" Note send: ");
      Serial.print(mappedNote);
      Serial.print(" Channel: ");
      Serial.print(channel);
      Serial.println();  
   #endif
    }

    
  // if incoming is none of above, just pass through without octave, project.transpose etc
  if ((channel!=bass_channel) && (channel!=lead_channel) && (channel!=project.l1_channel) && (channel!=project.l2_channel) && (channel!=project.b1_channel) && (channel!=project.b2_channel) &&(channel!=sample_channel) &&(channel!=mpc_channel))
  {

    MIDI.sendNoteOff(note, velocity, channel);
    MIDI2.sendNoteOff(note, velocity, channel);

    #ifdef DEBUG
      Serial.print("Note send: ");
      Serial.print(note);
      Serial.print(" Channel: ");
      Serial.print(channel);
      Serial.println();
    #endif     
   }

  #ifdef DEBUG
   //print modes and octaves and notes laying
   Serial.print("Bass mode: ");
   Serial.print(bassMode);
   Serial.print(" Lead mode: ");
   Serial.print(leadMode);
   Serial.print(" Notesplaying : ");
   Serial.print(notesPlaying);
   Serial.println();
  #endif
     
}

void handleCC(byte channel, byte control, byte value) {

  #ifdef DEBUG2
    Serial.println("Incoming MIDI CC:");
    Serial.print("Channel:");
    Serial.print(channel);
    Serial.print("Control:");
    Serial.print(control);
    Serial.print("Value:");
    Serial.print(value);
    
  #endif

  // channel is lead1
  if (channel==lead_channel && (notesPlaying==0)) {


  if (control==MIDI_CC_BASS && value==127) {
    bassPressed=true;
    }
  if (control==MIDI_CC_LEAD && value==127) {
    leadPressed=true;
    }

  if (bassPressed && leadPressed) {
    bothPressed=true;
    }
  
  if (!(bothPressed)) {  
    switch (control) {
        case MIDI_CC_BASS:
          if (value==0){
            bassPressed=false; 
            bassMode=1;
            lcdSubMenu(PROJECT);
          }
        break;

        case MIDI_CC_LEAD:
          if (value==0){
            leadPressed=false; 
            bassMode=0;
            lcdSubMenu(PROJECT);
          }
        
        break; 
      }
    }
    
    if ((bothPressed)) {
      
      if (control==MIDI_CC_BASS) {
        if (value==0) {
            bassPressed=false; 
          }
        }
      
      if (control==MIDI_CC_LEAD) {
        if (value==0) {
          leadPressed=false;
          }
        }

      if (!bassPressed && !leadPressed) {
        bothPressed=false;
        bassMode=2;
        lcdSubMenu(PROJECT);
        }
    
    }
   }   
  }


// POT FUNCTIONS -----------------------------------------------------

// potValue: 0-127
// returns: -3, -2, -1, 0, 1, 2, 3
int getThreePosition(int potValue) {
       
  int octave = 0;
        
    if (potValue >= 0 && potValue <= 18)
      {
        octave=-3;            
      }

     if (potValue > 18 && potValue <= 36) 
      {
      octave=-2;  
      }
     
     if (potValue > 36 && potValue <= 54) 
      {
      octave=-1;  
      }
          
     if (potValue > 72 && potValue <= 90) 
      {
      octave=1;  
      }
     
     if (potValue > 90 && potValue <= 108) 
      {
      octave=2;  
      }
     if (potValue > 108 && potValue <= 127) 
      {
      octave=3;  
      }

      //Serial.println(potValue);
      return octave;      
  }

// potValue: 0-127
// returs: -1, 0, 1
  
int getOnePosition(int potValue)
{
  int mode = 0;
  
  if (potValue >= 0 && potValue <= 42) {
    mode=-1;            
   }
    
  if (potValue > 84 && potValue <= 127) {
    mode=1;  
   }
           
  return mode;
  }


//*************************** SD CARD FUNCTIONS ************************

void sdCardToArray(byte cp) {

    // cleaning array
   #ifdef DEBUG 
    Serial.print("Cleaning array..");
   #endif 
  
    cleanData();
    
    #ifdef DEBUG    
      Serial.println("Done.");
    #endif

    #ifdef DEBUG    
      Serial.print("** CURRENT PROJECT: ");
      Serial.print(cp);
    #endif
  
    char filename[6];
    sprintf (filename, "%02d.txt",cp);
    dataFile = SD.open(filename);


    
    if (!dataFile) {
    #ifdef DEBUG
        Serial.print("The text file cannot be opened or does not exist");
    #endif    
        while(1);
      }
  
    int r = 0;
    while (dataFile.available()) {
        buf = dataFile.readStringUntil('\n',512);
     #ifdef DEBUG    
        Serial.print("Adding to row ");
        Serial.print(r);
        Serial.print(" : ");
        Serial.print(buf);
        Serial.println();
     #endif
        stringToArray(buf,r); 
        r++;
        buf="";
        delay(100);
      }
    dataFile.close();
    #ifdef DEBUG
      Serial.println("Done adding.");
    #endif
 }

void writeTracksDataSdCard(byte cp) {

    /**WRITE TRACK DATA**/
  if (cp<255) {  
    // remove the file first
    char filename[6];
    sprintf (filename, "%02d.txt",cp);
    SD.remove(filename);
      
    dataFile = SD.open(filename,FILE_WRITE);

    if (!dataFile) {
    #ifdef DEBUG
      Serial.print("The text file cannot be opened");
    #endif
      while(1);
    }
   #ifdef DEBUG      
      Serial.print("Starting to write TRACKS to SD card on file: ");
      Serial.println(filename);
   #endif 
    if(dataFile) {
        byte tracksCount = sizeof (sysex_data) / sizeof (sysex_data[0]);
 
      #ifdef DEBUG          
        Serial.print("Tracks:");
        Serial.print(tracksCount);
      #endif
      
        for (byte i = 0; i < tracksCount; i++) {
         if (sysex_data[i][0] > 0) {     
            buf = printBytesSting(sysex_data[i],sizeof(sysex_data[i]));
          #ifdef DEBUG
            Serial.print("Writing line ");
            Serial.print(i);
            Serial.print(" : ");
            Serial.println(buf);
          #endif
            dataFile.println(buf);
            buf="";
           delay(50);
          }   
        } 
      }
  dataFile.close();
  }
}

void writeProjectSdCard(byte cp) {
  
  if (cp<255) {
  /**WRITE PROJECT FILE*******/
    // remove the file first
    char filename2[7];
    sprintf (filename2, "_%02d.txt",cp);
    SD.remove(filename2);
      
    dataFile = SD.open(filename2,FILE_WRITE);

    if (!dataFile) {
    #ifdef DEBUG
      Serial.print("The text file cannot be opened");
    #endif
      while(1);
    }

    #ifdef DEBUG2      
      Serial.print("Starting to write PROJECT to SD card on file: ");
      Serial.println(filename2);
   #endif 
    if (dataFile){

       char buffer[40];
        
       //name
       String pName = "name=";
       pName = pName + project.name.trim();
       dataFile.println(pName);
       #ifdef DEBUG2
        Serial.println(pName);
       #endif
       
      //transpose
      sprintf  (buffer, "transpose=%d",project.transpose);
      dataFile.println(buffer);
       #ifdef DEBUG2
        Serial.println(buffer);
       #endif

      //tempo
      sprintf (buffer, "tempo=%d",project.tempo);
      dataFile.println(buffer);
      #ifdef DEBUG2
        Serial.println(buffer);
      #endif

      //scene tempo
      for(byte i=0;i<sizeof(project.scene_tempo);i++){
        if(project.scene_tempo[i]>0) {
          sprintf (buffer, "tempo_sc%d=%d",i,project.scene_tempo[i]);
          dataFile.println(buffer);
          #ifdef DEBUG2
            Serial.println(buffer);
          #endif
        }        
      }

      //id
      sprintf (buffer, "id=%d",project.id);
      dataFile.println(buffer);
      #ifdef DEBUG2
        Serial.println(buffer);
      #endif
      
      }
    dataFile.close();
   }
  }

void mySystemExclusive(byte *data, unsigned int length) {
  #ifdef DEBUG 
    Serial.print("Receiving sysex:");
    Serial.print(" (length: ");
    Serial.print(length);
    Serial.print(" )");
    Serial.println();
  #endif
  
    printBytes(data, length);


// ****************** SAVE DATA *********************
   // if start message received
   if (startReceived(data)) {
    isReceiving = true;

    #ifdef DEBUG   
      Serial.print("Start received for project: ");
      Serial.print(project.id);
      Serial.println();
      Serial.print("Cleaning array..");
    #endif  
      
      cleanData();
      
    #ifdef DEBUG     
      Serial.println("Done");
    #endif 
   }

   // if receveiving data, store to array
   if (isReceiving) {
        storeSysex(data,length);
    }

   // if end message received, write to SDcard
   if (endReceived(data)) {
    #ifdef DEBUG 
       Serial.println("End received, writing to SD card..");
    #endif    
       writeTracksDataSdCard(project.id);
       writeProjectSdCard(project.id);
      write_count=write_count+1;
     
    #ifdef DEBUG     
        Serial.print("Done writing: ");
        Serial.println(write_count);
      Serial.println("Updating projects..");
     #endif     
     
     sendProjectExist();
     updateProjectList();
     isReceiving = false;
   }

// *********** DELETE PROJECT *************************
  byte deleteProjectId = projectDeleteRequested(data);
  if ((deleteProjectId<255)&&(deleteProjectId!=project.id)) {
    deleteProjectSDCard(deleteProjectId);
    sendProjectExist();
    updateProjectList();
    }   


// *********** LOAD PROJECT ****************************
   //if loadProject request received, sent the project 
   byte loadProjectId = projectRequestReceived(data);
   
   if (loadProjectId <255) {

    project.id = loadProjectId;
    emptyProject = false;


   #ifdef DEBUG     
      Serial.print("Loading project: ");
      Serial.println(project.id);
   #endif    
    
    loadProjectSDCard(project.id);

    // load project details
   #ifdef DEBUG     
      Serial.print("Sending out project: ");
      Serial.println(project.id);
   #endif
    
    // send out project sysex
    sendOutProject(project.id);
    
    // load project menu
    menuMode=PROJECT;
    lcdSubMenu(99);  
      
    #ifdef DEBUG     
      Serial.println("Project loading done");
   #endif
   }

// ************** MIDI START / STOP REQUEST ************************

   // start/stop midiclock sysex request
   if (midiStartRequested(data) || midiStopRequested(data)) {
    // send out start message
    toggleMidiStart();
    }


// ************* SCENE TEMPO REQUEST *******************************
    // session tempo request
    byte scene_temp = sceneRequested(data);
    if (scene_temp <255) {

      #ifdef DEBUG
        Serial.print("Scene received:");
        Serial.println(scene_temp);
      #endif
      
      project.scene = scene_temp;
      setTempo(scene_temp);

      // set rotary edit to false      
      rotEditValue = 0;

      // refresh screen
      if (menuMode==PROJECT) {
        lcdSubMenu(99);
      }
      else {
      lcdMainMenu(99);  
      }
    }

// ************** VIEW PROJECT NAME ********************************

  byte viewProjectId = viewProjectRequested(data);
  if (viewProjectId<255) {

      
      #ifdef DEBUG2
        Serial.print("View project requested:");
        Serial.println(viewProjectId);
      #endif 

    lcdViewProject(viewProjectId);
    sendProjectExist();
  
    }
    
}


byte projectDeleteRequested(byte *data) {
  byte ret = 255;
  byte tr = data[4];
  byte proj = data[5];
  byte message = data[6];

    if ((tr==246)&&(message==9)&&(proj >=0)) {
    ret = proj;
    }
  return ret;  
  }

byte projectRequestReceived(byte *data) {
  byte ret = 255;
  byte tr = data[4];
  byte proj = data[5];
  byte message = data[6];

    if ((tr==246)&&(message==3)&&(proj >=0)) {
    ret = proj;
    }
  return ret;
  }

bool midiStartRequested(byte *data) {
    bool ret = false;
    byte tr = data[4];
    byte message = data[6];
    if ((tr==246)&&(message==5)) {
      ret = true;
    }
    return ret;
  }

bool midiStopRequested(byte *data) {
    bool ret = false;
    byte tr = data[4];
    byte message = data[6];
    if ((tr==246)&&(message==6)) {
      ret = true;
    }
    return ret;
  }

byte sceneRequested(byte *data) {
  byte ret = 255;
  byte tr = data[4];
  byte sc = data[5];
  byte message = data[6];

    if ((tr==246)&&(message==7)&&(sc >=0)) { 
      ret = sc;
    }
  return ret;
}

byte viewProjectRequested(byte *data) {
  byte ret = 255;
  byte tr = data[4];
  byte proj = data[5];
  byte message = data[6];

    if ((tr==246)&&(message==8)&&(proj >=0)) {
    ret = proj;
    }
  return ret; 
}


bool startReceived(byte *data) {
  bool ret = false;
  byte tr = data[4];
  byte proj = data[5];
  byte message = data[6];

  if ((tr==246)&&(message==1) &&(proj >=0)) {
    project.id = proj;
    ret = true;
  }
  return ret;
 }

bool endReceived(byte *data) {
  bool ret = false;
  byte tr = data[4];
  byte proj = data[5];
  byte message = data[6];
  
  if ((tr==246)&&(message==2) &&(proj == project.id)) {
    ret = true;
  }
  return ret;
  }  

bool projectStatusRequestReceived(byte *data) {
    byte ret = false;
  byte tr = data[4];
  byte message = data[6];

    if ((tr==246)&&(message==4)) {
    ret = true;
    }
  return ret;
  }

String printBytesSting(const byte *data, unsigned int size) {

   String buff = "";
   bool eom = false;
   while (size > 0) {
    if (!eom) {
      byte b = *data++;
      buff += b;    
      if (size > 1) buff +=" ";
      if (b == 247) eom = true;      
    }
      size = size - 1;
  }

  return buff;
  }

void printBytes(const byte *data, unsigned int size) {

#ifdef DEBUG           
  while (size > 0) {
    byte b = *data++;
   // if (b < 16) Serial.print('0');
  //  Serial.print(b, HEX);
      Serial.print(b);      
    if (size > 1){      
      Serial.print(' ');
    }
    size = size - 1;
  }
#endif
  
}

void updateFileSysex() {

    char filename[6];
    uint32_t b; 

    // prepare the start bits
    sysex_files[0] = 240;
    sysex_files[1] = 0;
    sysex_files[2] = 32;
    sysex_files[3] = 41;
    sysex_files[3] = 125;
     
    // prepare sysex
     for (byte i = 4; i < sizeof (sysex_files); i++) {  
        sprintf (filename, "%02d.txt",i);        
        if (SD.exists(filename)) {
            // add flag
            b |= 1 << i;
        }
        sysex_files[i]=b;
     }

     // add sysex end
     sysex_files[sizeof(sysex_files)-2] = 247;
     
  }

void stringToArray(String input,int row) {

  int counter = 0;
  int lastIndex = 0;
  
    for (int i = 0; i < input.length(); i++) {
        // Loop through each character and check if it's a comma
        if (input.substring(i, i+1) == " ") {
          // Grab the piece from the last index up to the current position and store it
          sysex_data[row][counter] = input.substring(lastIndex, i).toInt();
          // Update the last position and add 1, so it starts from the next character
          lastIndex = i + 1;
          // Increase the position in the array that we store into
          counter++;
        }

        // If we're at the end of the string (no more commas to stop us)
        if (i == input.length() - 1) {
          // Grab the last part of the string from the lastIndex to the end
          sysex_data[row][counter] = input.substring(lastIndex, i).toInt();
        }
    }

  }

void sendOutSysex() {

  byte tracksCount = sizeof (sysex_data) / sizeof (sysex_data[0]);
  
    #ifdef DEBUG       
      Serial.print("Tracks:");
      Serial.print(tracksCount);
      Serial.println("Sending all sysex data");
    #endif
    
  for (byte i = 0; i < tracksCount; i++) {
   if (sysex_data[i][0] > 0) {     
      midi1.sendSysEx(sizeof(sysex_data[i]), sysex_data[i], true);
     // Serial.print("<msg>");
      printBytes(sysex_data[i],sizeof(sysex_data[i]));
    //  Serial.print("</msg>");
     // Serial.println();
     delay(50);
    }   
  }
}

void sendOutProject(byte p) {   
    sdCardToArray(p);
    sendOutProjectNumber(p);
    delay(20);     
    sendOutSysex();
    delay(20);  
    sendProjectExist();
  }

  
void loadProjectSDCard(byte id) {
    
    
    // reactivate pots before loading
    resetPotStates();

    // clean project data
    cleanProjectData();
    
    char filename[7];

        // set temporary project name as filename
        sprintf (filename, "%02d.txt",id);   
        if (SD.exists(filename)) {
          project.name = filename;
          }

        // check if project file exisits
        sprintf (filename, "_%02d.txt",id);        
        if (SD.exists(filename)) {
          
          #ifdef DEBUG
          Serial.print("Project file found:");
          Serial.println(filename);        
          #endif
          
          project.name = filename;
          dataFile = SD.open(filename);
          
          if (!dataFile) {
          #ifdef DEBUG
              Serial.print("The text file cannot be opened or does not exist");
          #endif    
              while(1);
            }
        
          int r = 0;
          while (dataFile.available()) {
              buf = dataFile.readStringUntil('\n',512);
           #ifdef DEBUG2    
              Serial.print("Reading row ");
              Serial.print(r);
              Serial.print(" : ");
              Serial.print(buf);
              Serial.println();
           #endif

              //check row and assign it to the project value
              String key = getValue(buf,'=',0);
              String value = getValue(buf,'=',1);

             // values to the pr

               if (key=="id"){
                project.id=(byte)value.toInt();
               }
             
               if (key=="name"){
                project.name=value.trim();
               }
               
               if (key=="transpose") {
                project.transpose=value.toInt();
                project.transpose_temp=project.transpose;
               }
               
               if (key=="tempo") {
                project.tempo=(byte)value.toInt();
                project.tempo_temp=project.tempo;
               }

               for (byte i=0;i<sizeof(project.scene_tempo);i++) {
               String key_tmp = "tempo_sc";
               key_tmp += i;
                 if (key==key_tmp) {
                  project.scene_tempo[i]=(byte)value.toInt();
                 }
               }
               
              
            #ifdef DEBUG2    
              Serial.print("Key: ");
              Serial.println(key);
              Serial.print("Value: ");
              Serial.println(value);
           #endif
           
              r++;
              buf="";
              delay(50);
            }
          dataFile.close();
          
          
        }
  }

void deleteProjectSDCard(byte cp) {
    if (cp<255) 
    {
    
    //remove trackdata
    char filename[6];
    sprintf (filename, "%02d.txt",cp);
    SD.remove(filename);
    
    // remove project file
    char filename2[7];
    sprintf (filename2, "_%02d.txt",cp);
    SD.remove(filename2);

    #ifdef DEBUG2
      Serial.println("Files deleted:");
      Serial.print(filename);
      Serial.print(filename2);
    #endif
   
    }
  }


void sendOutProjectNumber(byte p) {

      char filename[6];
  
    // prepare the start bits
    sysex_fileExist[0] = 240;
    sysex_fileExist[1] = 0;
    sysex_fileExist[2] = 32;
    sysex_fileExist[3] = 41;
    sysex_fileExist[4] = 124;
    sysex_fileExist[5] = p;    
    sysex_fileExist[6] = 0;   
    sysex_fileExist[7] = 247;

   #ifdef DEBUG     
      Serial.println("Sending out project SET message:");
    #endif
    printBytes(sysex_fileExist,sizeof(sysex_fileExist));
  
    midi1.sendSysEx(sizeof(sysex_fileExist), sysex_fileExist, true);
  

  }

void sendProjectExist() {

    char filename[6];
  
    // prepare the start bits
    sysex_fileExist[0] = 240;
    sysex_fileExist[1] = 0;
    sysex_fileExist[2] = 32;
    sysex_fileExist[3] = 41;
    sysex_fileExist[4] = 125;
    sysex_fileExist[6] = 1;   
    sysex_fileExist[7] = 247;

     for (byte i = 0; i < NUMBER_OF_PROJECTS; i++) {  
        sprintf (filename, "%02d.txt",i);        
        if (SD.exists(filename)) {
            // light up led
        sysex_fileExist[5] = i; 
        midi1.sendSysEx(sizeof(sysex_fileExist), sysex_fileExist, true);

       #ifdef DEBUG     
          Serial.println("Sending out project EXITS message:");
        #endif
        printBytes(sysex_fileExist,sizeof(sysex_fileExist));
        delay(10);
        }
     }
      
  }

void sendWorkingMessage(byte stat) {

  
    // prepare the start bits
    sysex_fileExist[0] = 240;
    sysex_fileExist[1] = 0;
    sysex_fileExist[2] = 32;
    sysex_fileExist[3] = 41;
    sysex_fileExist[4] = 125;
    sysex_fileExist[5] = project.id;       
    sysex_fileExist[6] = 247;
    
    // light up led
    if (stat==0)
    midi1.sendSysEx(sizeof(sysex_fileExist), sysex_fileExist, true);
}


void storeSysex(const byte *data, unsigned int size) {
  
  byte tr = data[4];

  // take into account only tracks below 246 (start/end tracks numbers)
  if ((tr <246) && (size>6)) {

    // in case a track sysex
    //if ((tr >= 0) && (tr <NUMBER_OF_TRACKS)) {
    //   cleanArray(tr);
    // }

    // in case global settings
    if (tr == 127) {
      tr = NUMBER_OF_TRACKS;
    //  cleanArray(tr);
      }
      
    // in case mute track settings
    if (tr == 126) {
      tr = NUMBER_OF_TRACKS + 1 + data[5];
      }

    #ifdef DEBUG     
      Serial.print("Saving sysex, track: ");
      Serial.println(tr);
    #endif
          
    // prepare sysex
     for (byte i = 0; i < size; i++) {
      byte b = *data++;    
      sysex_data[tr][i]=b;
     }         
   }
 }

void cleanArray(byte track) {
  for (byte i = 0; i < sizeof(sysex_data[0]) - 1; i++) {
      sysex_data[track][i]=0;
     } 
  }

void cleanData() {
  
  for (byte i = 0; i < (NUMBER_OF_TRACKS+7); i++) {
      memset(sysex_data[i], 0, sizeof(sysex_data[i]));
     } 
  }

void cleanProjectData() {
  project.name="EMPTY MRG";
  project.tempo=150;
  project.id=255;
  project.scene=0;
  for (byte i=0;i<sizeof(project.scene_tempo);i++) {
    project.scene_tempo[i]=150;
    }
  }    

void resetMidi() {

 #ifdef DEBUG
 Serial.println("Resetting...");
 #endif

 //send note off in all channels
 // .. TODO ..

 //reset "notes_played"

  notesPlaying = 0;
  
  }


//****************** LCD *******************


void lcdMainMenu(byte currentMode) {
  
  if (menuMode!=currentMode) {
    lcd.clear();
  }

  // set cursor for arrows
  switch (menuMode) {
    
  case PROJECT:
    lcd.print("Project >"); 
    break;
  
  case OCTAVE:
  
    if (menuMode != currentMode) {

      
      // labels and inital values
      lcd.print("Oc> ME BR LL LH");
      lcd.setCursor(4,1);
      lcd.print(project.b1_oct);
      lcd.setCursor(7,1);
      lcd.print(project.b2_oct);    
      lcd.setCursor(10,1);
      lcd.print(project.l1_oct);           
      lcd.setCursor(13,1);
      lcd.print(project.l2_oct);
      

    }
 
    // values only when pots active
    if (pot3State==ACTIVE) {
      lcd.setCursor(4,1);
      lcd.print("  ");
      lcd.setCursor(4,1);
      lcd.print(project.b1_oct);
    }

    if (pot4State==ACTIVE) {
      lcd.setCursor(7,1);
      lcd.print("  ");
      lcd.setCursor(7,1);
      lcd.print(project.b2_oct);
    }

    if (pot5State==ACTIVE){
    lcd.setCursor(10,1);
    lcd.print("  ");
    lcd.setCursor(10,1);
    lcd.print(project.l1_oct);
    }

    if (pot6State==ACTIVE) {
      lcd.setCursor(13,1);
      lcd.print("  ");
      lcd.setCursor(13,1);
      lcd.print(project.l2_oct);
    }
  
    break;

  case TEMPO_TP:

      if (menuMode != currentMode) {
        // labels and inital values
        lcd.print("Te> Sc Bpm Trn");
        lcd.setCursor(4,1);
        lcd.print(project.scene);
        lcd.setCursor(7,1);
        lcd.print(project.scene_tempo[project.scene]);
        lcd.setCursor(12,1);
        lcd.print(project.transpose);
      }

    // values only when pots active
    if (pot4State==ACTIVE) {
      lcd.setCursor(4,1);
      lcd.print("  ");
      lcd.setCursor(4,1);
      lcd.print(project.scene);
    }

    if (pot5State==ACTIVE) {
      lcd.setCursor(7,1);
      lcd.print("   ");
      lcd.setCursor(7,1);
      lcd.print(project.scene_tempo[project.scene]);
    }

    if (pot6State==ACTIVE) {
      lcd.setCursor(12,1);
      lcd.print("   ");
      lcd.setCursor(12,1);
      lcd.print(project.transpose);
    }

     
    break;

  case KEYS:
    lcd.print("Ke>");
    break;

  
  case LOAD:
    lcd.print("Load >");
    break;
  
  case MIDICC:
    lcd.print("Midi >");
    break;
  
  case SAVE:
    lcd.print("Save pr data >");
    break;
  
  case CLEAN:
    lcd.print("New project >");
    break;
  
  case BACK:
    lcd.print("< BACK");
    
   default:
    break;
  } 
}

void lcdSubMenu(byte currentMode) {

  if (menuMode != currentMode) {
    lcd.clear();
  }
  
  switch (menuMode) {
  
    case PROJECT:

    #ifdef DEBUG
      Serial.println("******Project:");
      Serial.print("Name: ");
      Serial.println(project.name);
      Serial.print("Transpose: ");
      Serial.println(project.transpose);
      Serial.print("Tempo: ");
      Serial.println(project.tempo);
      Serial.print("ID: ");
      Serial.println(project.id);
    #endif
      
      
      // project
      lcd.setCursor(0,0);
      if (project.id<255) {
        lcd.print(project.id);
      }
      else
      {
        lcd.print("*");
        }
        
      lcd.print("/");
      lcd.print(project.scene);
      lcd.print(":");
      lcd.print(project.name);

      // project.tempo
      lcd.setCursor(1,1);
      lcd.print("B:"); 
      lcd.print(project.scene_tempo[project.scene]);

      // bass play mode
      lcd.setCursor(7,1);
      switch (bassMode) {
        case 0:
          lcd.print("Lead");
        break;

        case 1:
          lcd.print("Bass");
        break;

        case 2:
          lcd.print("L1+B");
        break;        
        }
     

      // transpose
      lcd.setCursor(12,1);
      lcd.print("T:");
      lcd.print(project.transpose);

  
      break;
    
    case OCTAVE:
      lcd.setCursor(0,0);
      lcd.print("OCT>");
      lcd.print(b1);
      lcd.print(":");
      lcd.print("  ");
      lcd.setCursor(7,0);     
      lcd.print(project.b1_oct);
    
      lcd.setCursor(11,0);
      lcd.print(b2);
      lcd.print(":");
      lcd.print("  ");
      lcd.setCursor(14,0);      
      lcd.print(project.b2_oct);
    
      lcd.setCursor(4,1);
      lcd.print(l1);
      lcd.print(":");
      lcd.print("  ");
      lcd.setCursor(7,1);     
      lcd.print(project.l1_oct);
    
      lcd.setCursor(11,1);
      lcd.print(l2);
      lcd.print("  ");
      lcd.print(":");    
      lcd.setCursor(14,1);   
      lcd.print(project.l2_oct);
    
      break;
    
    case LOAD:
     // dataFile = SD.open("/"); 
     // (dataFile);
     // dataFile.close();

      lcd.setCursor(0,0);
      lcd.print("Load >");
      lcd.setCursor(0,1);
      lcd.print("                ");
      lcd.setCursor(0,1);
      if (selectedProject>=0) {
        lcd.print(projectList[selectedProject].id);
        lcd.print(":");
        lcd.print(projectList[selectedProject].name);
      }
      else
      {
        lcd.print("< BACK");
        }

    break;    

    case SAVE: 
      lcd.setCursor(0,0);
      lcd.print("Save pr data >");
      lcd.setCursor(0,1);
      lcd.print(project.name);
      lcd.setCursor(15,1);
      lcd.print(">");
      //lcd.blink();
    break;
    
    case MIDICC:
       lcd.setCursor(0,0);
       lcd.print("Midi >");
       lcd.setCursor(0,1);
       if (playing) {
        lcd.print("Plying..       >");        
        }
       else
        {
         lcd.print("Start PLAY     >"); 
          }       

    break;
    

    
  }     
}

void lcdViewProject(byte p) {

Project projectTemp = getProjectById(p);

if (projectTemp.id!=255) {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Slot: ");
  lcd.print(projectTemp.id);
  lcd.setCursor(0,1);
  lcd.print(projectTemp.name);

        #ifdef DEBUG2
        Serial.print("Displaying project name:");
        Serial.println(projectTemp.name);
      #endif 
  }
  else
  {
      lcd.clear();
      lcd.print("No project on ");
      lcd.print (p);
    }
      
  }

void updateLcdScreen() {

 int tempValue, tempValue2, tempValue3, tempValue4;

 // if on mainMenu
 if (menuState==MAIN) {

    switch (menuMode) {
   case OCTAVE: 

     if (notesPlaying==0) {
        if (pot3State==ACTIVE) {        
          tempValue = map(pot3.getValue(),0,1023,-4,4);          
          if (project.b1_oct==tempValue) {
            pot3Ok = true;
          }          
          if (pot3Ok) {
            project.b1_oct = tempValue;
            lcdMainMenu(OCTAVE);
          }        
        }
  
        if (pot4State==ACTIVE) {
          tempValue = map(pot4.getValue(),0,1023,-4,4); 
          if (project.b2_oct==tempValue) {
            pot4Ok = true;
          }          
          if (pot4Ok) {
            project.b2_oct = tempValue;
            lcdMainMenu(OCTAVE);
          }   
        }
  
        if (pot5State==ACTIVE) {
          tempValue = map(pot5.getValue(),0,1023,-4,4);
          if (project.l1_oct==tempValue) {
            pot5Ok = true;
          }          
          if (pot5Ok) {
            project.l1_oct = tempValue;
            lcdMainMenu(OCTAVE);
          }  
        }
  
        if (pot6State==ACTIVE) {
          tempValue = map(pot6.getValue(),0,1023,-4,4); 
          if (project.l2_oct==tempValue) {
            pot6Ok = true;
          }          
          if (pot6Ok) {
            project.l2_oct = tempValue;
            lcdMainMenu(OCTAVE);
          } 
        }
     }
    
    break;

    case TEMPO_TP:
     if (notesPlaying==0) {

            if (pot5State==ACTIVE) {        
              tempValue = map(pot5.getValue(),0,1023,minTempo,maxTempo);          
              if (project.scene_tempo[project.scene]==tempValue) {
                pot5Ok = true;
              }          
              if (pot5Ok) {
                project.scene_tempo[project.scene] = tempValue;
                lcdMainMenu(TEMPO_TP);
              }        
            }
            
            if (pot6State==ACTIVE) {        
              tempValue = map(pot6.getValue(),0,1023,-5,5);          
              if (project.transpose==tempValue) {
                pot6Ok = true;
              }          
              if (pot6Ok) {
                project.transpose = tempValue;
                lcdMainMenu(TEMPO_TP);
              }        
            }
            
        }
      break;

    case KEYS:
        // .. to do ..
      break;

      
    }
  
  }

 
 // if on submenu
 if (menuState==SUB) {
    
  
  switch (menuMode) {
  case PROJECT: //project


     if (pot2State==ACTIVE){ 

      tempValue = map(pot2.getValue(),0,1023,0,2);          
      if (bassMode==tempValue) {
        pot2Ok = true;
        
      }          
      if (pot2Ok) {
        bassMode = tempValue;
        lcdSubMenu(PROJECT);
      }


     }
     
    /*
    if (pot5State==ACTIVE){ 
      tempValue = map(pot5.getValue(),0,1023,50,255); 
      if (tempValue!=project.tempo_temp) {
        project.tempo_temp = tempValue;
        lcdSubMenu(PROJECT);
      }
     }

         */


    
     /*
     // if any of the 4 pots are turned, load octave mode
     if (pot1State==ACTIVE || pot2State==ACTIVE || pot3State == ACTIVE || pot4State==ACTIVE) {
      tempValue = map(pot1.getValue(),0,1023,-4,4);
      tempValue2 = map(pot2.getValue(),0,1023,-4,4); 
      tempValue3 = map(pot3.getValue(),0,1023,-4,4);
      tempValue4 = map(pot4.getValue(),0,1023,-4,4); 
      if ((tempValue!=project.b1_oct)|| (tempValue2!=project.b2_oct)|| (tempValue3!=project.l1_oct)|| (tempValue4!=project.l2_oct)) {
        menuMode=OCTAVE;
        lcdSubMenu(99);
      }
     }
    */
   break;
    
   case OCTAVE: 

/*
     if (notesPlaying==0) {
        if (pot1State==ACTIVE) {        
          tempValue = map(pot1.getValue(),0,1023,-4,4);
          if (project.b1_oct!=tempValue) {
            project.b1_oct = tempValue;
            lcdSubMenu(OCTAVE);
          }
        }
  
        if (pot2State==ACTIVE) {
          tempValue = map(pot2.getValue(),0,1023,-4,4); 
          if (project.b2_oct!=tempValue) {
            project.b2_oct = tempValue;
            lcdSubMenu(OCTAVE);
          }
        }
  
        if (pot3State==ACTIVE) {
          tempValue = map(pot3.getValue(),0,1023,-4,4);
          if (project.l1_oct != tempValue) {
            project.l1_oct = tempValue;
            lcdSubMenu(OCTAVE);
          }
        }
  
        if (pot4State==ACTIVE) {
          tempValue = map(pot4.getValue(),0,1023,-4,4); 
          if (project.l2_oct!=tempValue) {
            project.l2_oct = tempValue;
            lcdSubMenu(OCTAVE);
          }
        }
  
       // if any of the 2 pots are turned, load octave mode
       if (pot5State==ACTIVE || pot6State==ACTIVE) {
        tempValue = map(pot5.getValue(),0,1023,50,255); 
        tempValue2 = map(pot6.getValue(),0,1023,-5,5);
        if ((tempValue != project.tempo) || (tempValue2!=project.transpose)) {
          menuMode=PROJECT;
          lcdSubMenu(99);
        }
       //   printStatus();
       }
    
     }
      */
    
    break;


    case LOAD: 

      break;

    case MIDICC: 

      break;

    default:
      // statements
      break;
  }
    
 }
    
}

void lcdResetMessage() {
  lcd.clear();
  lcd.print(".. RESET ...");
  }


//*************** POTS ******************************************


void resetPotStates() {
  pot1State = DEACTIVE;
  pot2State = DEACTIVE;
  pot3State = DEACTIVE;
  pot4State = DEACTIVE;
  pot5State = DEACTIVE;
  pot6State = DEACTIVE;  
}


void resetPotOks() {
  pot1Ok = false;
  pot2Ok = false;
  pot3Ok = false;
  pot4Ok = false;
  pot5Ok = false;
  pot6Ok = false;
  }


void updatePots() {

  // only check the analog inputs 50 times per second,
  // to prevent a flood of MIDI messages
    if (msec >= 20) { 
      msec = 0;
      
      // update pots values
      pot1.update();
      pot2.update();
      pot3.update();
      pot4.update();
      pot5.update();
      pot6.update();
      
  }

  // check if changed
  if (pot1.hasChanged()) {
    pot1State = ACTIVE;

  }
  if (pot2.hasChanged()) {
    pot2State = ACTIVE;

  }
  if (pot3.hasChanged()) {
    pot3State = ACTIVE;

  }
  if (pot4.hasChanged()) {
    pot4State = ACTIVE;

  }
  if (pot5.hasChanged()) {
    pot5State = ACTIVE;

  }
  if (pot6.hasChanged()) {
    pot6State = ACTIVE;
  }  
  
}


// **************************** BUTTONS / ENCODERS ***********************


void updateButtons() {

if (bsButton.update()) {
  if (bsButton.fallingEdge()) {
    #ifdef DEBUG
      Serial.println("Both MEEB and MB");
    #endif
    bassMode=0;
    }
  if (bsButton.risingEdge()) {
    #ifdef DEBUG
      Serial.println("Only MB");
    #endif
    bassMode=1;
    }

  menuState=SUB;
  menuMode=PROJECT;
  lcdSubMenu(99);  
  }
  
}

void updateEncButton() {

  // if encoder button bushed
   if (swButton.update()) {
    if (swButton.fallingEdge()) {

      pressedTime= 0;
      rotPressed = true;
      resetShown = false;
      
      #ifdef DEBUG
        Serial.print("Adding presstime. Current value:");
        Serial.println(pressedTime);
      #endif

     // ********** [ IN MAIN MENU ] ****************
    if (menuState==MAIN) {

      // ******************* BACK  ***********
      if (menuMode==BACK) {
        menuState=SUB;
        menuMode=PROJECT;
        lcdSubMenu(99);
        }

        // ****************** NEW PROJECT ***********
       if (menuMode==CLEAN) {
        cleanProjectData();
        menuMode=PROJECT;
        lcdSubMenu(99);
        }

       // ******************** LOAD ***************
       if (menuMode==LOAD) {
          menuState=SUB;
         lcdSubMenu(99);      
        }

        // ******************* SAVE **************
       if (menuMode==SAVE) {
          menuState=SUB;
         lcdSubMenu(99);      
        }      
        
        // ******************* TEMPO_TP **************
       if (menuMode==TEMPO_TP) {

                  toggleRotEdit(2);

          // clear edit marks
          lcd.setCursor(6,1);
          lcd.print(" ");
          lcd.setCursor(11,1);
          lcd.print(" ");          
          
          if (rotEditValue==R_TEMPO) {
              lcd.setCursor(6,1);
              lcd.print("*");           
            }
          
          if (rotEditValue==R_TRANSPOSE) {
              lcd.setCursor(11,1);
              lcd.print("*");
            }  
        }

        //********** OCTAVE **************************
        if (menuMode==OCTAVE) {
                  toggleRotEdit(4);

          // clear edit marks
          lcd.setCursor(3,1);
          lcd.print(" ");
          lcd.setCursor(6,1);
          lcd.print(" ");          
          lcd.setCursor(9,1);
          lcd.print(" ");
          lcd.setCursor(12,1);
          lcd.print(" ");
                              
          if (rotEditValue==R_B1) {
              lcd.setCursor(3,1);
              lcd.print("*");           
            }
          if (rotEditValue==R_B2) {
              lcd.setCursor(6,1);
              lcd.print("*");
            }   
          if (rotEditValue==R_L1) {
              lcd.setCursor(9,1);
              lcd.print("*");
            }   
          if (rotEditValue==R_L2) {
              lcd.setCursor(12,1);
              lcd.print("*");
            }                        
          }      
           
    }

     // *************** [ IN SUB MENU ] **************
     else if (menuState==SUB) {     

       // **************** PROJECT **************
       if (menuMode==PROJECT) {

          toggleRotEdit(2);

          // clear edit marks
          lcd.setCursor(0,1);
          lcd.print(" ");
          lcd.setCursor(11,1);
          lcd.print(" ");

          if (rotEditValue==R_TEMPO) {
              lcd.setCursor(0,1);
              lcd.print("*");           
            }
          
          if (rotEditValue==R_TRANSPOSE) {
              lcd.setCursor(11,1);
              lcd.print("*");
            }
                 
       }
       
       // ***************** LOAD ******************
       else if (menuMode==LOAD) {
          if (selectedProject>=0) {
            project.id = projectList[selectedProject].id;
            project.name = projectList[selectedProject].name.trim();
            emptyProject = false;
    
            // load project details
            loadProjectSDCard(project.id);
        
            // send out project sysex
            sendOutProject(project.id);
          }
          
          // set mode to PROJECT
          menuMode=PROJECT;
          menuState=SUB;
          lcdSubMenu(99);
        }
        
       // *********************** SAVE ************
       else if (menuMode==SAVE) {    
        if (cursorLocation==15) {
              if (!emptyProject) {
                writeProjectSdCard(project.id);
                updateProjectList();
              }
      
              // set mode to PROJECT
              cursorLocation=0;
              menuMode=PROJECT;
              menuState=SUB;
              lcdSubMenu(99);
              lcd.noCursor();
          }
        else {
          toggleEditChar();         
          }
        }

       // *********************** MIDI ************
       else if (menuMode==MIDICC) {
              if (cursorLocation==15) {
                cursorLocation=0;
                menuMode=PROJECT;
                menuState=SUB;
                lcdSubMenu(99);
                lcd.noCursor();                
                }
              else{
                 
                 toggleMidiStart();

               if (playing) {
                lcd.setCursor(0,1);
                lcd.print("Plying..    ");        
                }
               else
                {
                lcd.setCursor(0,1);
                 lcd.print("Start PLAY"); 
                  }  
                 
                }
        }
       
       else
       { // if in any of the other submenus, go to project
        menuMode=PROJECT;
        menuState=SUB;
        lcdSubMenu(99);
       }     
     }   
     printStatus();

    // if pushed long enough, fire resetMidi
     
    }

   // perform reset if pressed long
   if (swButton.risingEdge()) {
    
    #ifdef DEBUG
      Serial.print("Press time: ");
      Serial.println(pressedTime);
    #endif
    
    if (pressedTime >= resetTime) {
       resetMidi();
       pressedTime = 0;     
       lcdSubMenu(99);       
      }

      rotPressed = false;  
    }
      
   }
  
  
  }


void rotEncoder()
{
  rotating=true; // If motion is detected in the rotary encoder,
                 // set the flag to true
}  

void updateEnc() {
   
  
  // if encoder turned
  //int v = encValue(); 
  while (rotating) {

  int v = 0;
  delay(2); // debounce by waiting 2 milliseconds

    if (digitalRead(clk) == digitalRead(dt))  // CCW
    {
      v=1;
      }
    else                          // If not CCW, then it is CW
    {
      v=-1;
      }
      

   #ifdef DEBUG
    Serial.print("****Endoder: ");
    Serial.println(v);
   #endif
    
    // ****************** MAIN MENU ********************
    if(menuState==MAIN) {

    // ************** SCENE TEMPO **************
      if (rotEditValue!=0) {
        if(menuMode==TEMPO_TP) {
          if(rotEditValue==R_TEMPO) {
            project.scene_tempo[project.scene] = project.scene_tempo[project.scene] + v;
            lcd.setCursor(7,1);
            lcd.print("   ");
            lcd.setCursor(7,1);
            lcd.print(project.scene_tempo[project.scene]);         
           }          
          }


    // ************** OCTAVE **************
     if  (menuMode==OCTAVE) { 
        if (rotEditValue==R_B1) {
          project.b1_oct = project.b1_oct + v;
          lcd.setCursor(4,1);
          lcd.print("  ");
          lcd.setCursor(4,1);
          lcd.print(project.b1_oct);          
          }

        if (rotEditValue==R_B2) {
          project.b2_oct = project.b2_oct + v;
          lcd.setCursor(7,1);
          lcd.print("  ");
          lcd.setCursor(7,1);
          lcd.print(project.b2_oct);          
          }

        if (rotEditValue==R_L1) {
          project.l1_oct = project.l1_oct + v;
          lcd.setCursor(10,1);
          lcd.print("  ");
          lcd.setCursor(10,1);
          lcd.print(project.l1_oct);          
          }                  

        if (rotEditValue==R_L2) {
          project.l2_oct = project.l2_oct + v;
          lcd.setCursor(13,1);
          lcd.print("  ");
          lcd.setCursor(13,1);
          lcd.print(project.l2_oct);          
          }        
        }           
      }
      else {   
        resetPotOks();
        updateMainMenuValue(v);
        if (menuMode==PROJECT)
        {
          menuState=SUB;
          lcdSubMenu(99);
          }
        
        else {
        lcdMainMenu(99);
        }
      }
    }  

    // *********************** PROJECT ******************
    else if (menuState==SUB && menuMode==PROJECT) {
      
      if (rotEditValue==0) {
        menuMode = OCTAVE;
        menuState=MAIN;
        lcdMainMenu(99);
        }

       // **** TEMPO CHANGE *** 
       else if(rotEditValue==R_TEMPO) {
        
        if (((project.scene_tempo[project.scene]+v) >= minTempo) && ((project.scene_tempo[project.scene]+v)<=maxTempo)) {

          project.scene_tempo[project.scene] = project.scene_tempo[project.scene] + v;

          // copy the value to the rest of the scenes
          for(byte i = 0;i<NUMBER_SCENES;i++) {
            project.scene_tempo[i] = project.scene_tempo[project.scene];
          }
          
          lcd.setCursor(3,1);
          lcd.print("   ");
          lcd.setCursor(3,1);
          lcd.print(project.scene_tempo[project.scene]);
          } 
        }

        // ***** TRANSPOSE CHANGE ***
        else if (rotEditValue==R_TRANSPOSE) {
          if (((project.transpose+v) >= minTrans) && ((project.transpose+v)<=maxTrans)) {
            project.transpose = project.transpose + v;          

          lcd.setCursor(14,1);
          lcd.print("  ");
          lcd.setCursor(14,1);
          lcd.print(project.transpose);
            }
          }
      }
    
    // ************** LOAD *****************
    else if (menuState==SUB && menuMode==LOAD) {

      if ((selectedProject+v)<0) {
        selectedProject=-1;
        }
        
      if (projectList[selectedProject+v].name!="") {
          selectedProject = selectedProject+v;
          #ifdef DEBUG2
            Serial.print("Size of project list: ");
           Serial.println(sizeof(projectList));
            Serial.print("Selected project: ");
           Serial.println(selectedProject);
         #endif      
        }
     //}
       lcdSubMenu(LOAD);
    }


   //*************** SAVE *****************
   else if(menuState==SUB && menuMode==SAVE) {

    lcd.cursor();

    if (!editChar) {
      cursorLocation = cursorLocation+v;
      if (cursorLocation <0) {
        cursorLocation = 0;
        }
      if (cursorLocation > 15) {
        cursorLocation = 15;
        }
        
      // lcd.blink();
       lcd.setCursor(0,0);
       lcd.print("Save pr data >");
       lcd.setCursor(0,1);
       lcd.print(project.name);
       lcd.setCursor(cursorLocation,1);
      
      #ifdef DEBUG2
        Serial.print("Location:");
        Serial.println(cursorLocation); 
        Serial.print("Size of nameChars:");
        Serial.println(sizeof(nameChars));
      #endif      
      
      }
      else
      {
       charLocation = charLocation+v;
      if (charLocation <0) {
        charLocation = 0;
        }
      if (charLocation > sizeof(nameChars)-1) {
        charLocation = sizeof(nameChars)-1;
        }
      lcd.print(nameChars[charLocation]);
      lcd.setCursor(cursorLocation,1);
      
        }  
    }

    
    //**************** MIDI ************************
    else if (menuState==SUB && menuMode==MIDICC) {

      lcd.cursor();
      
      cursorLocation = cursorLocation+v;
     
      if (cursorLocation <0) {
        cursorLocation = 0;
        }
      if (cursorLocation > 15) {
        cursorLocation = 15;
        }
      
       lcd.setCursor(cursorLocation,1);
     
      }

    rotating=false; // Reset the flag
    printStatus();
  }
  
}


//********************** MISC *******************************************

void toggleEditChar() {
  if (editChar) {
    editChar=false;
    lcd.noBlink();
    
    // if we are adding charters to the end, append by it
    if (project.name.length()==cursorLocation) {
      project.name = project.name + nameChars[charLocation];
      }
    else {
      project.name.setCharAt(cursorLocation,nameChars[charLocation]);
    }
    
      #ifdef DEBUG2
    Serial.print("Size of project name: ");
    Serial.println(project.name.length());

      
    Serial.print("Cursor location: ");
    Serial.println(cursorLocation);

    
    Serial.print("nameofchar: ");
    Serial.println(nameChars[charLocation]);
    
    Serial.print("Project name: ");
    Serial.print(project.name);
  #endif
  }
  else {
    editChar=true;
    lcd.blink();
  }
  
}

void toggleMidiStart() {
  if (playing) {
     playing = false;
     midi1.sendRealTime(midi1.Stop);

     #ifdef DEBUG
      Serial.println("*** STOP MIDI ***");
     #endif
    }  
  else
  {
     playing = true;
     midi1.sendRealTime(midi1.Start);    

     #ifdef DEBUG
      Serial.println("*** STOP MIDI ***");
     #endif

    }
}

void toggleRotEdit(byte maxValue) {

if (rotEditValue >=0 && rotEditValue <=maxValue) {
  rotEditValue=rotEditValue+1;
  }

if (rotEditValue>maxValue) {
  rotEditValue=0;
  }

  

/*
switch (rotEditValue) {

case 0:
  rotEditValue = R_TEMPO;
break;

case R_TEMPO:
  rotEditValue  = R_TRANSPOSE;
break;

case R_TRANSPOSE:
  rotEditValue = 0;
break;

}
*/


  #ifdef DEBUG2
    Serial.print("rotEditValue:");
    Serial.print(rotEditValue);
  #endif
}

void setTempo(byte sc) {
    
  // set tempo to be scene tempo
    if (sc>=0) {
      if(project.scene_tempo[sc]>0) {
        project.tempo = project.scene_tempo[sc];
        project.tempo_temp = project.tempo;
        }
      else {
         project.tempo = project.scene_tempo[0];
         project.tempo_temp = project.tempo;       
        }
    }
}
 
int encValue() {
   
   
   int ret_value = 0;
   
   
   
   /*
   long newPositionEnc;
   newPositionEnc = knob.read();   
   stateChange=stateChange+1;
   
   if (stateChange==2) {
     if (newPositionEnc != positionEnc) {
      if (newPositionEnc>positionEnc) {
        ret_value = 1;
      }
      if (newPositionEnc<positionEnc) {
        ret_value = -1;
        }      
     }
     stateChange=0;
   }
     
   positionEnc = newPositionEnc;
   */

   return ret_value;
 }


void updateMainMenuValue(int step) {


  //middle
  if (menuMode > 0 && menuMode < numberOfModes) {
     if (step>0) {
       menuMode = menuMode+1;
     }
     else {
       menuMode = menuMode-1;
     }
  }
  
  //first item
  if (menuMode==0) {
    if (step>0) {
      menuMode = menuMode+1;
    }
  }
    
  //last item
   if (menuMode==numberOfModes) {
     if (step<0) {
       menuMode=menuMode-1;
     }
    }

  #ifdef DEBUG2
    Serial.print("Step: ");
    Serial.println(step);
    Serial.print("menuMode: ");
    Serial.println(menuMode);
  #endif
  
}

/*FILE LISTING*/

void updateProjectList() {

  #ifdef DEBUG
   Serial.println("***** UpdateProjectList ****");
  #endif


  //clear the current dirlist
  memset(projectList, 0, sizeof(projectList));
  filelist_count=0;

  // open file
   dataFile = SD.open("/"); 
  
  while(true) {
    File entry =  dataFile.openNextFile();
    if (! entry) {
      // no more files
      break;
    }

    String file_name = entry.name();
    if (file_name.indexOf('_') == 0 ) { // get only project files
      
      String tempname = entry.name(); 
      //projectList[filelist_count] = tempname.substring(0,2);
      
      //projectList[filelist_count].name = tempname;
      //projectList[filelist_count].id = (byte)tempname.toInt();
      
      // open project file
      
          while (entry.available()) {
              buf = entry.readStringUntil('\n',512);

              //check row and assign it to the project value
              String key = getValue(buf,'=',0);
              String value = getValue(buf,'=',1);
             
               if (key=="name"){
                projectList[filelist_count].name=value.trim();
               }

               if (key=="id") {
                projectList[filelist_count].id=(byte)value.toInt();
                }
           
              buf="";
              delay(50);
            }
       
          

      
      #ifdef DEBUG
        Serial.print("Project name: ");
        Serial.println(projectList[filelist_count].name);
        Serial.print("Project id: ");
        Serial.println(projectList[filelist_count].id);
      #endif       
      
      
      tempname = "";
      filelist_count++;     
    }
    entry.close();  
  }

 dataFile.close();

 //sort projectlist
 sortProjectList();

}

/****************** UTILTY FUNCTIONS***********************/


String getValue(String data, char separator, int index)
{
  int found = 0;
  int strIndex[] = {0, -1};
  int maxIndex = data.length()-1;

  for(int i=0; i<=maxIndex && found<=index; i++){
    if(data.charAt(i)==separator || i==maxIndex){
        found++;
        strIndex[0] = strIndex[1]+1;
        strIndex[1] = (i == maxIndex) ? i+1 : i;
    }
  }

  return found>index ? data.substring(strIndex[0], strIndex[1]) : "";
}


void sortProjectList() {
      
  for(int i=0; i<(filelist_count-1); i++) {
        for(int o=0; o<(filelist_count-(i+1)); o++) {
                if(projectList[o].id > projectList[o+1].id) {
                    Project temp = projectList[o];
                    projectList[o] = projectList[o+1];
                    projectList[o+1] = temp;
                }
        }
    }
}

Project getProjectById(byte id) {

Project emptyProject = {0,0,150,150,{150,150,150,150,150,150},0,"EMPTY MRG",255,0,0,0,0,meeb,brute,keys,de}; 

    for(int i=0; i<(filelist_count); i++) {
      if (projectList[i].id == id) {
        return projectList[i];
      }
    }
    return emptyProject;
}



/****************SERIAL PRINTING FUNCTIONS*/

void printStatus() {

   #ifdef DEBUG      
      print_count = print_count+1;
      Serial.print("****Status: ");
      Serial.print(print_count);
      Serial.println("****");
      
      Serial.print("MenuState: ");
      Serial.println(menuState);
  
      Serial.print("Menumode: ");
      Serial.println(menuMode);
   
      Serial.println("***************");
   #endif
    }

    
