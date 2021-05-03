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

// Define symbol to enable PCF8574 input port value caching (reduce I2C traffic).
#define PCF8574_OPTIMISE

// Constructor
PCF8574::PCF8574() {}

IODevice *PCF8574::createInstance(VPIN vpin, int nPins, uint8_t I2CAddress) {
  #ifdef DIAG_IO
  DIAG(F("PCF8574 created Vpins:%d-%d I2C:%x"), vpin, vpin+nPins-1, I2CAddress);
  #endif
  PCF8574 *dev = new PCF8574();
  dev->_firstVpin = vpin;
  dev->_nPins = min(nPins, 8*8);
  uint8_t nModules = (dev->_nPins + 7) / 8; // Number of modules in use.
  dev->_nModules = nModules;
  // Allocate memory for module state
  uint8_t *blockAddress = (uint8_t *)calloc(3, nModules);
  dev->_portInputState = blockAddress;
  dev->_portOutputState = blockAddress + nModules;
  dev->_portCounter = blockAddress + 2*nModules;
  dev->_I2CAddress = I2CAddress;
  addDevice(dev);
  return dev;
}

// Parameters: firstVPIN, nPins
// We allow up to 8 devices, on successive I2C addresses starting 
// with the specified one.  VPINS are allocated contiguously, 8 
// per device.
void PCF8574::create(VPIN vpin, int nPins, uint8_t I2CAddress) {
  createInstance(vpin, nPins, I2CAddress);
}

// Device-specific initialisation
void PCF8574::_begin() {
  I2CManager.begin();
  I2CManager.setClock(100000);  // Only supports slow clock by default
  for (int i=0; i<_nModules; i++) {
    if (I2CManager.exists(_I2CAddress+i))
      DIAG(F("PCF8574 configured on I2C:x%x"), (int)_I2CAddress+i);
    _portInputState[i] = 0x00;
    _portOutputState[i] = 0x00; // Defaults to output zero.
    _portCounter[i] = 0;
  }
}

// Device-specific pin configuration function.  The PCF8574 will not work in input mode
// unless a pullup is configured.  So return false (fail) if anything else requested.
bool PCF8574::_configure(VPIN vpin, bool pullup) {
  (void)vpin;   // Suppress compiler warning
  if (pullup) 
    return true;
  else
    return false;
}

// Device-specific write function.
void PCF8574::_write(VPIN vpin, int value) {
  int pin = vpin -_firstVpin;
  int deviceIndex = pin / 8;
  pin %= 8; // Pin within device
  #ifdef DIAG_IO
  DIAG(F("PCF8574::_write I2C:x%x Pin:%d Value:%d"), (int)_I2CAddress+deviceIndex, (int)vpin, value);
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
  int pin = vpin-_firstVpin;
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
    uint8_t nBytes = I2CManager.read(_I2CAddress+deviceIndex, &inBuffer, 1, &_portOutputState[deviceIndex], bytesToSend);
    if (nBytes == 1)
      _portInputState[deviceIndex] = inBuffer;
    else  
      _portInputState[deviceIndex] = 0;  // Return zero if error.
#ifdef PCF8574_OPTIMISE
    _portCounter[deviceIndex] = _minTicksBetweenPortReads;
#endif
  }
  if (_portInputState[deviceIndex] & mask) 
    result = 1;
  else
    result = 0;
  #ifdef DIAG_IO
  //DIAG(F("PCF8574::_read I2C:x%x Pin:%d Value:%d"), (int)_I2CAddress+deviceIndex, (int)pin, result);
  #endif
  return result;
}

// Loop function to maintain timers associated with port read optimisation.  Decrement port counters
// periodically.  When the portCounter reaches zero, the port value is considered to be out-of-date
// and will need re-reading.
void PCF8574::_loop(unsigned long currentMicros) {
  (void)currentMicros;  // suppress compiler not-used warning.
#ifdef PCF8574_OPTIMISE
  // Process every tick time
  if (currentMicros - _lastLoopEntry > _portTickTime) {
    for (int deviceIndex=0; deviceIndex < _nModules; deviceIndex++) {
      if (_portCounter[deviceIndex] > 0)
        _portCounter[deviceIndex]--;
    }
    _lastLoopEntry = currentMicros;
  }
#endif
}

void PCF8574::_display() {
  for (int i=0; i<_nModules; i++) {
    DIAG(F("PCF8574 VPins:%d-%d I2C:x%x"), (int)_firstVpin+i*8, 
      (int)min(_firstVpin+i*8+7,_firstVpin+_nPins-1), (int)(_I2CAddress+i));
  }
}

