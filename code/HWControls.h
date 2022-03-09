// This optional setting causes Encoder to use more optimized code,
// It must be defined before Encoder.h is included.
#define ENCODER_OPTIMIZE_INTERRUPTS
#include <Encoder.h>
#include <Bounce.h>
#include <ADC.h>
#include <ADC_util.h>

ADC *adc = new ADC();

//Teensy 3.6 - Mux Pins
#define MUX_0 28
#define MUX_1 32
#define MUX_2 30
#define MUX_3 31

#define MUX1_S A2 // ADC1
#define MUX2_S A0 // ADC1
#define MUX3_S A1 // ADC1
#define MUX4_S A14 // ADC1
#define MUX5_S A15 // ADC1
#define MUX6_S A17 // ADC1
#define MUX7_S A11 // ADC0
#define MUX8_S A16 // ADC1

//Mux 1 Connections
#define MUX1_midiSync1 0
#define MUX1_lfoMode1 1
#define MUX1_lfoWave1 2
#define MUX1_lfoSpeed1 3
#define MUX1_pwm1 4
#define MUX1_pw1 5
#define MUX1_square1 6
#define MUX1_saw1 7
#define MUX1_noise1 8
#define MUX1_db241 9
#define MUX1_hpf1 10
#define MUX1_lpf1 11
#define MUX1_hpfCutoff1 12
#define MUX1_hpfRes1 13
#define MUX1_lpfCutoff1 14
#define MUX1_lpfRes1 15

//Mux 2 Connections
#define MUX2_vcfIL1 0
#define MUX2_vcfAL1 1
#define MUX2_vcfAttack1 2
#define MUX2_vcfDecay1 3
#define MUX2_vcfRelease1 4
#define MUX2_vcfLevel1 5
#define MUX2_osc1Sine 6
#define MUX2_ampAttack1 7
#define MUX2_ampDecay1 8
#define MUX2_ampSustain1 9
#define MUX2_ampRelease1 10
#define MUX2_vcaLevel1 11
#define MUX2_initial1Brill 12
#define MUX2_initial1Level 13
#define MUX2_after1Brill 14
#define MUX2_after1Level 15

//Mux 3 Connections
#define MUX3_midiSync2 0
#define MUX3_lfoMode2 1
#define MUX3_lfoWave2 2
#define MUX3_lfoSpeed2 3
#define MUX3_pwm2 4
#define MUX3_pw2 5
#define MUX3_square2 6
#define MUX3_saw2 7
#define MUX3_noise2 8
#define MUX3_lpf2 9
#define MUX3_hpf2 10
#define MUX3_db242 11
#define MUX3_hpfCutoff2 12
#define MUX3_hpfRes2 13
#define MUX3_lpfCutoff2 14
#define MUX3_lpfRes2 15

//Mux 4 Connections
#define MUX4_vcfIL2 0
#define MUX4_vcfAL2 1
#define MUX4_vcfAttack2 2
#define MUX4_vcfDecay2 3
#define MUX4_vcfRelease2 4
#define MUX4_vcfLevel2 5
#define MUX4_osc2Sine 6
#define MUX4_ampAttack2 7
#define MUX4_ampDecay2 8
#define MUX4_ampSustain2 9
#define MUX4_ampRelease2 10
#define MUX4_vcaLevel2 11
#define MUX4_initial2Brill 12
#define MUX4_initial2Level 13
#define MUX4_after2Brill 14
#define MUX4_after2Level 15

//Mux 5 Connections
#define MUX5_subWheel 0
#define MUX5_modwheel 1
#define MUX5_detune 2
#define MUX5_ringAttack 3
#define MUX5_ringDecay 4
#define MUX5_ringDepth 5
#define MUX5_ringSpeed 6
#define MUX5_ringMod 7
#define MUX5_function 8
#define MUX5_subSpeed 9
#define MUX5_subVCO 10
#define MUX5_subVCF 11
#define MUX5_subVCA 12
#define MUX5_feet1 13
#define MUX5_feet2 14
#define MUX5_mix 15

//Mux 6 Connections
#define MUX6_brilliance 0
#define MUX6_reso 1
#define MUX6_touchPbend 2
#define MUX6_touchSpeed 3
#define MUX6_touchVCO 4
#define MUX6_touchVCF 5
#define MUX6_brillLow 6
#define MUX6_brillHigh 7
#define MUX6_levelLow 8
#define MUX6_levelHigh 9
#define MUX6_volume 10
#define MUX6_link 11
#define MUX6_sync 12
#define MUX6_13 13
#define MUX6_14 14
#define MUX6_15 15

//Mux 7 Connections
#define MUX7_arpPlay 0
#define MUX7_arpSync 1
#define MUX7_arpSpeed 2
#define MUX7_arpHold 3
#define MUX7_arpMode 4
#define MUX7_arpOctave 5
#define MUX7_arpRepeat 6
#define MUX7_expression 7
#define MUX7_expwah 8
#define MUX7_sustLongShort 9
#define MUX7_glissLongShort 10
#define MUX7_sust 11
#define MUX7_portaGliss 12
#define MUX7_sustMode 13
#define MUX7_gliss 14
#define MUX7_chorusSpeed 15

//Mux 8 Connections
#define MUX8_tremeloOn 0
#define MUX8_chorusDepth 1
#define MUX8_chorusOn 2
#define MUX8_delaySpeed 3
#define MUX8_delayOn 4
#define MUX8_delayDepth 5
#define MUX8_delaySync 6
#define MUX8_delayMix 7
#define MUX8_ribbonCourse 8
#define MUX8_ribbonPitch 9
#define MUX8_10 10
#define MUX8_11 11
#define MUX8_12 12
#define MUX8_13 13
#define MUX8_14 14
#define MUX8_15 15

//Teensy 3.6 Pins

#define RECALL_SW 17
#define SAVE_SW 24
#define SETTINGS_SW 12
#define BACK_SW 10

#define ENCODER_PINA 4
#define ENCODER_PINB 5

#define MUXCHANNELS 16
#define QUANTISE_FACTOR 2

#define DEBOUNCE 30

static byte muxInput = 0;
static int mux1ValuesPrev[MUXCHANNELS] = {};
static int mux2ValuesPrev[MUXCHANNELS] = {};
static int mux3ValuesPrev[MUXCHANNELS] = {};
static int mux4ValuesPrev[MUXCHANNELS] = {};
static int mux5ValuesPrev[MUXCHANNELS] = {};
static int mux6ValuesPrev[MUXCHANNELS] = {};
static int mux7ValuesPrev[MUXCHANNELS] = {};
static int mux8ValuesPrev[MUXCHANNELS] = {};

static int mux1Read = 0;
static int mux2Read = 0;
static int mux3Read = 0;
static int mux4Read = 0;
static int mux5Read = 0;
static int mux6Read = 0;
static int mux7Read = 0;
static int mux8Read = 0;

static long encPrevious = 0;

//These are pushbuttons and require debouncing

Bounce recallButton = Bounce(RECALL_SW, DEBOUNCE); //On encoder
boolean recall = true; //Hack for recall button
Bounce saveButton = Bounce(SAVE_SW, DEBOUNCE);
boolean del = true; //Hack for save button
Bounce settingsButton = Bounce(SETTINGS_SW, DEBOUNCE);
boolean reini = true; //Hack for settings button
Bounce backButton = Bounce(BACK_SW, DEBOUNCE);
boolean panic = true; //Hack for back button
Encoder encoder(ENCODER_PINB, ENCODER_PINA);//This often needs the pins swapping depending on the encoder

void setupHardware()
{
     //Volume Pot is on ADC0
  adc->adc0->setAveraging(8); // set number of averages 0, 4, 8, 16 or 32.
  adc->adc0->setResolution(8); // set bits of resolution  8, 10, 12 or 16 bits.
  adc->adc0->setConversionSpeed(ADC_CONVERSION_SPEED::VERY_LOW_SPEED); // change the conversion speed
  adc->adc0->setSamplingSpeed(ADC_SAMPLING_SPEED::MED_SPEED); // change the sampling speed

  //MUXs on ADC1
  adc->adc1->setAveraging(8); // set number of averages 0, 4, 8, 16 or 32.
  adc->adc1->setResolution(8); // set bits of resolution  8, 10, 12 or 16 bits.
  adc->adc1->setConversionSpeed(ADC_CONVERSION_SPEED::VERY_LOW_SPEED); // change the conversion speed
  adc->adc1->setSamplingSpeed(ADC_SAMPLING_SPEED::MED_SPEED); // change the sampling speed


  //Mux address pins

  pinMode(MUX_0, OUTPUT);
  pinMode(MUX_1, OUTPUT);
  pinMode(MUX_2, OUTPUT);
  pinMode(MUX_3, OUTPUT);

  digitalWrite(MUX_0, LOW);
  digitalWrite(MUX_1, LOW);
  digitalWrite(MUX_2, LOW);
  digitalWrite(MUX_3, LOW);

  //analogReadResolution(8);

  //Switches
  pinMode(RECALL_SW, INPUT_PULLUP); //On encoder
  pinMode(SAVE_SW, INPUT_PULLUP);
  pinMode(SETTINGS_SW, INPUT_PULLUP);
  pinMode(BACK_SW, INPUT_PULLUP);

}
