/*
 * MainCycleController.h
 *
 *  Created on: Oct 20, 2019
 *      Author: realslimshady
 */

#ifndef MAINCYCLECONTROLLER_H_
#define MAINCYCLECONTROLLER_H_

class MainCycleController {

public:
  // FUNCTIONS:
  MainCycleController(int numberOfSteps);

  void setStepMode();
  bool stepMode();

  void setAutoMode();
  bool autoMode();

  void setMachineRunningState(bool machineState);
  void toggleMachineRunningState()  ;
  bool machineRunning();

  void switchToNextStep();
  void setCycleStepTo(int cycleStep);
  int currentCycleStep();
  bool stepSwitchHappened();

  void setResetMode(bool resetState);
  bool resetMode();

  void setRunAfterReset(bool runAfterReset);
  bool runAfterReset();

  // VARIABLES:
  // n.a.

private:
  // FUNCTIONS:
  // n.a.

  // VARIABLES:
  int _numberOfSteps;
  int _currentCycleStep;
  bool _machineRunning;
  int _previousCycleStep;
  bool _stepMode;
  bool _autoMode;
  bool _resetMode;
  bool _runAfterReset;
};
#endif /* MAINCYCLECONTROLLER_H_ */
