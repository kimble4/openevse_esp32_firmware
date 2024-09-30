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

void irc_begin(EvseManager &evse) {
    _evse = &evse;
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

void onIRCConnect() {
    DEBUG_PORT.println("onIRCConnect");
    ircJoinChannel(IRC_CHANNEL);
}

void onIRCDebug(const char * line) {  //got debug line from IRC library
    DEBUG_PORT.println(line);
}

void onIRCRaw(const char * line) {
    DEBUG_PORT.println(line);
}

void onPrivateMessage(const char * from, const char * message) {
    DEBUG_PORT.print("<");
    DEBUG_PORT.print(from);
    DEBUG_PORT.print("> ");
    DEBUG_PORT.println(message);
}

void onPrivateNotice(const char * from, const char * message) {
    DEBUG_PORT.print("[");
    DEBUG_PORT.print(from);
    DEBUG_PORT.print("] ");
    DEBUG_PORT.println(message);
}

void irc_check_connection() {
    if (!ircConnected()) {
        ircSetDebug(onIRCDebug);
        ircSetOnPrivateMessage(onPrivateMessage);
        ircSetOnPrivateNotice(onPrivateNotice);
        ircSetClient(wifiClient_irc);
        ircSetOnConnect(onIRCConnect);
        ircSetOnRaw(onIRCRaw);
        ircSetVersion("OpenEVSEbot");
        ircSetNick(IRC_NICK);
        ircSetNickServPassword(NICKSERV_PASSWORD);
        ircConnect(IRC_SERVER_1, IRC_PORT);
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