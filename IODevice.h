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

#ifndef iodevice_h
#define iodevice_h

#define DIAG_IO Y

#include "DIAG.h"
#include "FSH.h"
#include "I2CManager.h"

typedef uint16_t VPIN;
#define VPIN_MAX 65534
#define VPIN_NONE 65535


enum DeviceType {
  Analogue = 0xDAC
};

class IODeviceType;
class IODevice;

/* 
 * IODeviceType class
 * 
 * This class supports the registration of device types and the creation
 * of devices.
 * 
 */

class IODeviceType {
public:
  IODeviceType(int deviceType) { _deviceType = deviceType; }
  int getDeviceType() { return _deviceType; }
  IODeviceType *_nextDeviceType = 0;
  IODevice *(*createFunction)(VPIN vpin);
private:
  int _deviceType = 0;
};

/*
 * IODevice class
 * 
 * This class is the basis of the Hardware Abstraction Layer (HAL) for
 * the DCC++EX Command Station.  All device classes derive from this.
 * 
 */

class IODevice {
public:
  // Class factory method for creating arbitrary device types
  static IODevice *create(int deviceTypeID, VPIN firstID, int paramCount, int params[]);
  // Static functions to find the device and invoke its member functions
  static void begin();
  static bool configure(VPIN vpin, int paramCount, int params[]);
  static void write(VPIN vpin, int value);
  static bool read(VPIN vpin);
  static void loop();
  static void DumpAll();
  static bool exists(VPIN vpin);
  static void remove(VPIN vpin);

  // When a turnout needs to allocate a vpin as its output, it allocates one using ID+turnoutVpinOffset.
  static const VPIN turnoutVpinOffset = 300; 
  // VPIN of first PCA9685 servo controller pin.  
  static const VPIN firstServoVPin = 100;
  
protected:
  // Method to register the device handler to the IODevice system (called from device's register() method)
  static void _registerDeviceType(int deviceTypeID, IODevice *createFunction(VPIN));
  // Method to perform initialisation of the device (optionally implemented within device class)
  virtual void _begin() {}
  // Method to configure device (optionally implemented within device class)
  virtual bool _configure(VPIN vpin, int paramCount, int params[]) { 
    (void)vpin; (void)paramCount; (void)params; // Suppress compiler warning.
    return true; 
  };
  // Method to write new state (optionally implemented within device class)
  virtual void _write(VPIN vpin, int value) {
    (void)vpin; (void)value;
  };
  // Method called from within a filter device to trigger its output (which may
  // have the same VPIN id as the input to the filter).  It works through the 
  // later devices in the chain only.
  void writeDownsream(VPIN vpin, int value);
  // Method to read pin state (optionally implemented within device class)
  virtual int _read(VPIN vpin) { 
    (void)vpin; 
    return 0;
  };
  // Method to perform updates on an ongoing basis (optionally implemented within device class)
  virtual void _loop(unsigned long currentMicros) {
    (void)currentMicros; // Suppress compiler warning.
  };
  // Method for displaying info on DIAG output (optionally implemented within device class)
  virtual void _display();

  // Destructor
  virtual ~IODevice() {};
  
  // Common object fields.
  VPIN _firstID;
  int _nPins;

  // Static support function for subclass creation
  static void addDevice(IODevice *newDevice);

private:
  // Method called from subclasses to write to a downstream device, which must have 
  // been configured before the issuing one (i.e. be later in the chain).  This allows
  // a filter device (such as Analogue) to be configured with the same input and output
  // VPIN numbers.
  // Method to check whether the vpin corresponds to this device
  bool owns(VPIN vpin);
  IODevice *_nextDevice = 0;
  static IODevice *_firstDevice;

  // Chain of installed device driver types
  static IODeviceType *_firstDeviceType;
};


/////////////////////////////////////////////////////////////////////////////////////////////////////
/*
 * Example of an IODevice subclass for PCA9685 16-channel PWM module.
 */
 
class PCA9685 : public IODevice {
public:
  static void create(VPIN vpin, int nPins);

private:
  // Constructor
  PCA9685();
  // Device-specific initialisation
  void _begin();
  // Device-specific write function.
  void _write(VPIN vpin, int value);
  void _display();
  // Helper function
  void writeRegister(byte address, byte reg, byte value);

  static const uint8_t _I2CAddress = 0x40; // 0x40-0x43 used
  uint8_t _currentPortState;

};

/////////////////////////////////////////////////////////////////////////////////////////////////////
/*
 * Example of an IODevice subclass for PCF8574 8-bit I/O expander.
 */
 
class PCF8574 : public IODevice {
public:
  IODevice *createInstance(VPIN vpin);
  static void create(VPIN vpin, int nPins) ;

private:
  // Constructor
  PCF8574();  
  // Device-specific initialisation
  void _begin();
  // Device-specific write function.
  void _write(VPIN vpin, int value);
  // Device-specific read function.
  int _read(VPIN vpin);
  void _display();
  void _loop(unsigned long currentMicros);
  // Address may be up to 0x27, but this may conflict with an LCD if present
  static const uint8_t _I2CAddress = 0x20; 
  // Maximum number of PCF8574 modules supported.
  uint8_t _nModules = 8;
  uint8_t _portInputState[8] = {0, 0, 0, 0, 0, 0, 0, 0}; 
  uint8_t _portOutputState[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  uint8_t _portCounter[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  // Interval between ticks when counters are updated
  static const int _portTickTime = 500; 
  // Number of ticks to elapse before cached port values expire.
  static const int _minTicksBetweenPortReads = 2;
  unsigned long _lastLoopEntry = 0;
};


/////////////////////////////////////////////////////////////////////////////////////////////////////
/*
 * Example of an IODevice subclass for MCP23017 16-bit I/O expander.
 */
 
class MCP23017 : public IODevice {
public:
  static void create(VPIN vpin, int nPins, uint8_t I2CAddress);
  
private:
  // Constructor
  MCP23017();
  // Device-specific initialisation
  void _begin();
  // Device-specific write function.
  void _write(VPIN vpin, int value);
  // Device-specific read function.
  int _read(VPIN vpin);
  // Helper functions
  void writeRegister(uint8_t reg, uint8_t value) ;
  uint8_t readRegister(uint8_t reg);

  uint8_t _I2CAddress;
  uint8_t _currentPortStateA = 0;
  uint8_t _currentPortStateB = 0;
  uint8_t _portModeA = 0xff; // Read mode
  uint8_t _portModeB = 0xff; // Read mode

  enum {
    IODIRA = 0x00,
    IODIRB = 0x01,
    GPIOA = 0x12,
    GPIOB = 0x13
  };
};

/////////////////////////////////////////////////////////////////////////////////////////////////////
/*
 * Example of an IODevice subclass for DCC accessory decoder.
 */
 
class DCCAccessoryDecoder: public IODevice {
public:
  static void create(VPIN firstID, int DCCAddress, int DCCSubaddress);
  static void create(VPIN firstID, int DCCLinearAddress);

private:
  // Constructor
  DCCAccessoryDecoder();
  // Device-specific write function.
  void _write(VPIN vpin, int value);
  void _display();
  int _DCCAddress;
  int _DCCSubaddress;
};


/////////////////////////////////////////////////////////////////////////////////////////////////////
/* 
 *  Example of an IODevice subclass for arduino input/output pins.
 */

// TODO: Implement pullup configuration (currently permanently enabled by default).
 
class ArduinoPins: public IODevice {
public:
  static void create(VPIN firstID, int nPins) {
    addDevice(new ArduinoPins(firstID, nPins));
  }
  
  // Constructor
  ArduinoPins(VPIN firstID, int nPins);

private:
  // Device-specific write function.
  void _write(VPIN vpin, int value);
  // Device-specific read function.
  int _read(VPIN vpin);
  void _display();
};

/////////////////////////////////////////////////////////////////////////////////////////////////////

#include "IO_AnalogueDevice.h"

#endif // iodevice_h