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

// Set these in platformio.ini:
//   '-D IRC_SERVER_0="server0.example.com"'
//   '-D IRC_SERVER_1="server1.example.com"'
//   -D IRC_PORT=6667
//   '-D IRC_CHANNEL="#test"'
//   '-D IRC_NICK="OpenEVSE"'
//   '-D NICKSERV_PASSWORD="passwd"'


WiFiClient wifiClient_irc;
EvseManager *_evse;
uint8_t _vehicle_connection_state = 0;
uint8_t _evse_state = OPENEVSE_STATE_STARTING;
uint8_t _pilot_amps = 0;

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

void irc_event(JsonDocument &data) {
    if (!ircConnected()) {
        return;
    }
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
}

void printStatusToIRC(const char * target) {
    char vehicle_string[] = "disconnected";
    if (_vehicle_connection_state) {
        snprintf(vehicle_string, sizeof(vehicle_string), "connected");
    }
    char buffer[100];
    snprintf(buffer, sizeof(buffer), "Vehicle is %s.  Current limit is set to %uA", vehicle_string, _pilot_amps);
    ircSendMessage(target, buffer);
    switch (_evse_state) {
        case OPENEVSE_STATE_STARTING:
            ircSendMessage(target, "EVSE is starting up...");
            break;
        case OPENEVSE_STATE_NOT_CONNECTED:
            ircSendMessage(target, "EVSE is waiting for a vehicle...");
            break;
        case OPENEVSE_STATE_CONNECTED:
            ircSendMessage(target, "EVSE is ready to charge.");
            break;
        case OPENEVSE_STATE_CHARGING:
            ircSendMessage(target, "Vehicle is charging...");
            break;
        case OPENEVSE_STATE_VENT_REQUIRED:
            ircSendMessage(target, "ERROR: Vehicle set 'vent required'");
            break;
        case OPENEVSE_STATE_DIODE_CHECK_FAILED:
            ircSendMessage(target, "ERROR: Diode check failed'");
            break;
        case OPENEVSE_STATE_GFI_FAULT:
            ircSendMessage(target, "ERROR: GFI fault");
            break;
        case OPENEVSE_STATE_NO_EARTH_GROUND:
            ircSendMessage(target, "ERROR: No ground");
            break;
        case OPENEVSE_STATE_STUCK_RELAY:
            ircSendMessage(target, "ERROR: Stuck relay");
            break;
        case OPENEVSE_STATE_GFI_SELF_TEST_FAILED:
            ircSendMessage(target,  "ERROR: GFI self-test failed");
            break;
        case OPENEVSE_STATE_OVER_TEMPERATURE:
            ircSendMessage(target, "ERROR: Over temperature");
            break;
        case OPENEVSE_STATE_OVER_CURRENT:
            ircSendMessage(target, "ERROR: Over current");
            break;
        case OPENEVSE_STATE_SLEEPING:
            ircSendMessage(target, "EVSE is sleeping.");
            break;
        case OPENEVSE_STATE_DISABLED:
            ircSendMessage(target, "EVSE is disabled.");
            break;
        default:
            break;
        }
    uint32_t elapsed = _evse->getSessionElapsed();
    uint32_t hours = elapsed / 3600;
    uint32_t minutes = (elapsed % 3600) / 60;
    uint32_t seconds = elapsed % 60;
    char energy_buffer[10];
    get_scaled_number_value(_evse->getSessionEnergy(), 2, "Wh", energy_buffer, sizeof(energy_buffer));
    snprintf(buffer, sizeof(buffer), "%.1FC, %.1fV, %.2fA, %.3fkW, Elapsed: %2d:%02d:%02d, Delivered: %s",
        _evse->getTemperature(EVSE_MONITOR_TEMP_MONITOR), _evse->getVoltage(), _evse->getAmps(), _evse->getPower()/1000.0, hours, minutes, seconds, energy_buffer);
    ircSendMessage(target, buffer);
}

void onIRCConnect() {
    DEBUG_PORT.println("onIRCConnect");
    ircJoinChannel(IRC_CHANNEL);
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

void irc_begin(EvseManager &evse) {
    _evse = &evse;
    ircSetDebug(onIRCDebug);
    ircSetOnPrivateMessage(onPrivateMessage);
    ircSetOnChannelMessage(onChannelMessage);
    ircSetClient(wifiClient_irc);
    ircSetOnConnect(onIRCConnect);
    ircSetOnRaw(onIRCRaw);
    ircSetVersion("OpenEVSEbot");
}

void irc_check_connection() {
    if (!ircConnected()) {
        ircSetNick(IRC_NICK);
        ircSetNickServPassword(NICKSERV_PASSWORD);
        ircConnect(IRC_SERVER_0, IRC_PORT);
    }
}




// -------------------------------------------------------------------
// IRC state management
//
// Call every time around loop() if connected to the WiFi
// -------------------------------------------------------------------
void irc_loop() {
    doIRC();
}