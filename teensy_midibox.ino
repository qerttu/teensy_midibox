/*
Version: V1.1 
Next version to-do:
- project mode: allow transpose editing with rotary edit


1.1
- project mode: allow tempo editing with rotary encoder
- bottom 4 knobs in main menu screen edits

1.0 (7.5.2022)
- intial import from storage  
- added merging for second midi in/out port

*/

const char version_number[] = "v1.1";

#include <MIDI.h>
#include <ResponsiveAnalogRead.h>
#include <USBHost_t36.h>
#include <SD.h>
#include <SPI.h>
#include <LiquidCrystal.h>
#include <Bounce.h>

#include <Encoder.h>

//#define DEBUG
#define DEBUG2

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

MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, MIDI);
MIDI_CREATE_INSTANCE(HardwareSerial, Serial2, MIDI2);

/******************** MIDI *****************************/

/* CONSTANTS */

/* PINS */

//pins
const int ledPin = 3;
const int sw_pin = 39;
const int bs_pin = 38;

//lcd
const int rs = 11, en = 12, d4 = 29, d5 = 30, d6 = 31, d7 = 32;

//rotary enc
const int dt = 40, clk = 41;

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

//midi channels in Alesis Micron
const int bass_channel = 2;
const int lead_channel = 6;
const int sample_channel = 11;

// instrument names and channels
const int meeb = 5;
const int brute = 2;
const int keys = 6;
const int de = 7;

//max min values
const int maxTempo = 254;
const int minTempo = 50;

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

//inital playmode (-1=left; 0=both; 1=right)
int bassMode=0;
int leadMode=0;

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
bool rotEditValue = false;


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


    // USB midi setups
    myusb.begin();
    midi1.setHandleSystemExclusive(mySystemExclusive); 

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

    // load default dirlist
   //   dataFile = SD.open("/"); 
      updateProjectList();
   //   dataFile.close();

      
    // Connect the handleNoteOn function to the library,
    // so it is called upon reception of a NoteOn.
    MIDI.setHandleNoteOn(handleNoteOn);  // Put only the name of the function
    MIDI2.setHandleNoteOn(handleNoteOn);

    // Do the same for NoteOffs
    MIDI.setHandleNoteOff(handleNoteOff);
    MIDI2.setHandleNoteOff(handleNoteOff);


    // handle midi cloc
    MIDI.setHandleClock(myClock);
    MIDI2.setHandleClock(myClock);


    // handle midi start
    MIDI.setHandleStart(myStart);
    MIDI2.setHandleStart(myStart);


    // handle stop
    MIDI.setHandleStop(myStop);
    MIDI2.setHandleStop(myStop);


    // handle continue
    MIDI.setHandleContinue(myContinue);
    MIDI2.setHandleContinue(myContinue);


    // Initiate MIDI communications, listen to all channels
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

    pot1.setActivityThreshold(10);
    pot2.setActivityThreshold(10);
    pot3.setActivityThreshold(10);
    pot4.setActivityThreshold(10);
    pot5.setActivityThreshold(10);
    pot6.setActivityThreshold(10);


// Attach Interrupts
  attachInterrupt(digitalPinToInterrupt(dt), rotEncoder, CHANGE);  // ISR for rotary encoder

  // set up the LCD's number of columns and rows:
  lcd.begin(16, 2);

  
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
    updatePots();

  // updateScreen    
    updateLcdScreen();

  // reset all input states to DEACTIVE
  resetPotStates();

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
  #ifdef DEBUG
    Serial.println("START"); 
  #endif
}

void myStop() {
  analogWrite(ledPin, ledLow); 
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
 
  //if incoming is bass note
  if (channel==bass_channel)
  {
    // send to both  
    if (bassMode==0)
     { 
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

      
      MIDI.sendNoteOn(note + (project.b1_oct*12) + project.transpose, velocity, project.b1_channel);    
      MIDI.sendNoteOn(note + (project.b2_oct*12) + project.transpose, velocity, project.b2_channel);
      
      MIDI2.sendNoteOn(note + (project.b1_oct*12) + project.transpose, velocity, project.b1_channel);    
      MIDI2.sendNoteOn(note + (project.b2_oct*12) + project.transpose, velocity, project.b2_channel);
      
      notesPlaying=notesPlaying+2; 
     }
     
    // only left
    if (bassMode==-1)
     { 

    #ifdef DEBUG     
      Serial.print("Note send: ");
      Serial.print(note + (project.b1_oct*12) + project.transpose);
      Serial.print(" Channel: ");
      Serial.print(project.b1_channel);
      Serial.println();
    #endif
    
      MIDI.sendNoteOn(note + (project.b1_oct*12) + project.transpose, velocity, project.b1_channel);
      MIDI2.sendNoteOn(note + (project.b1_oct*12) + project.transpose, velocity, project.b1_channel);
 
      notesPlaying=notesPlaying+1; 

 
     }

    // only right
    if (bassMode==1)
     { 

    #ifdef DEBUG
      Serial.print("Note send: ");
      Serial.print(note + (project.b2_oct*12) + project.transpose);
      Serial.print(" Channel: ");
      Serial.print(project.b2_channel);
      Serial.println();
    #endif
         
      MIDI.sendNoteOn(note + (project.b2_oct*12) + project.transpose, velocity, project.b2_channel);
      MIDI2.sendNoteOn(note + (project.b2_oct*12) + project.transpose, velocity, project.b2_channel);

      notesPlaying=notesPlaying+1; 

     }
  }


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
   
/* 
   // send to both  
    if (leadMode==0)
     { 
      MIDI.sendNoteOn(note + (project.l1_oct*12) + project.transpose, velocity, project.l1_channel);
      MIDI2.sendNoteOn(note + (project.l1_oct*12) + project.transpose, velocity, project.l1_channel);

    

      
      MIDI.sendNoteOn(note + (project.l2_oct*12) + project.transpose, velocity, project.l2_channel);            
      MIDI2.sendNoteOn(note + (project.l2_oct*12) + project.transpose, velocity, project.l2_channel);  
      
      notesPlaying=notesPlaying+2; 
     }
     
    // only left
    if (leadMode==-1)
     { 

    #ifdef DEBUG    
      Serial.print("Note send: ");
      Serial.print(note + (project.l1_oct*12) + project.transpose);
      Serial.print(" Channel: ");
      Serial.print(project.l1_channel);
      Serial.println();     
    #endif
            
      MIDI.sendNoteOn(note + (project.l1_oct*12) + project.transpose, velocity, project.l1_channel);
      MIDI2.sendNoteOn(note + (project.l1_oct*12) + project.transpose, velocity, project.l1_channel);

      notesPlaying=notesPlaying+1; 
     }

    // only right
    if (leadMode==1)
     { 
    #ifdef DEBUG
      Serial.print("Note send: ");
      Serial.print(note + (project.l2_oct*12) + project.transpose);
      Serial.print(" Channel: ");
      Serial.print(project.l2_channel);
      Serial.println();     
    #endif
      
      MIDI.sendNoteOn(note + (project.l2_oct*12) + project.transpose, velocity, project.l2_channel);
      MIDI2.sendNoteOn(note + (project.l2_oct*12) + project.transpose, velocity, project.l2_channel);
      
      notesPlaying=notesPlaying+1; 
     }

   */  
  }

  // if incoming is for sample, do mapping
  if (channel==sample_channel)
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
      default:
        mappedNote = 37;
        break;
      }

      MIDI.sendNoteOn(mappedNote, velocity, sample_channel);
      MIDI2.sendNoteOn(mappedNote, velocity, sample_channel);

    #ifdef DEBUG  
      Serial.print("Note receive: ");
      Serial.print(note);
      Serial.print(" Note send: ");
      Serial.print(mappedNote);
      Serial.print(" Channel: ");
      Serial.print(sample_channel);
      Serial.println();  
   #endif
    }
  // if incoming is none of above, just pass through without octave, project.transpose etc
  if ((channel!=bass_channel) && (channel!=project.l1_channel) && (channel!=project.l2_channel) && (channel!=sample_channel))
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

  //led
  analogWrite(ledPin, ledHigh);



  //incoming is bass note
  if (channel==bass_channel)
  {
    // send to both  
    if (bassMode==0)
     { 

     #ifdef DEBUG
      Serial.print("Note OFF: ");
      Serial.print(note + (project.b1_oct*12) + project.transpose);
      Serial.print(" Channel: ");
      Serial.print(project.b1_channel);
      Serial.println();   
      
      Serial.print("Note OFF: ");
      Serial.print(note + (project.b2_oct*12) + project.transpose);
      Serial.print(" Channel: ");
      Serial.print(project.b2_channel);
      Serial.println();   
    #endif
       
      MIDI.sendNoteOff(note + (project.b1_oct*12) + project.transpose, velocity, project.b1_channel);
      MIDI.sendNoteOff(note + (project.b2_oct*12) + project.transpose, velocity, project.b2_channel); 

      MIDI2.sendNoteOff(note + (project.b1_oct*12) + project.transpose, velocity, project.b1_channel);
      MIDI2.sendNoteOff(note + (project.b2_oct*12) + project.transpose, velocity, project.b2_channel); 
      
      
      notesPlaying=notesPlaying-2; 
     }
     
    // only meeblip
    if (bassMode==-1)
     { 

    #ifdef DEBUG
      Serial.print("Note OFF: ");
      Serial.print(note + (project.b1_oct*12) + project.transpose);
      Serial.print(" Channel: ");
      Serial.print(project.b1_channel);
      Serial.println();   
    #endif
      
      MIDI.sendNoteOff(note + (project.b1_oct*12) + project.transpose, velocity, project.b1_channel);
      MIDI2.sendNoteOff(note + (project.b1_oct*12) + project.transpose, velocity, project.b1_channel);
      
      notesPlaying=notesPlaying-1; 
     }

    // only meeblip
    if (bassMode==1)
     { 

    #ifdef DEBUG
      Serial.print("Note OFF: ");
      Serial.print(note + (project.b2_oct*12) + project.transpose);
      Serial.print(" Channel: ");
      Serial.print(project.b2_channel);
      Serial.println();   
    #endif
      
      MIDI.sendNoteOff(note + (project.b2_oct*12) + project.transpose, velocity, project.b2_channel);
      MIDI2.sendNoteOff(note + (project.b2_oct*12) + project.transpose, velocity, project.b2_channel);
      notesPlaying=notesPlaying-1; 
     }
  }


  // incoming is lead note
  if ((channel==project.l1_channel) || (channel==project.l2_channel))
  {

    if (channel==project.l1_channel) {
      MIDI.sendNoteOff(note + (project.l1_oct*12) + project.transpose, velocity, channel);
      MIDI2.sendNoteOff(note + (project.l1_oct*12) + project.transpose, velocity, channel);

    #ifdef DEBUG
      Serial.print("Note OFF: ");
      Serial.print(note + (project.l1_oct*12) + project.transpose);
      Serial.print("Channel: ");
      Serial.print(project.l1_channel);
      Serial.println();   
    #endif
    }

    if (channel==project.l2_channel) {
      MIDI.sendNoteOff(note + (project.l2_oct*12) + project.transpose, velocity, channel);            
      MIDI2.sendNoteOff(note + (project.l2_oct*12) + project.transpose, velocity, channel); 

     #ifdef DEBUG         
      Serial.print("Note OFF: ");
      Serial.print(note + (project.l2_oct*12) + project.transpose);
      Serial.print(" Channel: ");
      Serial.print(project.l2_channel);
      Serial.println();     
    #endif
    }


    notesPlaying=notesPlaying-1; 

    /*
    
    // send to both  
    if (leadMode==0)
     { 
 
    #ifdef DEBUG
      Serial.print("Note OFF: ");
      Serial.print(note + (project.l1_oct*12) + project.transpose);
      Serial.print("Channel: ");
      Serial.print(project.l1_channel);
      Serial.println();  
      
      Serial.print("Note OFF: ");
      Serial.print(note + (project.l2_oct*12) + project.transpose);
      Serial.print(" Channel: ");
      Serial.print(project.l2_channel);
      Serial.println();  
    #endif
    
      MIDI.sendNoteOff(note + (project.l1_oct*12) + project.transpose, velocity, project.l1_channel);
      MIDI.sendNoteOff(note + (project.l2_oct*12) + project.transpose, velocity, project.l2_channel);  
      MIDI2.sendNoteOff(note + (project.l1_oct*12) + project.transpose, velocity, project.l1_channel);
      MIDI2.sendNoteOff(note + (project.l2_oct*12) + project.transpose, velocity, project.l2_channel);  

      notesPlaying=notesPlaying-2; 
     }
     
    // only L1
    if (leadMode==-1)
     { 

     #ifdef DEBUG
      Serial.print("Note OFF: ");
      Serial.print(note + (project.l1_oct*12) + project.transpose);
      Serial.print(" Channel: ");
      Serial.print(project.l1_channel);
      Serial.println();  
    #endif
    
     MIDI.sendNoteOff(note + (project.l1_oct*12) + project.transpose, velocity, project.l1_channel);
     MIDI2.sendNoteOff(note + (project.l1_oct*12) + project.transpose, velocity, project.l1_channel);
     
     notesPlaying=notesPlaying-1; 
     }

    // only L2
    if (leadMode==1)
     { 
    #ifdef DEBUG
      Serial.print("Note OFF: ");
      Serial.print(note + (project.l2_oct*12) + project.transpose);
      Serial.print(" Channel: ");
      Serial.print(project.l2_channel);
      Serial.println();  
    #endif
    
      MIDI.sendNoteOff(note + (project.l2_oct*12) + project.transpose, velocity, project.l2_channel);
      MIDI2.sendNoteOff(note + (project.l2_oct*12) + project.transpose, velocity, project.l2_channel);

      notesPlaying=notesPlaying-1; 
     }
  */
  }

  // incoming is for sample, do mapping
  if (channel==sample_channel)
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
      default:
        mappedNote = 37;
        break;
      }

      MIDI.sendNoteOff(mappedNote, velocity, sample_channel);
      MIDI2.sendNoteOff(mappedNote, velocity, sample_channel);

    #ifdef DEBUG
      Serial.print("Note OFF: ");
      Serial.print(mappedNote);
      Serial.print(" Channel: ");
      Serial.print(sample_channel);
      Serial.println();  
    #endif
    }
  // if incoming is none of above, just pass through without octave, project.transpose etc
  if ((channel!=bass_channel) && (channel!=project.l1_channel) && (channel!=project.l2_channel) && (channel!=sample_channel))
  {
    MIDI.sendNoteOff(note, velocity, channel);
    MIDI2.sendNoteOff(note, velocity, channel);

    #ifdef DEBUG
      Serial.print("Note OFF: ");
      Serial.print(note);
      Serial.print(" Channel: ");
      Serial.print(channel);
      Serial.println();  
    #endif
   }

  #ifdef DEBUG
   //print stats
   Serial.print("Bass mode: ");
   Serial.print(bassMode);
   Serial.print(" Lead mode: ");
   Serial.print(leadMode);
   Serial.print(" Notesplaying : ");
   Serial.print(notesPlaying);
   Serial.println();
  #endif
    
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

   //if loadProject request send, sent the project 
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

   // start/stop midiclock sysex request
   if (midiStartRequested(data) || midiStopRequested(data)) {
    // send out start message
    toggleMidiStart();
    }

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
      rotEditValue = false;

      // refresh screen
      if (menuMode==PROJECT) {
        lcdSubMenu(99);
      }
      else {
      lcdMainMenu(99);  
      }
    }
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

        // copy tempo to scene1
     //   if (project.tempo>0) {
     //     project.scene_tempo[0]=project.tempo;
     //     }
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
      lcd.print("Bpm:"); 
      lcd.print(project.scene_tempo[project.scene]);

      // transpose
      lcd.setCursor(10,1);
      lcd.print("Trn:");
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

    /*
    if (pot5State==ACTIVE){ 
      tempValue = map(pot5.getValue(),0,1023,50,255); 
      if (tempValue!=project.tempo_temp) {
        project.tempo_temp = tempValue;
        lcdSubMenu(PROJECT);
      }
     }
    
     if (pot6State==ACTIVE){ 
      if (notesPlaying==0) {
        tempValue = map(pot6.getValue(),0,1023,-5,5);
        if (tempValue!=project.transpose_temp) {  
         project.transpose_temp = tempValue;
         lcdSubMenu(PROJECT);
        }     
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
          menuState=SUB;
          menuMode=PROJECT;
          lcdSubMenu(99);    
        }      
        

       
        // menuState=SUB;
        // lcdSubMenu(99);
           
    }

     // *************** [ IN SUB MENU ] **************
     else if (menuState==SUB) {     

       // **************** PROJECT **************
       if (menuMode==PROJECT) {

          toggleRotEdit(); 
          if (rotEditValue) {
              lcd.setCursor(0,1);
              lcd.print("*");
            }
          else {
              lcd.setCursor(0,1);
              lcd.print(" ");
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
    
    // if under main menu
    if(menuState==MAIN) {
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

    // if turned in project menu, go straight to main menu
    else if (menuState==SUB && menuMode==PROJECT) {
      
      if (!rotEditValue) {
        menuMode = OCTAVE;
        menuState=MAIN;
        lcdMainMenu(99);
        }
       else {
        
        if (((project.scene_tempo[project.scene]+v) >= minTempo) && ((project.scene_tempo[project.scene]+v)<=maxTempo)) {

          project.scene_tempo[project.scene] = project.scene_tempo[project.scene] + v;

          // copy the value to the rest of the scenes
          for(byte i = 0;i<NUMBER_SCENES;i++) {
            project.scene_tempo[i] = project.scene_tempo[project.scene];
          }
          
          lcd.setCursor(5,1);
          lcd.print("   ");
          lcd.setCursor(5,1);
          lcd.print(project.scene_tempo[project.scene]);
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

void toggleRotEdit() {
  if (rotEditValue) {
    rotEditValue = false;
  }
  else {
    rotEditValue = true;
  }

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

    
