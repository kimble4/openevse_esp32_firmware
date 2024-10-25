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

#ifndef IRC_STATUS_INTERVAL
#define IRC_STATUS_INTERVAL 3600  //report status every this many elapsed seconds
#endif

#define REPORT_RATE_LIMIT 5000  //ms
#define REPORT_CURRENT_DELTA 1.0
#define REPORT_CHARGE_FINISHED_CURRENT 0
#define TAKING_CHARGE_CURRENT_THRESHOLD 2.0
WiFiClient wifiClient_irc;
EvseManager *_evse;
NetManagerTask *_net;
LcdTask *_lcd;
ManualOverride *_manual;
unsigned long *_got_last_knock;
uint32_t _last_second = 0;
uint8_t _vehicle_connection_state = 0;
uint8_t _evse_state = OPENEVSE_STATE_STARTING;
uint8_t _pilot_amps = 0;
double _amp = 0.0;
double _amp_last_reported = 0.0;
bool _taking_charge = false;
#ifdef IRC_SERVER_1
bool _use_backup_irc_server = false;
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

void printStatusToIRC(const char * target, bool full) {
    char buffer[100];
    uint64_t t;
    uint32_t hours;
    uint32_t minutes;
    uint32_t seconds;
    if (full) {
        char vehicle_string[] = IRC_COLOURS_RED "disconnected" IRC_COLOURS_NORMAL;
        if (_vehicle_connection_state) {
            snprintf(vehicle_string, sizeof(vehicle_string), IRC_COLOURS_GREEN "connected" IRC_COLOURS_NORMAL);
        }
        t = uptimeMillis() / 1000;
        hours = t / 3600;
        minutes = (t % 3600) / 60;
        seconds = t % 60;
        snprintf(buffer, sizeof(buffer), "Vehicle is %s.  Current limit " IRC_COLOURS_BOLD "%uA" IRC_COLOURS_NORMAL ", up: %d:%02d:%02d, RSSI: %ddB", vehicle_string, _pilot_amps, hours, minutes, seconds, WiFi.RSSI());
        ircSendMessage(target, buffer);
        switch (_evse_state) {
            case OPENEVSE_STATE_STARTING:
                ircSendAction(target, "is " IRC_COLOURS_BOLD IRC_COLOURS_ORANGE "starting up...");
                break;
            case OPENEVSE_STATE_NOT_CONNECTED:
                ircSendAction(target, "is " IRC_COLOURS_BOLD IRC_COLOURS_GREEN "waiting for a vehicle...");
                break;
            case OPENEVSE_STATE_CONNECTED:
                ircSendAction(target, "is " IRC_COLOURS_BOLD IRC_COLOURS_GREEN "ready to supply power.");
                break;
            case OPENEVSE_STATE_CHARGING:
                ircSendAction(target, "is " IRC_COLOURS_BOLD IRC_COLOURS_YELLOW "supplying power...");
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
                ircSendAction(target, "is " IRC_COLOURS_ORANGE "sleeping.");
                break;
            case OPENEVSE_STATE_DISABLED:
                ircSendAction(target, "is " IRC_COLOURS_ORANGE "disabled.");
                break;
            default:
                break;
            }
    }
    t = _evse->getSessionElapsed();
    hours = t / 3600;
    minutes = (t % 3600) / 60;
    seconds = t % 60;
    char energy_buffer[10];
    get_scaled_number_value(_evse->getSessionEnergy(), 2, "Wh", energy_buffer, sizeof(energy_buffer));
    snprintf(buffer, sizeof(buffer), "%.1FC, %.1fV × %.2fA = " IRC_COLOURS_BOLD "%.2fkW" IRC_COLOURS_NORMAL ", Elapsed: " IRC_COLOURS_BOLD "%d:%02d:%02d" IRC_COLOURS_NORMAL ", Delivered: " IRC_COLOURS_BOLD "%s" IRC_COLOURS_NORMAL,
        _evse->getTemperature(EVSE_MONITOR_TEMP_MONITOR), _evse->getVoltage(), _evse->getAmps(), _evse->getPower()/1000.0, hours, minutes, seconds, energy_buffer);
    ircSendMessage(target, buffer);
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
                    ircSendAction(IRC_CHANNEL, "is " IRC_COLOURS_ORANGE "starting up...");
                    break;
                case OPENEVSE_STATE_NOT_CONNECTED:
                    if (_evse_state == OPENEVSE_STATE_CHARGING) {
                        ircSendAction(IRC_CHANNEL, "is " IRC_COLOURS_BOLD IRC_COLOURS_RED "no longer suppying power.");
                    } else if (_evse_state == OPENEVSE_STATE_SLEEPING) {
                        ircSendAction(IRC_CHANNEL, "is " IRC_COLOURS_ORANGE "waiting for a vehicle...");
                    }
                    break;
                case OPENEVSE_STATE_CONNECTED:
                    if (_evse_state == OPENEVSE_STATE_CHARGING) {
                        ircSendAction(IRC_CHANNEL, "is " IRC_COLOURS_BOLD IRC_COLOURS_RED "no longer supplying power.");
                    } else {
                        ircSendAction(IRC_CHANNEL, "is " IRC_COLOURS_ORANGE "ready to supply power.");
                    }
                    break;
                case OPENEVSE_STATE_CHARGING:
                    ircSendAction(IRC_CHANNEL, "is " IRC_COLOURS_BOLD IRC_COLOURS_LIGHT_GREEN "supplying power...");
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
                        ircSendAction(IRC_CHANNEL, "is " IRC_COLOURS_BOLD IRC_COLOURS_RED "no longer supplying power.");
                    }
                    ircSendAction(IRC_CHANNEL, "is " IRC_COLOURS_ORANGE "sleeping.");
                    break;
                case OPENEVSE_STATE_DISABLED:
                    if (_evse_state == OPENEVSE_STATE_CHARGING) {
                        ircSendAction(IRC_CHANNEL, "is " IRC_COLOURS_BOLD IRC_COLOURS_RED "no longer supplying power.");
                    }
                    ircSendAction(IRC_CHANNEL, "is " IRC_COLOURS_ORANGE "disabled.");
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
                snprintf(buffer, sizeof(buffer), "Current is now: %.2fA", amp/AMPS_SCALE_FACTOR);
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

void onIRCConnect() {
    ircJoinChannel(IRC_CHANNEL);
    setAwayStatusFromEVSEState(_evse_state);
}

void onIRCDisconnect() {  //server has disconnected
    ircSetNick(IRC_NICK);  //reset nick in case it was changed by nickserv
#ifdef IRC_SERVER_1
    if (!_use_backup_irc_server) {
        ircSetServer(IRC_SERVER_0, IRC_PORT);
    } else {
        ircSetServer(IRC_SERVER_1, IRC_PORT);
    }
    _use_backup_irc_server = !_use_backup_irc_server;
#endif //IRC_SERVER_1
}

void onVoice(const char * from, const char * channel) {
    char buffer[100];
    snprintf(buffer, sizeof(buffer), "OpenEVSE " ESCAPEQUOTE(BUILD_TAG) " http://%s", _net->getIp().c_str());
    ircSendMessage(channel, buffer);
    printStatusToIRC(channel, true);
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
        ircSendMessage(from, F("toggle                       - Toggle manual override."));
        ircSendMessage(from, F("wakeup                       - Wake the LCD backlight."));
        return;
    }
    pch = strstr_P(command, "status");  //status
    if (pch != NULL) {
        DEBUG_PORT.println("[IRC] Got status command");
        printStatusToIRC(from, true);
        return;    
    }
    pch = strstr_P(command, "toggle");  //toggle manual override
    if (pch != NULL) {
        DEBUG_PORT.println("[IRC] Got toggle command");
        ircSendMessage(from, F("Toggling override..."));
        _manual->toggle();
        return;    
    }
    pch = strstr_P(command, "wakeup");  //wake lcd
    if (pch != NULL) {
        DEBUG_PORT.println("[IRC] Got wakeup command");
        ircSendMessage(from, F("Waking display..."));
        _lcd->wakeBacklight();
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
                ircSendMessage(channel, F("toggle                       - Toggle manual override."));
                ircSendMessage(channel, F("wakeup                       - Wake the LCD backlight."));
                return;
            }
            pch = strstr_P(command, "status");  //status
            if (pch != NULL) {
                DEBUG_PORT.println("[IRC] Got status command");
                printStatusToIRC(channel, true);
                return;    
            }
            pch = strstr_P(command, "toggle");  //toggle manual override
            if (pch != NULL) {
                DEBUG_PORT.println("[IRC] Got toggle command");
                ircSendMessage(channel, F("Toggling override..."));
                _manual->toggle();
                return;    
            }
            pch = strstr_P(command, "wakeup");  //status
            if (pch != NULL) {
                DEBUG_PORT.println("[IRC] Got wakeup command");
                ircSendMessage(channel, F("Waking display..."));
                _lcd->wakeBacklight();
                return;    
            }
        }
    }
}

void irc_begin(EvseManager &evse, NetManagerTask &net, LcdTask &lcd, ManualOverride &manual, unsigned long &last_knock) {
    _evse = &evse;
    _net = &net;
    _lcd = &lcd;
    _manual = &manual;
    _got_last_knock = &last_knock;
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
    ircSetNick(IRC_NICK);
    ircSetServer(IRC_SERVER_0, IRC_PORT);  //starts connection to server
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
    if (time(NULL) > _last_second) {  //on the second
        _last_second = time(NULL);
#ifdef IRC_STATUS_INTERVAL
        if (_evse_state == OPENEVSE_STATE_CHARGING) {
            uint32_t elapsed = _evse->getSessionElapsed();
            if (elapsed % IRC_STATUS_INTERVAL == 0) {
                printStatusToIRC(IRC_CHANNEL, false);
            }
        }
#endif //IRC_STATUS_INTERVAL
        if (millis() - *_got_last_knock <= 1000) {
            ircSendMessage(IRC_CHANNEL, "Got knock.");
        }
    }
    doIRC();
}

#endif  //IRC_SERVER_0