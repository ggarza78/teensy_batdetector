
/***********************************************************************
 *  TEENSY 3.6 BAT DETECTOR V0.1 20180814
 * 
 *  Copyright (c) 2018, Cor Berrevoets, registax@gmail.com
 *  
 *  TODO: use selectable presets
 * 
 *  HARDWARE USED:
 *     TEENSY 3.6
 *     TEENSY audio board
 *     Ultrasonic microphone with seperate preamplifier connected to mic/gnd on audioboard
 *       eg. Knowles MEMS SPU0410LR5H-QB 
 *     TFT based on ILI9341
 *     2 rotary encoders with pushbutton
 *     2 pushbuttons
 *     SDCard
 * 
*   IMPORTANT: uses the SD card slot of the Teensy, NOT the SD card slot of the audio board 
 * 
 *  4 operational modes: Heterodyne.
 *                       Frequency divider
 *                       Automatic heterodyne (1/10 implemented)
 *                       Automatic TimeExpansion (live)
 *
 *  Sample rates up to 352k
 *  
 *  User controlled parameters:
 *     Volume
 *     Gain
 *     Frequency
 *     Display (none, spectrum, waterfall)
 *     Samplerate
 *     
 *  Record raw data
 *  Play raw data (user selectable) on the SDcard using time_expansion (8, 11, 16,22,32,44k samplerate )
 * 
 * 
 *  Fixes compared to original base:
 *    - issue during recording due to not refilling part of the buffer (was repeating the original first 256 samples )
 *    - filenames have samplerate stored
 *    - RTC added (based on hardware)
 * 
 * **********************************************************************
 *   Based on code by DD4WH 
 * 
 *   https://forum.pjrc.com/threads/38988-Bat-detector
 *   
 *   https://github.com/DD4WH/Teensy-Bat-Detector
 *         
 *   made possible by the samplerate code by Frank Boesing, thanks Frank!
 *   Audio sample rate code - function setI2SFreq  
 *   Copyright (c) 2016, Frank Bösing, f.boesing@gmx.de
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice, development funding notice, and this permission
 * notice shall be included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 * 
 **********************************************************************/

/* CORBEE */
/* TEENSY 3.6 PINSETUP (20180814)

                  GND                  Vin  - PREAMP V+
                   0                   Analog GND
                   1                   3.3V - MEMS MIC
                   2                   23 AUDIO -LRCLK
                   3                   22 AUDIO -TX
                   4                   21 TFT CS
                   5                   20 TFT DC
       AUDIO MEMCS 6                   19 AUDIO - SCL
       AUDIO MOSI  7                   18 AUDIO - SDA
                   8                   17
       AUDIO BCLK  9                   16
       AUDIO SDCS 10                  15 AUDIO -VOL
       AUDIO MCLK 11                  14 AUDIO -SCLK
       AUDIO MISO 12                  13 AUDIO -RX
                  3.3V                GND
                  24                  A22
                  25                  A21
                  26                  39  TFT MISO
        TFT SCLK  27                  38  MICROPUSH_L
        TFT MOSI  28                  37  MICROPUSH_R
     ENC_L-BUTTON 29                  36  ENC_R-BUTTON
     ENC_L A      30                  35  ENC_R A
     ENC_L B      31                  34  ENC_R B
                  32                  33

*/

//#define DEBUG

#define USETFT

//SD1 uses default SDcard Fat, TODO !! SD2 uses faster SDIO library
#define USESD1

#ifdef USESD1
  #define USESD
  #include <SD.h>
  #include "ff.h"       // uSDFS lib
  #include "ff_utils.h" // uSDFS lib
  File root;
  FRESULT rc;        /* Result code */
  FATFS fatfs;      /* File system object */
  FIL fil;        /* File object */
#endif

// TODO: try and see if using the SdFs library is able to write faster 
// started setup and included several #ifdefs inside the audio-library SDrelated files (play_raw play_wav)
#ifdef USESD2
#define USESD
//#include "SdFs.h"

#include "logger_setup.h"


#endif

//default SD related
#ifdef USESD
  #define MAX_FILES    50
  #define MAX_FILE_LENGTH  13   // 8 chars plus 4 for.RAW plus NULL
  char filelist[ MAX_FILES ][ MAX_FILE_LENGTH ];
  int filecounter=0;
  int fileselect=0;
  int referencefile=0;
  //File frec; // audio is recorded to this file first
  int file_number = 0;
#endif

//#include <Time.h>
#include <TimeLib.h>

#include "Audio.h"
//#include <Wire.h>
#include <SPI.h>
#include <Bounce.h>
//#include <Metro.h>

boolean SD_ACTIVE=false;
boolean continousPlay=false;
boolean batTrigger=false;//triggers when an ultrasonic signalpeak is found during FFT
boolean TE_ready=true; //when a TEcall is played this signals the end of the call

time_t getTeensy3Time()
{
  return Teensy3Clock.get();
}
int helpmin; // definitions for time and date adjust - Menu
int helphour;
int helpday;
int helpmonth;
int helpyear;
int helpsec;
uint8_t hour10_old;
uint8_t hour1_old;
uint8_t minute10_old;
uint8_t minute1_old;
uint8_t second10_old;
uint8_t second1_old;
bool timeflag = 0;


uint32_t lastmillis;

#ifdef USETFT

 #define ILI9341
 #ifdef ILI9341
  #include "ILI9341_t3.h"
  #include "font_Arial.h"
  
  #define BACKLIGHT_PIN 255
  #define TOP_OFFSET 90
  #define BOTTOM_OFFSET 20

  #define TFT_DC      20
  #define TFT_CS      21
  #define TFT_RST     255  // 255 = unused. connect to 3.3V

  #define TFT_MOSI    28
  #define TFT_SCLK    27 
  #define TFT_MISO    39
  //#define Touch_CS    8

  ILI9341_t3 tft = ILI9341_t3(TFT_CS, TFT_DC, TFT_RST, TFT_MOSI, TFT_SCLK, TFT_MISO);
  //XPT2046_Touchscreen ts = XPT2046_Touchscreen(Touch_CS);
//predefine menu background etc colors
  #define ENC_MENU_COLOR COLOR_YELLOW
  #define ENC_VALUE_COLOR COLOR_LIGHTGREY
  #define MENU_BCK_COLOR COLOR_DARKRED
  
 #endif

#endif


// this audio comes from the codec by I2S2
AudioInputI2S                    i2s_in; // MIC input
AudioRecordQueue                 recorder; 
AudioSynthWaveformSineHires      sine1; // local oscillator
//AudioSynthWaveformSineHires      sine2; // local oscillator
//
AudioEffectMultiply              heterodyne_multiplier; // multiply = mix
//AudioEffectMultiply              mult2; // multiply = mix

//AudioAnalyzeFFT1024         fft1024_1; // for waterfall display
AudioAnalyzeFFT256               myFFT; // for spectrum display

AudioPlaySdRaw                   player; 

AudioEffectGranular              granular1;

AudioMixer4                      mixFFT;
AudioMixer4                      outputMixer; //selective output
AudioMixer4                      inputMixer; //selective input
AudioOutputI2S                   i2s_out; // headphone output          

AudioConnection mic_toinput         (i2s_in, 0, inputMixer, 0); //microphone signal
AudioConnection mic_torecorder      (i2s_in, 0, recorder, 0); //microphone signal
//AudioConnection mic_topeak (i2s_in, peakRMS);
//AudioConnection mic_topeak1 (i2s_in, peakVal);

AudioConnection player_toinput      (player, 0, inputMixer, 1); //player signal

AudioConnection input_toswitch      (inputMixer,0,  mixFFT,0);

AudioConnection input_todelay       (inputMixer,0, granular1, 0);

AudioConnection switch_toFFT        (mixFFT,0, myFFT,0 ); //raw recording channel 

AudioConnection input_toheterodyne1 (inputMixer, 0, heterodyne_multiplier, 0); //heterodyne 1 signal
AudioConnection sineheterodyne1    (sine1, 0, heterodyne_multiplier, 1);//heterodyne 1 mixerfreq

AudioConnection granular_toout (granular1,0, outputMixer,1);
//AudioConnection input_toheterodyne2 (granular1, 0, mult2, 0); //heterodyne 2
//AudioConnection sineheterodyne2     (sine2, 0, mult2, 1);//heterodyne 2 mixerfreq

AudioConnection heterodyne1_toout      (heterodyne_multiplier, 0, outputMixer, 0);  //heterodyne 1 output to outputmixer
//AudioConnection heterodyne2_toout      (mult2, 0, outputMixer, 1);  //heterodyne 2 output to outputmixer
AudioConnection player_toout           (inputMixer,0, outputMixer, 2);    //direct signal (use with player) to outputmixer

AudioConnection output_toheadphoneleft      (outputMixer, 0, i2s_out, 0); // output to headphone
AudioConnection output_toheadphoneright     (outputMixer, 0, i2s_out, 1);
//AudioConnection granular_toheadphone        (granular1,0,i2s_out,1);

AudioControlSGTL5000        sgtl5000;  

//const int myInput = AUDIO_INPUT_LINEIN;
const int myInput = AUDIO_INPUT_MIC;

#define GRANULAR_MEMORY_SIZE 30000  // enough for 100 ms at 281kHz
int16_t granularMemory[GRANULAR_MEMORY_SIZE];

// forward declaration Stop recording with message 
#ifdef DEBUGSERIAL
   void die(char *str, FRESULT rc);
#endif

extern "C" uint32_t usd_getError(void);

struct tm seconds2tm(uint32_t tt);

//continous timers
elapsedMillis since_bat_detection1; //start timing directly after FFT detects an ultrasound
elapsedMillis since_bat_detection2; //start timing directly after FFT detects the end of the ultrasound
//
elapsedMillis since_heterodyne=1000; //timing interval for auto_heterodyne frequency adjustments
uint16_t callLength=0;
//uint16_t clicker=0;

/************** RECORDING PLAYING SETTINGS *****************/

const int8_t    MODE_DETECT = 0;
const int8_t    MODE_REC = 1;
const int8_t    MODE_PLAY = 2;

int mode = MODE_DETECT; 

#if defined(__MK20DX256__)
  #define BUFFSIZE (8*1024) // size of buffer to be written
#elif defined(__MK66FX1M0__)
  #define BUFF 64
  #define BUFFSIZE (BUFF*1024) // size of buffer to be written

#endif

// buffer to store audiosamples during recording
uint8_t buffern[BUFFSIZE] __attribute__( ( aligned ( 16 ) ) );
//uint8_t buffern2[BUFFSIZE] __attribute__( ( aligned ( 16 ) ) );
uint wr;
uint32_t nj = 0;

#define waterfallgraph 1
#define spectrumgraph 2

int idx_t = 0;
int idx = 0;
int64_t sum;
float32_t mean;
int16_t FFT_bin [128]; 
int16_t FFT_max1 = 0;
uint32_t FFT_max_bin1 = 0;
int16_t FFT_mean1 = 0;
int16_t FFT_max2 = 0;
uint32_t FFT_max_bin2 = 0;
int16_t FFT_mean2 = 0;
//int16_t FFT_threshold = 0;
int16_t FFT_bat [3]; // max of 3 frequencies are being displayed
int16_t index_FFT;
int l_limit;
int u_limit;
int index_l_limit;
int index_u_limit;
//const uint16_t FFT_points = 1024;
const uint16_t FFT_points = 256;

int barm [512];

#define SAMPLE_RATE_MIN               0
#define SAMPLE_RATE_8K                0
#define SAMPLE_RATE_11K               1
#define SAMPLE_RATE_16K               2  
#define SAMPLE_RATE_22K               3
#define SAMPLE_RATE_32K               4
#define SAMPLE_RATE_44K               5
#define SAMPLE_RATE_48K               6
#define SAMPLE_RATE_88K               7
#define SAMPLE_RATE_96K               8
#define SAMPLE_RATE_176K              9
#define SAMPLE_RATE_192K              10
#define SAMPLE_RATE_234K              11
#define SAMPLE_RATE_281K              12
#define SAMPLE_RATE_352K              13
#define SAMPLE_RATE_MAX               13

typedef struct SR_Descriptor
{
    const int SR_n;
    const char* const txt; //display 
    
} SR_Desc;

// SRtext and position for the FFT spectrum display scale
const SR_Descriptor SR [SAMPLE_RATE_MAX + 1] =
{
    //   SR_n ,  f1
    {  SAMPLE_RATE_8K,  "8"}, 
    {  SAMPLE_RATE_11K,  "11"}, 
    {  SAMPLE_RATE_16K,  "16"}, 
    {  SAMPLE_RATE_22K,  "22"}, 
    {  SAMPLE_RATE_32K,  "32"}, 
    {  SAMPLE_RATE_44K,  "44"}, 
    {  SAMPLE_RATE_48K,  "48"},
    {  SAMPLE_RATE_88K,  "88"},
    {  SAMPLE_RATE_96K,  "96"},
    {  SAMPLE_RATE_176K,  "176"},
    {  SAMPLE_RATE_192K,  "192"}, 
    {  SAMPLE_RATE_234K,  "234"}, 
    {  SAMPLE_RATE_281K,  "281"}, 
    {  SAMPLE_RATE_352K,  "352"}
};    

// setup for FFTgraph denoising 
uint32_t FFTcount=0; //count the # of FFTs done 
uint16_t powerspectrumCounter=0;

float FFTavg[128];

float FFTpowerspectrum[128];
float powerspectrum_Max=0;

// defaults at startup functions
int displaychoice=waterfallgraph; //default display
int8_t mic_gain = 35; // start detecting with this MIC_GAIN in dB
int8_t volume=50;

int freq_real = 45000; // start heterodyne detecting at this frequency
int freq_real_backup=freq_real; //used to return to proper settingafter using the play_function

// initial sampling setup
int sample_rate = SAMPLE_RATE_281K;
int sample_rate_real = 281000;
char * SRtext="281";

int last_sample_rate=sample_rate;

float freq_Oscillator =50000;

/************************************************* MENU ********************************/
/***************************************************************************************/

typedef struct Menu_Descriptor
{
    const char* name;
    // ********preset variables below NOT USED YET
    const int len; // length of string to allow right-alignment
    const int def; //default settings
    const int low; // low threshold
    const int high; //high threshold
    
} Menu_Desc;


const int Leftchoices=10; //can have any value
const int Rightchoices=10;
const Menu_Descriptor MenuEntry [Leftchoices] =
{  {"Volume",6,60,0,100}, //divide by 100
   {"Gain",4,30,0,63},
   {"Frequency",9,45,20,90}, //multiply 1000
   {"Display",7,0,0,0},
   {"Denoise",7,0,0,0},
   {"SampleR",6,0,0,0},
   {"Record",6,0,0,0}, //functions where the LeftEncoder 
   {"Play",4,0,0,0},
   {"PlayD",5,0,0,0},
} ;

//TODO constants should be part of the menuentry, a single structure to hold the info
const int8_t  MENU_VOL = 0; //volume
const int8_t  MENU_MIC = 1; //mic_gain
const int8_t  MENU_FRQ = 2; //frequency
const int8_t  MENU_DSP = 3; //display
const int8_t  MENU_DNS = 4; //denoise
const int8_t  MENU_SR  = 5; //sample rate
const int8_t  MENU_REC = 6; //record
const int8_t  MENU_PLY = 7; //play 
const int8_t  MENU_PLD = 8; //play at original rate 

//available modes
const int detector_heterodyne=0;
const int detector_divider=1;
const int detector_Auto_heterodyne=2;
const int detector_Auto_TE=3;
const int detector_passive=4;

//default
int detector_mode=detector_heterodyne;  

//************************* ENCODER variables/constants
const int8_t enc_menu=0; //changing encoder sets menuchoice
const int8_t enc_value=1; //changing encoder sets value for a menuchoice

const int8_t enc_leftside=0; //encoder 
const int8_t enc_rightside=1; //encoder

const int8_t enc_up=1; //encoder goes up
const int8_t enc_nc=0;
const int8_t enc_dn=-1; //encoder goes down

int EncLeft_menu_idx=0; 
int EncRight_menu_idx=0;

int EncLeft_function=0; 
int EncRight_function=0;

/************************** */

// ******** LEFT AND RIGHT ENCODER CONNECTIONS/BUTTONS
#include <Encoder.h>
//try to avoid interrupts as they can (possibly ?) interfere during recording
#define ENCODER_DO_NOT_USE_INTERRUPTS

#define MICROPUSH_RIGHT  37 
Bounce micropushButton_R = Bounce(MICROPUSH_RIGHT, 50); 
#define encoderButton_RIGHT      36    
Bounce encoderButton_R = Bounce(encoderButton_RIGHT, 50); 
Encoder EncRight(34,35);
int EncRightPos=0;
int EncRightchange=0;

#define MICROPUSH_LEFT  38
Bounce micropushButton_L = Bounce(MICROPUSH_LEFT, 50); 
#define encoderButton_LEFT       29
Bounce encoderButton_L = Bounce(encoderButton_LEFT, 50); 
Encoder EncLeft(30,31);
int EncLeftPos=0;
int EncLeftchange=0;

// **END************ LEFT AND RIGHT ENCODER DEFINITIONS

#ifdef USESD1
void die(char *str, FRESULT rc) 
{ 
   #ifdef DEBUGSERIAL
   Serial.printf("%s: Failed with rc=%u.\n", str, rc); for (;;) delay(100); 
   #endif 
   }

//=========================================================================
#endif

#ifdef USESD1
//uint32_t count=0;
uint32_t ifn=0;
uint32_t isFileOpen=0;

TCHAR wfilename[80];
uint32_t t0=0;
uint32_t t1=0;
#endif

char filename[80];

void display_settings() {
  #ifdef USETFT
    
    tft.setTextColor(ENC_MENU_COLOR);
    
    tft.setFont(Arial_16);
    tft.fillRect(0,0,240,TOP_OFFSET-50,MENU_BCK_COLOR);
    tft.fillRect(0,TOP_OFFSET-10,240,10,COLOR_BLACK);
    tft.fillRect(0,ILI9341_TFTHEIGHT-BOTTOM_OFFSET,240,BOTTOM_OFFSET,MENU_BCK_COLOR);

    tft.setCursor(0,0);
    tft.print("g:"); tft.print(mic_gain);
    tft.print(" f:"); tft.print(freq_real);
    tft.print(" v:"); tft.print(volume);
    tft.print(" SR"); tft.print(SRtext);
    tft.setCursor(0,20);
    
    switch (detector_mode) {
       case detector_heterodyne:
         tft.print("HTD"); // 
       break;
       case detector_divider:
         tft.print("FD");
       break;
       case detector_Auto_heterodyne:
         tft.print("Auto_HTD");
       break;
       case detector_Auto_TE:
        tft.print("Auto_TE");
       break;
       case detector_passive:
        tft.print("PASS");
       break;
       default:
        tft.print("error");
       
     }
     // push the cursor to the lower part of the screen
     tft.setCursor(0,ILI9341_TFTHEIGHT-BOTTOM_OFFSET);

     /****************** SHOW ENCODER SETTING ***********************/

     // set the colors according to the function of the encoders
     if (mode==MODE_DETECT ) 
     // show menu selection as menu-active of value-active
      {
       if (EncLeft_function==enc_value) 
        { tft.setTextColor(ENC_MENU_COLOR);
         }
        else
        { tft.setTextColor(ENC_VALUE_COLOR);}

       tft.print(MenuEntry[EncLeft_menu_idx].name);
       tft.print(" "); 

       if (EncRight_function==enc_value) 
         { tft.setTextColor(ENC_MENU_COLOR);} //value is active 
       else
         { tft.setTextColor(ENC_VALUE_COLOR);} //menu is active 
       
       //if MENU on the left-side is PLAY and selected than show the filename
        if ((EncLeft_menu_idx==MENU_PLY) and (EncLeft_function==enc_value))     
           { //tft.print(fileselect); 
             tft.print(filelist[fileselect]);
           }
        else
         if (EncLeft_menu_idx==MENU_REC)      
          // show the filename that will be used for the next recording
           {  sprintf(filename, "B%u_%s.raw", file_number+1,SRtext);
              tft.print(filename );
            }
         else
         if (EncLeft_menu_idx==MENU_SR)
          { tft.print(SR[sample_rate].txt);

          }
          else
          { //tft.print(EncRightchange); 
            tft.setCursor(ILI9341_TFTWIDTH/2  ,ILI9341_TFTHEIGHT-BOTTOM_OFFSET);
            tft.print(MenuEntry[EncRight_menu_idx].name);
          }
    }
    else
      { 
        if (mode==MODE_REC)
          { tft.setTextColor(ENC_VALUE_COLOR);
            tft.print("REC:"); 
            tft.print(filename);
         }
        if (mode==MODE_PLAY) 
         {if (EncLeft_menu_idx==MENU_PLY)
          { tft.setTextColor(ENC_VALUE_COLOR);
            tft.print("PLAY:"); 
            tft.print(filename);
          }
          else
           {tft.setTextColor(ENC_VALUE_COLOR);
            tft.print(MenuEntry[EncLeft_menu_idx].name);
            tft.print(" "); 
            tft.print(MenuEntry[EncRight_menu_idx].name);
      
           }

        }
      }

    //scale every 10kHz  
    float x_factor=10000/(0.5*(sample_rate_real / FFT_points)); 
    int curF=2*int(freq_real/(sample_rate_real / FFT_points));

    int maxScale=int(sample_rate_real/20000);
    for (int i=1; i<maxScale; i++) 
     { tft.drawFastVLine(i*x_factor, TOP_OFFSET-10, 9, ENC_MENU_COLOR);  
     }    
    tft.fillCircle(curF,TOP_OFFSET-4,3,ENC_MENU_COLOR);
    
   #endif
}


void       set_mic_gain(int8_t gain) {
    
    AudioNoInterrupts();
    //sgtl5000.micGainNew (24);
    sgtl5000.micGain (gain);
    //sgtl5000.lineInLevel(gain/4);
    AudioInterrupts();

    display_settings();
    powerspectrum_Max=0; // change the powerspectrum_Max for the FFTpowerspectrum
} // end function set_mic_gain

void       set_freq_Oscillator(int freq) {
    // audio lib thinks we are still in 44118sps sample rate
    // therefore we have to scale the frequency of the local oscillator
    // in accordance with the REAL sample rate
      
    freq_Oscillator = (freq) * (AUDIO_SAMPLE_RATE_EXACT / sample_rate_real); 
    //float F_LO2= (freq+5000) * (AUDIO_SAMPLE_RATE_EXACT / sample_rate_real); 
    // if we switch to LOWER samples rates, make sure the running LO 
    // frequency is allowed ( < 22k) ! If not, adjust consequently, so that
    // LO freq never goes up 22k, also adjust the variable freq_real  
    if(freq_Oscillator > 22000) {
      freq_Oscillator = 22000;
      freq_real = freq_Oscillator * (sample_rate_real / AUDIO_SAMPLE_RATE_EXACT) + 9;
    }
    AudioNoInterrupts();
    //setup multiplier SINE
    sine1.frequency(freq_Oscillator);
    //sine2.frequency(freq_Oscillator);
        
    AudioInterrupts();
    display_settings();
} // END of function set_freq_Oscillator

// set samplerate code by Frank Boesing 
void setI2SFreq(int freq) {
  typedef struct {
    uint8_t mult;
    uint16_t div;
  } tmclk;
//MCLD Divide sets the MCLK divide ratio such that: MCLK output = MCLK input * ( (FRACT + 1) / (DIVIDE + 1) ).
// FRACT must be set equal or less than the value in the DIVIDE field.
//(double)F_PLL * (double)clkArr[iFreq].mult / (256.0 * (double)clkArr[iFreq].div);
//ex 180000000* 1 /(256* 3 )=234375Hz  setting   {1,3} at 180Mhz

  
#if (F_PLL==16000000)
  const tmclk clkArr[numfreqs] = {{16, 125}, {148, 839}, {32, 125}, {145, 411}, {64, 125}, {151, 214}, {12, 17}, {96, 125}, {151, 107}, {24, 17}, {192, 125}, {127, 45}, {48, 17}, {255, 83} };
#elif (F_PLL==72000000)
  const tmclk clkArr[numfreqs] = {{32, 1125}, {49, 1250}, {64, 1125}, {49, 625}, {128, 1125}, {98, 625}, {8, 51}, {64, 375}, {196, 625}, {16, 51}, {128, 375}, {249, 397}, {32, 51}, {185, 271} };
#elif (F_PLL==96000000)
  const tmclk clkArr[numfreqs] = {{8, 375}, {73, 2483}, {16, 375}, {147, 2500}, {32, 375}, {147, 1250}, {2, 17}, {16, 125}, {147, 625}, {4, 17}, {32, 125}, {151, 321}, {8, 17}, {64, 125} };
#elif (F_PLL==120000000)
  const tmclk clkArr[numfreqs] = {{32, 1875}, {89, 3784}, {64, 1875}, {147, 3125}, {128, 1875}, {205, 2179}, {8, 85}, {64, 625}, {89, 473}, {16, 85}, {128, 625}, {178, 473}, {32, 85}, {145, 354} };
#elif (F_PLL==144000000)
  const tmclk clkArr[numfreqs] = {{16, 1125}, {49, 2500}, {32, 1125}, {49, 1250}, {64, 1125}, {49, 625}, {4, 51}, {32, 375}, {98, 625}, {8, 51}, {64, 375}, {196, 625}, {16, 51}, {128, 375} };
#elif (F_PLL==168000000)
  const tmclk clkArr[numfreqs] = {{32, 2625}, {21, 1250}, {64, 2625}, {21, 625}, {128, 2625}, {42, 625}, {8, 119}, {64, 875}, {84, 625}, {16, 119}, {128, 875}, {168, 625}, {32, 119}, {189, 646} };
#elif (F_PLL==180000000)
  const int numfreqs = 17;
  const int samplefreqs[numfreqs] = {  8000,      11025,      16000,      22050,       32000,       44100, (int)44117.64706 , 48000,      88200, (int)44117.64706 * 2,   96000, 176400, (int)44117.64706 * 4, 192000,  234000, 281000, 352800};
  const tmclk clkArr[numfreqs] = {{46, 4043}, {49, 3125}, {73, 3208}, {98, 3125}, {183, 4021}, {196, 3125}, {16, 255},   {128, 1875}, {107, 853},     {32, 255},   {219, 1604}, {1, 4},      {64, 255},     {219,802}, { 1,3 },  {2,5} , {1,2} };  //last value 219 802

#elif (F_PLL==192000000)
  const tmclk clkArr[numfreqs] = {{4, 375}, {37, 2517}, {8, 375}, {73, 2483}, {16, 375}, {147, 2500}, {1, 17}, {8, 125}, {147, 1250}, {2, 17}, {16, 125}, {147, 625}, {4, 17}, {32, 125} };
#elif (F_PLL==216000000)
  const tmclk clkArr[numfreqs] = {{32, 3375}, {49, 3750}, {64, 3375}, {49, 1875}, {128, 3375}, {98, 1875}, {8, 153}, {64, 1125}, {196, 1875}, {16, 153}, {128, 1125}, {226, 1081}, {32, 153}, {147, 646} };
#elif (F_PLL==240000000)
  const tmclk clkArr[numfreqs] = {{16, 1875}, {29, 2466}, {32, 1875}, {89, 3784}, {64, 1875}, {147, 3125}, {4, 85}, {32, 625}, {205, 2179}, {8, 85}, {64, 625}, {89, 473}, {16, 85}, {128, 625} };
#endif

  for (int f = 0; f < numfreqs; f++) {
    if ( freq == samplefreqs[f] ) {
      while (I2S0_MCR & I2S_MCR_DUF) ;
      I2S0_MDR = I2S_MDR_FRACT((clkArr[f].mult - 1)) | I2S_MDR_DIVIDE((clkArr[f].div - 1));
      return;
    }
  }
}


void      set_sample_rate (int sr) {
  switch (sr) {
    case SAMPLE_RATE_8K:
    sample_rate_real = 8000;
    SRtext = " 8";
    break;
    case SAMPLE_RATE_11K:
    sample_rate_real = 11025;
    SRtext = "11";
    break;
    case SAMPLE_RATE_16K:
    sample_rate_real = 16000;
    SRtext = "16";
    break;
    case SAMPLE_RATE_22K:
    sample_rate_real = 22050;
    SRtext = "22";
    break;
    case SAMPLE_RATE_32K:
    sample_rate_real = 32000;
    SRtext = "32";
    break;
    case SAMPLE_RATE_44K:
    sample_rate_real = 44100;
    SRtext = "44";
    break;
    case SAMPLE_RATE_48K:
    sample_rate_real = 48000;
    SRtext = "48";
    break;
    case SAMPLE_RATE_88K:
    sample_rate_real = 88200;
    SRtext = "88k";
    break;
    case SAMPLE_RATE_96K:
    sample_rate_real = 96000;
    SRtext = "96";
    break;
    case SAMPLE_RATE_176K:
    sample_rate_real = 176400;
    SRtext = "176";
    break;
    case SAMPLE_RATE_192K:
    sample_rate_real = 192000;
    SRtext = "192";
    break;
    case SAMPLE_RATE_234K:
    sample_rate_real = 234000;
    SRtext = "234";
    break;
    case SAMPLE_RATE_281K:
    sample_rate_real = 281000;
    SRtext = "281";
    break;
    case SAMPLE_RATE_352K:
    sample_rate_real = 352800;
    SRtext = "352";
    break;
  }
    
    AudioNoInterrupts();
    setI2SFreq (sample_rate_real); 
    delay(200); // this delay seems to be very essential !
    set_freq_Oscillator (freq_real);
    AudioInterrupts();
    delay(20);
    display_settings();
   
} // END function set_sample_rate



void spectrum() { // spectrum analyser code by rheslip - modified
     #ifdef USETFT
     if (myFFT.available()) {
//     if (fft1024_1.available()) {
    int16_t peak=0; uint16_t avgF=0;
    
    // find the BIN corresponding to the current frequency-setting 
    int curF=int(freq_real/(sample_rate_real / FFT_points));

//    for (int i = 0; i < 240; i++) {
    //startup sequence to denoise the FFT
    FFTcount++;
    if (FFTcount==1)
     {for (int16_t i = 0; i < 128; i++) {
         FFTavg[i]=0; 
     }
     }

    if (FFTcount<1000)
     { 
       for (int i = 0; i < 128; i++) {
         FFTavg[i]=FFTavg[i]+abs(myFFT.output[i])*0.001; //0.1% of total values
         }
     }

for (int16_t x = 2; x < 128; x++) {
   avgF=avgF+FFT_bin[x];
   if (FFT_bin[x]>peak)
      {
        peak=FFT_bin[x];   
      }
}

/*
avgF=avgF/128;
//check if the peak is at least 2x higher than the average otherwise set the indicator low
if ((peak-avgF)<(avgF/3))
  { maxF=2;}
  */  
  for (int16_t x = 2; x < 128; x++) {
//  for (uint16_t x = 8; x < 512; x+=4) {
     FFT_bin[x] = (myFFT.output[x]);//-FFTavg[x]*0.9; 
     int colF=ENC_VALUE_COLOR;
     
//     FFT_bin[x/4] = abs(fft1024_1.output[x]); 
     int barnew = (FFT_bin[x])/2 ;
     
     // this is a very simple first order IIR filter to smooth the reaction of the bars
     int bar = 0.05 * barnew + 0.95 * barm[x]; 
     if (bar >(ILI9341_TFTHEIGHT-BOTTOM_OFFSET-TOP_OFFSET)) 
        { bar=(ILI9341_TFTHEIGHT-BOTTOM_OFFSET-TOP_OFFSET);
        }
     if (bar <0) bar=0;
     if (barnew >(ILI9341_TFTHEIGHT-BOTTOM_OFFSET-TOP_OFFSET)) 
        { barnew=(ILI9341_TFTHEIGHT-BOTTOM_OFFSET-TOP_OFFSET);
        }
     int g_x=x*2;
     int spectrumline=barm[x];
     int spectrumline_new=barnew;
          
     //tft.drawFastVLine(g_x,TOP_OFFSET,ILI9341_TFTHEIGHT-BOTTOM_OFFSET-TOP_OFFSET, COLOR_BLACK);
     tft.drawFastVLine(g_x,TOP_OFFSET,spectrumline_new, COLOR_GREEN);
     //tft.drawFastVLine(g_x,TOP_OFFSET,spectrumline, COLOR_RED);
     tft.drawFastVLine(g_x,TOP_OFFSET+spectrumline_new,ILI9341_TFTHEIGHT-BOTTOM_OFFSET-TOP_OFFSET-spectrumline_new, COLOR_BLACK);
     tft.drawFastVLine(g_x+1,TOP_OFFSET,spectrumline, COLOR_DARKGREEN);
     tft.drawFastVLine(g_x+1,TOP_OFFSET+spectrumline,ILI9341_TFTHEIGHT-BOTTOM_OFFSET-TOP_OFFSET-spectrumline, COLOR_BLACK);
    /* if (x==maxF)
       { colF=COLOR_ORANGE;
         tft.drawFastVLine(g_x,TOP_OFFSET,240-bar, colF);
         }
      */   
     
     //tft.drawPixel(g_x,ILI9341_TFTHEIGHT-BOTTOM_OFFSET-bar,colF);

     barm[x] = bar;
  }
  
    // if (mode == MODE_DETECT)  search_bats();     
  } //end if
  if (mode==MODE_PLAY)
    {//float ww=( player.positionMillis()/player.lengthMillis()*240.0);
     tft.drawFastHLine(0,320-BOTTOM_OFFSET-5,240*player.positionMillis()/player.lengthMillis()-10,COLOR_BLACK);
     tft.drawFastHLine(240*player.positionMillis()/player.lengthMillis()-9,320-BOTTOM_OFFSET-5,5,ENC_MENU_COLOR);
     tft.drawFastHLine(0,320-BOTTOM_OFFSET-4,240*player.positionMillis()/player.lengthMillis()-10,COLOR_BLACK);
     tft.drawFastHLine(240*player.positionMillis()/player.lengthMillis()-9,320-BOTTOM_OFFSET-4,5,ENC_MENU_COLOR);
    }
  #endif
}
#ifdef DEBUGSERIAL 

/*void check_processor() {
      if (second.check() == 1) {
      Serial.print("Proc = ");
      Serial.print(AudioProcessorUsage());
      Serial.print(" (");    
      Serial.print(AudioProcessorUsageMax());
      Serial.print("),  Mem = ");
      Serial.print(AudioMemoryUsage());
      Serial.print(" (");    
      Serial.print(AudioMemoryUsageMax());
      Serial.println(")");
 
      AudioProcessorUsageMaxReset();
      AudioMemoryUsageMaxReset();
    }


}
*/ // END function check_processor
#endif



void waterfall(void) // thanks to Frank B !
{ 
  
#ifdef USETFT

// code for 256 point FFT 
     
 if (myFFT.available()) {
  const uint16_t Y_OFFSET = TOP_OFFSET;
  static int count = TOP_OFFSET;
  //int curF=int(freq_real/(sample_rate_real / FFT_points));

  // lowest frequencybin to detect as a batcall
  int batCall_LoF_bin= int(30000/(sample_rate_real / FFT_points));
  int batCall_HiF_bin= int(80000/(sample_rate_real / FFT_points));

  uint16_t FFT_pixels[240]; // maximum of 240 pixels, each one is the result of one FFT 
  FFT_pixels[0]=0; FFT_pixels[1]=0;  FFT_pixels[2]=0; FFT_pixels[3]=0;
  
    FFTcount++;

    //requested to start with a clean FFTavg array to denoise
    if (FFTcount==1)
       {for (int16_t i = 0; i < 128; i++) {
          FFTavg[i]=0; 
       }
     }

    // collect 1000 FFT samples for the denoise array
    if (FFTcount<100)
     { for (int i = 2; i < 128; i++) {
         //FFTavg[i]=FFTavg[i]+myFFT.read(i)*65536.0*5*0.001; //0.1% of total values
         FFTavg[i]=FFTavg[i]+myFFT.output[i]*10*0.001; //0.1% of total values
         }
     }
    

    int FFT_peakF_bin=0; 
    int peak=512;
    int avgFFTbin=0;
    // there are 128 FFT different bins only 120 are shown on the graphs  
    
    for (int i = 2; i < 120; i++) { 
      int val = myFFT.output[i]*10 -FFTavg[i]*0.9 + 10; //v1
      avgFFTbin+=val;
      //detect the peakfrequency
      if (val>peak)
       { peak=val; 
         FFT_peakF_bin=i;
        }
       if (val<5) 
           {val=5;}

       FFT_pixels[i*2] = tft.color565(
              min(255, val/2),
              (val/6>255)? 255 : val/6,
              //(val/4>255)? 255 : val/4
                            0
              //((255-val)>>1) <0? 0: (255-val)>>1 
             ); 
       
      FFT_pixels[i*2+1]=FFT_pixels[i*2];       
    }
    avgFFTbin=avgFFTbin/120;
    //mark the peak
    //FFT_pixels[FFT_peakF_bin*2]=COLOR_RED;
    //FFT_pixels[FFT_peakF_bin*2+1]=COLOR_RED;
   if ((peak/avgFFTbin)<1.2) //very low peakvalue so probably noise
     { FFT_peakF_bin=0; 
     }

  int powerSpectrum_Maxbin=0;
  // detected a peak in the bat frequencies
  if ((FFT_peakF_bin>batCall_LoF_bin) and (FFT_peakF_bin<batCall_HiF_bin))
  {
    //collect data for the powerspectrum 
    for (int i = 2; i < 120; i++)
     { 
        //add new samples
        FFTpowerspectrum[i]+=myFFT.output[i];
        //keep track of the maximum
        if (FFTpowerspectrum[i]>powerspectrum_Max) 
           { powerspectrum_Max=FFTpowerspectrum[i];
             powerSpectrum_Maxbin=i;
           }

     }
     //keep track of the no of samples with bat-activity
     powerspectrumCounter++;
  }
    // update display after every 100th FFT sample with bat-activity
    if ((powerspectrumCounter>50)  )
       { powerspectrumCounter=0;
         //clear powerspectrumbox
         tft.fillRect(0,TOP_OFFSET-50,240,45, COLOR_BLACK);
         // keep a minimum maximumvalue to the powerspectrum
         int binLo=2; int binHi=0;

         for (int i=2; i<120; i++)
          {             
            int ypos=FFTpowerspectrum[i]/powerspectrum_Max*45; 

            // first encounter of 1/20 of maximum
            if (i<powerSpectrum_Maxbin)
              {if (FFTpowerspectrum[i]<(powerspectrum_Max*0.1))
                    {binLo=i;}}
            else
              {if (FFTpowerspectrum[i]>(powerspectrum_Max*0.1))
                    {binHi=i;}
              }

            tft.drawFastVLine(i*2,TOP_OFFSET-ypos-6,ypos,COLOR_RED);
            if (i==powerSpectrum_Maxbin)                        
              { tft.drawFastVLine(i*2,TOP_OFFSET-ypos-6,ypos,ENC_MENU_COLOR);
               }
            
            //tft.drawFastVLine(i*2+1,TOP_OFFSET-ypos-6,ypos,COLOR_RED);
            FFTpowerspectrum[i]=0;
          }
         
         //tft.setCursor(0,TOP_OFFSET-45);
         //tft.print(powerspectrum_Max);
         if (powerspectrum_Max==20000)
          {binLo=0; binHi=0;
          }
         float multiplier=(sample_rate_real / FFT_points)*0.001;
         powerspectrum_Max=powerspectrum_Max*0.5; //lower the max after a graphupdate
         tft.setCursor(140,TOP_OFFSET-45);
         tft.setTextColor(ENC_VALUE_COLOR);
         tft.print(int(binLo*multiplier) );
         tft.print(" ");
         tft.setTextColor(ENC_MENU_COLOR);
         tft.print(int(powerSpectrum_Maxbin*multiplier) );
         tft.print(" ");
         tft.setTextColor(ENC_VALUE_COLOR);
         tft.print(int(binHi*multiplier) );
        
       }
      
    
    if ((FFT_peakF_bin>batCall_LoF_bin) and (FFT_peakF_bin<batCall_HiF_bin)) // we got a high-frequent signal peak
      { 
        // when a batcall is first discovered 
        if (not batTrigger) 
          { since_bat_detection1=0; //start of the call mark
            //clicker=0;
            FFT_pixels[5]=ENC_VALUE_COLOR; // mark the start on the screen
            FFT_pixels[6]=ENC_VALUE_COLOR;
            FFT_pixels[7]=ENC_VALUE_COLOR;
            
            if (detector_mode==detector_Auto_heterodyne)
               if (since_heterodyne>1000) //update the most every second
                {freq_real=int((FFT_peakF_bin*(sample_rate_real / FFT_points)/500))*500; //round to nearest 500hz
                 set_freq_Oscillator(freq_real); 
                 since_heterodyne=0;
                 //granular1.stop();
                }
            
            //restart the TimeExpansion only if the previous call was played
            if ((detector_mode==detector_Auto_TE) and (TE_ready) )
             { granular1.stop();
               granular1.beginTimeExpansion(GRANULAR_MEMORY_SIZE);
               granular1.setSpeed(0.05);
               TE_ready=false;
               
             }
                      
          }
         //clicker++; 
         batTrigger=true;
         
     }
   else // FFT_peakF_bin does not show a battcall  
        { 
          if (batTrigger) //previous sample was still a call
           { callLength=since_bat_detection1; // got a pause so store the time since the start of the call
             
             /*if (callLength>20) //call is too long 
              { TE_ready=true; // break the TE replay
                } 
              */  
              since_bat_detection2=0; //start timing the length of the replay
             }
          batTrigger=false;
        }    
    // restart TimeExpansion recording a bit after the call has finished completely
    if ((!TE_ready) and (since_bat_detection2>(callLength*10)))
      { //stop the time expansion
        TE_ready=true;
        granular1.stopTimeExpansion();
       
      }
    

    if (since_bat_detection2<50) //keep scrolling 100ms after the last bat-call
      {  tft.writeRect( 0,count, ILI9341_TFTWIDTH,1, (uint16_t*) &FFT_pixels); //show a line with spectrumdata
         tft.setScroll(count);
        count++;
        
      } 


    if (count >= ILI9341_TFTHEIGHT-BOTTOM_OFFSET) count = Y_OFFSET;
    

  }
#endif

}

void startRecording() {
  mode = MODE_REC;
  #ifdef USESD1
  
    #ifdef DEBUGSERIAL
      Serial.print("startRecording");
    #endif  
    
    // close file
    if(isFileOpen)
    {
      //close file
      rc = f_close(&fil);
      if (rc) die("close", rc);
      isFileOpen=0;
    }
  
  if(!isFileOpen)
  {
  file_number++;
  //automated filename BA_S.raw where A=file_number and S shows samplerate. Has to fit 8 chars
  // so max is B999_192.raw
  sprintf(filename, "B%u_%s.raw", file_number, SRtext);
    #ifdef DEBUGSERIAL
    Serial.println(filename);
    #endif  
  char2tchar(filename, 13, wfilename);
  filecounter++;
  strcpy(filelist[filecounter],filename );

  rc = f_stat (wfilename, 0);
  #ifdef DEBUGSERIAL
    Serial.printf("stat %d %x\n",rc,fil.obj.sclust);
 #endif   
  rc = f_open (&fil, wfilename, FA_WRITE | FA_CREATE_ALWAYS);
#ifdef DEBUGSERIAL
    Serial.printf(" opened %d %x\n\r",rc,fil.obj.sclust);
#endif 
    // check if file has errors
    if(rc == FR_INT_ERR)
    { // only option then is to close file
        rc = f_close(&fil);
        if(rc == FR_INVALID_OBJECT)
        { 
          #ifdef DEBUGSERIAL
          Serial.println("unlinking file");
          #endif
          rc = f_unlink(wfilename);
          if (rc) {
            die("unlink", rc);
          }
        }
        else
        {
          die("close", rc);
        }
    }
    // retry open file
    rc = f_open(&fil, wfilename, FA_WRITE | FA_CREATE_ALWAYS);
    if(rc) { 
      die("open", rc);
    }
    isFileOpen=1;
  }

  #endif

  //clear the screen completely
  tft.fillRect(0,0,ILI9341_TFTWIDTH,ILI9341_TFTHEIGHT,COLOR_BLACK);
  tft.setTextColor(ENC_VALUE_COLOR);
  tft.setFont(Arial_28);
  tft.setCursor(0,100);
  tft.print("RECORDING");
  tft.setFont(Arial_16);
  
  display_settings();
  
  granular1.stop(); //stop granular

  //switch off several circuits
  mixFFT.gain(0,0);
  
  outputMixer.gain(1,0);  //shutdown granular output      
  
  detector_mode=detector_heterodyne;

  outputMixer.gain(0,1); 
  
  nj=0;
  recorder.begin();
    
}

void continueRecording() {
  #ifdef USESD1
  const uint32_t N_BUFFER = 2;
  const uint32_t N_LOOPS = BUFF*N_BUFFER; // !!! NLOOPS and BUFFSIZE ARE DEPENDENT !!! NLOOPS = BUFFSIZE/N_BUFFER
  // buffer size total = 256 * n_buffer * n_loops
  // queue: write n_buffer blocks * 256 bytes to buffer at a time; free queue buffer;
  // repeat n_loops times ( * n_buffer * 256 = total amount to write at one time)
  // then write to SD card
    
  if (recorder.available() >= N_BUFFER  )
  {// one buffer = 256 (8bit)-bytes = block of 128 16-bit samples
    //read N_BUFFER sample-blocks into memory
    for (int i = 0; i < N_BUFFER; i++) {
       //copy a new bufferblock from the audiorecorder into memory
       memcpy(buffern + i*256 + nj * 256 * N_BUFFER, recorder.readBuffer(), 256);
       //free the last buffer that was read
       recorder.freeBuffer();
       } 

    nj++; 

    if (nj >  (N_LOOPS-1)) 
    {
      nj = 0;
      //old code used to copy into a 2nd buffer, not needed since the writing to SD of the buffer seems faster than the filling 
      //this allows larger buffers to be used
      //memcpy(buffern2,buffern,BUFFSIZE);  
      //push to SDcard  
      rc =  f_write (&fil, buffern, N_BUFFER * 256 * N_LOOPS, &wr);
      }
  }
  #endif
}

void stopRecording() {
#ifdef USESD1
  #ifdef DEBUGSERIAL  
    Serial.print("stopRecording");
  #endif  
    recorder.end();
    if (mode == MODE_REC) {
      while (recorder.available() > 0) {
      rc = f_write (&fil, (byte*)recorder.readBuffer(), 256, &wr);
  //      frec.write((byte*)recorder.readBuffer(), 256);
        recorder.freeBuffer();
      }
        //close file
        rc = f_close(&fil);
        if (rc) die("close", rc);
        //
        isFileOpen=0;
  //    frec.close();
  //    playfile = recfile;
    }
    
    mode = MODE_DETECT;
  //  clearname();
  #ifdef DEBUGSERIAL  
    Serial.println (" Recording stopped!");
  #endif 
#endif
  //switch on FFT
  tft.fillScreen(COLOR_BLACK);
  mixFFT.gain(0,1); 
      
}

void startPlaying(int SR) {
//      String NAME = "Bat_"+String(file_number)+".raw";
//      char fi[15];
//      NAME.toCharArray(fi, sizeof(NAME));

inputMixer.gain(0,0); //switch off the mic-line as input
inputMixer.gain(1,1); //switch on the playerline as input

if (EncLeft_menu_idx==MENU_PLY) 
  {
      outputMixer.gain(2,1);  //player to output 
      outputMixer.gain(1,0);  //shutdown granular output      
      outputMixer.gain(0,0);  //shutdown heterodyne output
      EncRight_menu_idx=MENU_SR;
      EncRight_function=enc_value;
      freq_real_backup=freq_real; //keep track of heterodyne setting
  }
  //direct play is used to test functionalty based on previous recorded data
  //this will play a previous recorded raw file through the system as if it were live data coming from the microphone
if (EncLeft_menu_idx==MENU_PLD) 
  {
      outputMixer.gain(2,0);  //shutdown direct audio from player to output 
      outputMixer.gain(0,1);  //default mode will be heterodyne based output 
      if (detector_mode==detector_Auto_TE)
         { outputMixer.gain(1,1);  //start granular output processing      
           outputMixer.gain(0,0);
         }
  }

//allow settling
  delay(100);
  
  //keep track of the sample_rate 
  last_sample_rate=sample_rate;
  SR=constrain(SR,SAMPLE_RATE_MIN,SAMPLE_RATE_MAX);
  set_sample_rate(SR);
  
  fileselect=constrain(fileselect,0,filecounter);
  strncpy(filename, filelist[fileselect],  13);
  
  //default display is waterfall
  displaychoice=waterfallgraph;
  display_settings();

  player.play(filename);
  mode = MODE_PLAY;

}
  

void stopPlaying() {
  
#ifdef DEBUGSERIAL      
  Serial.print("stopPlaying");
#endif  
  if (mode == MODE_PLAY) player.stop();
  mode = MODE_DETECT;
#ifdef DEBUGSERIAL      
  Serial.println (" Playing stopped");
#endif  
  
  //restore last sample_rate setting
  set_sample_rate(last_sample_rate);
if (EncLeft_menu_idx==MENU_PLY)
{
  freq_real=freq_real_backup;
  //restore heterodyne frequency
  set_freq_Oscillator (freq_real);
}
  outputMixer.gain(2,0); //stop the direct line output
  outputMixer.gain(1,1); // open granular output
  outputMixer.gain(0,1); // open heterodyne output  

  inputMixer.gain(0,1); //switch on the mic-line
  inputMixer.gain(1,0); //switch off the playerline

}


void continuePlaying() {
  //the end of file was reached
  if (!player.isPlaying()) {
    stopPlaying();
    if (continousPlay) //keep playing until stopped by the user
      { startPlaying(SAMPLE_RATE_176K);
      }
  }
}

void changeDetector_mode()
{
  if (detector_mode==detector_heterodyne)
         { granular1.stop(); //stop other detecting routines
           outputMixer.gain(1,0);  //stop granular output      
           outputMixer.gain(0,1);  //start heterodyne output
          //switch menu to volume/frequency
           EncLeft_menu_idx=MENU_VOL;
           EncLeft_function=enc_value;
           EncRight_menu_idx=MENU_FRQ;
           EncRight_function=enc_value;
          
         } 
      if (detector_mode==detector_divider)
         { granular1.beginDivider(GRANULAR_MEMORY_SIZE);
           outputMixer.gain(1,1);  //start granular output      
           outputMixer.gain(0,0);  //shutdown heterodyne output
      
           //switch menu to volume/gain
           EncLeft_menu_idx=MENU_VOL;
           EncLeft_function=enc_value;
           EncRight_menu_idx=MENU_MIC;
           EncRight_function=enc_value;

         }  

      if (detector_mode==detector_Auto_TE)
         { granular1.beginTimeExpansion(GRANULAR_MEMORY_SIZE);
           outputMixer.gain(1,1);  //start granular output      
           outputMixer.gain(0,0);  //shutdown heterodyne output
           granular1.setSpeed(0.06); //default TE is 1/0.06 ~ 1/16 :TODO, switch from 1/x floats to divider value x
           //switch menu to volume/gain
           EncLeft_menu_idx=MENU_VOL;
           EncLeft_function=enc_value;
           EncRight_menu_idx=MENU_MIC;
           EncRight_function=enc_value;

         }  
      if (detector_mode==detector_Auto_heterodyne)
         { granular1.stop(); 
           outputMixer.gain(1,0);  //stop granular output      
           outputMixer.gain(0,1);  //start heterodyne output
      
           //switch menu to volume/gain
           EncLeft_menu_idx=MENU_VOL;
           EncLeft_function=enc_value;
           EncRight_menu_idx=MENU_MIC;
           EncRight_function=enc_value;

           
         }  

      if (detector_mode==detector_passive)
         { granular1.stop(); //stop all other detecting routines
           outputMixer.gain(2,1);  //direct line to output 
           outputMixer.gain(1,0);  //shutdown granular output      
           outputMixer.gain(0,0);  //shutdown heterodyne output
           //switch menu to volume/gain
           EncLeft_menu_idx=MENU_VOL;
           EncLeft_function=enc_value;
           EncRight_menu_idx=MENU_MIC;
           EncRight_function=enc_value;

         }
       else //all other options use the heterodyne and granular output line
        {  outputMixer.gain(2,0);  //shut down direct line to output 
           //outputMixer.gain(1,1);  //granular   output      
           //outputMixer.gain(0,1);  //heterodyne output
 
        } 

}

//*****************************************************update encoders
void updateEncoder(uint8_t Encoderside )
 {
   
  /************************setup vars*************************/
   int encodermode=-1; // menu=0 value =1;
   int change=0;
   int menu_idx=0;
   int choices=0;

    //get encodermode 
   if (Encoderside==enc_leftside)
    { encodermode=EncLeft_function;
      change=EncLeftchange;
      menu_idx=EncLeft_menu_idx;
      choices=Leftchoices; //available menu options
    }

   if (Encoderside==enc_rightside)
    { encodermode=EncRight_function;
      change=EncRightchange;
      menu_idx=EncRight_menu_idx;
      choices=Rightchoices; //available menu options
    } 


  /************************proces changes*************************/
  //encoder is in menumode
  if (encodermode==enc_menu)
    { menu_idx=menu_idx+change;
            
      //allow revolving choices
      if (menu_idx<0)
        {menu_idx=choices-1;}
      if (menu_idx>=choices)
        {menu_idx=0;}
      //remove functionality when SD is not active, so no SDCARD mounted or SDCARD is unreadable  
      if (!SD_ACTIVE)
        { if ((menu_idx==MENU_PLD) or (menu_idx==MENU_PLY) or (menu_idx==MENU_REC))
           { // move menu to volume
             menu_idx=MENU_VOL;
           }
        }

      if (Encoderside==enc_leftside)
          { EncLeft_menu_idx=menu_idx; //limit the menu 
               }
     
     //limit the changes of the rightside encoder for specific functions
      if ((EncLeft_menu_idx!=MENU_SR) )      
        if (Encoderside==enc_rightside)
          { EncRight_menu_idx=menu_idx; //limit the menu 
               }
    }
        
  //encoder is in valuemode and has changed position
  if ((encodermode==enc_value) and (change!=0))
    { 
      /******************************VOLUME  ***************/
      if (menu_idx==MENU_VOL)
        { volume+=change;
          volume=constrain(volume,0,90);
          float V=volume*0.01;
          AudioNoInterrupts();
          sgtl5000.volume(V);
          AudioInterrupts();
        }
      /******************************MIC_GAIN  ***************/
      if (menu_idx==MENU_MIC)
        {
         mic_gain+=change;
         mic_gain=constrain(mic_gain,0,63);
         set_mic_gain(mic_gain);
         FFTcount=0; //start denoise after changing gain
         //reset FFTdenoise array
         {for (int16_t i = 0; i < 128; i++) {
           FFTavg[i]=0; 
         }}  
        }
      /******************************FREQUENCY  ***************/
      if (menu_idx==MENU_FRQ)
         { int delta=500;
           uint32_t currentmillis=millis();
           //when turning the encoder fast make the change larger
           if ((currentmillis-lastmillis)<500)
              { delta=1000;}
           if ((currentmillis-lastmillis)<250)
              { delta=2000;}
           if ((currentmillis-lastmillis)<100)
              { delta=5000;}

          freq_real=freq_real+delta*change;
          // limit the frequencies to 500hz steps
          freq_real=constrain(freq_real,7000,int(sample_rate_real/2000)*1000-1000);
          set_freq_Oscillator (freq_real);
          lastmillis=millis();
         }
      /******************************DENOISE  ***************/
      if (menu_idx==MENU_DNS)
        { // setting FFTcount to 0 activates a 1000 sample denoise
          FFTcount=0;
        }
      
      /******************************DISPLAY  ***************/
    
      if (menu_idx==MENU_DSP)
         { 
           displaychoice+=change;
           displaychoice=displaychoice%3; //limit to 0(none),1(spectrum),2(waterfall)
           if (displaychoice==waterfallgraph) 
              {
               tft.setRotation( 0 );
            }
           if (displaychoice==spectrumgraph) 
             { 
            tft.setScroll(0);
            tft.setRotation( 0 );
              }
            tft.fillScreen(COLOR_BLACK);
        }

      

/************** SPECIAL MODES WHERE THE LEFTENCODER SETS A FUNCTION AND THE RIGHT ENCODER SELECTS */
      
      /******************************SAMPLE_RATE  ***************/
      if (EncLeft_menu_idx==MENU_SR)  //only selects a possible sample_rate, user needs to press a button to SET sample_rate
        { sample_rate+=EncRightchange;
          sample_rate=constrain(sample_rate,SAMPLE_RATE_MIN,SAMPLE_RATE_MAX);

          
        }   

      
      /******************************SELECT A FILE  ***************/
      if ((EncLeft_menu_idx==MENU_PLY) and (EncRight_menu_idx==MENU_PLY) and (EncRight_function==enc_value))//menu play selected on the left and right 
         {  
           if (mode!=MODE_PLAY)
            {
             fileselect+=EncRightchange; 
             fileselect=constrain(fileselect,0,filecounter-1);
            
            }   
         }  

      /******************************CHANGE SR during PLAY  ***************/
      if ((EncLeft_menu_idx==MENU_PLY) and (EncRight_menu_idx==MENU_SR) and (EncRight_function==enc_value))//menu play selected on the left and right    
          {
           if (mode==MODE_PLAY)
              {
                 sample_rate+=EncRightchange;
                 sample_rate=constrain(sample_rate,SAMPLE_RATE_8K,SAMPLE_RATE_44K);
                 set_sample_rate(sample_rate);
                 
              }

        }

    }

 }

void updateEncoders()
{
//only react to changes large enough (depending on the steps of the encoder for one rotation)
 long EncRightnewPos = EncRight.read()/4;
 if (EncRightnewPos>EncRightPos)
   { EncRightchange=enc_up; }
   else
   if (EncRightnewPos<EncRightPos)
   { EncRightchange=enc_dn; }
   else
   { EncRightchange=enc_nc; }

 if (EncRightchange!=0)
    {updateEncoder(enc_rightside);
     } 

 EncRightPos=EncRightnewPos;
 
 long EncLeftnewPos = EncLeft.read()/4;
 if (EncLeftnewPos>EncLeftPos)
   { EncLeftchange=enc_up; }
   else
   if (EncLeftnewPos<EncLeftPos)
   { EncLeftchange=enc_dn; }
   else
   { EncLeftchange=enc_nc; }

 if (EncLeftchange!=0)
    {updateEncoder(enc_leftside);
    } 
 
 EncLeftPos=EncLeftnewPos;
 //update display only if a change has happened
 if ((EncRightchange!=0) or (EncLeftchange!=0))
      display_settings();

}

void updateButtons()
{
 // Respond to button presses
   
 // try to make the interrupts as short as possible when recording
 if (mode==MODE_REC) 
   {
     encoderButton_L.update(); //check the left encoderbutton
      if ((encoderButton_L.risingEdge())  )
       { stopRecording();
          EncLeft_function=enc_menu; //force into active-menu
          display_settings();      
       }
   }
 else // not MODE_REC
  {
  // Respond to button presses
 
  encoderButton_L.update();
  encoderButton_R.update();

  micropushButton_L.update();
  micropushButton_R.update();
    
   /*RIGHT MICROPUSH */
  if (micropushButton_R.risingEdge()) {
        detector_mode++;
        if (detector_mode>detector_passive)
          {detector_mode=0;}
        changeDetector_mode();
        display_settings();      
    }
/*LEFT MICROPUSH */
  if (micropushButton_L.risingEdge()) {
      //no function yet
    }



  /************  LEFT ENCODER BUTTON *******************/
  if (encoderButton_L.risingEdge()) {

      EncLeft_function=!EncLeft_function;
        
      //*SPECIAL MODES that change rightside Encoder based on leftside Encoder menusetting
      //****************************************SAMPLERATE
      if ((EncLeft_menu_idx==MENU_SR) and (EncLeft_function==enc_value))
       { EncRight_menu_idx=MENU_SR;
         EncRight_function=enc_value; // set the rightcontroller to select
       }
      
     if (SD_ACTIVE)
     {
        /*if (mode==MODE_REC)
                 { stopRecording();
                   EncLeft_function=enc_menu; //force into active-menu
                 }*/
        if ((mode==MODE_PLAY) and ((EncLeft_menu_idx==MENU_PLY) or (EncLeft_menu_idx==MENU_PLD)))
                 { stopPlaying();
                   EncLeft_menu_idx=MENU_PLY;
                   EncLeft_function=enc_menu; //force into active-menu
                   continousPlay=false;
                 }
               
        //Direct play menu got choosen, now change the rightencoder to choose a file
        if ((EncLeft_menu_idx==MENU_PLD) and (EncLeft_function==enc_value))
         { 
           EncRight_menu_idx=MENU_FRQ ;
           EncRight_function=enc_value; // set the rightcontroller to select
           
         }

        //play menu got choosen, now change the rightencoder to choose a file
        if ((EncLeft_menu_idx==MENU_PLY) and (EncLeft_function==enc_value))
         { 
           EncRight_menu_idx=MENU_PLY ;
           EncRight_function=enc_value; // set the rightcontroller to select
           
         }
       
     
     } //END SD_ACTIVE
     display_settings();      
  }

/************  RIGHT ENCODER BUTTON *******************/

 if (encoderButton_R.risingEdge()) {
    
    EncRight_function=!EncRight_function; //switch between menu/value control
        
    //when EncLeftoder is SR menu than EncRightoder can directly be setting the samplerate at a press 
    //the press should be a transfer from enc_value to enc_menu before it gets activated
    if ( (EncLeft_menu_idx==MENU_SR) and (EncRight_menu_idx==MENU_SR) and (EncRight_function==enc_menu))
    { set_sample_rate(sample_rate);
      last_sample_rate=sample_rate;

    }
    
    //recording and playing are only possible with an active SD card
    if (SD_ACTIVE)
     {
      /*if (mode==MODE_REC)
             { stopRecording();
               EncRight_function=enc_menu; //force into active-menu
              }
          else*/
           if (EncLeft_menu_idx==MENU_REC)
            {
              if (mode == MODE_DETECT) 
                 startRecording();
              }  
      else
      if (mode==MODE_PLAY)
             {
              if (not continousPlay) 
                { stopPlaying();
                  EncLeft_menu_idx=MENU_PLY;
                  EncLeft_function=enc_menu;
                }
             }     
      else       
      if (mode==MODE_DETECT)   
       { if (EncLeft_menu_idx==MENU_PLY)
            { last_sample_rate=sample_rate; 
              startPlaying(SAMPLE_RATE_8K);
             }
          
          if (EncLeft_menu_idx==MENU_PLD)
              {  fileselect=referencefile;
                 continousPlay=true;
                 last_sample_rate=sample_rate;
                 startPlaying(SAMPLE_RATE_176K);
              }  
       }
     }
      
      display_settings();
  }

  }
}



/********************************************************* */

void setup() {
 #ifdef DEBUGSERIAL
  Serial.begin(115200);
 #endif  
  delay(200);
 
//setup Encoder Buttonpins with pullups
  pinMode(encoderButton_RIGHT,INPUT_PULLUP);
  pinMode(encoderButton_LEFT,INPUT_PULLUP);

  pinMode(MICROPUSH_RIGHT,INPUT_PULLUP);
  pinMode(MICROPUSH_LEFT,INPUT_PULLUP);
 
 // EncLeft.write(10000); //set default far away from 0 to avoid negative transitions
  //EncRight.write(10000); //set default far away from 0 to avoid negative transitions

//startup menu
  EncLeft_menu_idx=MENU_VOL;
  EncRight_menu_idx=MENU_FRQ;
  EncLeft_function=enc_menu;
  EncRight_function=enc_menu;

  // Audio connections require memory. 
  AudioMemory(300);

  setSyncProvider(getTeensy3Time);

// Enable the audio shield. select input. and enable output
  sgtl5000.enable();
  sgtl5000.inputSelect(myInput);
  
  sgtl5000.volume(0.45);
  sgtl5000.micGain (mic_gain);
  //sgtl5000.adcHighPassFilterDisable(); // does not help too much!
  sgtl5000.lineInLevel(0);
  mixFFT.gain(0,1);

// Init TFT display  
#ifdef USETFT
  tft.begin();
  //ts.begin();
  tft.setRotation( 0 );
  tft.fillScreen(COLOR_BLACK);

  tft.setCursor(0, 0);
  tft.setScrollarea(TOP_OFFSET,BOTTOM_OFFSET);
  display_settings();
  tft.setCursor(80,50);
  tft.setFont(Arial_24);
  char tstr[9];
  snprintf(tstr,9, "%02d:%02d:%02d",  hour(), minute(), second() );
  tft.print(tstr);
  delay(2000); //wait a second to clearly show the time 
#endif

   //Init SD card use
// uses the SD card slot of the Teensy, NOT that of the audio boards
// this init only for playback
#ifdef USESD1
  if(!(SD.begin(BUILTIN_SDCARD))) 
  {
      #ifdef DEBUGSERIAL
          Serial.println("Unable to access the SD card");
          delay(500);  
      #endif     
      
    SD_ACTIVE=false;
    tft.fillCircle(70,50,5,COLOR_RED);
     
  }
  else
  { 
    SD_ACTIVE=true;
    tft.fillCircle(70,50,5,COLOR_GREEN);
    filecounter=0;
    root = SD.open("/");


    //TODO: check if file is a RAW file and also read the SAMPLERATE
    while (true) {
        
        File entry =  root.openNextFile();
        if (! entry) {
          // no more files
          tft.setCursor(0,50);
          tft.print(filecounter);
          break;
        }
        
        if (entry.isDirectory()) {
          // do nothing, only look for raw files in the root
        } else 
        {
          
        strcpy(filelist[filecounter],entry.name() );
      
        if (String(entry.name())=="TEST_176.RAW")
           {referencefile=filecounter;
            }
        filecounter++;  
          
        }
        entry.close();
       }

    }


if (SD_ACTIVE)
// Recording on SD card by uSDFS library
  {f_mount (&fatfs, (TCHAR *)_T("0:/"), 0);      /* Mount/Unmount a logical drive */
   for (int i=0; i<filecounter; i++)
     {   
       #ifdef DEBUGSERIAL
          #ifdef USETFT
            tft.setCursor(0,50+i*20);
            tft.print(filelist[i]);
          #endif
      #endif    

     }
     file_number=filecounter+1;

  }

#endif

#ifdef USESD2
 uSD.init();

#endif

set_sample_rate (sample_rate);
set_freq_Oscillator (freq_real);
inputMixer.gain(0,1); //microphone active
inputMixer.gain(1,0); //player off

outputMixer.gain(0,1); // heterodyne1 to output 
outputMixer.gain(1,0); // granular to output off
outputMixer.gain(2,0); // player to output off

// the Granular effect requires memory to operate
granular1.begin(granularMemory, GRANULAR_MEMORY_SIZE);

// reset the FFT denoising array at startup
for (int16_t i = 0; i < 128; i++) {
  FFTavg[i]=0; 
    }
} // END SETUP


void loop() {
// If we're playing or recording, carry on...
  if (mode == MODE_REC) {
    continueRecording();
    
  } 
  
  if (mode == MODE_PLAY) {
    continuePlaying();
  }

updateButtons();   
// during recording only the left encoders button is used and screens are not updated
if (mode!=MODE_REC)
{
 updateEncoders();
 #ifdef USETFT
   if (displaychoice==waterfallgraph) 
    { waterfall();
      
     }
   else
    if (displaychoice==spectrumgraph)
    {  spectrum();
     }
 #endif
 }   

}

