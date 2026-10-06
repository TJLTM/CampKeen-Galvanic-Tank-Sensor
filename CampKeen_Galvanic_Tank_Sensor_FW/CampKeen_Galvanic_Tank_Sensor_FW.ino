#include <EEPROM.h>
#include <SPI.h>

// CANBus
#include "mcp2515_can.h"
mcp2515_can CAN(9);
#define MAX_DATA_SIZE 8
int DeviceAddress = 1;  // within the  subnet
int DeviceSubnet = 0;
int PacketIdentifier = NULL;  //complete Device Address

//Device Configuration setting
#define DeviceType 0x101
long PacingTimer;
uint8_t ErrorNumber = 0;
uint8_t ErrorCommandNumber = 0;

#define ComPort Serial
String inputString = "";  // a String to hold incoming data from ports

char *const AcceptedCommands[] = {
  "ERROR?",
  "RESETERROR",
  "REBOOT",
  "PACING?",
  "SUBNET?",
  "ADDRESS?",
  "IDENTIFIER?"
};

char *const ParameterCommands[] = {
  "SETPACINGTIME",
  "SETDEVICEADDRESS",
  "SETDEVICESSUBNET"
};


void setup() {
  ComPort.begin(115200);
  SerialGetPacketIdentifier();
  CANBusSetup();

  ComPort.print("Pacing:");
  ComPort.println(GetPacingTimeFromMemory());

  DiscoveryResponse(0x00);
}

void loop() {
  CANBusRecieveCheck();

  long CurrentTime = millis();
  if (abs(PacingTimer - CurrentTime) > GetPacingTimeFromMemory()) {
    //do the read... 
    PacingTimer = CurrentTime;
  }


  serialEvent();
}

//----------------------------------------------------------------------------------------------------
//Serial Port Handling Functions
//----------------------------------------------------------------------------------------------------
void SendSerial(String Data, bool CR = true) {
  /*
    :param Data: String to be sent out of the Serial Port
    :type Data: String
    :return: None
    :rtype: None
  */
  if (CR == true) {
    ComPort.println(Data);
  } else {
    ComPort.print(Data);
  }
}

void serialEvent() {
  while (ComPort.available()) {
    char inChar = (char)ComPort.read();
    inputString += inChar;
    if (inChar == '\r') {
      PainlessInstructionSet(inputString);
    }
  }
}

/*
  SCC = start command character
  case 1 - no SCC found and there is data in the buffer - dump the buffer
  case 2 - SCC is found and not at position 0 - trim the buffer up to the SCC and insert error
  case 3 - SCC is found and at position 0  - process command
  case 4 - SCC is found and no delimiter found and there is data in the buffer  - add back to the buffer
  case 5 - SCC is found no delimiter found and another scc is found trim up to the second
  case 6 - No SCC and No Delimiter and there is data in teh buffer - dump the buffer
  case 7 - Valid SSC and Delimiter is found but the command is not in the list of commands - tell the user
*/

String PainlessInstructionSet(String &TestString) {
  int Search = 1;
  while (Search == 1) {
    bool ParamCommandCalled = false;
    bool CommandCalled = false;
    int FindStart = TestString.indexOf('%');
    int Param = TestString.indexOf('*');
    int FindEnd = TestString.indexOf('\r');
    if (TestString != "") {
      if (FindStart != -1) {   //case 1
        if (FindStart != 0) {  //case 2
          //Serial.println("PIS Case 2");
          SendSerial("%R,Error,BAD Command Format No Start or Stop Delimiters");
          TestString.remove(0, FindStart);
        } else {  //Case 3 & Case 5 & Case 4
          String Case5Test = TestString.substring(FindStart + 1);
          int FindStart1 = Case5Test.indexOf('%');
          int FindEnd1 = Case5Test.indexOf('\r');
          if ((FindEnd1 > FindStart1) && (FindStart1 != -1)) {
            SendSerial("%R,Error,BAD Command Format - No End Delimiter");
            //Serial.println("PIS Case 5");
            TestString.remove(0, FindStart1 + 1);
          } else {
            if (FindEnd != -1 || FindEnd1 != -1) {
              //Serial.println("PIS Case 3");
              String CommandCandidate = TestString.substring(FindStart + 1, FindEnd);
              CommandCandidate.toUpperCase();
              if ((Param < FindEnd) && Param != -1) {
                for (int i = 0; i < (sizeof(ParameterCommands) / sizeof(int)); i++) {
                  //Serial.println("PIS Case 3A");
                  String ParamHeader = CommandCandidate.substring(FindStart, Param - 1);
                  ParamHeader.toUpperCase();
                  if (ParamHeader == ParameterCommands[i]) {
                    ParamCommandToCall(i, CommandCandidate);
                    ParamCommandCalled = true;
                    break;
                  }
                }
              } else {
                for (int i = 0; i < (sizeof(AcceptedCommands) / sizeof(int)); i++) {
                  //Non Parameter Commands
                  if (CommandCandidate == AcceptedCommands[i]) {
                    //Serial.println("PIS Case 3B");
                    CommandToCall(i);
                    CommandCalled = true;
                    break;
                  }
                }
              }
              if (CommandCalled == false && ParamCommandCalled == false) {
                //Serial.println("PIS Case 7");
                SendSerial("%R,Error,Command not recognized");
              }
              TestString.remove(0, FindEnd + 1);
            } else {
              //Serial.println("PIS Case 4");
              Search = 0;
            }
          }
        }
      } else {  //Case 1 Dump the buffer if ther is no start character is found
        //Serial.println("PIS Case 1");
        Search = 0;
        TestString = "";
        SendSerial("%R,Error,BAD Command Format - No Start Command Character");
      }
    }       //if TestString is empty
    else {  //Exit Search While if Buffer is empty
      Search = 0;
      //Serial.println("PIS Case 6");
    }
  }  //End of Search While
  return TestString;
}  //End of PIS Function

void ParamCommandToCall(int Index, String CommandRaw) {
  int ParamDelimIndex = CommandRaw.indexOf("*");
  int End = CommandRaw.indexOf("\r");
  String ThingToTest = CommandRaw.substring(ParamDelimIndex + 1, End - 1);

  switch (Index) {
    case 2:
      //SETPACINGTIME
      if (PacingValueCheck(ThingToTest.toInt()) == true) {
        PacingSet(-1, ThingToTest.toInt());
      }
      break;
    case 3:
      //SETDEVICEADDRESS
      SetDeviceAddress(ThingToTest.toInt());
      break;
    case 4:
      //SETDEVICESSUBNET
      SetDeviceSubnet(ThingToTest.toInt());
      break;
  }
}

void CommandToCall(int Index) {
  switch (Index) {
    case 0:
      //ERROR?
      GetError(-1);
      break;
    case 3:
      //RESETERROR
      SendSerial("Error Reset:0x05");
      ResetError(-1);
      break;
    case 4:
      //REBOOT
      SendSerial("Rebooting:0x06");
      RebootDevice(-1);
      break;
    case 5:
      //PACING?
      PacingResponse(-1);
      break;
    case 8:
      //IDENTIFIER?
      SerialGetPacketIdentifier();
      break;
  }
}
//----------------------------------------------------------------------------------------------------
//End Of Serial Port Handling Functions
//----------------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------------------
//System related functions
//----------------------------------------------------------------------------------------------------
void (*resetFunc)(void) = 0;  // declare reset fuction at address 0

bool PacingValueCheck(unsigned int Value) {
  if (Value >= 250 && Value <= 65535) {
    return true;
  } else {
    SendSerial("%R,Error,Invalid Parameter 250 <= x <= 65535");
    return false;
  }
}

int CalcAddress(int ReplyToAddress) {
  // converts the full packet identifier to the 0-254 int to put into the CAN Bus Packet
  int Subnet_Start = DeviceSubnet * 256;
  return ReplyToAddress - Subnet_Start;
}

//----------------------------------------------------------------------------------------------------
//End Of System related functions
//----------------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------------------
//CAN Bus Functions
//----------------------------------------------------------------------------------------------------
void CANBusSetup() {
#if MAX_DATA_SIZE > 8
  /*
        To compatible with MCP2515 API,
        default mode is CAN_CLASSIC_MODE
        Now set to CANFD mode.
  */
  CAN.setMode(CAN_NORMAL_MODE);
#endif

  while (CAN_OK != CAN.begin(CAN_500KBPS)) {  // init can bus : baudrate = 500k
    ComPort.println("CAN init fail, retrying. This is unlikely to recover");
    delay(1000);
  }
  ComPort.println(F("CAN init ok!"));
}

void CANBusRecieveCheck() {
  // check if data coming
  if (CAN_MSGAVAIL != CAN.checkReceive()) {
    return;
  }
  byte cdata[MAX_DATA_SIZE] = { 0 };

  CAN.readMsgBuf(8, cdata);

  // Serial.print("got some CAN Data:ID:");
  // Serial.print(CAN.getCanId());

//  Serial.print(" Data:");
//  for (uint8_t i = 0; i < 8; i++) {
//    Serial.print(i);
//    Serial.print(": ");
//    Serial.print(cdata[i], HEX);
//    Serial.print(",");
//  }
//  Serial.println();

  //Check if message is coming in on correct subnet
  if (CAN.getCanId() >= (DeviceSubnet * 256) && CAN.getCanId() <= (DeviceSubnet * 256) + 254) {
    //Check if this is the target device
    if (cdata[0] == DeviceAddress) {
      int CommandNumber = cdata[1];
      int CommandType = cdata[2];
      //      Serial.print("CommandNumber:");
      //      Serial.print(CommandNumber);
      //      Serial.print(" ::Command Type:");
      //      Serial.print(CommandType);
      //      Serial.println();

      switch (CommandNumber) {
        case 0:  // Discovery
          if (cdata[2] == 0x3F && cdata[3] == 0x00 && cdata[4] == 0xFF && cdata[5] == 0x00 && cdata[6] == 0xFF && cdata[7] == 0x00) {
            DiscoveryResponse(CAN.getCanId());
          }
          break;
        case 1:  //State
          if (CommandType == 0x3F) { // 0x3F == ?
            StatusResponse(CAN.getCanId(), cdata[3]);
          }
          break;
        case 3:  // Pacing
          if (CommandType == 0x3F) { // 0x3F == ?
            PacingResponse(CAN.getCanId());
          } else if (CommandType == 0x53) { // 0x53 == S
            PacingSet(CAN.getCanId(), cdata[3]);
          }
          break;

        case 6:  // Error
          if (CommandType == 0x3F) { // 0x3F == ?
            GetError(CAN.getCanId());
          } else if (CommandType == 0x53) { // 0x53 == S
            if (cdata[3] == 0x0A && cdata[4] == 0x0A && cdata[5] == 0x0A && cdata[6] == 0xFF && cdata[7] == 0xFF) {
              ResetError(CAN.getCanId());
            }
          }
          break;
        case 9:  // Device Reboot
          if (CommandType == 0x53) { // 0x53 == S
            if (cdata[3] == 0x0A && cdata[4] == 0x0A && cdata[5] == 0x0A && cdata[6] == 0x0A && cdata[7] == 0x0A) {
              RebootDevice(CAN.getCanId());
            }
          }
          break;
        default:
          SetError(CAN.getCanId(), 1, CommandNumber, true);
          break;
      }
    }
  }
}

void CanBusSend(byte Zero, byte One, byte Two, byte Three, byte Four, byte Five, byte Six, byte Seven) {
  byte DataPacket[8] = { Zero, One, Two, Three, Four, Five, Six, Seven };  //construct data packet array
  CAN.sendMsgBuf(PacketIdentifier, 0, 8, DataPacket);
}
//----------------------------------------------------------------------------------------------------
//End Of CAN Bus Functions
//----------------------------------------------------------------------------------------------------


//----------------------------------------------------------------------------------------------------
//EEPROM Functions
//----------------------------------------------------------------------------------------------------
int GetIdentifier() {
  GetDeviceAddressFromMemory(0);
  GetDeviceSubnetFromMemory(0);
  PacketIdentifier = (DeviceSubnet * 256) + DeviceAddress;
  return PacketIdentifier;
}

void SerialGetPacketIdentifier() {
  SendSerial("CAN Bus Identifier: " + String(GetIdentifier()));
}

void SetDeviceAddress(int Address) {
  if (Address >= 1 && Address <= 254) {
    EEPROM.update(1, Address);
    GetDeviceAddressFromMemory(1);
  } else {
    SendSerial("Address must be between 1 and 254");
  }
}

void GetDeviceAddressFromMemory(int PrintSerial) {
  DeviceAddress = EEPROM.read(1);
  if (PrintSerial == 1) {
    SendSerial("CAN Bus Address: " + String(DeviceAddress));
  }
}

void SetDeviceSubnet(int Subnet) {
  if (Subnet >= 0 && Subnet <= 7) {
    EEPROM.update(0, Subnet);
    GetDeviceSubnetFromMemory(1);
  } else {
    SendSerial("Subnet must be between 0 and 7");
  }
}

void GetDeviceSubnetFromMemory(int PrintSerial) {
  DeviceSubnet = EEPROM.read(0);
  if (PrintSerial == 1) {
    SendSerial("CAN Bus Subnet: " + String(DeviceSubnet));
  }
}

char GetUnitSystemFromMemory() {
  //Read Units out of EEPROM
  char TempValue = EEPROM.read(4);
  if (TempValue != 'I' && TempValue != 'M') {
    EEPROM.update(4, 'I');
  }
  return TempValue;
}

int GetStreamingFromMemory() {
  //Read Stream Value out of EEPROM
  int TempValue = EEPROM.read(5);
  if (TempValue < 0 || TempValue > 1) {
    EEPROM.update(5, 0);
    TempValue = 0;
  }
  return TempValue;
}

void PutPacingInMemory(int Value) {
  EEPROM.write(3, highByte(Value));
  EEPROM.write(2, lowByte(Value));
}

unsigned int GetPacingTimeFromMemory() {
  //Read Pacing value out of EEPROM
  unsigned int Value = EEPROM.read(3) << 8 | EEPROM.read(2);
  if (PacingValueCheck(Value) == false) {
    PutPacingInMemory(250);
    Value = 250;
  }
  return Value;
}
//----------------------------------------------------------------------------------------------------
// End Of EEPROM Functions
//----------------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------------------
//General API Functions
//----------------------------------------------------------------------------------------------------
void DiscoveryResponse(int ReplyAddress) {
  /*
    :param ReplyToAddress: reply address to put into the CAN packet header
    :type ReplyToAddress: int
    :return: None
    :rtype: None
  */
  CanBusSend(CalcAddress(ReplyAddress), 0x00, 0xFF, byte(DeviceType), byte(DeviceType), byte(DeviceType), byte(DeviceType), byte(DeviceType));
}

void SetError(int ReplyToAddress, int ErrorNum, int CommandNum, bool AutoReset) {
  ErrorNumber = ErrorNum;
  ErrorCommandNumber = CommandNum;
  GetError(ReplyToAddress);
  if (AutoReset == true) {
    ErrorNumber = 0;
    ErrorCommandNumber = 0;
  }
}

void GetError(int ReplyToAddress) {
  if (ReplyToAddress != -1) {
    CanBusSend(CalcAddress(ReplyToAddress), 0x06, 0x52, byte(ErrorCommandNumber), byte(ErrorNumber), 0x00, 0x00, 0x00);
  } else {
    SendSerial("Error:0x06:" + String(ErrorCommandNumber) + ":" + String(ErrorNumber));
  }
}

void ResetError(int ReplyToAddress) {
  SetError(ReplyToAddress, 0, 0, false);
}

void RebootDevice(int ReplyToAddress) {
  CanBusSend(CalcAddress(ReplyToAddress), 0x09, 0xFF, 0x10, 0x10, 0xFF, 0x10, 0x10);
  SendSerial("Device is Rebooting");
  delay(2500);
  resetFunc();
}

void StatusResponse(int ChannelNumber, int ReplyToAddress) {
  /*
    :param ReplyToAddress: reply address to put into the CAN packet header, defaults to -1
    :type ReplyToAddress: int
    :return: None
    :rtype: None
  */
  if (ChannelNumber >= 0 && ChannelNumber <= MaxChannelNumber) {

    int ReturnedValue = SensorCode(ChannelNumber);

    CanBusSend(CalcAddress(ReplyToAddress), 0x01, byte(ChannelNumber), highByte(ReturnedValue), lowByte(ReturnedValue), byte(SensorType[ChannelNumber]), 0x00, 0x00);

    SendSerial("StatusResponse:0x01:" + String(ChannelNumber) + ":" + String(ReturnedValue) + ":" + String(SensorType[ChannelNumber]));
  } else {
    // return error that channel doesn't exist
    SetError(ReplyToAddress, 3, 0, true);
    SendSerial("Error:0x01" + ErrorNumber);
  }
}

void PacingResponse(int ReplyToAddress) {
  /*
    :param ReplyToAddress: reply address to put into the CAN packet header, defaults to -1
    :type ReplyToAddress: int
    :return: None
    :rtype: None
  */
  if (ReplyToAddress != -1) {
    CanBusSend(CalcAddress(ReplyToAddress), 0x52, 0x03, highByte(GetPacingTimeFromMemory()), lowByte(GetPacingTimeFromMemory()), 0x00, 0x00, 0x00);
  } else {
    SendSerial("Pacing:0x04:" + String(GetPacingTimeFromMemory()));
  }
}

void PacingSet(int ReplyToAddress, int Data) {
  /*
    :param ReplyToAddress: reply address to put into the CAN packet header
    :type ReplyToAddress: int
    :return: None
    :rtype: None
  */

  if (PacingValueCheck(Data) == true) {
    PutPacingInMemory(Data);
  } else {
    SetError(ReplyToAddress, 3, 4, true);
  }
  PacingResponse(ReplyToAddress);
}
//----------------------------------------------------------------------------------------------------
//End Of General API Functions
//----------------------------------------------------------------------------------------------------


//----------------------------------------------------------------------------------------------------
//Sensor Helpers
//----------------------------------------------------------------------------------------------------
float ReadAnalog(int Samples, int PinNumber) {
  long Sum = 0;
  float Value = 0;
  for (int x = 0; x < Samples; x++) {
    Sum = Sum + analogRead(PinNumber);
  }
  Value = (Sum / Samples);
  return Value;
}
//----------------------------------------------------------------------------------------------------
//End Of Sensor Helpers
//----------------------------------------------------------------------------------------------------
