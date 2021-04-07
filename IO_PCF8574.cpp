/*
 *  © 2021, Neil McKechnie. All rights reserved.
 *  
 *  This file is part of DCC++EX API
 *
 *  This is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  It is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with CommandStation.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "IODevice.h"
#include "DIAG.h"
#include "I2CManager.h"

// Constructor
PCF8574::PCF8574() {}

IODevice *PCF8574::createInstance(VPIN firstID) {
  PCF8574 *dev = new PCF8574();
  dev->_firstID = firstID;
  addDevice(dev);
  return dev;
}

// Parameters: firstVPIN, nPins, I2CAddress
// We allow up to 8 devices, on successive I2C addresses starting 
// with the specified one.  VPINS are allocated contiguously, 8 
// per device.
void PCF8574::create(VPIN firstID, int nPins) {
  PCF8574 *dev = new PCF8574();
  dev->_firstID = firstID;
  dev->_nPins = max(nPins, 64);
  dev->_nModules = (dev->_nPins + 7) / 8; // Number of modules in use.
  addDevice(dev);
}

// Device-specific initialisation 
void PCF8574::_begin() {
  I2CManager.begin();
  I2CManager.setClock(100000);  // Only supports slow clock
}

// Device-specific write function.
void PCF8574::_write(VPIN vpin, int value) {
  int pin = vpin -_firstID;
  int deviceIndex = pin / 8;
  pin %= 8; // Pin within device
  #ifdef DIAG_IO
  DIAG(F("PCF8574 Write I2C:x%x Pin:%d Value:%d"), (int)_I2CAddress+deviceIndex, (int)vpin, value);
  #endif
  uint8_t mask = 1 << pin;
  if (value) 
    _portOutputState[deviceIndex] |= mask;
  else
    _portOutputState[deviceIndex] &= ~mask;
  I2CManager.write(_I2CAddress+deviceIndex, &_portOutputState[deviceIndex], 1);
  // Assume that writing to the port invalidates any cached read, so set the port counter to 0
  //  to force the port to be refreshed next time a read is issued.
  _portCounter[deviceIndex] = 0;
}

// Device-specific read function.
// We reduce number of I2C reads by cacheing 
// the port value, so that a call from _read
// can use the cached value if (a) it's not too
// old and (b) the port mode hasn't been changed and 
// (c) the port hasn't been written to.
int PCF8574::_read(VPIN vpin) {
  byte inBuffer;
  int result;
  int pin = vpin-_firstID;
  int deviceIndex = pin / 8;  
  pin %= 8;
  uint8_t mask = 1 << pin;
  // To enable the pin to be read, write a '1' to it first.  The connected
  // equipment should pull the input down to ground.
  byte bytesToSend = 0;
  if (!(_portOutputState[deviceIndex] & mask)) {
    // Pin currently driven to zero, so set to one first
    bytesToSend = 1;
    _portOutputState[deviceIndex] |= mask;
    _portCounter[deviceIndex] = 0;
  }
  if (bytesToSend || _portCounter[deviceIndex] == 0) {
    I2CManager.read(_I2CAddress+deviceIndex, &inBuffer, 1, &_portOutputState[deviceIndex], bytesToSend);
    _portInputState[deviceIndex] = inBuffer;
    _portCounter[deviceIndex] = _minTicksBetweenPortReads;
  }
  if (_portInputState[deviceIndex] & mask) 
    result = 1;
  else
    result = 0;
  #ifdef DIAG_IO
  //DIAG(F("PCF8574 Read I2C:x%x Pin:%d Value:%d"), (int)_I2CAddress+deviceIndex, (int)pin, result);
  #endif
  return result;
}

// Loop function to maintain timers associated with port read optimisation.  Decrement port counters
// periodically.  When the portCounter reaches zero, the port value is considered to be out-of-date
// and will need re-reading.
void PCF8574::_loop(unsigned long currentMicros) {
  // Process every tick
  if (currentMicros - _lastLoopEntry > _portTickTime) {
    int elapsedTicks = (currentMicros - _lastLoopEntry) / _portTickTime;
    for (int deviceIndex=0; deviceIndex < _nModules; deviceIndex++) {
      if (_portCounter[deviceIndex] > elapsedTicks)
        _portCounter[deviceIndex]-= elapsedTicks;
      else  
        _portCounter[deviceIndex] = 0;

    }
    _lastLoopEntry = currentMicros;
  }
}

void PCF8574::_display() {
  DIAG(F("PCF8574 VPins:%d-%d I2C:x%x"), (int)_firstID, (int)_firstID+_nPins-1, (int)_I2CAddress);
}

