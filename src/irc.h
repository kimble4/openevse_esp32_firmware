#include "evse_man.h"

void irc_begin(EvseManager &evse);
void irc_event(JsonDocument &data);
void onIRCConnect();
void onIRCDebug(const char * line);
void onIRCRaw(const char * line);
void onPrivateMessage(const char * from, const char * message);
void onPrivateNotice(const char * from, const char * message);
void irc_check_connection();
void irc_loop();
