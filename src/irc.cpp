#ifdef ESP32
#include <WiFi.h>
#include <esp_wifi.h>
#include <ESPmDNS.h>              // Resolve URL for update server etc.
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>              // Resolve URL for update server etc.
#else
#error Platform not supported
#endif

// copy https://github.com/kimble4/irc_client to 'lib'
#include "irc_client.h"
#include "evse_man.h"
#include "net_manager.h"

// Set these in platformio.ini:
//   '-D IRC_SERVER_0="server0.example.com"'
//   '-D IRC_SERVER_1="server1.example.com"'
//   -D IRC_PORT=6667
//   '-D IRC_CHANNEL="#test"'
//   '-D IRC_NICK="OpenEVSE"'
//   '-D NICKSERV_PASSWORD="passwd"'

#ifdef IRC_SERVER_0

#ifndef IRC_NICK
#define IRC_NICK "OpenEVSE"
#endif

#ifndef IRC_PORT
#define IRC_PORT 6667
#endif

#define REPORT_CURRENT_DELTA 1.0
#define REPORT_CHARGE_FINISHED_CURRENT 0
#define TAKING_CHARGE_CURRENT_THRESHOLD 2.0
WiFiClient wifiClient_irc;
EvseManager *_evse;
NetManagerTask *_net;
uint8_t _vehicle_connection_state = 0;
uint8_t _evse_state = OPENEVSE_STATE_STARTING;
uint8_t _pilot_amps = 0;
double _amp = 0.0;
double _amp_last_reported = 0.0;
#ifdef IRC_SERVER_1
bool _use_backup_irc_server = false;
bool _taking_charge = false;
#endif 

void get_scaled_number_value(double value, int precision, const char *unit, char *buffer, size_t size) {
  static const char *mod[] = {
    "",
    "k",
    "M",
    "G",
    "T",
    "P"
  };

  int index = 0;
  while (value > 1000 && index < ARRAY_ITEMS(mod))
  {
    value /= 1000;
    index++;
  }

  snprintf(buffer, size, "%.*f %s%s", precision, value, mod[index], unit);
}

void setAwayStatusFromEVSEState(uint8_t state) {
    switch (state) {
        case OPENEVSE_STATE_STARTING:
        case OPENEVSE_STATE_CONNECTED:
        case OPENEVSE_STATE_CHARGING:
        case OPENEVSE_STATE_VENT_REQUIRED:
        case OPENEVSE_STATE_DIODE_CHECK_FAILED:
        case OPENEVSE_STATE_GFI_FAULT:
        case OPENEVSE_STATE_NO_EARTH_GROUND:
        case OPENEVSE_STATE_STUCK_RELAY:
        case OPENEVSE_STATE_GFI_SELF_TEST_FAILED:
        case OPENEVSE_STATE_OVER_TEMPERATURE:
        case OPENEVSE_STATE_OVER_CURRENT:
            ircUnAway();
            break;
        case OPENEVSE_STATE_NOT_CONNECTED:
            if (_evse_state == OPENEVSE_STATE_CHARGING) {
                ircSendMessage(IRC_CHANNEL, IRC_COLOURS_BOLD IRC_COLOURS_RED "Vehicle is no longer charging.");
            } else if (_evse_state == OPENEVSE_STATE_SLEEPING) {
                ircSendMessage(IRC_CHANNEL, IRC_COLOURS_ORANGE "EVSE is waiting for a vehicle...");
            }
            ircAway("EVSE is not connected.");
            break;
        case OPENEVSE_STATE_SLEEPING:
            ircAway("EVSE is sleeping.");
            break;
        case OPENEVSE_STATE_DISABLED:
            ircAway("EVSE is disabled.");
            break;
        default:
            ircUnAway();
            break;
        }
}

void irc_event(JsonDocument &data) {
    JsonObject root = data.as<JsonObject>();
    if (root["vehicle"].is<int>()) {
        uint8_t vehicle = root["vehicle"];
        if (vehicle != _vehicle_connection_state) {
            _vehicle_connection_state = vehicle;
            if (_vehicle_connection_state) {
                ircSendMessage(IRC_CHANNEL, IRC_COLOURS_ORANGE "Vehicle connected.");
            } else {
                ircSendMessage(IRC_CHANNEL, IRC_COLOURS_ORANGE "Vehicle disconnected.");
            }
        }
    if (root["state"].is<int>()) {
        uint8_t state = root["state"];
        if (state != _evse_state) {
            switch (state) {
                case OPENEVSE_STATE_STARTING:
                    ircSendMessage(IRC_CHANNEL, IRC_COLOURS_ORANGE "EVSE is starting up...");
                    break;
                case OPENEVSE_STATE_NOT_CONNECTED:
                    if (_evse_state == OPENEVSE_STATE_CHARGING) {
                        ircSendMessage(IRC_CHANNEL, IRC_COLOURS_BOLD IRC_COLOURS_RED "Vehicle is no longer charging.");
                    } else if (_evse_state == OPENEVSE_STATE_SLEEPING) {
                        ircSendMessage(IRC_CHANNEL, IRC_COLOURS_ORANGE "EVSE is waiting for a vehicle...");
                    }
                    break;
                case OPENEVSE_STATE_CONNECTED:
                    if (_evse_state == OPENEVSE_STATE_CHARGING) {
                        ircSendMessage(IRC_CHANNEL, IRC_COLOURS_BOLD IRC_COLOURS_RED "Vehicle is no longer charging.");
                    } else {
                        ircSendMessage(IRC_CHANNEL, IRC_COLOURS_ORANGE "EVSE is ready to charge.");
                    }
                    break;
                case OPENEVSE_STATE_CHARGING:
                    ircSendMessage(IRC_CHANNEL, IRC_COLOURS_BOLD IRC_COLOURS_LIGHT_GREEN "Vehicle is charging...");
                    break;
                case OPENEVSE_STATE_VENT_REQUIRED:
                    ircSendMessage(IRC_CHANNEL, IRC_COLOURS_BOLD IRC_COLOURS_RED "ERROR: Vehicle set 'vent required'");
                    break;
                case OPENEVSE_STATE_DIODE_CHECK_FAILED:
                    ircSendMessage(IRC_CHANNEL, IRC_COLOURS_BOLD IRC_COLOURS_RED "ERROR: Diode check failed'");
                    break;
                case OPENEVSE_STATE_GFI_FAULT:
                    ircSendMessage(IRC_CHANNEL, IRC_COLOURS_BOLD IRC_COLOURS_RED "ERROR: GFI fault");
                    break;
                case OPENEVSE_STATE_NO_EARTH_GROUND:
                    ircSendMessage(IRC_CHANNEL, IRC_COLOURS_BOLD IRC_COLOURS_RED "ERROR: No ground");
                    break;
                case OPENEVSE_STATE_STUCK_RELAY:
                    ircSendMessage(IRC_CHANNEL, IRC_COLOURS_BOLD IRC_COLOURS_RED "ERROR: Stuck relay");
                    break;
                case OPENEVSE_STATE_GFI_SELF_TEST_FAILED:
                    ircSendMessage(IRC_CHANNEL, IRC_COLOURS_BOLD IRC_COLOURS_RED "ERROR: GFI self-test failed");
                    break;
                case OPENEVSE_STATE_OVER_TEMPERATURE:
                    ircSendMessage(IRC_CHANNEL, IRC_COLOURS_BOLD IRC_COLOURS_RED "ERROR: Over temperature");
                    break;
                case OPENEVSE_STATE_OVER_CURRENT:
                      ircSendMessage(IRC_CHANNEL, IRC_COLOURS_BOLD IRC_COLOURS_RED "ERROR: Over current");
                    break;
                case OPENEVSE_STATE_SLEEPING:
                    if (_evse_state == OPENEVSE_STATE_CHARGING) {
                        ircSendMessage(IRC_CHANNEL, IRC_COLOURS_BOLD IRC_COLOURS_RED "Vehicle is no longer charging.");
                    }
                    ircSendMessage(IRC_CHANNEL, IRC_COLOURS_ORANGE "EVSE is sleeping.");
                    break;
                case OPENEVSE_STATE_DISABLED:
                    if (_evse_state == OPENEVSE_STATE_CHARGING) {
                        ircSendMessage(IRC_CHANNEL, IRC_COLOURS_BOLD IRC_COLOURS_RED "Vehicle is no longer charging.");
                    }
                    ircSendMessage(IRC_CHANNEL, IRC_COLOURS_ORANGE "EVSE is disabled.");
                    break;
                default:
                    break;
                }
            }
            _evse_state = state;
            setAwayStatusFromEVSEState(_evse_state);
        }
    }
    if (root["pilot"].is<int>()) {
        uint8_t pilot = root["pilot"];
        if (pilot != _pilot_amps) {
            char buffer[100];
            snprintf(buffer, sizeof(buffer), "Current limit set to: %uA", pilot);
            ircSendMessage(IRC_CHANNEL, buffer);
            _pilot_amps = pilot;          
        }
    }
    if (root["amp"].is<double>()) {
        double amp = root["amp"];
        if (amp != _amp) {  //current has changed
            if (abs(amp - _amp_last_reported) > REPORT_CURRENT_DELTA * AMPS_SCALE_FACTOR) {  //by enough to report
                char buffer[100];
                snprintf(buffer, sizeof(buffer), "Current is: %.2fA", amp/AMPS_SCALE_FACTOR);
                ircSendMessage(IRC_CHANNEL, buffer);
                _amp_last_reported = amp;
            }       
            if (_taking_charge && amp < _amp && amp <= REPORT_CHARGE_FINISHED_CURRENT * AMPS_SCALE_FACTOR) {  //is below finished threshold
                char buffer[100];
                if (_evse_state == OPENEVSE_STATE_CHARGING) {
                    snprintf(buffer, sizeof(buffer), "Charging has finished! (Current <= %.2fA)", REPORT_CHARGE_FINISHED_CURRENT);
                } else {
                    snprintf(buffer, sizeof(buffer), "Charging has stopped!");
                }
                ircSendNotice(IRC_CHANNEL, buffer);
                _amp_last_reported = amp;
                _taking_charge = false;
            }
            if (amp >= TAKING_CHARGE_CURRENT_THRESHOLD) {
                _taking_charge = true;
            }
            _amp = amp;
        }
    }
}

void printStatusToIRC(const char * target) {
    char vehicle_string[] = IRC_COLOURS_RED "disconnected" IRC_COLOURS_NORMAL;
    if (_vehicle_connection_state) {
        snprintf(vehicle_string, sizeof(vehicle_string), IRC_COLOURS_GREEN "connected" IRC_COLOURS_NORMAL);
    }
    char buffer[100];
    uint64_t up = uptimeMillis() / 1000;
    uint32_t hours = up / 3600;
    uint32_t minutes = (up % 3600) / 60;
    uint32_t seconds = up % 60;
    snprintf(buffer, sizeof(buffer), "Vehicle is %s.  Current limit " IRC_COLOURS_BOLD "%uA" IRC_COLOURS_NORMAL ", up: %d:%02d:%02d, RSSI: %ddb", vehicle_string, _pilot_amps, hours, minutes, seconds, WiFi.RSSI());
    ircSendMessage(target, buffer);
    switch (_evse_state) {
        case OPENEVSE_STATE_STARTING:
            ircSendMessage(target, IRC_COLOURS_BOLD "EVSE is " IRC_COLOURS_ORANGE "starting up...");
            break;
        case OPENEVSE_STATE_NOT_CONNECTED:
            ircSendMessage(target, IRC_COLOURS_BOLD "EVSE is " IRC_COLOURS_GREEN "waiting for a vehicle...");
            break;
        case OPENEVSE_STATE_CONNECTED:
            ircSendMessage(target, IRC_COLOURS_BOLD "EVSE is " IRC_COLOURS_GREEN "ready to charge.");
            break;
        case OPENEVSE_STATE_CHARGING:
            ircSendMessage(target, IRC_COLOURS_BOLD "EVSE is " IRC_COLOURS_YELLOW "charging...");
            break;
        case OPENEVSE_STATE_VENT_REQUIRED:
            ircSendMessage(target, IRC_COLOURS_BOLD IRC_COLOURS_RED "ERROR: Vehicle set 'vent required'");
            break;
        case OPENEVSE_STATE_DIODE_CHECK_FAILED:
            ircSendMessage(target, IRC_COLOURS_BOLD IRC_COLOURS_RED "ERROR: Diode check failed'");
            break;
        case OPENEVSE_STATE_GFI_FAULT:
            ircSendMessage(target, IRC_COLOURS_BOLD IRC_COLOURS_RED "ERROR: GFI fault");
            break;
        case OPENEVSE_STATE_NO_EARTH_GROUND:
            ircSendMessage(target, IRC_COLOURS_BOLD IRC_COLOURS_RED "ERROR: No ground");
            break;
        case OPENEVSE_STATE_STUCK_RELAY:
            ircSendMessage(target, IRC_COLOURS_BOLD IRC_COLOURS_RED "ERROR: Stuck relay");
            break;
        case OPENEVSE_STATE_GFI_SELF_TEST_FAILED:
            ircSendMessage(target, IRC_COLOURS_BOLD IRC_COLOURS_RED "ERROR: GFI self-test failed");
            break;
        case OPENEVSE_STATE_OVER_TEMPERATURE:
            ircSendMessage(target, IRC_COLOURS_BOLD IRC_COLOURS_RED "ERROR: Over temperature");
            break;
        case OPENEVSE_STATE_OVER_CURRENT:
            ircSendMessage(target, IRC_COLOURS_BOLD IRC_COLOURS_RED "ERROR: Over current");
            break;
        case OPENEVSE_STATE_SLEEPING:
            ircSendMessage(target, IRC_COLOURS_BOLD "EVSE is sleeping.");
            break;
        case OPENEVSE_STATE_DISABLED:
            ircSendMessage(target, IRC_COLOURS_BOLD "EVSE is disabled.");
            break;
        default:
            break;
        }
    uint32_t elapsed = _evse->getSessionElapsed();
    hours = elapsed / 3600;
    minutes = (elapsed % 3600) / 60;
    seconds = elapsed % 60;
    char energy_buffer[10];
    get_scaled_number_value(_evse->getSessionEnergy(), 2, "Wh", energy_buffer, sizeof(energy_buffer));
    snprintf(buffer, sizeof(buffer), "%.1FC, %.1fV × %.2fA = " IRC_COLOURS_BOLD "%.2fkW" IRC_COLOURS_NORMAL ", Elapsed: %d:%02d:%02d, Delivered: %s",
        _evse->getTemperature(EVSE_MONITOR_TEMP_MONITOR), _evse->getVoltage(), _evse->getAmps(), _evse->getPower()/1000.0, hours, minutes, seconds, energy_buffer);
    ircSendMessage(target, buffer);
}

void onIRCConnect() {
    ircJoinChannel(IRC_CHANNEL);
    setAwayStatusFromEVSEState(_evse_state);
}

void onVoice(const char * from, const char * channel) {
    char buffer[100];
    snprintf(buffer, sizeof(buffer), "OpenEVSE " ESCAPEQUOTE(BUILD_TAG) " http://%s", _net->getIp().c_str());
    ircSendMessage(channel, buffer);
    printStatusToIRC(channel);
}

void onIRCDebug(const char * line) {  //got debug line from IRC library
    DEBUG_PORT.print("[IRC] ");
    DEBUG_PORT.println(line);
}

void onIRCRaw(const char * line) {
    DEBUG_PORT.print("[IRC] Could not parse: ");
    DEBUG_PORT.println(line);
}

void onPrivateMessage(const char * from, const char * message) {
    while (isspace(message[0])) {  //skip whitespace
        message++;
        if (message[0] == '\0') {
        DEBUG_PORT.println("[IRC] Got empty private message?");
        return;
        }
    }
    char * pch;
    char msg[50];
    snprintf(msg, sizeof(msg), "%s", message);
    strlwr(msg);  //convert to lower case
    char * command = msg;
    while (!isalnum(command[0])) {  //skip to first alphabetical char
        command++;
        if (command[0] == '\0') {
            DEBUG_PORT.println("[IRC] Got private message but did not get a command!");
            return;
        }
    }
    DEBUG_PORT.print("[IRC] Got command: ");
    DEBUG_PORT.println(command);
    pch = strstr_P(command, "help");  //help
    if (pch != NULL) {
        DEBUG_PORT.println("[IRC] Got help command");
        ircSendMessage(from, F(IRC_COLOURS_UNDERLINE "I understand the following commands:"));
        ircSendMessage(from, F("help                         - Print this help."));
        ircSendMessage(from, F("status                       - Print status."));
        return;
    }
    pch = strstr_P(command, "status");  //status
    if (pch != NULL) {
        DEBUG_PORT.println("[IRC] Got status command");
        printStatusToIRC(from);
        return;    
    }
}

void onChannelMessage(const char * from, const char * channel, const char * message) {
    while (isspace(message[0])) {  //skip whitespace
        message++;
        if (message[0] == '\0') {
            DEBUG_PORT.println("[IRC] Got empty string?\r\n");
            return;
        }
    }
    char * pch;
    if (strcmp(channel, IRC_CHANNEL) == 0) {
        char msg[50];
        snprintf_P(msg, sizeof(msg), PSTR("%s"), message);
        strlwr(msg);  //convert to lower case
        char lowercase_nick[16];
        snprintf_P(lowercase_nick, sizeof(lowercase_nick), PSTR("%s"), ircNick());
        strlwr(lowercase_nick);
        pch = strstr_P(msg, lowercase_nick);
        if (pch == &msg[0]) {  //command starts with our username
            char * command = msg + strlen(lowercase_nick);  //strip the username
            while (!isalnum(command[0])) {  //skip to first alphanumeric char
                command++;
                if (command[0] == '\0') {
                DEBUG_PORT.println("[IRC] Saw our username but did not get a command!");
                return;
                }
            }
            DEBUG_PORT.print("[IRC] Got command: ");
            DEBUG_PORT.println(command);
            pch = strstr_P(command, "help");  //help
            if (pch != NULL) {
                DEBUG_PORT.println("[IRC] Got help command");
                ircSendMessage(channel, F(IRC_COLOURS_UNDERLINE "I understand the following commands:"));
                ircSendMessage(channel, F("help                         - Print this help."));
                ircSendMessage(channel, F("status                       - Print status."));
                return;
            }
            pch = strstr_P(command, "status");  //status
            if (pch != NULL) {
                DEBUG_PORT.println("[IRC] Got status command");
                printStatusToIRC(channel);
                return;    
            }
        }
    }
}

void irc_begin(EvseManager &evse, NetManagerTask &net) {
    _evse = &evse;
    _net = &net;
    ircSetClient(wifiClient_irc);
    ircSetDebug(onIRCDebug);
    ircSetOnPrivateMessage(onPrivateMessage);
    ircSetOnChannelMessage(onChannelMessage);
    ircSetOnConnect(onIRCConnect);
    ircSetOnVoice(onVoice);
    ircSetOnRaw(onIRCRaw);
    // Get running firmware version from build tag environment variable
    #define TEXTIFY(A) #A
    #define ESCAPEQUOTE(A) TEXTIFY(A)
    ircSetVersion("OpenEVSE " ESCAPEQUOTE(BUILD_TAG));
#ifdef IRC_NICKSERV_PASSWORD
    ircSetNickServPassword(IRC_NICKSERV_PASSWORD);
#endif
#ifdef IRC_SERVER_PASSWORD
    ircSetServerPassword(IRC_SERVER_PASSWORD);
#endif
}

void irc_check_connection() {
    if (!ircConnected()) {
        ircSetNick(IRC_NICK);
#ifdef IRC_SERVER_1
        if (!_use_backup_irc_server) {
            ircConnect(IRC_SERVER_0, IRC_PORT);
        } else {
            ircConnect(IRC_SERVER_1, IRC_PORT);
        }
        _use_backup_irc_server = !_use_backup_irc_server;
#else
        ircConnect(IRC_SERVER_0, IRC_PORT);
#endif //IRC_SERVER_1
    }
}


void irc_disconnect(const char * reason) {
    ircDisconnect(reason);
}

// -------------------------------------------------------------------
// IRC state management
//
// Call every time around loop() if connected to the WiFi
// -------------------------------------------------------------------
void irc_loop() {
    doIRC();
}

#endif  //IRC_SERVER_0