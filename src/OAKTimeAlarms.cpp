/*
  TimeAlarms.cpp - Arduino Time alarms for use with Time library
  Copyright (c) 2008-2011 Michael Margolis.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.
 */

 /*
  2 July 2011 - replaced alarm types implied from alarm value with enums to make trigger logic more robust
              - this fixes bug in repeating weekly alarms - thanks to Vincent Valdy and draythomp for testing
*/

#include "OAKTimeAlarms.h"

#define IS_ONESHOT  true   // constants used in arguments to create method
#define IS_REPEAT   false

//**************************************************************
//* Alarm Class Constructor

OAKAlarmClass::OAKAlarmClass()
{
  Mode.isEnabled = Mode.isOneShot = 0;
  Mode.alarmType = dtNotAllocated;
  value = nextTrigger = 0;
  onTickHandler = NULL;  // prevent a callback until this pointer is explicitly set
  instance = NULL;
}

//**************************************************************
//* Private Methods


void OAKAlarmClass::updateNextTrigger()
{
  if (Mode.isEnabled) {
    time_t time = now();
    if (dtIsAlarm(Mode.alarmType) && nextTrigger <= time) {
      // update alarm if next trigger is not yet in the future
      if (Mode.alarmType == dtExplicitAlarm) {
        // is the value a specific date and time in the future
        nextTrigger = value;  // yes, trigger on this value
      } else if (Mode.alarmType == dtDailyAlarm) {
        //if this is a daily alarm
        if (value + previousMidnight(now()) <= time) {
          // if time has passed then set for tomorrow
          nextTrigger = value + nextMidnight(time);
        } else {
          // set the date to today and add the time given in value
          nextTrigger = value + previousMidnight(time);
        }
      } else if (Mode.alarmType == dtWeeklyAlarm) {
        // if this is a weekly alarm
        if ((value + previousSunday(now())) <= time) {
          // if day has passed then set for the next week.
          nextTrigger = value + nextSunday(time);
        } else {
          // set the date to this week today and add the time given in value
          nextTrigger = value + previousSunday(time);
        }
      } else {
        // its not a recognized alarm type - this should not happen
        Mode.isEnabled = false;  // Disable the alarm
      }
    }
    if (Mode.alarmType == dtTimer) {
      // its a timer
      nextTrigger = time + value;  // add the value to previous time (this ensures delay always at least Value seconds)
    }
  }
}

//**************************************************************
//* Time Alarms Public Methods

OAKTimeAlarmsClass::OAKTimeAlarmsClass()
{
  isServicing = false;
  for(uint8_t id = 0; id < dtNBR_ALARMS; id++) {
    free(id);   // ensure all Alarms are cleared and available for allocation
  }
}

void OAKTimeAlarmsClass::enable(AlarmID_t ID)
{
  if (isAllocated(ID)) {
    if (( !(dtUseAbsoluteValue(Alarm[ID].Mode.alarmType) && (Alarm[ID].value == 0)) ) && (Alarm[ID].onTickHandler != NULL)) {
      // only enable if value is non zero and a tick handler has been set
      // (is not NULL, value is non zero ONLY for dtTimer & dtExplicitAlarm
      // (the rest can have 0 to account for midnight))
      Alarm[ID].Mode.isEnabled = true;
      Alarm[ID].updateNextTrigger(); // trigger is updated whenever  this is called, even if already enabled
    } else {
      Alarm[ID].Mode.isEnabled = false;
    }
  }
}

void OAKTimeAlarmsClass::disable(AlarmID_t ID)
{
  if (isAllocated(ID)) {
    Alarm[ID].Mode.isEnabled = false;
  }
}

// write the given value to the given alarm
void OAKTimeAlarmsClass::write(AlarmID_t ID, time_t value)
{
  if (isAllocated(ID)) {
    Alarm[ID].value = value;  //note: we don't check value as we do it in enable()
    Alarm[ID].nextTrigger = 0; // clear out previous trigger time (see issue #12)
    enable(ID);  // update trigger time
  }
}

// return the value for the given alarm ID
time_t OAKTimeAlarmsClass::read(AlarmID_t ID)
{
  if (isAllocated(ID)) {
    return Alarm[ID].value ;
  } else {
    return dtINVALID_TIME;
  }
}

// return the alarm type for the given alarm ID
dtAlarmPeriod_t OAKTimeAlarmsClass::readType(AlarmID_t ID)
{
  if (isAllocated(ID)) {
    return (dtAlarmPeriod_t)Alarm[ID].Mode.alarmType ;
  } else {
    return dtNotAllocated;
  }
}

void OAKTimeAlarmsClass::free(AlarmID_t ID)
{
  if (isAllocated(ID)) {
    Alarm[ID].Mode.isEnabled = false;
    Alarm[ID].Mode.alarmType = dtNotAllocated;
    Alarm[ID].onTickHandler = NULL;
    Alarm[ID].instance = NULL;
    Alarm[ID].value = 0;
    Alarm[ID].nextTrigger = 0;
  }
}

// returns the number of allocated timers
uint8_t OAKTimeAlarmsClass::count()
{
  uint8_t c = 0;
  for(uint8_t id = 0; id < dtNBR_ALARMS; id++) {
    if (isAllocated(id)) c++;
  }
  return c;
}

// returns true only if id is allocated and the type is a time based alarm, returns false if not allocated or if its a timer
bool OAKTimeAlarmsClass::isAlarm(AlarmID_t ID)
{
  return( isAllocated(ID) && dtIsAlarm(Alarm[ID].Mode.alarmType) );
}

// returns true if this id is allocated
bool OAKTimeAlarmsClass::isAllocated(AlarmID_t ID)
{
  return (ID < dtNBR_ALARMS && Alarm[ID].Mode.alarmType != dtNotAllocated);
}

// returns the currently triggered alarm id
// returns dtINVALID_ALARM_ID if not invoked from within an alarm handler
AlarmID_t OAKTimeAlarmsClass::getTriggeredAlarmId()
{
  if (isServicing) {
    return servicedAlarmId;  // new private data member used instead of local loop variable i in serviceAlarms();
  } else {
    return dtINVALID_ALARM_ID; // valid ids only available when servicing a callback
  }
}

// following functions are not Alarm ID specific.
void OAKTimeAlarmsClass::delay(unsigned long ms)
{
  unsigned long start = millis();
  while (millis() - start  <= ms) {
    serviceAlarms();
  }
}

void OAKTimeAlarmsClass::waitForDigits( uint8_t Digits, dtUnits_t Units)
{
  while (Digits != getDigitsNow(Units)) {
    serviceAlarms();
  }
}

void OAKTimeAlarmsClass::waitForRollover( dtUnits_t Units)
{
  // if its just rolled over than wait for another rollover
  while (getDigitsNow(Units) == 0) {
    serviceAlarms();
  }
  waitForDigits(0, Units);
}

uint8_t OAKTimeAlarmsClass::getDigitsNow( dtUnits_t Units)
{
  time_t time = now();
  if (Units == dtSecond) return numberOfSeconds(time);
  if (Units == dtMinute) return numberOfMinutes(time);
  if (Units == dtHour) return numberOfHours(time);
  if (Units == dtDay) return dayOfWeek(time);
  return 255;  // This should never happen
}

//returns isServicing
bool OAKTimeAlarmsClass::getIsServicing()
{
  return isServicing;
}

//***********************************************************
//* Private Methods
void OAKTimeAlarmsClass::serviceAlarms()
{
  if (!isServicing) {
    isServicing = true;
    for (servicedAlarmId = 0; servicedAlarmId < dtNBR_ALARMS; servicedAlarmId++) {
      if (Alarm[servicedAlarmId].Mode.isEnabled && (now() >= Alarm[servicedAlarmId].nextTrigger)) {
        voidFuncPtr TickHandler = Alarm[servicedAlarmId].onTickHandler;
        void *instance = Alarm[servicedAlarmId].instance;
        if (Alarm[servicedAlarmId].Mode.isOneShot) {
          free(servicedAlarmId);  // free the ID if mode is OnShot
        } else {
          Alarm[servicedAlarmId].updateNextTrigger();
        }
        if (TickHandler != NULL) {
          TickHandler(instance);
        }
      }
    }
    isServicing = false;
  }
}

// returns the absolute time of the next scheduled alarm, or 0 if none
time_t OAKTimeAlarmsClass::getNextTrigger()
{
  time_t nextTrigger = (time_t)0xffffffff;  // the max time value

  for (uint8_t id = 0; id < dtNBR_ALARMS; id++) {
    if (isAllocated(id)) {
      if (Alarm[id].nextTrigger <  nextTrigger) {
        nextTrigger = Alarm[id].nextTrigger;
      }
    }
  }
  return nextTrigger == (time_t)0xffffffff ? 0 : nextTrigger;
}

// attempt to create an alarm and return true if successful
AlarmID_t OAKTimeAlarmsClass::create(time_t value, voidFuncPtr onTickHandler, void *clas, uint8_t isOneShot, dtAlarmPeriod_t alarmType)
{
  if ( ! ( (dtIsAlarm(alarmType) && now() < SECS_PER_YEAR) || (dtUseAbsoluteValue(alarmType) && (value == 0)) ) ) {
    // only create alarm ids if the time is at least Jan 1 1971
    for (uint8_t id = 0; id < dtNBR_ALARMS; id++) {
      if (Alarm[id].Mode.alarmType == dtNotAllocated) {
        // here if there is an Alarm id that is not allocated
        Alarm[id].onTickHandler = onTickHandler;
        Alarm[id].Mode.isOneShot = isOneShot;
        Alarm[id].Mode.alarmType = alarmType;
        Alarm[id].value = value;
        enable(id);
        return id;  // alarm created ok
      }
    }
  }
  return dtINVALID_ALARM_ID; // no IDs available or time is invalid
}

// make one instance for the user to use
OAKTimeAlarmsClass Alarm = OAKTimeAlarmsClass() ;
