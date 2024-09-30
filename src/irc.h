#include "evse_man.h"

void get_scaled_number_value(double value, int precision, const char *unit, char *buffer, size_t size);
void irc_event(JsonDocument &data);
void printStatusToIRC(const char * target);
void onIRCConnect();
void onIRCDebug(const char * line);
void onIRCRaw(const char * line);
void onPrivateMessage(const char * from, const char * message);
void onChannelMessage(const char * from, const char * channel, const char * message);
void irc_begin(EvseManager &evse);
void irc_check_connection();
void irc_loop();