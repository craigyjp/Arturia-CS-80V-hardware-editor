/*
  CS80 Editor - Firmware Rev 1.1

  Includes code by:
    Dave Benn - Handling MUXs, a few other bits and original inspiration  https://www.notesandvolts.com/2019/01/teensy-synth-part-10-hardware.html

  Arduino IDE
  Tools Settings:
  Board: "Teensy4,1"
  USB Type: "Serial + MIDI"
  CPU Speed: "600"
  Optimize: "Fastest"

  Performance Tests   CPU  Mem
  180Mhz Faster       81.6 44
  180Mhz Fastest      77.8 44
  180Mhz Fastest+PC   79.0 44
  180Mhz Fastest+LTO  76.7 44
  240MHz Fastest+LTO  55.9 44

  Additional libraries:
    Agileware CircularBuffer available in Arduino libraries manager
    Replacement files are in the Modified Libraries folder and need to be placed in the teensy Audio folder.
*/

#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <SerialFlash.h>
#include <MIDI.h>
#include <USBHost_t36.h>
#include "MidiCC.h"
#include "Constants.h"
#include "Parameters.h"
#include "PatchMgr.h"
#include "HWControls.h"
#include "EepromMgr.h"


#define PARAMETER 0 //The main page for displaying the current patch and control (parameter) changes
#define RECALL 1 //Patches list
#define SAVE 2 //Save patch page
#define REINITIALISE 3 // Reinitialise message
#define PATCH 4 // Show current patch bypassing PARAMETER
#define PATCHNAMING 5 // Patch naming page
#define DELETE 6 //Delete patch page
#define DELETEMSG 7 //Delete patch message page
#define SETTINGS 8 //Settings page
#define SETTINGSVALUE 9 //Settings page

unsigned int state = PARAMETER;

#include "ST7735Display.h"

boolean cardStatus = false;

//USB HOST MIDI Class Compliant
USBHost myusb;
USBHub hub1(myusb);
USBHub hub2(myusb);
MIDIDevice midi1(myusb);

//MIDI 5 Pin DIN
MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, MIDI);


byte ccType = 0; //(EEPROM)
#include "Settings.h"


int count = 0;//For MIDI Clk Sync
int DelayForSH3 = 12;
int patchNo = 1;//Current patch no
int voiceToReturn = -1; //Initialise
long earliestTime = millis(); //For voice allocation - initialise to now

void setup()
{
  SPI.begin();
  setupDisplay();
  setUpSettings();
  setupHardware();

  cardStatus = SD.begin(BUILTIN_SDCARD);
  if (cardStatus)
  {
    Serial.println("SD card is connected");
    //Get patch numbers and names from SD card
    loadPatches();
    if (patches.size() == 0)
    {
      //save an initialised patch to SD card
      savePatch("1", INITPATCH);
      loadPatches();
    }
  }
  else
  {
    Serial.println("SD card is not connected or unusable");
    reinitialiseToPanel();
    showPatchPage("No SD", "conn'd / usable");
  }

  //Read MIDI Channel from EEPROM
  midiChannel = getMIDIChannel();
  Serial.println("MIDI Ch:" + String(midiChannel) + " (0 is Omni On)");

  //Read CC type from EEPROM
  ccType = getCCType();

  //  switch (ccType)
  //  {
  //    case 0:
  //      Serial.println("CC Type: Controller 0-127");
  //      break;
  //
  //    case 1:
  //      Serial.println("CC Type: NPRN");
  //      break;
  //
  //    case 2:
  //      Serial.println("CC Type: SYSEX");
  //      break;
  //  }

  //USB HOST MIDI Class Compliant
  delay(200); //Wait to turn on USB Host
  myusb.begin();
  midi1.setHandleControlChange(myControlChange);
  midi1.setHandleProgramChange(myProgramChange);
  Serial.println("USB HOST MIDI Class Compliant Listening");

  //USB Client MIDI
  usbMIDI.setHandleControlChange(myControlChange);
  usbMIDI.setHandleProgramChange(myProgramChange);
  Serial.println("USB Client MIDI Listening");

  //MIDI 5 Pin DIN
  MIDI.begin();
  MIDI.setHandleControlChange(myControlChange);
  MIDI.setHandleProgramChange(myProgramChange);
  Serial.println("MIDI In DIN Listening");

  //Read Encoder Direction from EEPROM
  encCW = getEncoderDir();
  //Read MIDI Out Channel from EEPROM
  midiOutCh = getMIDIOutCh();

  recallPatch(patchNo); //Load first patch
}

void allNotesOff()
{
}

void updatevolumeControl()
{
  showCurrentParameterPage("Volume", String(volumeControlstr) + " dB");
}

void updatemodWheel()
{
  showCurrentParameterPage("Mod Wheel", modWheelLevelstr);
}

void updatemidiSync1()
{
  if (midiSync1 > 127 )
  {
    showCurrentParameterPage("LFO 1 Sync", String("On"));
  }
  else
  {
    showCurrentParameterPage("LFO 1 Sync", String("Off"));
  }
}

void updatemidiSync2()
{
  if (midiSync2 > 127 )
  {
    showCurrentParameterPage("LFO 2 Sync", String("On"));
  }
  else
  {
    showCurrentParameterPage("LFO 2 Sync", String("Off"));
  }
}

void updatelfoMode1()
{
  if (lfoMode1 > 188)
  {
    showCurrentParameterPage("LFO 1 Mode", String("FREE"));
  }
  else if (lfoMode1 <= 188 && lfoMode1 > 100)
  {
    showCurrentParameterPage("LFO 1 Mode", String("TRIG"));
  }
  else
  {
    showCurrentParameterPage("LFO 1 Mode", String("MONO"));
  }
}

void updatelfoMode2()
{
  if (lfoMode2 > 188)
  {
    showCurrentParameterPage("LFO 2 Mode", String("FREE"));
  }
  else if (lfoMode2 <= 188 && lfoMode2 > 100)
  {
    showCurrentParameterPage("LFO 2 Mode", String("TRIG"));
  }
  else
  {
    showCurrentParameterPage("LFO 2 Mode", String("MONO"));
  }
}

void updatelfoWave1()
{
  getCS80LFOWaveform1(lfoWave1);
  showCurrentParameterPage("LFO 1 Wave", CS80LFOWaveform1);
}

void updatefunction()
{
  getCS80LFOWaveform1(function);
  showCurrentParameterPage("Sub Wave", CS80LFOWaveform1);
}

int getCS80LFOWaveform1(int value)
{
  if (value >= 0 && value < 43)
  {
    CS80LFOWaveform1 = "Random";
  }
  else if (value >= 43 && value < 85)
  {
    CS80LFOWaveform1 = "Noise";
  }
  else if (value >= 85 && value < 127)
  {
    CS80LFOWaveform1 = "Square";
  }
  else if (value >= 127 && value < 170)
  {
    CS80LFOWaveform1 = "Saw Up";
  }
  else if (value >= 170 && value < 213)
  {
    CS80LFOWaveform1 = "Saw Down";
  }
  else if (value >= 213 && value < 256)
  {
    CS80LFOWaveform1 = "Sine";
  }
}

void updatelfoWave2()
{
  getCS80LFOWaveform1(lfoWave2);
  showCurrentParameterPage("LFO 2 Wave", CS80LFOWaveform1);
}

int MIDIsyncSpeed(int value)
{
  if (value > 240)
  {
    syncSpeed = "Tempo * 9";
  }
  else if (value <= 240 && value > 224)
  {
    syncSpeed = "Tempo * 8";
  }
  else if (value <= 224 && value > 208)
  {
    syncSpeed = "Tempo * 7";
  }
  else if (value <= 208 && value > 192)
  {
    syncSpeed = "Tempo * 6";
  }
  else if (value <= 192 && value > 176)
  {
    syncSpeed = "Tempo * 5";
  }
  else if (value <= 176 && value > 160)
  {
    syncSpeed = "Tempo * 4";
  }
  else if (value <= 160 && value > 144)
  {
    syncSpeed = "Tempo * 3";
  }
  else if (value <= 144 && value > 128)
  {
    syncSpeed = "Tempo * 2";
  }
  else if (value <= 128 && lfoSpeed1 > 112)
  {
    syncSpeed = "Tempo * 1";
  }
  else if (value <= 112 && value > 96)
  {
    syncSpeed = "Tempo / 2";
  }
  else if (value <= 96 && value > 80)
  {
    syncSpeed = "Tempo / 3";
  }
  else if (value <= 80 && value > 64)
  {
    syncSpeed = "Tempo / 4";
  }
  else if (value <= 64 && value > 48)
  {
    syncSpeed = "Tempo / 5";
  }
  else if (value <= 48 && value > 32)
  {
    syncSpeed = "Tempo / 6";
  }
  else if (value <= 32 && value > 16)
  {
    syncSpeed = "Tempo / 7";
  }
  else
  {
    syncSpeed = "Tempo / 8";
  }
}

void updatelfoSpeed1()
{
  if (midiSync1 > 127 )
  {
    MIDIsyncSpeed(lfoSpeed1);
    showCurrentParameterPage("LFO 1 Speed", syncSpeed);
  }
  else
  {
    showCurrentParameterPage("LFO 1 Speed", String(lfoSpeed1str) + " Hz");
  }
}

void updatelfoSpeed2()
{
  if (midiSync2 > 127 )
  {
    MIDIsyncSpeed(lfoSpeed2);
    showCurrentParameterPage("LFO 2 Speed", syncSpeed);
  }
  else
  {
    showCurrentParameterPage("LFO 2 Speed", String(lfoSpeed2str) + " Hz");
  }
}

void updatepwm1()
{
  showCurrentParameterPage("PWM 1 Depth", String(pwm1str));
}

void updatepwm2()
{
  showCurrentParameterPage("PWM 2 Depth", String(pwm2str));
}

void updatepw1()
{
  showCurrentParameterPage("Pulse Width 1", int(pw1str));
}

void updatepw2()
{
  showCurrentParameterPage("Pulse Width 2", int(pw2str));
}

void updatesquare1()
{
  if (square1 > 127 )
  {
    showCurrentParameterPage("Squarewave 1", String("On"));
  }
  else
  {
    showCurrentParameterPage("Squarewave 1", String("Off"));
  }
}

void updatesquare2()
{
  if (square2 > 127 )
  {
    showCurrentParameterPage("Squarewave 2", String("On"));
  }
  else
  {
    showCurrentParameterPage("Squarewave 2", String("Off"));
  }
}

void updatesaw1()
{
  if (saw1 > 188)
  {
    showCurrentParameterPage("Sawtooth 1", String("On"));
  }
  else if (saw1 <= 188 && saw1 > 100)
  {
    showCurrentParameterPage("Triangle 1", String("On"));
  }
  else
  {
    showCurrentParameterPage("Saw & Tri 1", String("Off"));
  }
}

void updatesaw2()
{
  if (saw2 > 188)
  {
    showCurrentParameterPage("Sawtooth 2", String("On"));
  }
  else if (saw2 <= 188 && saw2 > 100)
  {
    showCurrentParameterPage("Triangle 2", String("On"));
  }
  else
  {
    showCurrentParameterPage("Saw & Tri 2", String("Off"));
  }
}

void updatenoise1()
{
  showCurrentParameterPage("Noise Level 1", String(noise1str) + " dB");
}

void updatenoise2()
{
  showCurrentParameterPage("Noise Level 2", String(noise2str) + " dB");
}

void updatedb241()
{
  if (db241 > 127 )
  {
    showCurrentParameterPage("24dB Slope 1", String("On"));
  }
  else
  {
    showCurrentParameterPage("24dB Slope 1", String("Off"));
  }
}

void updatedb242()
{
  if (db242 > 127 )
  {
    showCurrentParameterPage("24dB Slope 2", String("On"));
  }
  else
  {
    showCurrentParameterPage("24dB Slope 2", String("Off"));
  }
}

void updatehpf1()
{
  if (hpf1 > 127 )
  {
    showCurrentParameterPage("HP Filter 1", String("On"));
  }
  else
  {
    showCurrentParameterPage("HP Filter 1", String("Off"));
  }
}

void updatehpf2()
{
  if (hpf2 > 127 )
  {
    showCurrentParameterPage("HP Filter 2", String("On"));
  }
  else
  {
    showCurrentParameterPage("HP Filter 2", String("Off"));
  }
}

void updatelpf1()
{
  if (lpf1 > 127 )
  {
    showCurrentParameterPage("LP Filter 1", String("On"));
  }
  else
  {
    showCurrentParameterPage("LP Filter 1", String("Off"));
  }
}

void updatelpf2()
{
  if (lpf2 > 127 )
  {
    showCurrentParameterPage("LP Filter 2", String("On"));
  }
  else
  {
    showCurrentParameterPage("LP Filter 2", String("Off"));
  }
}

void updatehpfCutoff1()
{
  showCurrentParameterPage("HP Filter 1", String(hpfCutoff1str) + " Hz");
}

void updatehpfCutoff2()
{
  showCurrentParameterPage("HP Filter 2", String(hpfCutoff2str) + " Hz");
}

void updatehpfRes1()
{
  showCurrentParameterPage("HPF Res 1", String(hpfRes1str));
}

void updatehpfRes2()
{
  showCurrentParameterPage("HPF Res 2", String(hpfRes2str));
}

void updatelpfCutoff1()
{
  showCurrentParameterPage("LP Filter 1", String(lpfCutoff1str) + " Hz");
}

void updatelpfCutoff2()
{
  showCurrentParameterPage("LP Filter 2", String(lpfCutoff2str) + " Hz");
}

void updatelpfRes1()
{
  showCurrentParameterPage("LPF Res 1", String(lpfRes1str));
}

void updatelpfRes2()
{
  showCurrentParameterPage("LPF Res 2", String(lpfRes2str));
}

void updatevcfIL1()
{
  showCurrentParameterPage("VCF 1 IL", (vcfIL1str / 2));
}

void updatevcfIL2()
{
  showCurrentParameterPage("VCF 2 IL", (vcfIL2str / 2));
}

void updatevcfAL1()
{
  showCurrentParameterPage("VCF 1 AL", String(vcfAL1str));
}

void updatevcfAL2()
{
  showCurrentParameterPage("VCF 2 AL", String(vcfAL2str));
}

void updatevcfAttack1()
{
  if (vcfAttack1str < 1000)
  {
    showCurrentParameterPage("VCF 1 Attack", String(int(vcfAttack1str)) + " ms");
  }
  else
  {
    showCurrentParameterPage("VCF 1 Attack", String(vcfAttack1str * 0.001) + " s");
  }
}

void updatevcfAttack2()
{
  if (vcfAttack2str < 1000)
  {
    showCurrentParameterPage("VCF 2 Attack", String(int(vcfAttack2str)) + " ms");
  }
  else
  {
    showCurrentParameterPage("VCF 2 Attack", String(vcfAttack2str * 0.001) + " s");
  }
}

void updatevcfDecay1()
{
  if (vcfDecay1str < 1000)
  {
    showCurrentParameterPage("VCF 1 Decay", String(int(vcfDecay1str)) + " ms");
  }
  else
  {
    showCurrentParameterPage("VCF 1 Decay", String(vcfDecay1str * 0.001) + " s");
  }
}

void updatevcfDecay2()
{
  if (vcfDecay2str < 1000)
  {
    showCurrentParameterPage("VCF 2 Decay", String(int(vcfDecay2str)) + " ms");
  }
  else
  {
    showCurrentParameterPage("VCF 2 Decay", String(vcfDecay2str * 0.001) + " s");
  }
}

void updatevcfRelease1()
{
  if (vcfRelease1str < 1000)
  {
    showCurrentParameterPage("VCF 1 Release", String(int(vcfRelease1str)) + " ms");
  }
  else
  {
    showCurrentParameterPage("VCF 1 Release", String(vcfRelease1str * 0.001) + " s");
  }
}

void updatevcfRelease2()
{
  if (vcfRelease2str < 1000)
  {
    showCurrentParameterPage("VCF 2 Release", String(int(vcfRelease2str)) + " ms");
  }
  else
  {
    showCurrentParameterPage("VCF 2 Release", String(vcfRelease2str * 0.001) + " s");
  }
}

void updatevcfLevel1()
{
  showCurrentParameterPage("VCF 1 Level", String(vcfLevel1str) + " dB");
}

void updatevcfLevel2()
{
  showCurrentParameterPage("VCF 2 Level", String(vcfLevel2str) + " dB");
}

void updateosc1Sine()
{
  showCurrentParameterPage("OSC 1 Sine", String(osc1Sinestr) + " dB");
}

void updateosc2Sine()
{
  showCurrentParameterPage("OSC 2 Sine", String(osc2Sinestr) + " dB");
}

void updateampAttack1()
{
  if (ampAttack1str < 1000)
  {
    showCurrentParameterPage("VCA 1 Attack", String(int(ampAttack1str)) + " ms");
  }
  else
  {
    showCurrentParameterPage("VCA 1 Attack", String(ampAttack1str * 0.001) + " s");
  }
}

void updateampDecay1()
{
  if (ampDecay1str < 1000)
  {
    showCurrentParameterPage("VCA 1 Decay", String(int(ampDecay1str)) + " ms");
  }
  else
  {
    showCurrentParameterPage("VCA 1 Decay", String(ampDecay1str * 0.001) + " s");
  }
}

void updateampSustain1()
{
  showCurrentParameterPage("VCA 1 Sustain", String(ampSustain1str));
}

void updateampRelease1()
{
  if (ampRelease1str < 1000)
  {
    showCurrentParameterPage("VCA 1 Release", String(int(ampRelease1str)) + " ms");
  }
  else
  {
    showCurrentParameterPage("VCA 1 Release", String(ampRelease1str * 0.001) + " s");
  }
}

void updateampAttack2()
{
  if (ampAttack2str < 1000)
  {
    showCurrentParameterPage("VCA 2 Attack", String(int(ampAttack2str)) + " ms");
  }
  else
  {
    showCurrentParameterPage("VCA 2 Attack", String(ampAttack2str * 0.001) + " s");
  }
}

void updateampDecay2()
{
  if (ampDecay1str < 1000)
  {
    showCurrentParameterPage("VCA 2 Decay", String(int(ampDecay2str)) + " ms");
  }
  else
  {
    showCurrentParameterPage("VCA 2 Decay", String(ampDecay2str * 0.001) + " s");
  }
}

void updateampSustain2()
{
  showCurrentParameterPage("VCA 2 Sustain", String(ampSustain2str));
}

void updateampRelease2()
{
  if (ampRelease1str < 1000)
  {
    showCurrentParameterPage("VCA 2 Release", String(int(ampRelease2str)) + " ms");
  }
  else
  {
    showCurrentParameterPage("VCA 2 Release", String(ampRelease2str * 0.001) + " s");
  }
}

void updatevcaLevel1()
{
  showCurrentParameterPage("VCA 1 Level", String(vcaLevel1str) + " dB");
}

void updatevcaLevel2()
{
  showCurrentParameterPage("VCA 2 Level", String(vcaLevel2str) + " dB");
}

void updateinitial1Brill()
{
  showCurrentParameterPage("Initial 1 Brill", String(initial1Brillstr));
}

void updateinitial2Brill()
{
  showCurrentParameterPage("Initial 2 Brill", String(initial2Brillstr));
}

void updateafter1Brill()
{
  showCurrentParameterPage("After 1 Brill", String(after1Brillstr));
}

void updateafter2Brill()
{
  showCurrentParameterPage("After 2 Brill", String(after2Brillstr));
}

void updateinitial1Level()
{
  showCurrentParameterPage("Inital 1 Level", String(initial1Levelstr) + " dB");
}

void updateinitial2Level()
{
  showCurrentParameterPage("Inital 2 Level", String(initial2Levelstr) + " dB");
}

void updateafter1Level()
{
  showCurrentParameterPage("After 1 Level", String(after1Levelstr) + " dB");
}

void updateafter2Level()
{
  showCurrentParameterPage("After 2 Level", String(after2Levelstr) + " dB");
}

void updatesubWheel()
{
  if (subWheel > 127 )
  {
    showCurrentParameterPage("Sub Wheel", String("On"));
  }
  else
  {
    showCurrentParameterPage("Sub Wheel", String("Off"));
  }
}

void updatedetune()
{
  showCurrentParameterPage("Detune CH II", String(detunestr) + " cent");
}

void updateringAttack()
{
  showCurrentParameterPage("Ring Attack", String(ringAttackstr) + " ms");
}

void updateringDecay()
{
  showCurrentParameterPage("Ring Decay", String(ringDecaystr) + " ms");
}

void updateringDepth()
{
  showCurrentParameterPage("Ring Depth", String(ringDepthstr));
}

void updateringSpeed()
{
  showCurrentParameterPage("Ring Speed", String(ringSpeedstr) + " Hz");
}

void updateringMod()
{
  showCurrentParameterPage("Ring Mod", String(ringModstr));
}

void updatesubSpeed()
{
  showCurrentParameterPage("SubOsc Speed", String(subSpeedstr) + " Hz");
}

void updatesubVCO()
{
  showCurrentParameterPage("Sub to VCO", String(subVCOstr));
}

void updatesubVCF()
{
  showCurrentParameterPage("Sub to VCF", String(subVCFstr));
}

void updatesubVCA()
{
  showCurrentParameterPage("Sub to VCA", String(subVCAstr));
}

void updatefeet1()
{
  showCurrentParameterPage("Feet 1", String(feet1str));
}

void updatefeet2()
{
  showCurrentParameterPage("Feet 2", String(feet2str));
}

void updatemix()
{
  //  showCurrentParameterPage("Osc Mix I<>II", "   " + String(mixstr));
  showCurrentParameterPage("Osc Mix I<>II", "   " + String(mixstr, 0) + " - " + String(mixstr2, 0));
}

void updatebrill()
{
  showCurrentParameterPage("Brilliance", String(brillstr));
}

void updatereso()
{
  showCurrentParameterPage("Resonance", String(resostr));
}

void updatetouchPbend()
{
  showCurrentParameterPage("Touch P.Bend", String(touchPbendstr));
}

void updatetouchSpeed()
{
  showCurrentParameterPage("Touch Speed", String(touchSpeedstr));
}

void updatetouchVCO()
{
  showCurrentParameterPage("Touch VCO", String(touchVCOstr));
}

void updatetouchVCF()
{
  showCurrentParameterPage("Touch VCF", String(touchVCFstr));
}

void updatebrillLow()
{
  showCurrentParameterPage("Brilliance Low", String(brillLowstr));
}

void updatebrillHigh()
{
  showCurrentParameterPage("Brilliance High", String(brillHighstr));
}

void updatelevelLow()
{
  showCurrentParameterPage("Level Low", String(levelLowstr));
}

void updatelevelHigh()
{
  showCurrentParameterPage("Level High", String(levelHighstr));
}

void updatelink()
{
  if (link > 127 )
  {
    showCurrentParameterPage("Link", String("On"));
  }
  else
  {
    showCurrentParameterPage("Link", String("Off"));
  }
}

void updatesync()
{
  if (sync > 127 )
  {
    showCurrentParameterPage("Osc Sync", String("On"));
  }
  else
  {
    showCurrentParameterPage("Osc Sync", String("Off"));
  }
}

void updatearpSync()
{
  if (arpSync > 127 )
  {
    showCurrentParameterPage("Arp Sync", String("On"));
  }
  else
  {
    showCurrentParameterPage("Arp Sync", String("Off"));
  }
}

void updatearpSpeed()
{
  if (arpSync > 127 )
  {
    MIDIsyncSpeed(arpSpeed);
    showCurrentParameterPage("Arp Speed", syncSpeed);
  }
  else
  {
    showCurrentParameterPage("Arp Speed", String(arpSpeedstr) + " ms");
  }
}

void updatearpPlay()
{
  if (arpPlay > 127 )
  {
    showCurrentParameterPage("Arp Play", String("On"));
  }
  else
  {
    showCurrentParameterPage("Arp Play", String("Off"));
  }
}

void updatearpHold()
{
  if (arpHold > 127 )
  {
    showCurrentParameterPage("Arp Hold", String("On"));
  }
  else
  {
    showCurrentParameterPage("Arp Hold", String("Off"));
  }
}

void updatearpMode()
{
  if (arpMode > 205)
  {
    showCurrentParameterPage("Arp Mode", String("Up"));
  }
  else if (arpMode <= 205 && arpMode > 154)
  {
    showCurrentParameterPage("Arp Mode", String("Down"));
  }
  else if (arpMode <= 154 && arpMode > 102)
  {
    showCurrentParameterPage("Arp Mode", String("Up/Down"));
  }
  else if (arpMode <= 102 && arpMode > 51)
  {
    showCurrentParameterPage("Arp Mode", String("Random"));
  }
  else
  {
    showCurrentParameterPage("Arp Mode", String("Notes"));
  }
}

void updatearpOctave()
{
  if (arpOctave > 192)
  {
    showCurrentParameterPage("Arp Octave", String("4 Octaves"));
  }
  else if (arpOctave <= 192 && arpOctave > 127)
  {
    showCurrentParameterPage("Arp Octave", String("3 Octaves"));
  }
  else if (arpOctave <= 127 && arpOctave > 64)
  {
    showCurrentParameterPage("Arp Octave", String("2 Octaves"));
  }
  else
  {
    showCurrentParameterPage("Arp Octave", String("1 Octave"));
  }
}

void updatearpRepeat()
{
  if (arpRepeat > 192)
  {
    showCurrentParameterPage("Arp Repeat", String("4 Repeat"));
  }
  else if (arpRepeat <= 192  && arpRepeat > 127)
  {
    showCurrentParameterPage("Arp Repeat", String("3 Repeat"));
  }
  else if (arpRepeat <= 127 && arpRepeat > 64)
  {
    showCurrentParameterPage("Arp Repeat", String("2 Repeat"));
  }
  else
  {
    showCurrentParameterPage("Arp Repeat", String("1 Repeat"));
  }
}

//void updateButton(String param, byte value)
//{
//  if (value > 127 )
//  {
//    showCurrentParameterPage((String char param), String("On"));
//  }
//  else
//  {
//    showCurrentParameterPage((String char param), String("Off"));
//  }
//}

void updateexpression()
{
  if (expression > 127 )
  {
    showCurrentParameterPage("Expression", String("On"));
  }
  else
  {
    showCurrentParameterPage("Expression", String("Off"));
  }
}

void updateexpwah()
{
  if (expwah > 127 )
  {
    showCurrentParameterPage("Exp Wah", String("On"));
  }
  else
  {
    showCurrentParameterPage("Exp Wah", String("Off"));
  }
}

void updatesustLongShort()
{
  showCurrentParameterPage("Sustain Time", String(sustLongShortstr) + " ms");
}

void updateglissLongShort()
{
  showCurrentParameterPage("Portamento", String(glissLongShortstr) + " ms");
}

void updatesust()
{
  if (sust > 127 )
  {
    showCurrentParameterPage("Sustain", String("On"));
  }
  else
  {
    showCurrentParameterPage("Sustain", String("Off"));
  }
}

void updateportaGliss()
{
  if (portaGliss > 127 )
  {
    showCurrentParameterPage("Porta/Gliss", String("On"));
  }
  else
  {
    showCurrentParameterPage("Porta/Gliss", String("Off"));
  }
}

void updatesustMode()
{
  if (sustMode > 127 )
  {
    showCurrentParameterPage("Sustain Mode", String("On"));
  }
  else
  {
    showCurrentParameterPage("Sustain Mode", String("Off"));
  }
}

void updategliss()
{
  if (gliss > 127 )
  {
    showCurrentParameterPage("Porta/Gliss", String("Portamento"));
  }
  else
  {
    showCurrentParameterPage("Porta/Gliss", String("Glissendo"));
  }
}

void updatechorusSpeed()
{
  if (tremeloOn > 127)
  {
    showCurrentParameterPage("Trem Speed", String(chorusSpeedstr) + " Hz");
  }
  else
  {
    showCurrentParameterPage("Chorus Speed", String(chorusSpeedstr) + " Hz");
  }
}

void updatetremeloOn()
{
  if (tremeloOn > 127 )
  {
    showCurrentParameterPage("Tremelo", String("On"));
  }
  else
  {
    showCurrentParameterPage("Tremelo", String("Off"));
  }
}

void updatechorusDepth()
{
  showCurrentParameterPage("Chorus Depth", String(chorusDepthstr));
}

void updatechorusOn()
{
  if (chorusOn > 127 )
  {
    showCurrentParameterPage("Chorus", String("On"));
  }
  else
  {
    showCurrentParameterPage("Chorus", String("Off"));
  }
}

void updatedelaySpeed()
{
  if (delaySync > 127 )
  {
    MIDIsyncSpeed(delaySpeed);
    showCurrentParameterPage("Delay Speed", syncSpeed);
  }
  else
  {
    showCurrentParameterPage("Delay Speed", String(delaySpeedstr) + " ms");
  }
}

void updatedelayOn()
{
  if (delayOn > 127 )
  {
    showCurrentParameterPage("Delay", String("On"));
  }
  else
  {
    showCurrentParameterPage("Delay", String("Off"));
  }
}

void updatedelayDepth()
{
  showCurrentParameterPage("Delay Depth", String(delayDepthstr));
}

void updatedelaySync()
{
  if (delaySync > 127 )
  {
    showCurrentParameterPage("MIDI Sync", String("On"));
  }
  else
  {
    showCurrentParameterPage("MIDI Sync", String("Off"));
  }
}

void updatedelayMix()
{
  showCurrentParameterPage("Delay Mix", String(delayMixstr));
}

void updateribbonCourse()
{
  if (ribbonCourse > 243)
  {
    showCurrentParameterPage("Ribbon Course", String("24 Semitone"));
  }
  else if (ribbonCourse <= 243 && ribbonCourse > 233)
  {
    showCurrentParameterPage("Ribbon Course", String("23 Semitone"));
  }
  else if (ribbonCourse <= 233 && ribbonCourse > 223)
  {
    showCurrentParameterPage("Ribbon Course", String("22 Semitone"));
  }
  else if (ribbonCourse <= 223 && ribbonCourse > 212)
  {
    showCurrentParameterPage("Ribbon Course", String("21 Semitone"));
  }
  else if (ribbonCourse <= 212 && ribbonCourse > 202)
  {
    showCurrentParameterPage("Ribbon Course", String("20 Semitone"));
  }
  else if (ribbonCourse <= 202 && ribbonCourse > 192)
  {
    showCurrentParameterPage("Ribbon Course", String("19 Semitone"));
  }
  else if (ribbonCourse <= 192 && ribbonCourse > 182)
  {
    showCurrentParameterPage("Ribbon Course", String("18 Semitone"));
  }
  else if (ribbonCourse <= 182 && ribbonCourse > 172)
  {
    showCurrentParameterPage("Ribbon Course", String("17 Semitone"));
  }
  else if (ribbonCourse <= 172 && ribbonCourse > 162)
  {
    showCurrentParameterPage("Ribbon Course", String("16 Semitone"));
  }
  else if (ribbonCourse <= 162 && ribbonCourse > 151)
  {
    showCurrentParameterPage("Ribbon Course", String("15 Semitone"));
  }
  else if (ribbonCourse <= 151 && ribbonCourse > 141)
  {
    showCurrentParameterPage("Ribbon Course", String("14 Semitone"));
  }
  else if (ribbonCourse <= 141 && ribbonCourse > 131)
  {
    showCurrentParameterPage("Ribbon Course", String("13 Semitone"));
  }
  else if (ribbonCourse <= 131 && ribbonCourse > 121)
  {
    showCurrentParameterPage("Ribbon Course", String("12 Semitone"));
  }
  else if (ribbonCourse <= 121 && ribbonCourse > 111)
  {
    showCurrentParameterPage("Ribbon Course", String("11 Semitone"));
  }
  else if (ribbonCourse <= 111 && ribbonCourse > 100)
  {
    showCurrentParameterPage("Ribbon Course", String("10 Semitone"));
  }
  else if (ribbonCourse <= 100 && ribbonCourse > 90)
  {
    showCurrentParameterPage("Ribbon Course", String("09 Semitone"));
  }
  else if (ribbonCourse <= 90 && ribbonCourse > 80)
  {
    showCurrentParameterPage("Ribbon Course", String("08 Semitone"));
  }
  else if (ribbonCourse <= 80 && ribbonCourse > 70)
  {
    showCurrentParameterPage("Ribbon Course", String("07 Semitone"));
  }
  else if (ribbonCourse <= 70 && ribbonCourse > 60)
  {
    showCurrentParameterPage("Ribbon Course", String("06 Semitone"));
  }
  else if (ribbonCourse <= 60 && ribbonCourse > 50)
  {
    showCurrentParameterPage("Ribbon Course", String("05 Semitone"));
  }
  else if (ribbonCourse <= 50 && ribbonCourse > 40)
  {
    showCurrentParameterPage("Ribbon Course", String("04 Semitone"));
  }
  else if (ribbonCourse <= 40 && ribbonCourse > 30)
  {
    showCurrentParameterPage("Ribbon Course", String("03 Semitone"));
  }
  else if (ribbonCourse <= 30 && ribbonCourse > 20)
  {
    showCurrentParameterPage("Ribbon Course", String("02 Semitone"));
  }
  else if (ribbonCourse <= 20 && ribbonCourse > 9)
  {
    showCurrentParameterPage("Ribbon Course", String("01 Semitone"));
  }
  else
  {
    showCurrentParameterPage("Ribbon Course", String("Linear Mode"));
  }
}

void updateribbonPitch()
{
  if (ribbonPitch > 127 )
  {
    showCurrentParameterPage("Ribbon Pitch", String("On"));
  }
  else
  {
    showCurrentParameterPage("Ribbon Pitch", String("Off"));
  }
}






void updatePatchname()
{
  showPatchPage(String(patchNo), patchName);
}

void myControlChange(byte channel, byte control, int value)
{
  switch (control)
  {
    case CCvolumeControl:
      volumeControl = value;
      volumeControlstr = VOLUMEDB[value / readresdivider];
      updatevolumeControl();
      break;

    case CCmodwheel:
      modWheelLevel = value;
      modWheelLevelstr = LINEAR[value / readresdivider];
      updatemodWheel();
      break;

    case CCmidiSync1:
      midiSync1 = value;
      updatemidiSync1();
      break;

    case CCmidiSync2:
      midiSync2 = value;
      updatemidiSync2();
      break;

    case CClfoMode1:
      lfoMode1 = value;
      updatelfoMode1();
      break;

    case CClfoMode2:
      lfoMode2 = value;
      updatelfoMode2();
      break;

    case CClfoWave1:
      lfoWave1 = value;
      updatelfoWave1();
      break;

    case CClfoWave2:
      lfoWave2 = value;
      updatelfoWave2();
      break;

    case CClfoSpeed1:
      lfoSpeed1 = value;
      lfoSpeed1str = LFOSPEED[value / readresdivider]; // for display
      updatelfoSpeed1();
      break;

    case CClfoSpeed2:
      lfoSpeed2 = value;
      lfoSpeed2str = LFOSPEED[value / readresdivider]; // for display
      updatelfoSpeed2();
      break;

    case CCpwm1:
      pwm1 = value;
      pwm1str = LINEAR[value / readresdivider]; // for display
      updatepwm1();
      break;

    case CCpwm2:
      pwm2 = value;
      pwm2str = LINEAR[value / readresdivider]; // for display
      updatepwm2();
      break;

    case CCpw1:
      pw1 = value;
      pw1str = PULSEWIDTH[value / readresdivider]; // for display
      updatepw1();
      break;

    case CCpw2:
      pw2 = value;
      pw2str = PULSEWIDTH[value / readresdivider]; // for display
      updatepw2();
      break;

    case CCsquare1:
      square1 = value;
      updatesquare1();
      break;

    case CCsquare2:
      square2 = value;
      updatesquare2();
      break;

    case CCsaw1:
      saw1 = value;
      updatesaw1();
      break;

    case CCsaw2:
      saw2 = value;
      updatesaw2();
      break;

    case CCnoise1:
      noise1 = value;
      noise1str = NOISEDB[value / readresdivider];
      Serial.print("Noise ");
      Serial.println(int(noise1 / readresdivider));
      updatenoise1();
      break;

    case CCnoise2:
      noise2 = value;
      noise2str = NOISEDB[value / readresdivider];
      updatenoise2();
      break;

    case CCdb241:
      db241 = value;
      updatedb241();
      break;

    case CCdb242:
      db242 = value;
      updatedb242();
      break;

    case CChpf1:
      hpf1 = value;
      updatehpf1();
      break;

    case CChpf2:
      hpf2 = value;
      updatehpf2();
      break;

    case CClpf1:
      lpf1 = value;
      updatelpf1();
      break;

    case CClpf2:
      lpf2 = value;
      updatelpf2();
      break;

    case CChpfCutoff1:
      hpfCutoff1str = HPFFILTERCUTOFF[value / readresdivider];
      hpfCutoff1 = value;
      updatehpfCutoff1();
      break;

    case CChpfCutoff2:
      hpfCutoff2str = HPFFILTERCUTOFF[value / readresdivider];
      hpfCutoff2 = value;
      updatehpfCutoff2();
      break;

    case CChpfRes1:
      hpfRes1str = FILTERRES[value / readresdivider];
      hpfRes1 = value;
      updatehpfRes1();
      break;

    case CChpfRes2:
      hpfRes2str = FILTERRES[value / readresdivider];
      hpfRes2 = value;
      updatehpfRes2();
      break;

    case CClpfCutoff1:
      lpfCutoff1str = HPFFILTERCUTOFF[value / readresdivider];
      lpfCutoff1 = value;
      updatelpfCutoff1();
      break;

    case CClpfCutoff2:
      lpfCutoff2str = HPFFILTERCUTOFF[value / readresdivider];
      lpfCutoff2 = value;
      updatelpfCutoff2();
      break;

    case CClpfRes1:
      lpfRes1str = FILTERRES[value / readresdivider];
      lpfRes1 = value;
      updatelpfRes1();
      break;

    case CClpfRes2:
      lpfRes2str = FILTERRES[value / readresdivider];
      lpfRes2 = value;
      updatelpfRes2();
      break;

    case CCvcfIL1:
      vcfIL1str = LINEAR[value / readresdivider];
      vcfIL1 = value;
      updatevcfIL1();
      break;

    case CCvcfIL2:
      vcfIL2str = LINEAR[value / readresdivider];
      vcfIL2 = value;
      updatevcfIL2();
      break;

    case CCvcfAL1:
      vcfAL1str = LINEAR[value / readresdivider];
      vcfAL1 = value;
      updatevcfAL1();
      break;

    case CCvcfAL2:
      vcfAL2str = LINEAR[value / readresdivider];
      vcfAL2 = value;
      updatevcfAL2();
      break;

    case CCvcfAttack1:
      vcfAttack1str = VCFATTACKTIMES[value / readresdivider];
      vcfAttack1 = value;
      updatevcfAttack1();
      break;

    case CCvcfAttack2:
      vcfAttack2str = VCFATTACKTIMES[value / readresdivider];
      vcfAttack2 = value;
      updatevcfAttack2();
      break;

    case CCvcfDecay1:
      vcfDecay1str = VCFDECAYTIMES[value / readresdivider];
      vcfDecay1 = value;
      updatevcfDecay1();
      break;

    case CCvcfDecay2:
      vcfDecay2str = VCFDECAYTIMES[value / readresdivider];
      vcfDecay2 = value;
      updatevcfDecay2();
      break;

    case CCvcfRelease1:
      vcfRelease1str = VCFRELEASETIMES[value / readresdivider];
      vcfRelease1 = value;
      updatevcfRelease1();
      break;

    case CCvcfRelease2:
      vcfRelease2str = VCFRELEASETIMES[value / readresdivider];
      vcfRelease2 = value;
      updatevcfRelease2();
      break;

    case CCvcfLevel1:
      vcfLevel1str = VCFDB[value / readresdivider];
      vcfLevel1 = value;
      updatevcfLevel1();
      break;

    case CCvcfLevel2:
      vcfLevel2str = VCFDB[value / readresdivider];
      vcfLevel2 = value;
      updatevcfLevel2();
      break;

    case CCosc1Sine:
      osc1Sinestr = SINEDB[value / readresdivider];
      osc1Sine = value;
      updateosc1Sine();
      break;

    case CCosc2Sine:
      osc2Sinestr = SINEDB[value / readresdivider];
      osc2Sine = value;
      updateosc2Sine();
      break;

    case CCampAttack1:
      ampAttack1 = value;
      ampAttack1str = VCFATTACKTIMES[value / readresdivider];
      updateampAttack1();
      break;

    case CCampDecay1:
      ampDecay1 = value;
      ampDecay1str = VCFDECAYTIMES[value / readresdivider];
      updateampDecay1();
      break;

    case CCampSustain1:
      ampSustain1 = value;
      ampSustain1str = LINEAR[value / readresdivider];
      updateampSustain1();
      break;

    case CCampRelease1:
      ampRelease1 = value;
      ampRelease1str = VCFRELEASETIMES[value / readresdivider];
      updateampRelease1();
      break;

    case CCampAttack2:
      ampAttack2 = value;
      ampAttack2str = VCFATTACKTIMES[value / readresdivider];
      updateampAttack2();
      break;

    case CCampDecay2:
      ampDecay2 = value;
      ampDecay2str = VCFDECAYTIMES[value / readresdivider];
      updateampDecay2();
      break;

    case CCampSustain2:
      ampSustain2 = value;
      ampSustain2str = LINEAR[value / readresdivider];
      updateampSustain2();
      break;

    case CCampRelease2:
      ampRelease2 = value;
      ampRelease2str = VCFRELEASETIMES[value / readresdivider];
      updateampRelease2();
      break;

    case CCvcaLevel1:
      vcaLevel1str = VCFDB[value / readresdivider];
      vcaLevel1 = value;
      updatevcaLevel1();
      break;

    case CCvcaLevel2:
      vcaLevel2str = VCFDB[value / readresdivider];
      vcaLevel2 = value;
      updatevcaLevel2();
      break;

    case CCinitial1Brill:
      initial1Brillstr = LINEAR[value / readresdivider];
      initial1Brill = value;
      updateinitial1Brill();
      break;

    case CCinitial2Brill:
      initial2Brillstr = LINEAR[value / readresdivider];
      initial2Brill = value;
      updateinitial2Brill();
      break;

    case CCinitial1Level:
      initial1Levelstr = TOUCHDB[value / readresdivider];
      initial1Level = value;
      updateinitial1Level();
      break;

    case CCinitial2Level:
      initial2Levelstr = TOUCHDB[value / readresdivider];
      initial2Level = value;
      updateinitial2Level();
      break;

    case CCafter1Brill:
      after1Brillstr = LINEAR[value / readresdivider];
      after1Brill = value;
      updateafter1Brill();
      break;

    case CCafter2Brill:
      after2Brillstr = LINEAR[value / readresdivider];
      after2Brill = value;
      updateafter2Brill();
      break;

    case CCafter1Level:
      after1Levelstr = TOUCHDB[value / readresdivider];
      after1Level = value;
      updateafter1Level();
      break;

    case CCafter2Level:
      after2Levelstr = TOUCHDB[value / readresdivider];
      after2Level = value;
      updateafter2Level();
      break;

    case CCsubWheel:
      subWheel = value;
      updatesubWheel();
      break;

    case CCdetune:
      detune = value;
      detunestr = LINEARCENTREZERO[value / readresdivider];
      updatedetune();
      break;

    case CCringAttack:
      ringAttack = value;
      ringAttackstr = RINGTIMES[value / readresdivider];
      updateringAttack();
      break;

    case CCringDecay:
      ringDecay = value;
      ringDecaystr = RINGTIMES[value / readresdivider];
      updateringDecay();
      break;

    case CCringDepth:
      ringDepth = value;
      ringDepthstr = REVERSELINEAR[value / readresdivider];
      updateringDepth();
      break;

    case CCringSpeed:
      ringSpeed = value;
      ringSpeedstr = RINGTEMPO[value / readresdivider];
      updateringSpeed();
      break;

    case CCringMod:
      ringMod = value;
      ringModstr = REVERSELINEAR[value / readresdivider];
      updateringMod();
      break;

    case CCfunction:
      function = value;
      updatefunction();
      break;

    case CCsubSpeed:
      subSpeed = value;
      subSpeedstr = SUBTEMPO[value / readresdivider];
      updatesubSpeed();
      break;

    case CCsubVCO:
      subVCO = value;
      subVCOstr = REVERSELINEAR[value / readresdivider];
      updatesubVCO();
      break;

    case CCsubVCF:
      subVCF = value;
      subVCFstr = REVERSELINEAR[value / readresdivider];
      updatesubVCF();
      break;

    case CCsubVCA:
      subVCA = value;
      subVCAstr = REVERSELINEAR[value / readresdivider];
      updatesubVCA();
      break;

    case CCfeet1:
      feet1 = value;
      feet1str = FEET[value / readresdivider];
      updatefeet1();
      break;

    case CCfeet2:
      feet2 = value;
      feet2str = FEET[value / readresdivider];
      updatefeet2();
      break;

    case CCmix:
      mix = value;
      mixstr = LINEAR_NORMAL[value / readresdivider];
      mixstr2 = LINEAR_INVERSE[value / readresdivider];
      updatemix();
      break;

    case CCbrilliance:
      brill = value;
      brillstr = LINEARCENTREZERO[value / readresdivider];
      updatebrill();
      break;

    case CCreso:
      reso = value;
      resostr = LINEARCENTREZERO[value / readresdivider];
      updatereso();
      break;

    case CCtouchPbend:
      touchPbend = value;
      touchPbendstr = REVERSELINEAR[value / readresdivider];
      updatetouchPbend();
      break;

    case CCtouchSpeed:
      touchSpeed = value;
      touchSpeedstr = REVERSELINEAR[value / readresdivider];
      updatetouchSpeed();
      break;

    case CCtouchVCO:
      touchVCO = value;
      touchVCOstr = REVERSELINEAR[value / readresdivider];
      updatetouchVCO();
      break;

    case CCtouchVCF:
      touchVCF = value;
      touchVCFstr = REVERSELINEAR[value / readresdivider];
      updatetouchVCF();
      break;

    case CCbrillLow:
      brillLow = value;
      brillLowstr = LINEARCENTREZERO[value / readresdivider];
      updatebrillLow();
      break;

    case CCbrillHigh:
      brillHigh = value;
      brillHighstr = LINEARCENTREZERO[value / readresdivider];
      updatebrillHigh();
      break;

    case CClevelLow:
      levelLow = value;
      levelLowstr = LINEARCENTREZERO[value / readresdivider];
      updatelevelLow();
      break;

    case CClevelHigh:
      levelHigh = value;
      levelHighstr = LINEARCENTREZERO[value / readresdivider];
      updatelevelHigh();
      break;

    case CClink:
      link = value;
      updatelink();
      break;

    case CCsync:
      sync = value;
      updatesync();
      break;

    case CCarpSync:
      arpSync = value;
      updatearpSync();
      break;

    case CCarpSpeed:
      arpSpeed = value;
      arpSpeedstr = ARPTIMES[value / readresdivider];
      updatearpSpeed();
      break;

    case CCarpPlay:
      arpPlay = value;
      updatearpPlay();
      break;

    case CCarpHold:
      arpHold = value;
      updatearpHold();
      break;

    case CCarpMode:
      arpMode = value;
      updatearpMode();
      break;

    case CCarpOctave:
      arpOctave = value;
      updatearpOctave();
      break;

    case CCarpRepeat:
      arpRepeat = value;
      updatearpRepeat();
      break;

    case CCexpression:
      expression = value;
      updateexpression();
      break;

    case CCexpwah:
      expwah = value;
      updateexpwah();
      break;

    case CCsustLongShort:
      sustLongShort = value;
      sustLongShortstr = SUSTTIMES[value / readresdivider];
      updatesustLongShort();
      break;

    case CCglissLongShort:
      glissLongShort = value;
      glissLongShortstr = GLISSTIMES[value / readresdivider];
      updateglissLongShort();
      break;

    case CCsust:
      sust = value;
      updatesust();
      break;

    case CCportaGliss:
      portaGliss = value;
      updateportaGliss();
      break;

    case CCsustMode:
      sustMode = value;
      updatesustMode();
      break;

    case CCgliss:
      gliss = value;
      updategliss();
      break;

    case CCchorusSpeed:
      chorusSpeed = value;
      if (tremeloOn > 127)
      {
        chorusSpeedstr = FASTCHORUSTEMPO[value / readresdivider];
      }
      else
      {
        chorusSpeedstr = SLOWCHORUSTEMPO[value / readresdivider];
      }
      updatechorusSpeed();
      break;

    case CCtremeloOn:
      tremeloOn = value;
      updatetremeloOn();
      break;

    case CCchorusDepth:
      chorusDepth = value;
      chorusDepthstr = LINEAR[value / readresdivider];
      updatechorusDepth();
      break;

    case CCchorusOn:
      chorusOn = value;
      updatechorusOn();
      break;

    case CCdelaySpeed:
      delaySpeed = value;
      if (delaySync > 511)
      {
        delaySpeedstr = value;
      }
      else
      {
        delaySpeedstr = DELAYTEMPO[value / readresdivider];
      }
      updatedelaySpeed();
      break;

    case CCdelayOn:
      delayOn = value;
      updatedelayOn();
      break;

    case CCdelayDepth:
      delayDepth = value;
      delayDepthstr = LINEAR[value / readresdivider];
      updatedelayDepth();
      break;

    case CCdelaySync:
      delaySync = value;
      updatedelaySync();
      break;

    case CCdelayMix:
      delayMix = value;
      delayMixstr = LINEAR[value / readresdivider];
      updatedelayMix();
      break;

    case CCribbonCourse:
      ribbonCourse = value;
      updateribbonCourse();
      break;

    case CCribbonPitch:
      ribbonPitch = value;
      updateribbonPitch();
      break;

    case CCallnotesoff:
      allNotesOff();
      break;
  }
}

void myProgramChange(byte channel, byte program)
{
  state = PATCH;
  patchNo = program + 1;
  recallPatch(patchNo);
  Serial.print("MIDI Pgm Change:");
  Serial.println(patchNo);
  state = PARAMETER;
}

void recallPatch(int patchNo)
{
  allNotesOff();
  File patchFile = SD.open(String(patchNo).c_str());
  if (!patchFile)
  {
    Serial.println("File not found");
  }
  else
  {
    String data[NO_OF_PARAMS]; //Array of data read in
    recallPatchData(patchFile, data);
    setCurrentPatchData(data);
    patchFile.close();
  }
}

void setCurrentPatchData(String data[])
{
  patchName = data[0];
  midiSync1 = data[1].toFloat();
  lfoMode1 = data[2].toFloat();
  lfoWave1 = data[3].toFloat();
  lfoSpeed1 = data[4].toFloat();
  pwm1 = data[5].toFloat();
  pw1 = data[6].toFloat();
  square1 = data[7].toFloat();
  saw1 = data[8].toFloat();
  noise1 = data[9].toFloat();
  db241 = data[10].toFloat();
  hpf1 = data[11].toFloat();
  lpf1 = data[12].toFloat();
  hpfCutoff1 = data[13].toFloat();
  hpfRes1 = data[14].toFloat();
  lpfCutoff1 = data[15].toFloat();
  lpfRes1 = data[16].toFloat();
  vcfIL1 = data[17].toFloat();
  vcfAL1 = data[18].toFloat();
  vcfAttack1 = data[19].toFloat();
  vcfDecay1 = data[20].toFloat();
  vcfRelease1 = data[21].toFloat();
  vcfLevel1 = data[22].toFloat();
  osc1Sine = data[23].toFloat();
  ampAttack1 = data[24].toFloat();
  ampDecay1 = data[25].toFloat();
  ampSustain1 = data[26].toFloat();
  ampRelease1 = data[27].toFloat();
  vcaLevel1 = data[28].toFloat();
  initial1Brill = data[29].toFloat();
  initial1Level = data[30].toFloat();
  after1Brill = data[31].toFloat();
  after1Level = data[32].toFloat();

  midiSync2 = data[33].toFloat();
  lfoMode2 = data[34].toFloat();
  lfoWave2 = data[35].toFloat();
  lfoSpeed2 = data[36].toFloat();
  pwm2 = data[37].toFloat();
  pw2 = data[38].toFloat();
  square2 = data[39].toFloat();
  saw2 = data[40].toFloat();
  noise2 = data[41].toFloat();
  db242 = data[42].toFloat();
  hpf2 = data[43].toFloat();
  lpf2 = data[44].toFloat();
  hpfCutoff2 = data[45].toFloat();
  hpfRes2 = data[46].toFloat();
  lpfCutoff2 = data[47].toFloat();
  lpfRes2 = data[48].toFloat();
  vcfIL2 = data[49].toFloat();
  vcfAL2 = data[50].toFloat();
  vcfAttack2 = data[51].toFloat();
  vcfDecay2 = data[52].toFloat();
  vcfRelease2 = data[53].toFloat();
  vcfLevel2 = data[54].toFloat();
  osc2Sine = data[55].toFloat();
  ampAttack2 = data[56].toFloat();
  ampDecay2 = data[57].toFloat();
  ampSustain2 = data[58].toFloat();
  ampRelease2 = data[59].toFloat();
  vcaLevel2 = data[60].toFloat();
  initial2Brill = data[61].toFloat();
  initial2Level = data[62].toFloat();
  after2Brill = data[63].toFloat();
  after2Level = data[64].toFloat();

  subWheel = data[65].toFloat();
  modWheelLevel = data[66].toFloat();
  detune = data[67].toFloat();
  ringAttack = data[68].toFloat();
  ringDecay = data[69].toFloat();
  ringDepth = data[70].toFloat();
  ringSpeed = data[71].toFloat();
  ringMod = data[72].toFloat();
  function = data[73].toFloat();
  subSpeed = data[74].toFloat();
  subVCO = data[75].toFloat();
  subVCF = data[76].toFloat();
  subVCA = data[77].toFloat();
  feet1 = data[78].toFloat();
  feet2 = data[79].toFloat();
  mix = data[80].toFloat();
  brill = data[81].toFloat();
  reso = data[82].toFloat();
  touchPbend = data[83].toFloat();
  touchSpeed = data[84].toFloat();
  touchVCO = data[85].toFloat();
  touchVCF = data[86].toFloat();
  brillLow = data[87].toFloat();
  brillHigh = data[88].toFloat();
  levelLow = data[89].toFloat();
  levelHigh = data[90].toFloat();
  volumeControl = data[91].toFloat();
  link = data[92].toFloat();
  sync = data[93].toFloat();

  arpSync = data[94].toFloat();
  arpSpeed = data[95].toFloat();
  arpPlay = data[96].toFloat();
  arpHold = data[97].toFloat();
  arpMode = data[98].toFloat();
  arpOctave = data[99].toFloat();
  arpRepeat = data[100].toFloat();
  expression = data[101].toFloat();
  expwah = data[102].toFloat();
  sustLongShort = data[103].toFloat();
  glissLongShort = data[104].toFloat();
  sust = data[105].toFloat();
  portaGliss = data[106].toFloat();
  sustMode = data[107].toFloat();
  gliss = data[108].toFloat();
  chorusSpeed = data[109].toFloat();
  tremeloOn = data[110].toFloat();
  chorusDepth = data[111].toFloat();
  chorusOn =  data[112].toFloat();
  delaySpeed = data[113].toFloat();
  delayOn = data[114].toFloat();
  delayDepth = data[115].toFloat();
  delaySync = data[116].toFloat();
  delayMix = data[117].toFloat();
  ribbonCourse = data[118].toFloat();
  ribbonPitch = data[119].toFloat();

  //Mux1

  updatemidiSync1();
  updatelfoMode1();
  updatelfoWave1();
  updatelfoSpeed1();
  updatepwm1();
  updatepw1();
  updatesquare1();
  updatesaw1();
  updatenoise1();
  updatedb241();
  updatehpf1();
  updatelpf1();
  updatehpfCutoff1();
  updatehpfRes1();
  updatelpfCutoff1();
  updatelpfRes1();

  //MUX 2
  updatevcfIL1();
  updatevcfAL1();
  updatevcfAttack1();
  updatevcfDecay1();
  updatevcfRelease1();
  updatevcfLevel1();
  updateosc1Sine();
  updateampAttack1();
  updateampDecay1();
  updateampSustain1();
  updateampRelease1();
  updatevcaLevel1();
  updateinitial1Brill();
  updateinitial1Level();
  updateafter1Brill();
  updateafter1Level();

  //MUX3
  updatemidiSync2();
  updatelfoMode2();
  updatelfoWave2();
  updatelfoSpeed2();
  updatepwm2();
  updatepw2();
  updatesquare2();
  updatesaw2();
  updatenoise2();
  updatedb242();
  updatehpf2();
  updatelpf2();
  updatehpfCutoff2();
  updatehpfRes2();
  updatelpfCutoff2();
  updatelpfRes2();

  //MUX4
  updatevcfIL2();
  updatevcfAL2();
  updatevcfAttack2();
  updatevcfDecay2();
  updatevcfRelease2();
  updatevcfLevel2();
  updateosc2Sine();
  updateampAttack2();
  updateampDecay2();
  updateampSustain2();
  updateampRelease2();
  updatevcaLevel2();
  updateinitial2Brill();
  updateinitial2Level();
  updateafter2Brill();
  updateafter2Level();

  //MUX5
  updatesubWheel();
  updatemodWheel();
  updatedetune();
  updateringAttack();
  updateringDecay();
  updateringDepth();
  updateringSpeed();
  updateringMod();
  updatefunction();
  updatesubSpeed();
  updatesubVCO();
  updatesubVCF();
  updatesubVCA();
  updatefeet1();
  updatefeet2();
  updatemix();

  //MUX6
  updatebrill();
  updatereso();
  updatetouchPbend();
  updatetouchSpeed();
  updatetouchVCO();
  updatetouchVCF();
  updatebrillLow();
  updatebrillHigh();
  updatelevelLow();
  updatelevelHigh();
  updatevolumeControl();
  updatelink();
  updatesync();

  //MUX7

  updatearpSync();
  updatearpSpeed();
  updatearpPlay();
  updatearpHold();
  updatearpMode();
  updatearpOctave();
  updatearpRepeat();
  updateexpression();
  updateexpwah();
  updatesustLongShort();
  updateglissLongShort();
  updatesust();
  updateportaGliss();
  updatesustMode();
  updategliss();
  updatechorusSpeed();

  //MUX8
  updatetremeloOn();
  updatechorusDepth();
  updatechorusOn();
  updatedelaySpeed();
  updatedelayOn();
  updatedelayDepth();
  updatedelaySync();
  updatedelayMix();
  updateribbonCourse();
  updateribbonPitch();


  //Patchname
  updatePatchname();

  Serial.print("Set Patch: ");
  Serial.println(patchName);
}

String getCurrentPatchData()
{
  return patchName + "," + String(midiSync1) + "," + String(lfoMode1) + "," + String(lfoWave1) + "," + String(lfoSpeed1) + "," + String(pwm1) + "," + String(pw1) + "," + String(noise1) + "," + String(db241) + "," + String(hpf1) + "," +
         String(lpf1) + "," + String(hpfCutoff1) + "," + String(hpfRes1) + "," + String(lpfCutoff1) + "," + String(lpfRes1) + "," + String(vcfIL1) + "," + String(vcfAL1) + "," + String(vcfAttack1) + "," + String(vcfDecay1) + "," +
         String(vcfRelease1) + "," + String(vcfLevel1) + "," + String(osc1Sine) + "," + String(ampAttack1) + "," + String(ampDecay1) + "," + String(ampSustain1) + "," + String(ampRelease1) + "," + String(vcaLevel1) + "," + String(initial1Brill) + "," +
         String(initial1Level) + "," + String(after1Brill) + "," + String(after1Level) + "," +
         String(midiSync2) + "," + String(lfoMode2) + "," + String(lfoWave2) + "," + String(lfoSpeed2) + "," + String(pwm2) + "," + String(pw2) + "," + String(noise2) + "," + String(db242) + "," + String(hpf2) + "," +
         String(lpf2) + "," + String(hpfCutoff2) + "," + String(hpfRes2) + "," + String(lpfCutoff2) + "," + String(lpfRes2) + "," + String(vcfIL2) + "," + String(vcfAL2) + "," + String(vcfAttack2) + "," + String(vcfDecay2) + "," +
         String(vcfRelease2) + "," + String(vcfLevel2) + "," + String(osc2Sine) + "," + String(ampAttack2) + "," + String(ampDecay2) + "," + String(ampSustain2) + "," + String(ampRelease2) + "," + String(vcaLevel2) + "," + String(initial2Brill) + "," +
         String(initial2Level) + "," + String(after2Brill) + "," + String(after2Level) + "," +
         String(subWheel) + "," + String(modWheelLevel) + "," + String(detune) + "," + String(ringAttack) + "," + String(ringDecay) + "," + String(ringDepth) + "," + String(ringSpeed) + "," + String(ringMod) + "," + String(function) + "," +
         String(subSpeed) + "," + String(subVCO) + "," + String(subVCF) + "," + String(subVCA) + "," + String(feet1) + "," + String(feet2) + "," + String(mix) + "," + String(brill) + "," + String(reso) + "," + String(touchPbend) + "," +
         String(touchSpeed) + "," + String(touchVCO) + "," + String(touchVCF) + "," + String(brillLow) + "," + String(brillHigh) + "," + String(levelLow) + "," + String(levelHigh) + "," + String(volumeControl) + "," + String(link) + "," +
         String(sync) + "," + String(arpSync) + "," + String(arpSpeed) + "," + String(arpPlay) + "," + String(arpHold) + "," + String(arpMode) + "," + String(arpOctave) + "," + String(arpRepeat) + "," + String(expression) + "," + String(expwah) + "," +
         String(sustLongShort) + "," + String(glissLongShort) + "," + String(sust) + "," + String(portaGliss) + "," + String(sustMode) + "," + String(gliss) + "," + String(chorusSpeed) + "," + String(tremeloOn) + "," + String(chorusDepth) + "," +
         String(chorusOn) + "," + String(delaySpeed) + "," + String(delayOn) + "," + String(delayDepth) + "," + String(delaySync) + "," + String(delayMix) + "," + String(ribbonCourse) + "," + String(ribbonPitch);
}

void checkMux()
{

  mux1Read = adc->adc1->analogRead(MUX1_S);
  mux2Read = adc->adc1->analogRead(MUX2_S);
  mux3Read = adc->adc1->analogRead(MUX3_S);
  mux4Read = adc->adc1->analogRead(MUX4_S);
  mux5Read = adc->adc1->analogRead(MUX5_S);
  mux6Read = adc->adc1->analogRead(MUX6_S);
  mux7Read = adc->adc0->analogRead(MUX7_S);
  mux8Read = adc->adc1->analogRead(MUX8_S);

  if (mux1Read > (mux1ValuesPrev[muxInput] + QUANTISE_FACTOR) || mux1Read < (mux1ValuesPrev[muxInput] - QUANTISE_FACTOR))
  {
    mux1ValuesPrev[muxInput] = mux1Read;

    switch (muxInput)
    {
      case MUX1_midiSync1:
        midiCCOut(CCmidiSync1, int(mux1Read / 2));
        myControlChange(midiChannel, CCmidiSync1, mux1Read);
        break;
      case MUX1_lfoMode1:
        midiCCOut(CClfoMode1, int(mux1Read / 2));
        myControlChange(midiChannel, CClfoMode1, mux1Read);
        break;
      case MUX1_lfoWave1:
        midiCCOut(CClfoWave1, int(mux1Read / 2));
        myControlChange(midiChannel, CClfoWave1, mux1Read);
        break;
      case MUX1_lfoSpeed1:
        midiCCOut(CClfoSpeed1, int(mux1Read / 2));
        myControlChange(midiChannel, CClfoSpeed1, mux1Read);
        break;
      case MUX1_pwm1:
        midiCCOut(CCpwm1, int(mux1Read / 2));
        myControlChange(midiChannel, CCpwm1, mux1Read);
        break;
      case MUX1_pw1:
        midiCCOut(CCpw1, int(mux1Read / 2));
        myControlChange(midiChannel, CCpw1, mux1Read);
        break;
      case MUX1_square1:
        midiCCOut(CCsquare1, int(mux1Read / 2));
        myControlChange(midiChannel, CCsquare1, mux1Read);
        break;
      case MUX1_saw1:
        midiCCOut(CCsaw1, int(mux1Read / 2));
        myControlChange(midiChannel, CCsaw1, mux1Read);
        break;
      case MUX1_noise1:
        midiCCOut(CCnoise1, int(mux1Read / 2));
        myControlChange(midiChannel, CCnoise1, mux1Read);
        break;
      case MUX1_db241:
        midiCCOut(CCdb241, int(mux1Read / 2));
        myControlChange(midiChannel, CCdb241, mux1Read);
        break;
      case MUX1_hpf1:
        midiCCOut(CChpf1, int(mux1Read / 2));
        myControlChange(midiChannel, CChpf1, mux1Read);
        break;
      case MUX1_lpf1:
        midiCCOut(CClpf1, int(mux1Read / 2));
        myControlChange(midiChannel, CClpf1, mux1Read);
        break;
      case MUX1_hpfCutoff1:
        midiCCOut(CChpfCutoff1, int(mux1Read / 2));
        myControlChange(midiChannel, CChpfCutoff1, mux1Read);
        break;
      case MUX1_hpfRes1:
        midiCCOut(CChpfRes1, int(mux1Read / 2));
        myControlChange(midiChannel, CChpfRes1, mux1Read);
        break;
      case MUX1_lpfCutoff1:
        midiCCOut(CClpfCutoff1, int(mux1Read / 2));
        myControlChange(midiChannel, CClpfCutoff1, mux1Read);
        break;
      case MUX1_lpfRes1:
        midiCCOut(CClpfRes1, int(mux1Read / 2));
        myControlChange(midiChannel, CClpfRes1, mux1Read);
        break;
    }
  }

  if (mux2Read > (mux2ValuesPrev[muxInput] + QUANTISE_FACTOR) || mux2Read < (mux2ValuesPrev[muxInput] - QUANTISE_FACTOR))
  {
    mux2ValuesPrev[muxInput] = mux2Read;

    switch (muxInput)
    {
      case MUX2_vcfIL1:
        midiCCOut(CCvcfIL1, int(mux2Read / 2));
        myControlChange(midiChannel, CCvcfIL1, mux2Read);
        break;
      case MUX2_vcfAL1:
        midiCCOut(CCvcfAL1, int(mux2Read / 2));
        myControlChange(midiChannel, CCvcfAL1, mux2Read);
        break;
      case MUX2_vcfAttack1:
        midiCCOut(CCvcfAttack1, int(mux2Read / 2));
        myControlChange(midiChannel, CCvcfAttack1, mux2Read);
        break;
      case MUX2_vcfDecay1:
        midiCCOut(CCvcfDecay1, int(mux2Read / 2));
        myControlChange(midiChannel, CCvcfDecay1, mux2Read);
        break;
      case MUX2_vcfRelease1:
        midiCCOut(CCvcfRelease1, int(mux2Read / 2));
        myControlChange(midiChannel, CCvcfRelease1, mux2Read);
        break;
      case MUX2_vcfLevel1:
        midiCCOut(CCvcfLevel1, int(mux2Read / 2));
        myControlChange(midiChannel, CCvcfLevel1, mux2Read);
        break;
      case MUX2_osc1Sine:
        midiCCOut(CCosc1Sine, int(mux2Read / 2));
        myControlChange(midiChannel, CCosc1Sine, mux2Read);
        break;
      case MUX2_ampAttack1:
        midiCCOut(CCampAttack1, int(mux2Read / 2));
        myControlChange(midiChannel, CCampAttack1, mux2Read);
        break;
      case MUX2_ampDecay1:
        midiCCOut(CCampDecay1, int(mux2Read / 2));
        myControlChange(midiChannel, CCampDecay1, mux2Read);
        break;
      case MUX2_ampSustain1:
        midiCCOut(CCampSustain1, int(mux2Read / 2));
        myControlChange(midiChannel, CCampSustain1, mux2Read);
        break;
      case MUX2_ampRelease1:
        midiCCOut(CCampRelease1, int(mux2Read / 2));
        myControlChange(midiChannel, CCampRelease1, mux2Read);
        break;
      case MUX2_vcaLevel1:
        midiCCOut(CCvcaLevel1, int(mux2Read / 2));
        myControlChange(midiChannel, CCvcaLevel1, mux2Read);
        break;
      case MUX2_initial1Brill:
        midiCCOut(CCinitial1Brill, int(mux2Read / 2));
        myControlChange(midiChannel, CCinitial1Brill, mux2Read);
        break;
      case MUX2_initial1Level:
        midiCCOut(CCinitial1Level, int(mux2Read / 2));
        myControlChange(midiChannel, CCinitial1Level, mux2Read);
        break;
      case MUX2_after1Brill:
        midiCCOut(CCafter1Brill, int(mux2Read / 2));
        myControlChange(midiChannel, CCafter1Brill, mux2Read);
        break;
      case MUX2_after1Level:
        midiCCOut(CCafter1Level, int(mux2Read / 2));
        myControlChange(midiChannel, CCafter1Level, mux2Read);
        break;
    }
  }

  if (mux3Read > (mux3ValuesPrev[muxInput] + QUANTISE_FACTOR) || mux3Read < (mux3ValuesPrev[muxInput] - QUANTISE_FACTOR))
  {
    mux3ValuesPrev[muxInput] = mux3Read;

    switch (muxInput)
    {
      case MUX3_midiSync2:
        midiCCOut(CCmidiSync2, int(mux3Read / 2));
        myControlChange(midiChannel, CCmidiSync2, mux3Read);
        break;
      case MUX3_lfoMode2:
        midiCCOut(CClfoMode2, int(mux3Read / 2));
        myControlChange(midiChannel, CClfoMode2, mux3Read);;
        break;
      case MUX3_lfoWave2:
        midiCCOut(CClfoWave2, int(mux3Read / 2));
        myControlChange(midiChannel, CClfoWave2, mux3Read);
        break;
      case MUX3_lfoSpeed2:
        midiCCOut(CClfoSpeed2, int(mux3Read / 2));
        myControlChange(midiChannel, CClfoSpeed2, mux3Read);
        break;
      case MUX3_pwm2:
        midiCCOut(CCpwm2, int(mux3Read / 2));
        myControlChange(midiChannel, CCpwm2, mux3Read);
        break;
      case MUX3_pw2:
        midiCCOut(CCpw2, int(mux3Read / 2));
        myControlChange(midiChannel, CCpw2, mux3Read);
        break;
      case MUX3_square2:
        midiCCOut(CCsquare2, int(mux3Read / 2));
        myControlChange(midiChannel, CCsquare2, mux3Read);
        break;
      case MUX3_saw2:
        midiCCOut(CCsaw2, int(mux3Read / 2));
        myControlChange(midiChannel, CCsaw2, mux3Read);
        break;
      case MUX3_noise2:
        midiCCOut(CCnoise2, int(mux3Read / 2));
        myControlChange(midiChannel, CCnoise2, mux3Read);
        break;
      case MUX3_lpf2:
        midiCCOut(CClpf2, int(mux3Read / 2));
        myControlChange(midiChannel, CClpf2, mux3Read);
        break;
      case MUX3_hpf2:
        midiCCOut(CChpf2, int(mux3Read / 2));
        myControlChange(midiChannel, CChpf2, mux3Read);
        break;
      case MUX3_db242:
        midiCCOut(CCdb242, int(mux3Read / 2));
        myControlChange(midiChannel, CCdb242, mux3Read);
        break;
      case MUX3_hpfCutoff2:
        midiCCOut(CChpfCutoff2, int(mux3Read / 2));
        myControlChange(midiChannel, CChpfCutoff2, mux3Read);
        break;
      case MUX3_hpfRes2:
        midiCCOut(CChpfRes2, int(mux3Read / 2));
        myControlChange(midiChannel, CChpfRes2, mux3Read);
        break;
      case MUX3_lpfCutoff2:
        midiCCOut(CClpfCutoff2, int(mux3Read / 2));
        myControlChange(midiChannel, CClpfCutoff2, mux3Read);
        break;
      case MUX3_lpfRes2:
        midiCCOut(CClpfRes2, int(mux3Read / 2));
        myControlChange(midiChannel, CClpfRes2, mux3Read);
        break;
    }
  }

  if (mux4Read > (mux4ValuesPrev[muxInput] + QUANTISE_FACTOR) || mux4Read < (mux4ValuesPrev[muxInput] - QUANTISE_FACTOR))
  {
    mux4ValuesPrev[muxInput] = mux4Read;

    switch (muxInput)
    {
      case MUX4_vcfIL2:
        midiCCOut(CCvcfIL2, int(mux4Read / 2));
        myControlChange(midiChannel, CCvcfIL2, mux4Read);
        break;
      case MUX4_vcfAL2:
        midiCCOut(CCvcfAL2, int(mux4Read / 2));
        myControlChange(midiChannel, CCvcfAL2, mux4Read);
        break;
      case MUX4_vcfAttack2:
        midiCCOut(CCvcfAttack2, int(mux4Read / 2));
        myControlChange(midiChannel, CCvcfAttack2, mux4Read);
        break;
      case MUX4_vcfDecay2:
        midiCCOut(CCvcfDecay2, int(mux4Read / 2));
        myControlChange(midiChannel, CCvcfDecay2, mux4Read);
        break;
      case MUX4_vcfRelease2:
        midiCCOut(CCvcfRelease2, int(mux4Read / 2));
        myControlChange(midiChannel, CCvcfRelease2, mux4Read);
        break;
      case MUX4_vcfLevel2:
        midiCCOut(CCvcfLevel2, int(mux4Read / 2));
        myControlChange(midiChannel, CCvcfLevel2, mux4Read);
        break;
      case MUX4_osc2Sine:
        midiCCOut(CCosc2Sine, int(mux4Read / 2));
        myControlChange(midiChannel, CCosc2Sine, mux4Read);
        break;
      case MUX4_ampAttack2:
        midiCCOut(CCampAttack2, int(mux4Read / 2));
        myControlChange(midiChannel, CCampAttack2, mux4Read);
        break;
      case MUX4_ampDecay2:
        midiCCOut(CCampDecay2, int(mux4Read / 2));
        myControlChange(midiChannel, CCampDecay2, mux4Read);
        break;
      case MUX4_ampSustain2:
        midiCCOut(CCampSustain2, int(mux4Read / 2));
        myControlChange(midiChannel, CCampSustain2, mux4Read);
        break;
      case MUX4_ampRelease2:
        midiCCOut(CCampRelease2, int(mux4Read / 2));
        myControlChange(midiChannel, CCampRelease2, mux4Read);
        break;
      case MUX4_vcaLevel2:
        midiCCOut(CCvcaLevel2, int(mux4Read / 2));
        myControlChange(midiChannel, CCvcaLevel2, mux4Read);
        break;
      case MUX4_initial2Brill:
        midiCCOut(CCinitial2Brill, int(mux4Read / 2));
        myControlChange(midiChannel, CCinitial2Brill, mux4Read);
        break;
      case MUX4_initial2Level:
        midiCCOut(CCinitial2Level, int(mux4Read / 2));
        myControlChange(midiChannel, CCinitial2Level, mux4Read);
        break;
      case MUX4_after2Brill:
        midiCCOut(CCafter2Brill, int(mux4Read / 2));
        myControlChange(midiChannel, CCafter2Brill, mux4Read);
        break;
      case MUX4_after2Level:
        midiCCOut(CCafter2Level, int(mux4Read / 2));
        myControlChange(midiChannel, CCafter2Level, mux4Read);
        break;
    }
  }

  if (mux5Read > (mux5ValuesPrev[muxInput] + QUANTISE_FACTOR) || mux5Read < (mux5ValuesPrev[muxInput] - QUANTISE_FACTOR))
  {
    mux5ValuesPrev[muxInput] = mux5Read;

    switch (muxInput)
    {
      case MUX5_subWheel:
        midiCCOut(CCsubWheel, int(mux5Read / 2));
        myControlChange(midiChannel, CCsubWheel, mux5Read);
        break;
      case MUX5_modwheel:
        midiCCOut(CCmodwheel, int(mux5Read / 2));
        myControlChange(midiChannel, CCmodwheel, mux5Read);
        break;
      case MUX5_detune:
        midiCCOut(CCdetune, int(mux5Read / 2));
        myControlChange(midiChannel, CCdetune, mux5Read);
        break;
      case MUX5_ringAttack:
        midiCCOut(CCringAttack, int(mux5Read / 2));
        myControlChange(midiChannel, CCringAttack, mux5Read);
        break;
      case MUX5_ringDecay:
        midiCCOut(CCringDecay, int(mux5Read / 2));
        myControlChange(midiChannel, CCringDecay, mux5Read);
        break;
      case MUX5_ringDepth:
        midiCCOut(CCringDepth, int(mux5Read / 2));
        myControlChange(midiChannel, CCringDepth, mux5Read);
        break;
      case MUX5_ringSpeed:
        midiCCOut(CCringSpeed, int(mux5Read / 2));
        myControlChange(midiChannel, CCringSpeed, mux5Read);
        break;
      case MUX5_ringMod:
        midiCCOut(CCringMod, int(mux5Read / 2));
        myControlChange(midiChannel, CCringMod, mux5Read);
        break;
      case MUX5_function:
        midiCCOut(CCfunction, int(mux5Read / 2));
        myControlChange(midiChannel, CCfunction, mux5Read);
        break;
      case MUX5_subSpeed:
        midiCCOut(CCsubSpeed, int(mux5Read / 2));
        myControlChange(midiChannel, CCsubSpeed, mux5Read);
        break;
      case MUX5_subVCO:
        midiCCOut(CCsubVCO, int(mux5Read / 2));
        myControlChange(midiChannel, CCsubVCO, mux5Read);
        break;
      case MUX5_subVCF:
        midiCCOut(CCsubVCF, int(mux5Read / 2));
        myControlChange(midiChannel, CCsubVCF, mux5Read);
        break;
      case MUX5_subVCA:
        midiCCOut(CCsubVCA, int(mux5Read / 2));
        myControlChange(midiChannel, CCsubVCA, mux5Read);
        break;
      case MUX5_feet1:
        midiCCOut(CCfeet1, int(mux5Read / 2));
        myControlChange(midiChannel, CCfeet1, mux5Read);
        break;
      case MUX5_feet2:
        midiCCOut(CCfeet2, int(mux5Read / 2));
        myControlChange(midiChannel, CCfeet2, mux5Read);
        break;
      case MUX5_mix:
        midiCCOut(CCmix, int(mux5Read / 2));
        myControlChange(midiChannel, CCmix, mux5Read);
        break;
    }
  }

  if (mux6Read > (mux6ValuesPrev[muxInput] + QUANTISE_FACTOR) || mux6Read < (mux6ValuesPrev[muxInput] - QUANTISE_FACTOR))
  {
    mux6ValuesPrev[muxInput] = mux6Read;

    switch (muxInput)
    {
      case MUX6_brilliance:
        midiCCOut(CCbrilliance, int(mux6Read / 2));
        myControlChange(midiChannel, CCbrilliance, mux6Read);
        break;
      case MUX6_reso:
        midiCCOut(CCreso, int(mux6Read / 2));
        myControlChange(midiChannel, CCreso, mux6Read);
        break;
      case MUX6_touchPbend:
        midiCCOut(CCtouchPbend, int(mux6Read / 2));
        myControlChange(midiChannel, CCtouchPbend, mux6Read);
        break;
      case MUX6_touchSpeed:
        midiCCOut(CCtouchSpeed, int(mux6Read / 2));
        myControlChange(midiChannel, CCtouchSpeed, mux6Read);
        break;
      case MUX6_touchVCO:
        midiCCOut(CCtouchVCO, int(mux6Read / 2));
        myControlChange(midiChannel, CCtouchVCO, mux6Read);
        break;
      case MUX6_touchVCF:
        midiCCOut(CCtouchVCF, int(mux6Read / 2));
        myControlChange(midiChannel, CCtouchVCF, mux6Read);
        break;
      case MUX6_brillLow:
        midiCCOut(CCbrillLow, int(mux6Read / 2));
        myControlChange(midiChannel, CCbrillLow, mux6Read);
        break;
      case MUX6_brillHigh:
        midiCCOut(CCbrillHigh, int(mux6Read / 2));
        myControlChange(midiChannel, CCbrillHigh, mux6Read);
        break;
      case MUX6_levelLow:
        midiCCOut(CClevelLow, int(mux6Read / 2));
        myControlChange(midiChannel, CClevelLow, mux6Read);
        break;
      case MUX6_levelHigh:
        midiCCOut(CClevelHigh, int(mux6Read / 2));
        myControlChange(midiChannel, CClevelHigh, mux6Read);
        break;
      case MUX6_volume:
        midiCCOut(CCvolumeControl, int(mux6Read / 2));
        myControlChange(midiChannel, CCvolumeControl, mux6Read);
        break;
      case MUX6_link:
        midiCCOut(CClink, int(mux6Read / 2));
        myControlChange(midiChannel, CClink, mux6Read);
        break;
      case MUX6_sync:
        midiCCOut(CCsync, int(mux6Read / 2));
        myControlChange(midiChannel, CCsync, mux6Read);
        break;
        //      case MUX6_13:
        //        myControlChange(midiChannel, CCLfoSlope, mux6Read);
        //        break;
        //      case MUX6_14:
        //        myControlChange(midiChannel, CCfilterType, mux6Read);
        //        break;
        //      case MUX6_15:
        //        midiCCOut(CCnoiseLevel, int(mux6Read / 2));
        //        myControlChange(midiChannel, CCnoiseLevel, mux6Read);
        //        break;
    }
  }

  if (mux7Read > (mux7ValuesPrev[muxInput] + QUANTISE_FACTOR) || mux7Read < (mux7ValuesPrev[muxInput] - QUANTISE_FACTOR))
  {
    mux7ValuesPrev[muxInput] = mux7Read;

    switch (muxInput)
    {
      case MUX7_arpSync:
        midiCCOut(CCarpSync, int(mux7Read / 2));
        myControlChange(midiChannel, CCarpSync, mux7Read);
        break;
      case MUX7_arpSpeed:
        midiCCOut(CCarpSpeed, int(mux7Read / 2));
        myControlChange(midiChannel, CCarpSpeed, mux7Read);
        break;
      case MUX7_arpPlay:
        midiCCOut(CCarpPlay, int(mux7Read / 2));
        myControlChange(midiChannel, CCarpPlay, mux7Read);
        break;
      case MUX7_arpHold:
        midiCCOut(CCarpHold, int(mux7Read / 2));
        myControlChange(midiChannel, CCarpHold, mux7Read);
        break;
      case MUX7_arpMode:
        midiCCOut(CCarpMode, int(mux7Read / 2));
        myControlChange(midiChannel, CCarpMode, mux7Read);
        break;
      case MUX7_arpOctave:
        midiCCOut(CCarpOctave, int(mux7Read / 2));
        myControlChange(midiChannel, CCarpOctave, mux7Read);
        break;
      case MUX7_arpRepeat:
        midiCCOut(CCarpRepeat, int(mux7Read / 2));
        myControlChange(midiChannel, CCarpRepeat, mux7Read);
        break;
      case MUX7_expression:
        midiCCOut(CCexpression, int(mux7Read / 2));
        myControlChange(midiChannel, CCexpression, mux7Read);
        break;
      case MUX7_expwah:
        midiCCOut(CCexpwah, int(mux7Read / 2));
        myControlChange(midiChannel, CCexpwah, mux7Read);
        break;
      case MUX7_sustLongShort:
        midiCCOut(CCsustLongShort, int(mux7Read / 2));
        myControlChange(midiChannel, CCsustLongShort, mux7Read);
        break;
      case MUX7_glissLongShort:
        midiCCOut(CCglissLongShort, int(mux7Read / 2));
        myControlChange(midiChannel, CCglissLongShort, mux7Read);
        break;
      case MUX7_sust:
        midiCCOut(CCsust, int(mux7Read / 2));
        myControlChange(midiChannel, CCsust, mux7Read);
        break;
      case MUX7_portaGliss:
        midiCCOut(CCportaGliss, int(mux7Read / 2));
        myControlChange(midiChannel, CCportaGliss, mux7Read);
        break;
      case MUX7_sustMode:
        midiCCOut(CCsustMode, int(mux7Read / 2));
        myControlChange(midiChannel, CCsustMode, mux7Read);
        break;
      case MUX7_gliss:
        midiCCOut(CCgliss, int(mux7Read / 2));
        myControlChange(midiChannel, CCgliss, mux7Read);
        break;
      case MUX7_chorusSpeed:
        midiCCOut(CCchorusSpeed, int(mux7Read / 2));
        myControlChange(midiChannel, CCchorusSpeed, mux7Read);
        break;
    }
  }

  if (mux8Read > (mux8ValuesPrev[muxInput] + QUANTISE_FACTOR) || mux8Read < (mux8ValuesPrev[muxInput] - QUANTISE_FACTOR))
  {
    mux8ValuesPrev[muxInput] = mux8Read;

    switch (muxInput)
    {
      case MUX8_tremeloOn:
        midiCCOut(CCtremeloOn, int(mux8Read / 2));
        myControlChange(midiChannel, CCtremeloOn, mux8Read);
        break;
      case MUX8_chorusDepth:
        midiCCOut(CCchorusDepth, int(mux8Read / 2));
        myControlChange(midiChannel, CCchorusDepth, mux8Read);
        break;
      case MUX8_chorusOn:
        midiCCOut(CCchorusOn, int(mux8Read / 2));
        myControlChange(midiChannel, CCchorusOn, mux8Read);
        break;
      case MUX8_delaySpeed:
        midiCCOut(CCdelaySpeed, int(mux8Read / 2));
        myControlChange(midiChannel, CCdelaySpeed, mux8Read);
        break;
      case MUX8_delayOn:
        midiCCOut(CCdelayOn, int(mux8Read / 2));
        myControlChange(midiChannel, CCdelayOn, mux8Read);
        break;
      case MUX8_delayDepth:
        midiCCOut(CCdelayDepth, int(mux8Read / 2));
        myControlChange(midiChannel, CCdelayDepth, mux8Read);
        break;
      case MUX8_delaySync:
        midiCCOut(CCdelaySync, int(mux8Read / 2));
        myControlChange(midiChannel, CCdelaySync, mux8Read);
        break;
      case MUX8_delayMix:
        midiCCOut(CCdelayMix, int(mux8Read / 2));
        myControlChange(midiChannel, CCdelayMix, mux8Read);
        break;
      case MUX8_ribbonCourse:
        midiCCOut(CCribbonCourse, int(mux8Read / 2));
        myControlChange(midiChannel, CCribbonCourse, mux8Read);
        break;
      case MUX8_ribbonPitch:
        midiCCOut(CCribbonPitch, int(mux8Read / 2));
        myControlChange(midiChannel, CCribbonPitch, mux8Read);
        break;
        //      case MUX8_10:
        //        myControlChange(midiChannel, CCosc2PW, mux8Read);
        //        break;
        //      case MUX8_11:
        //        myControlChange(midiChannel, CCosc2PWM, mux8Read);
        //        break;
        //      case MUX8_12:
        //        myControlChange(midiChannel, CCosc1Detune, mux8Read);
        //        break;
        //      case MUX8_13:
        //        myControlChange(midiChannel, CCLfoSlope, mux8Read);
        //        break;
        //      case MUX8_14:
        //        myControlChange(midiChannel, CCfilterType, mux8Read);
        //        break;
        //      case MUX8_15:
        //        midiCCOut(CCnoiseLevel, int(mux8Read / 2));
        //        myControlChange(midiChannel, CCnoiseLevel, mux8Read);
        //        break;
    }
  }

  muxInput++;
  if (muxInput >= MUXCHANNELS)
    muxInput = 0;

  digitalWriteFast(MUX_0, muxInput & B0001);
  digitalWriteFast(MUX_1, muxInput & B0010);
  digitalWriteFast(MUX_2, muxInput & B0100);
  digitalWriteFast(MUX_3, muxInput & B1000);

}

void midiCCOut(byte cc, byte value)
{
  if (midiOutCh > 0)
  {
    switch (ccType)
    {
      case 0:
        {
          usbMIDI.sendControlChange(cc, value, midiOutCh); //MIDI DIN is set to Out
          midi1.sendControlChange(cc, value, midiOutCh);
          MIDI.sendControlChange(cc, value, midiOutCh); //MIDI DIN is set to Out
          break;
        }
      case 1:
        {
          usbMIDI.sendControlChange(99, 0, midiOutCh); //MIDI DIN is set to Out
          usbMIDI.sendControlChange(98, cc, midiOutCh); //MIDI DIN is set to Out
          usbMIDI.sendControlChange(38, value, midiOutCh); //MIDI DIN is set to Out
          usbMIDI.sendControlChange(6, 0, midiOutCh); //MIDI DIN is set to Out

          midi1.sendControlChange(99, 0, midiOutCh); //MIDI DIN is set to Out
          midi1.sendControlChange(98, cc, midiOutCh); //MIDI DIN is set to Out
          midi1.sendControlChange(38, value, midiOutCh); //MIDI DIN is set to Out
          midi1.sendControlChange(6, 0, midiOutCh); //MIDI DIN is set to Out

          MIDI.sendControlChange(99, 0, midiOutCh); //MIDI DIN is set to Out
          MIDI.sendControlChange(98, cc, midiOutCh); //MIDI DIN is set to Out
          MIDI.sendControlChange(38, value, midiOutCh); //MIDI DIN is set to Out
          MIDI.sendControlChange(6, 0, midiOutCh); //MIDI DIN is set to Out
          break;
        }
      case 2:
        {
          Serial.println(ccType);
          break;
        }
    }
  }
}

void checkSwitches()
{

  saveButton.update();
  if (saveButton.read() == LOW && saveButton.duration() > HOLD_DURATION)
  {
    switch (state)
    {
      case PARAMETER:
      case PATCH:
        state = DELETE;
        saveButton.write(HIGH); //Come out of this state
        del = true;             //Hack
        break;
    }
  }
  else if (saveButton.risingEdge())
  {
    if (!del)
    {
      switch (state)
      {
        case PARAMETER:
          if (patches.size() < PATCHES_LIMIT)
          {
            resetPatchesOrdering(); //Reset order of patches from first patch
            patches.push({patches.size() + 1, INITPATCHNAME});
            state = SAVE;
          }
          break;
        case SAVE:
          //Save as new patch with INITIALPATCH name or overwrite existing keeping name - bypassing patch renaming
          patchName = patches.last().patchName;
          state = PATCH;
          savePatch(String(patches.last().patchNo).c_str(), getCurrentPatchData());
          showPatchPage(patches.last().patchNo, patches.last().patchName);
          patchNo = patches.last().patchNo;
          loadPatches(); //Get rid of pushed patch if it wasn't saved
          setPatchesOrdering(patchNo);
          renamedPatch = "";
          state = PARAMETER;
          break;
        case PATCHNAMING:
          if (renamedPatch.length() > 0) patchName = renamedPatch;//Prevent empty strings
          state = PATCH;
          savePatch(String(patches.last().patchNo).c_str(), getCurrentPatchData());
          showPatchPage(patches.last().patchNo, patchName);
          patchNo = patches.last().patchNo;
          loadPatches(); //Get rid of pushed patch if it wasn't saved
          setPatchesOrdering(patchNo);
          renamedPatch = "";
          state = PARAMETER;
          break;
      }
    }
    else
    {
      del = false;
    }
  }

  settingsButton.update();
  if (settingsButton.read() == LOW && settingsButton.duration() > HOLD_DURATION)
  {
    //If recall held, set current patch to match current hardware state
    //Reinitialise all hardware values to force them to be re-read if different
    state = REINITIALISE;
    reinitialiseToPanel();
    settingsButton.write(HIGH); //Come out of this state
    reini = true;           //Hack
  }
  else if (settingsButton.risingEdge())
  { //cannot be fallingEdge because holding button won't work
    if (!reini)
    {
      switch (state)
      {
        case PARAMETER:
          settingsValueIndex = getCurrentIndex(settingsOptions.first().currentIndex);
          showSettingsPage(settingsOptions.first().option, settingsOptions.first().value[settingsValueIndex], SETTINGS);
          state = SETTINGS;
          break;
        case SETTINGS:
          settingsOptions.push(settingsOptions.shift());
          settingsValueIndex = getCurrentIndex(settingsOptions.first().currentIndex);
          showSettingsPage(settingsOptions.first().option, settingsOptions.first().value[settingsValueIndex], SETTINGS);
        case SETTINGSVALUE:
          //Same as pushing Recall - store current settings item and go back to options
          settingsHandler(settingsOptions.first().value[settingsValueIndex], settingsOptions.first().handler);
          showSettingsPage(settingsOptions.first().option, settingsOptions.first().value[settingsValueIndex], SETTINGS);
          state = SETTINGS;
          break;
      }
    }
    else
    {
      reini = false;
    }
  }

  backButton.update();
  if (backButton.read() == LOW && backButton.duration() > HOLD_DURATION)
  {
    //If Back button held, Panic - all notes off
    allNotesOff();
    backButton.write(HIGH); //Come out of this state
    panic = true;           //Hack
  }
  else if (backButton.risingEdge())
  { //cannot be fallingEdge because holding button won't work
    if (!panic)
    {
      switch (state)
      {
        case RECALL:
          setPatchesOrdering(patchNo);
          state = PARAMETER;
          break;
        case SAVE:
          renamedPatch = "";
          state = PARAMETER;
          loadPatches();//Remove patch that was to be saved
          setPatchesOrdering(patchNo);
          break;
        case PATCHNAMING:
          charIndex = 0;
          renamedPatch = "";
          state = SAVE;
          break;
        case DELETE:
          setPatchesOrdering(patchNo);
          state = PARAMETER;
          break;
        case SETTINGS:
          state = PARAMETER;
          break;
        case SETTINGSVALUE:
          settingsValueIndex = getCurrentIndex(settingsOptions.first().currentIndex);
          showSettingsPage(settingsOptions.first().option, settingsOptions.first().value[settingsValueIndex], SETTINGS);
          state = SETTINGS;
          break;
      }
    }
    else
    {
      panic = false;
    }
  }

  //Encoder switch
  recallButton.update();
  if (recallButton.read() == LOW && recallButton.duration() > HOLD_DURATION)
  {
    //If Recall button held, return to current patch setting
    //which clears any changes made
    state = PATCH;
    //Recall the current patch
    patchNo = patches.first().patchNo;
    recallPatch(patchNo);
    state = PARAMETER;
    recallButton.write(HIGH); //Come out of this state
    recall = true;            //Hack
  }
  else if (recallButton.risingEdge())
  {
    if (!recall)
    {
      switch (state)
      {
        case PARAMETER:
          state = RECALL;//show patch list
          break;
        case RECALL:
          state = PATCH;
          //Recall the current patch
          patchNo = patches.first().patchNo;
          recallPatch(patchNo);
          state = PARAMETER;
          break;
        case SAVE:
          showRenamingPage(patches.last().patchName);
          patchName  = patches.last().patchName;
          state = PATCHNAMING;
          break;
        case PATCHNAMING:
          if (renamedPatch.length() < 13)
          {
            renamedPatch.concat(String(currentCharacter));
            charIndex = 0;
            currentCharacter = CHARACTERS[charIndex];
            showRenamingPage(renamedPatch);
          }
          break;
        case DELETE:
          //Don't delete final patch
          if (patches.size() > 1)
          {
            state = DELETEMSG;
            patchNo = patches.first().patchNo;//PatchNo to delete from SD card
            patches.shift();//Remove patch from circular buffer
            deletePatch(String(patchNo).c_str());//Delete from SD card
            loadPatches();//Repopulate circular buffer to start from lowest Patch No
            renumberPatchesOnSD();
            loadPatches();//Repopulate circular buffer again after delete
            patchNo = patches.first().patchNo;//Go back to 1
            recallPatch(patchNo);//Load first patch
          }
          state = PARAMETER;
          break;
        case SETTINGS:
          //Choose this option and allow value choice
          settingsValueIndex = getCurrentIndex(settingsOptions.first().currentIndex);
          showSettingsPage(settingsOptions.first().option, settingsOptions.first().value[settingsValueIndex], SETTINGSVALUE);
          state = SETTINGSVALUE;
          break;
        case SETTINGSVALUE:
          //Store current settings item and go back to options
          settingsHandler(settingsOptions.first().value[settingsValueIndex], settingsOptions.first().handler);
          showSettingsPage(settingsOptions.first().option, settingsOptions.first().value[settingsValueIndex], SETTINGS);
          state = SETTINGS;
          break;
      }
    }
    else
    {
      recall = false;
    }
  }
}

void reinitialiseToPanel()
{
  //This sets the current patch to be the same as the current hardware panel state - all the pots
  //The four button controls stay the same state
  //This reinialises the previous hardware values to force a re-read
  muxInput = 0;
  for (int i = 0; i < MUXCHANNELS; i++)
  {
    mux1ValuesPrev[i] = RE_READ;
    mux2ValuesPrev[i] = RE_READ;
    mux3ValuesPrev[i] = RE_READ;
    mux4ValuesPrev[i] = RE_READ;
    mux5ValuesPrev[i] = RE_READ;
    mux6ValuesPrev[i] = RE_READ;
    mux7ValuesPrev[i] = RE_READ;
    mux8ValuesPrev[i] = RE_READ;

  }
  patchName = INITPATCHNAME;
  showPatchPage("Initial", "Panel Settings");
}

void checkEncoder()
{
  //Encoder works with relative inc and dec values
  //Detent encoder goes up in 4 steps, hence +/-3

  long encRead = encoder.read();
  if ((encCW && encRead > encPrevious + 3) || (!encCW && encRead < encPrevious - 3) )
  {
    switch (state)
    {
      case PARAMETER:
        state = PATCH;
        patches.push(patches.shift());
        patchNo = patches.first().patchNo;
        recallPatch(patchNo);
        state = PARAMETER;
        break;
      case RECALL:
        patches.push(patches.shift());
        break;
      case SAVE:
        patches.push(patches.shift());
        break;
      case PATCHNAMING:
        if (charIndex == TOTALCHARS) charIndex = 0;//Wrap around
        currentCharacter = CHARACTERS[charIndex++];
        showRenamingPage(renamedPatch + currentCharacter);
        break;
      case DELETE:
        patches.push(patches.shift());
        break;
      case SETTINGS:
        settingsOptions.push(settingsOptions.shift());
        settingsValueIndex = getCurrentIndex(settingsOptions.first().currentIndex);
        showSettingsPage(settingsOptions.first().option, settingsOptions.first().value[settingsValueIndex] , SETTINGS);
        break;
      case SETTINGSVALUE:
        if (settingsOptions.first().value[settingsValueIndex + 1] != '\0')
          showSettingsPage(settingsOptions.first().option, settingsOptions.first().value[++settingsValueIndex], SETTINGSVALUE);
        break;
    }
    encPrevious = encRead;
  }
  else if ((encCW && encRead < encPrevious - 3) || (!encCW && encRead > encPrevious + 3))
  {
    switch (state)
    {
      case PARAMETER:
        state = PATCH;
        patches.unshift(patches.pop());
        patchNo = patches.first().patchNo;
        recallPatch(patchNo);
        state = PARAMETER;
        break;
      case RECALL:
        patches.unshift(patches.pop());
        break;
      case SAVE:
        patches.unshift(patches.pop());
        break;
      case PATCHNAMING:
        if (charIndex == -1)
          charIndex = TOTALCHARS - 1;
        currentCharacter = CHARACTERS[charIndex--];
        showRenamingPage(renamedPatch + currentCharacter);
        break;
      case DELETE:
        patches.unshift(patches.pop());
        break;
      case SETTINGS:
        settingsOptions.unshift(settingsOptions.pop());
        settingsValueIndex = getCurrentIndex(settingsOptions.first().currentIndex);
        showSettingsPage(settingsOptions.first().option, settingsOptions.first().value[settingsValueIndex], SETTINGS);
        break;
      case SETTINGSVALUE:
        if (settingsValueIndex > 0)
          showSettingsPage(settingsOptions.first().option, settingsOptions.first().value[--settingsValueIndex], SETTINGSVALUE);
        break;
    }
    encPrevious = encRead;
  }
}

void loop()
{

  checkMux();
  checkSwitches();
  checkEncoder();

}
