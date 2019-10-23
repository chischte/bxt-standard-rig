/*
 * *****************************************************************************
 * BXT STANDARD RIG
 * *****************************************************************************
 * Program to control the BXT standard Rig
 * *****************************************************************************
 * Michael Wettstein
 * October 2019, Zürich
 * *****************************************************************************
 */

#include <Controllino.h>    // https://github.com/CONTROLLINO-PLC/CONTROLLINO_Library
#include <Cylinder.h>       // https://github.com/chischte/cylinder-library
#include <Debounce.h>       // https://github.com/chischte/debounce-library
#include <EEPROM_Counter.h> // https://github.com/chischte/eeprom-counter-library
#include <Insomnia.h>       // https://github.com/chischte/insomnia-delay-library

#include "StateController.h" // contains all machine states

//******************************************************************************
// DEFINE NAMES AND SEQUENCE OF STEPS FOR THE MAIN CYCLE:
//******************************************************************************
enum mainCycleSteps {
  BremszylinderZurueckfahren,
  ToolAufwecken,
  BandVorschieben,
  BandKlemmen,
  BandSpannen,
  BandSchneiden,
  Schweissen,
  WippenhebelZiehen,
  BandklemmeLoesen,
  endOfMainCycleEnum
};
byte numberOfMainCycleSteps = endOfMainCycleEnum;

// DEFINE NAMES TO DISPLAY ON THE TOUCH SCREEN:
String cycleName[] = { "Bremszylinder zurueckfahren", "Tool aufwecken", "Band vorschieben",
    "Band klemmen", "Band spannen", "Band schneiden", "Schweissen", "Wippenhebel ziehen",
    "Bandklemme loesen" };

//******************************************************************************
// SETUP EEPROM ERROR LOG:
//******************************************************************************
// create one storage slot for every mainCycleStep to count timeout errors
int numberOfEepromValues = (endOfMainCycleEnum - 1);
int eepromSize = 4096;
EEPROM_Counter eepromErrorLog(eepromSize, numberOfEepromValues);

//******************************************************************************
// DECLARATION OF VARIABLES
//******************************************************************************
// INTERRUPT SERVICE ROUTINE:
volatile bool toggleMachineState = false;  //******************************************************************************
// eepromErrorLog.setAllZero(); // to reset the error counter
//****************************************************************************** */
volatile bool errorBlinkState = false;

// PINS:
const byte startStopInterruptPin = CONTROLLINO_IN1;
const byte errorBlinkRelay = CONTROLLINO_R0;
//******************************************************************************
// GENERATE INSTANCES OF CLASSES:
//******************************************************************************
Cylinder BandKlemmZylinder(6);
Cylinder SpanntastenZylinder(7);
Cylinder BremsZylinder(5);
Cylinder SchweisstastenZylinder(8);
Cylinder WippenhebelZylinder(9);
Cylinder MesserZylinder(10);

Debounce ModeSwitch(CONTROLLINO_A2);
Debounce EndSwitchLeft(CONTROLLINO_A5);
Debounce EndSwitchRight(CONTROLLINO_A0);
Debounce StrapDetectionSensor(A1);

Insomnia errorBlinkTimer;
Insomnia resetTimeout(40 * 1000L); // reset rig after 40 seconds inactivity

StateController stateController(numberOfMainCycleSteps);
//******************************************************************************

unsigned long ReadCoolingPot() {
  int potVal = analogRead(CONTROLLINO_A4);
  unsigned long coolingTime = map(potVal, 1023, 0, 4000, 14000); // Abkühlzeit min 4, max 14 Sekunden
  return coolingTime;
}

void PrintErrorLog() {
  Serial.println("ERROR LIST: ");
  for (int i = 0; i < numberOfMainCycleSteps; i++) {
    if (eepromErrorLog.getValue(i) != 0) {
      Serial.print(cycleName[i]);
      Serial.print(" ");
      Serial.print(eepromErrorLog.getValue(i));
      Serial.println("x Timeout");
    }
  }
  Serial.println();
}

void WriteErrorLog() {
  eepromErrorLog.countOneUp(stateController.currentCycleStep()); // count the error in the eeprom log of the current step
}

void PrintCurrentStep() {
  Serial.print(stateController.currentCycleStep());
  Serial.print(" ");
  Serial.println(cycleName[stateController.currentCycleStep()]);
}

void RunTimeoutManager() {
  static byte shutOffCounter;
  byte maxNoOfTimeoutsInARow = 3;

  //DETECTING THE RIGHT ENDSWITCH SHOWS THAT RIG IS MOVING AND RESETS THE TIMEOUT
  if (EndSwitchRight.switchedHigh()) {
    resetTimeout.resetTime();
    shutOffCounter = 0;
  }
  // IF TIMEOUT HAPPENED:
  if (resetTimeout.timedOut()) {
    WriteErrorLog();
    PrintErrorLog();
    shutOffCounter++;
    Serial.print("TIMEOUT NUMMER ");
    Serial.print(shutOffCounter);
    // RESET:
    if (shutOffCounter < maxNoOfTimeoutsInARow) {
      Serial.println(" > RUN RESET");
      stateController.setRunAfterReset(1);
      stateController.setResetMode(1);
      resetTimeout.resetTime();
    } else {
      // OR SHUT OFF:
      Serial.println(" > STOP RIG");
      StopTestRig();
      errorBlinkState = 1; // error blink starts
      shutOffCounter = 0;
    }
  }
}

void ResetTestRig() {
  static byte resetStage = 1;
  stateController.setMachineRunningState(0);

  if (resetStage == 1) {
    ResetCylinderStates();
    resetStage++;
  }
  if (resetStage == 2) {
    WippenhebelZylinder.stroke(1500, 0);
    if (WippenhebelZylinder.stroke_completed()) {
      resetStage++;
    }
  }
  if (resetStage == 3) {
    stateController.setCycleStepTo(0);
    stateController.setResetMode(0);
    bool runAfterReset = stateController.runAfterReset();
    stateController.setMachineRunningState(runAfterReset);
    resetStage = 1;
  }
}

void StopTestRig() {
  ResetCylinderStates();
  stateController.setMachineRunningState(false);
}

void ResetCylinderStates() {
  BremsZylinder.set(0);
  SpanntastenZylinder.set(0);
  SchweisstastenZylinder.set(0);
  WippenhebelZylinder.set(0);
  MesserZylinder.set(0);
  BandKlemmZylinder.set(0);
}

void ToggleMachineRunningISR() {
  static unsigned long previousInterruptTime;
  unsigned long interruptDebounceTime = 200;
  if (millis() - previousInterruptTime > interruptDebounceTime) {
    // TEST RIG EIN- ODER AUSSCHALTEN:
    toggleMachineState = true;
  }
  previousInterruptTime = millis();
  errorBlinkState = 0;
}

void GenerateErrorBlink() {
  if (errorBlinkTimer.delayTimeUp(800)) {
      digitalWrite(errorBlinkRelay, !digitalRead(errorBlinkRelay));
  }
}

void RunMainTestCycle() {
  int cycleStep = stateController.currentCycleStep();
  switch (cycleStep) {

  case BremszylinderZurueckfahren:
    BremsZylinder.stroke(2000, 0);
    if (BremsZylinder.stroke_completed()) {
      stateController.switchToNextStep();
    }
    break;

  case ToolAufwecken:
    WippenhebelZylinder.stroke(1500, 1000);
    if (WippenhebelZylinder.stroke_completed()) {
      stateController.switchToNextStep();
    }
    break;

  case BandVorschieben:
    SpanntastenZylinder.stroke(550, 0);
    if (SpanntastenZylinder.stroke_completed()) {
      stateController.switchToNextStep();
    }
    break;

  case BandKlemmen:
    BandKlemmZylinder.set(1);
    stateController.switchToNextStep();
    break;

  case BandSpannen:
    static byte subStep = 1;
    if (subStep == 1) {
      SpanntastenZylinder.set(1);
      if (EndSwitchRight.requestButtonState()) {
        subStep = 2;
      }
    }
    if (subStep == 2) {
      SpanntastenZylinder.stroke(800, 0); // kurze Pause für Spannkraftaufbau
      if (SpanntastenZylinder.stroke_completed()) {
        subStep = 1;
        stateController.switchToNextStep();
      }
    }
    break;

  case BandSchneiden:
    MesserZylinder.stroke(2500, 2000);
    if (MesserZylinder.stroke_completed()) {
      stateController.switchToNextStep();
    }
    break;

  case Schweissen:
    SchweisstastenZylinder.stroke(600, ReadCoolingPot());
    if (SchweisstastenZylinder.stroke_completed()) {
      stateController.switchToNextStep();
    }
    break;

  case WippenhebelZiehen:
    WippenhebelZylinder.stroke(1500, 1000);
    if (WippenhebelZylinder.stroke_completed()) {
      stateController.switchToNextStep();
    }
    break;

  case BandklemmeLoesen:
    BandKlemmZylinder.set(0);
    stateController.switchToNextStep();
    break;
  }
}

void setup() {
  //******************************************************************************
  // eepromErrorLog.setAllZero(); // to reset the error counter
  //******************************************************************************
  pinMode(startStopInterruptPin, INPUT);
  pinMode(errorBlinkRelay, OUTPUT);
  EndSwitchLeft.setDebounceTime(100);
  EndSwitchRight.setDebounceTime(100);
  ModeSwitch.setDebounceTime(200);
  StrapDetectionSensor.setDebounceTime(500);
  attachInterrupt(digitalPinToInterrupt(startStopInterruptPin), ToggleMachineRunningISR, RISING);
  Serial.begin(115200);
  Serial.println("EXIT SETUP");
  PrintErrorLog();
  PrintCurrentStep();
}

void loop() {

  // DETEKTIEREN OB DER SCHALTER AUF STEP- ODER AUTO-MODUS EINGESTELLT IST:
  if (ModeSwitch.requestButtonState()) {
    stateController.setAutoMode();
  } else {
    stateController.setStepMode();
  }

  // MACHINE EIN- ODER AUSSCHALTEN (AUSGELÖST DURCH ISR):
  if (toggleMachineState) {
    stateController.toggleMachineRunningState();
    toggleMachineState = false;
  }

  // ABFRAGEN DER BANDDETEKTIERUNG, AUSSCHALTEN FALLS KEIN BAND:
  bool strapDetected = !StrapDetectionSensor.requestButtonState();
  if (!strapDetected) {
    StopTestRig();
    errorBlinkState = 1;
  }

  // DER TIMEOUT TIMER LÄUFT NUR AB, WENN DAS RIG IM AUTO MODUS LÄUFT:
  if (!(stateController.machineRunning() && stateController.autoMode())) {
    resetTimeout.resetTime();
  }

  // TIMEOUT ÜBERWACHEN, FEHLERSPEICHER SCHREIBEN, RESET ODER STOP EINLEITEN:
  RunTimeoutManager();

  // FALLS RESET AKTIVIERT, TEST RIG RESETEN,
  if (stateController.resetMode()) {
    ResetTestRig();
  }

  // ERROLR BLINK FALLS AKTIVIERT:
  if (errorBlinkState) {
    GenerateErrorBlink();
  }
  
  //IM STEP MODE HÄLT DAS RIG NACH JEDEM SCHRITT AN:
  if (stateController.stepSwitchHappened()) {
    if (stateController.stepMode()) {
      stateController.setMachineRunningState(false);
    }
    PrintCurrentStep(); // zeigt den nächsten step
  }

  // AUFRUFEN DER UNTERFUNKTIONEN JE NACHDEM OB DAS RIG LÄUFT ODER NICHT:
  if (stateController.machineRunning()) {
    RunMainTestCycle();
  } else {
    SpanntastenZylinder.set(0);
  }
}
