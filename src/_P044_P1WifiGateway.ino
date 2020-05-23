#ifdef USES_P044
//#################################### Plugin 044: P1WifiGateway ########################################
//
//  based on P020 Ser2Net, extended by Ronald Leenes romix/-at-/macuser.nl
//
//  designed for combo
//    Wemos D1 mini (see http://wemos.cc) and
//    P1 wifi gateway shield (see https://circuits.io/circuits/2460082)
//    see http://romix.macuser.nl for kits
//#######################################################################################################

#include "_Plugin_Helper.h"

#define PLUGIN_044
#define PLUGIN_ID_044         44
#define PLUGIN_NAME_044       "Communication - P1 Wifi Gateway"
#define PLUGIN_VALUENAME1_044 "P1WifiGateway"

#define P044_STATUS_LED                    12
#define P044_BUFFER_SIZE                   1024
#define P044_NETBUF_SIZE                   128
#define P044_DISABLED                      0
#define P044_WAITING                       1
#define P044_READING                       2
#define P044_CHECKSUM                      3
#define P044_DONE                          4



struct P044_data_struct : public PluginTaskData_base {

  P044_data_struct() {
    clearBuffer();
  }

  ~P044_data_struct() {
    stopServer();
  }

  void startServer(unsigned int portnumber) {
    stopServer();
    P1GatewayServer = new WiFiServer(portnumber);
    if (nullptr != P1GatewayServer) {
      P1GatewayServer->begin();
      addLog(LOG_LEVEL_DEBUG, String(F("P1   : WiFi server started at port ")) + portnumber);
    }
  }

  void stopServer() {
    clearBuffer();
    if (nullptr != P1GatewayServer) {
      P1GatewayServer->close();
      addLog(LOG_LEVEL_DEBUG, F("P1   : WiFi server closed"));
      delete P1GatewayServer;
      P1GatewayServer = nullptr;
    }
  }

  void clearBuffer() {
    serial_buffer = "";
    serial_buffer.reserve(P044_BUFFER_SIZE);
    bytes_read = 0;
  }

  bool isInit() const {
  	return nullptr != P1GatewayServer;
  }

  void addChar(char ch) {
    serial_buffer += ch;
    ++bytes_read;
  }

  /*  checkDatagram
      checks whether the P044_CHECKSUM of the data received from P1 matches the P044_CHECKSUM attached to the
      telegram
     based on code written by Jan ten Hove
     https://github.com/jantenhove/P1-Meter-ESP8266
  */
  bool checkDatagram(int len) {
    int startChar = serial_buffer.lastIndexOf('/');
    int endChar = serial_buffer.lastIndexOf('!');
    bool validCRCFound = false;

    if (!CRCcheck) return true;

/*
    if (serialdebug) {
      serialPrint(F("input length: "));
      serialPrintln(String(len));
      serialPrint("Start char \\ : ");
      serialPrintln(String(startChar));
      serialPrint(F("End char ! : "));
      serialPrintln(String(endChar));
    }
*/

    if (endChar >= 0)
    {
      currCRC = CRC16(0x0000, serial_buffer, endChar - startChar + 1);

      char messageCRC[5];
      strncpy(messageCRC, &serial_buffer[endChar + 1], 4);
      messageCRC[4] = 0;
      if (serialdebug) {
        for (int cnt = 0; cnt < len; cnt++)
          serialPrint(serial_buffer.substring(cnt, 1));
      }

      validCRCFound = (strtoul(messageCRC, NULL, 16) == currCRC);
      currCRC = 0;
    }
    return validCRCFound;
  }


  /*
     validP1char
         checks whether the incoming character is a valid one for a P1 datagram. Returns false if not, which signals corrupt datagram
  */
  bool validP1char(char ch) {
    if ((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z'))
    {
      return true;
    }
    switch (ch) {
      case '.':
      case '!':
      case ' ':
      case '\\':  // Single backslash, but escaped in C++
      case '\r':
      case '\n':
      case '(':
      case ')':
      case '-':
      case '*':
      case ':':
        return true;
    }
    return false;
  }

  void handle_serial_in(struct EventStruct *event) {

    int RXWait = PCONFIG(0);

    if (RXWait == 0)
      RXWait = 1;
    int timeOut = RXWait;
    while (timeOut > 0)
    {
      while (Serial.available() && state != P044_DONE) {
        if (bytes_read < P044_BUFFER_SIZE - 5) {
          char ch = Serial.read();
          digitalWrite(P044_STATUS_LED, 1);
          switch (state) {
            case P044_DISABLED: //ignore incoming data
              break;
            case P044_WAITING:
              if (ch == '/')  {
                clearBuffer();
                addChar(ch);
                state = P044_READING;
              } // else ignore data
              break;
            case P044_READING:
              if (ch == '!') {
                if (CRCcheck) {
                  state = P044_CHECKSUM;
                } else {
                  state = P044_DONE;
                }
              }
              if (validP1char(ch)) {
                addChar(ch);
              } else if (ch=='/') {
                addLog(LOG_LEVEL_DEBUG, F("P1   : Error: Start detected, discarded input."));
                clearBuffer();
                addChar(ch);
              } else {              // input is non-ascii
                addLog(LOG_LEVEL_DEBUG, F("P1   : Error: DATA corrupt, discarded input."));
                if (serialdebug) {
                  serialPrint(F("faulty char>"));
                  serialPrint(String(ch));
                  serialPrintln("<");
                }
                clearBuffer();
                state = P044_WAITING;
              }
              break;
            case P044_CHECKSUM:
              ++checkI;
              if (checkI == 4) {
                checkI = 0;
                state = P044_DONE;
              }
              addChar(ch);
              break;
            case P044_DONE:
              // Plugin_044_serial_buf[bytes_read]= '\n';
              // bytes_read++;
              // Plugin_044_serial_buf[bytes_read] = 0;
              break;
          }
        }
        else
        {
          Serial.read();      // when the buffer is full, just read remaining input, but do not store...
          clearBuffer();
          bytes_read = 0;
          state = P044_WAITING;    // reset
        }
        digitalWrite(P044_STATUS_LED, 0);
        timeOut = RXWait; // if serial received, reset timeout counter
      }
      delay(1);
      timeOut--;
    }

    if (state == P044_DONE) {
      if (checkDatagram(bytes_read)) {
        addChar('\r');
        addChar('\n');
        // No longer needed for the string to be null-terminated, since .c_str() does deliver 0-terminated char array pointer
//          serial_buffer[bytes_read] = 0;
        P1GatewayClient.write(serial_buffer.c_str(), bytes_read);
        P1GatewayClient.flush();

        // start: was exported
        addLog(LOG_LEVEL_DEBUG, F("P1   : data send!"));
        blinkLED();

        if (Settings.UseRules)
        {
          LoadTaskSettings(event->TaskIndex);
          String eventString = getTaskDeviceName(event->TaskIndex);
          eventString += F("#Data");
          eventQueue.add(eventString);
        }
        // end: was exported

      } else {
        addLog(LOG_LEVEL_DEBUG, F("P1   : Error: Invalid CRC, dropped data"));
      }

      clearBuffer();
      state = P044_WAITING;
    }   // state == P044_DONE
  }

  WiFiServer *P1GatewayServer = nullptr;
  WiFiClient P1GatewayClient;
  String serial_buffer;
  unsigned int bytes_read = 0;
  unsigned int currCRC = 0;
  int state = P044_DISABLED;
  int checkI = 0;
  byte connectionState = 0;
  boolean serialdebug = false;
  boolean CRCcheck = false;
};

boolean Plugin_044(byte function, struct EventStruct *event, String& string)
{
  boolean success = false;

  switch (function)
  {

    case PLUGIN_DEVICE_ADD:
      {
        Device[++deviceCount].Number = PLUGIN_ID_044;
        Device[deviceCount].Type = DEVICE_TYPE_SINGLE;
        Device[deviceCount].Custom = true;
        Device[deviceCount].TimerOption = false;
        break;
      }

    case PLUGIN_GET_DEVICENAME:
      {
        string = F(PLUGIN_NAME_044);
        break;
      }

    case PLUGIN_GET_DEVICEVALUENAMES:
      {
        strcpy_P(ExtraTaskSettings.TaskDeviceValueNames[0], PSTR(PLUGIN_VALUENAME1_044));
        break;
      }

    case PLUGIN_WEBFORM_LOAD:
      {
        LoadTaskSettings(event->TaskIndex);
      	addFormNumericBox(F("TCP Port"), F("p044_port"), ExtraTaskSettings.TaskDevicePluginConfigLong[0], 0);
      	addFormNumericBox(F("Baud Rate"), F("p044_baud"), ExtraTaskSettings.TaskDevicePluginConfigLong[1], 0);

        byte serialConfChoice = serialHelper_convertOldSerialConfig(PCONFIG(1));
        serialHelper_serialconfig_webformLoad(event, serialConfChoice);

        // FIXME TD-er: Why isn't this using the normal pin selection functions?
      	addFormPinSelect(F("Reset target after boot"), F("taskdevicepin1"), CONFIG_PIN1);

      	addFormNumericBox(F("RX Receive Timeout (mSec)"), F("p044_rxwait"), PCONFIG(0), 0);

        success = true;
        break;
      }

    case PLUGIN_WEBFORM_SAVE:
      {
        LoadTaskSettings(event->TaskIndex);
        ExtraTaskSettings.TaskDevicePluginConfigLong[0] = getFormItemInt(F("p044_port"));
        ExtraTaskSettings.TaskDevicePluginConfigLong[1] = getFormItemInt(F("p044_baud"));
        PCONFIG(0) = getFormItemInt(F("p044_rxwait"));
        PCONFIG(1) = serialHelper_serialconfig_webformSave();

        success = true;
        break;
      }

    case PLUGIN_INIT:
      {
        pinMode(P044_STATUS_LED, OUTPUT);
        digitalWrite(P044_STATUS_LED, 0);

        LoadTaskSettings(event->TaskIndex);
        if ((ExtraTaskSettings.TaskDevicePluginConfigLong[0] == 0) ||
            (ExtraTaskSettings.TaskDevicePluginConfigLong[1] == 0))
          {
            break;
          }

    #if defined(ESP8266)
         byte serialconfig = 0;
    #elif defined(ESP32)
         uint32_t serialconfig = 0x8000000;
    #endif
        serialconfig |= serialHelper_convertOldSerialConfig(PCONFIG(1));
    #if defined(ESP8266)
        Serial.begin(ExtraTaskSettings.TaskDevicePluginConfigLong[1], (SerialConfig)serialconfig);
    #elif defined(ESP32)
        Serial.begin(ExtraTaskSettings.TaskDevicePluginConfigLong[1], serialconfig);
    #endif

        initPluginTaskData(event->TaskIndex, new P044_data_struct());
        P044_data_struct *P044_data =
            static_cast<P044_data_struct *>(getPluginTaskData(event->TaskIndex));
        if (nullptr == P044_data) {
          break;
        }

        P044_data->startServer(ExtraTaskSettings.TaskDevicePluginConfigLong[0]);

        if (!P044_data->isInit()) {
          break;
        }

        if (CONFIG_PIN1 != -1)
        {
          pinMode(CONFIG_PIN1, OUTPUT);
          digitalWrite(CONFIG_PIN1, LOW);
          delay(500);
          digitalWrite(CONFIG_PIN1, HIGH);
          pinMode(CONFIG_PIN1, INPUT_PULLUP);
        }

        blinkLED();
        if (ExtraTaskSettings.TaskDevicePluginConfigLong[1] == 115200) {
          addLog(LOG_LEVEL_DEBUG, F("P1   : DSMR version 4 meter, CRC on"));
          P044_data->CRCcheck = true;
        } else {
          addLog(LOG_LEVEL_DEBUG, F("P1   : DSMR version 4 meter, CRC off"));
          P044_data->CRCcheck = false;
        }

        P044_data->state = P044_WAITING;
        success = true;
        break;
      }

    case PLUGIN_EXIT:
      {
        clearPluginTaskData(event->TaskIndex);
        success = true;
        break;
      }

    case PLUGIN_TEN_PER_SECOND:
      {
        P044_data_struct *P044_data =
            static_cast<P044_data_struct *>(getPluginTaskData(event->TaskIndex));
        if (nullptr == P044_data) {
          break;
        }

        if (P044_data->isInit())
        {
          if (P044_data->P1GatewayServer->hasClient())
          {
            if (P044_data->P1GatewayClient) P044_data->P1GatewayClient.stop();
            P044_data->P1GatewayClient = P044_data->P1GatewayServer->available();
            P044_data->P1GatewayClient.setTimeout(CONTROLLER_CLIENTTIMEOUT_DFLT);
            addLog(LOG_LEVEL_ERROR, F("P1   : Client connected!"));
          }

          if (P044_data->P1GatewayClient.connected())
          {
            P044_data->connectionState = 1;
            uint8_t net_buf[P044_NETBUF_SIZE];
            int count = P044_data->P1GatewayClient.available();
            if (count > 0)
            {
              size_t net_bytes_read;
              if (count > P044_NETBUF_SIZE)
                count = P044_NETBUF_SIZE;
              net_bytes_read = P044_data->P1GatewayClient.read(net_buf, count);
              Serial.write(net_buf, net_bytes_read);
              Serial.flush(); // Waits for the transmission of outgoing serial data to complete

              if (count == P044_NETBUF_SIZE) // if we have a full buffer, drop the last position to stuff with string end marker
              {
                count--;
                // and log buffer full situation
                addLog(LOG_LEVEL_ERROR, F("P1   : Error: network buffer full!"));
              }
              net_buf[count] = 0; // before logging as a char array, zero terminate the last position to be safe.
              char log[P044_NETBUF_SIZE + 40] = {0};
              sprintf_P(log, PSTR("P1   : Error: N>: %s"), (char*)net_buf);
              ZERO_TERMINATE(log);
              addLog(LOG_LEVEL_DEBUG, log);
            }
          }
          else
          {
            if (P044_data->connectionState == 1) // there was a client connected before...
            {
              P044_data->connectionState = 0;
              addLog(LOG_LEVEL_ERROR, F("P1   : Client disconnected!"));
            }

            while (Serial.available())
              Serial.read();
          }

          success = true;
        }
        break;
      }

    case PLUGIN_SERIAL_IN:
      {
        P044_data_struct *P044_data =
            static_cast<P044_data_struct *>(getPluginTaskData(event->TaskIndex));
        if (nullptr == P044_data || !P044_data->isInit()) {
          break;
        }

        if (P044_data->P1GatewayClient.connected())
        {
          P044_data->handle_serial_in(event);
        }
        success = true;
        break;
      }

  }
  return success;
}
void blinkLED() {
  digitalWrite(P044_STATUS_LED, 1);
  delay(500);
  digitalWrite(P044_STATUS_LED, 0);
}



/*
   CRC16
      based on code written by Jan ten Hove
     https://github.com/jantenhove/P1-Meter-ESP8266
*/
unsigned int CRC16(unsigned int crc, const String& buf, int len)
{
  for (int pos = 0; pos < len; pos++)
  {
    crc ^= static_cast<const unsigned int>(buf[pos]);    // XOR byte into least sig. byte of crc

    for (int i = 8; i != 0; i--) {    // Loop over each bit
      if ((crc & 0x0001) != 0) {      // If the LSB is set
        crc >>= 1;                    // Shift right and XOR 0xA001
        crc ^= 0xA001;
      }
      else                            // Else LSB is not set
        crc >>= 1;                    // Just shift right
    }
  }

  return crc;
}

#endif // USES_P044
