#include "evse_man.h"
#include "net_manager.h"

#ifdef IRC_SERVER_0
void get_scaled_number_value(double value, int precision, const char *unit, char *buffer, size_t size);
void setAwayStatusFromEVSEState(uint8_t state);
void irc_event(JsonDocument &data);
void printStatusToIRC(const char * target);
void onIRCConnect();
void onVoice(const char * from, const char * channel);
void onIRCDebug(const char * line);
void onIRCRaw(const char * line);
void onPrivateMessage(const char * from, const char * message);
void onChannelMessage(const char * from, const char * channel, const char * message);
void irc_begin(EvseManager &evse, NetManagerTask &net, LcdTask &lcd, ManualOverride &manual);
void irc_check_connection();
void irc_disconnect(const char * reason);
void irc_loop();
#endif //IRC_SERVER_0