#ifndef __OPENEVSE_H
#define __OPENEVSE_H

#include <functional>
#include <time.h>

#include "RapiSender.h"

#define OPENEVSE_STATE_INVALID               -1

#define OPENEVSE_STATE_STARTING               0
#define OPENEVSE_STATE_NOT_CONNECTED          1
#define OPENEVSE_STATE_CONNECTED              2
#define OPENEVSE_STATE_CHARGING               3
#define OPENEVSE_STATE_VENT_REQUIRED          4
#define OPENEVSE_STATE_DIODE_CHECK_FAILED     5
#define OPENEVSE_STATE_GFI_FAULT              6
#define OPENEVSE_STATE_NO_EARTH_GROUND        7
#define OPENEVSE_STATE_STUCK_RELAY            8
#define OPENEVSE_STATE_GFI_SELF_TEST_FAILED   9
#define OPENEVSE_STATE_OVER_TEMPERATURE      10
#define OPENEVSE_STATE_OVER_CURRENT          11
#define OPENEVSE_STATE_SLEEPING             254
#define OPENEVSE_STATE_DISABLED             255

#define OPENEVSE_POST_CODE_OK                     0
#define OPENEVSE_POST_CODE_NO_EARTH_GROUND        7
#define OPENEVSE_POST_CODE_STUCK_RELAY            8
#define OPENEVSE_POST_CODE_GFI_SELF_TEST_FAILED   9

// J1772EVSEController m_wFlags bits - saved to EEPROM
#define OPENEVSE_ECF_L2                 0x0001 // service level 2
#define OPENEVSE_ECF_DIODE_CHK_DISABLED 0x0002 // no diode check
#define OPENEVSE_ECF_VENT_REQ_DISABLED  0x0004 // no vent required state
#define OPENEVSE_ECF_GND_CHK_DISABLED   0x0008 // no chk for ground fault
#define OPENEVSE_ECF_STUCK_RELAY_CHK_DISABLED 0x0010 // no chk for stuck relay
#define OPENEVSE_ECF_AUTO_SVC_LEVEL_DISABLED  0x0020 // auto detect svc level - requires ADVPWR
// Ability set the EVSE for manual button press to start charging - GoldServe
#define OPENEVSE_ECF_AUTO_START_DISABLED 0x0040  // no auto start charging
#define OPENEVSE_ECF_SERIAL_DBG         0x0080 // enable debugging messages via serial
#define OPENEVSE_ECF_MONO_LCD           0x0100 // monochrome LCD backlight
#define OPENEVSE_ECF_GFI_TEST_DISABLED  0x0200 // no GFI self test
#define OPENEVSE_ECF_TEMP_CHK_DISABLED  0x0400 // no Temperature Monitoring
#define OPENEVSE_ECF_BUTTON_DISABLED    0x8000 // front panel button disabled

// J1772EVSEController volatile m_wVFlags bits - not saved to EEPROM
#define OPENEVSE_VFLAG_AUTOSVCLVL_SKIPPED   0x0001 // auto svc level test skipped during post
#define OPENEVSE_VFLAG_HARD_FAULT           0x0002 // in non-autoresettable fault
#define OPENEVSE_VFLAG_LIMIT_SLEEP          0x0004 // currently sleeping after reaching time/charge limit
#define OPENEVSE_VFLAG_AUTH_LOCKED          0x0008 // locked pending authentication
#define OPENEVSE_VFLAG_AMMETER_CAL          0x0010 // ammeter calibration mode
#define OPENEVSE_VFLAG_NOGND_TRIPPED        0x0020 // no ground has tripped at least once
#define OPENEVSE_VFLAG_CHARGING_ON          0x0040 // charging relay is closed
#define OPENEVSE_VFLAG_GFI_TRIPPED          0x0080 // gfi has tripped at least once since boot
#define OPENEVSE_VFLAG_EV_CONNECTED         0x0100 // EV connected - valid only when pilot not N12
#define OPENEVSE_VFLAG_SESSION_ENDED        0x0200 // used for charging session time calc
#define OPENEVSE_VFLAG_EV_CONNECTED_PREV    0x0400 // prev EV connected flag
#define OPENEVSE_VFLAG_UI_IN_MENU           0x0800 // onboard UI currently in a menu
#define OPENEVSE_VFLAG_BOOT_LOCK            0x4000 // locked at boot
#define OPENEVSE_VFLAG_MENNEKES_MANUAL      0x8000 // Mennekes lock manual mode

#if defined(AUTH_LOCK) && (AUTH_LOCK != 0)
#define OPENEVSE_VFLAG_DEFAULT              ECVF_AUTH_LOCKED|ECVF_SESSION_ENDED
#else
#define OPENEVSE_VFLAG_DEFAULT              ECVF_SESSION_ENDED
#endif

#define OPENEVSE_WIFI_MODE_AP 0
#define OPENEVSE_WIFI_MODE_CLIENT 1
#define OPENEVSE_WIFI_MODE_AP_DEFAULT 2

#define OPENEVSE_ENCODE_VERSION(a,b,c) (((a)*1000) + ((b)*100) + (c))

#define OPENEVSE_OCPP_SUPPORT_PROTOCOL_VERSION  OPENEVSE_ENCODE_VERSION(5,0,0)

#define OPENEVSE_LCD_OFF      0
#define OPENEVSE_LCD_RED      1
#define OPENEVSE_LCD_GREEN    2
#define OPENEVSE_LCD_YELLOW   3
#define OPENEVSE_LCD_BLUE     4
#define OPENEVSE_LCD_VIOLET   5
#define OPENEVSE_LCD_TEAL     6
#define OPENEVSE_LCD_WHITE    7

#define OPENEVSE_FEATURE_BUTTON             'B' // disable/enable front panel button
#define OPENEVSE_FEATURE_DIODE_CKECK        'D' // Diode check
#define OPENEVSE_FEATURE_ECHO               'E' // command Echo
#define OPENEVSE_FEATURE_GFI_SELF_TEST      'F' // GFI self test
#define OPENEVSE_FEATURE_GROUND_CHECK       'G' // Ground check
#define OPENEVSE_FEATURE_RELAY_CKECK        'R' // stuck Relay check
#define OPENEVSE_FEATURE_TEMPURATURE_CHECK  'T' // temperature monitoring
#define OPENEVSE_FEATURE_VENT_CHECK         'V' // Vent required check

#define OPENEVSE_SERVICE_LEVEL_AUTO         'A'
#define OPENEVSE_SERVICE_LEVEL_L1           '1'
#define OPENEVSE_SERVICE_LEVEL_L2           '2'

typedef std::function<void(uint8_t post_code, const char *firmware)> OpenEVSEBootCallback;
typedef std::function<void(uint8_t evse_state, uint8_t pilot_state, uint32_t current_capacity, uint32_t vflags)> OpenEVSEStateCallback;
typedef std::function<void(uint8_t event)> OpenEVSEWiFiCallback;
typedef std::function<void(uint8_t long_press)> OpenEVSEButtonCallback;

class OpenEVSEClass
{
  private:
    RapiSender *_sender;

    bool _connected;
    uint32_t _protocol;

    OpenEVSEBootCallback _boot;
    OpenEVSEStateCallback _state;
    OpenEVSEWiFiCallback _wifi;
    OpenEVSEButtonCallback _button;

    void onEvent();

  public:
    OpenEVSEClass();
    ~OpenEVSEClass() { }

    void begin(RapiSender &sender, std::function<void(bool connected)> callback);
    void begin(RapiSender &sender, std::function<void(bool connected, const char *firmware, const char *protocol)> callback);

    void getStatus(std::function<void(int ret, uint8_t evse_state, uint32_t session_time, uint8_t pilot_state, uint32_t vflags)> callback);

    void getVersion(std::function<void(int ret, const char *firmware, const char *protocol)> callback);

    void getTime(std::function<void(int ret, time_t time)> callback);
    void setTime(time_t time, std::function<void(int ret)> callback);
    void setTime(tm &time, std::function<void(int ret)> callback);

    void getChargeCurrentAndVoltage(std::function<void(int ret, double amps, double volts)> callback);
    void getTemperature(std::function<void(int ret, double temp1, bool temp1_valid, double temp2, bool temp2_valid, double temp3, bool temp3_valid)> callback);
    void getEnergy(std::function<void(int ret, double session, double total)> callback);
    void getFaultCounters(std::function<void(int ret, long gfci_count, long nognd_count, long stuck_count)> callback);
    void getSettings(std::function<void(int ret, long pilot, uint32_t flags)> callback);
    void getSerial(std::function<void(int ret, const char *serial)> callback);

    void setServiceLevel(uint8_t level, std::function<void(int ret)> callback);

    void getCurrentCapacity(std::function<void(int ret, long min_current, long pilot, long max_configured_current, long max_hardware_current)> callback);
    void setCurrentCapacity(long amps, bool save, std::function<void(int ret, long pilot)> callback);

    void getAmmeterSettings(std::function<void(int ret, long scale, long offset)> callback);
    void setAmmeterSettings(long scale, long offset, std::function<void(int ret)> callback);

    void setVoltage(uint32_t milliVolts, std::function<void(int ret)> callback);
    void setVoltage(double volts, std::function<void(int ret)> callback);
    
    void setTemperature(int sensor, double temperature, std::function<void(int ret)> callback);

    void getTimer(std::function<void(int ret, int start_hour, int start_minute, int end_hour, int end_minute)> callback);
    void setTimer(int start_hour, int start_minute, int end_hour, int end_minute, std::function<void(int ret)> callback);
    void clearTimer(std::function<void(int ret)> callback) {
      setTimer(0, 0, 0, 0, callback);
    }

    void enable(std::function<void(int ret)> callback);
    void sleep(std::function<void(int ret)> callback);
    void disable(std::function<void(int ret)> callback);
    void restart(std::function<void(int ret)> callback);
    void clearBootLock(std::function<void(int ret)> callback);

    void lcdEnable(bool enable, std::function<void(int ret)> callback);
    void lcdSetColour(int colour, std::function<void(int ret)> callback);
    void lcdDisplayText(int x, int y, const char *text, std::function<void(int ret)> callback);

    void feature(uint8_t feature, bool enable, std::function<void(int ret)> callback);

    void heartbeatEnable(int interval, int current, std::function<void(int ret, int interval, int current, int triggered)> callback);
    void heartbeatPulse(bool ack_missed, std::function<void(int ret)> callback);
    void heartbeatPulse(std::function<void(int ret)> callback) {
      heartbeatPulse(true, callback);
    }

    bool isConnected() {
      return _connected;
    }

    void onBoot(OpenEVSEBootCallback callback) {
      _boot = callback;
    }
    void onState(OpenEVSEStateCallback callback) {
      _state = callback;
    }
    void onWiFi(OpenEVSEWiFiCallback callback) {
      _wifi = callback;
    }
    void onButton(OpenEVSEButtonCallback callback) {
      _button = callback;
    }
};

extern OpenEVSEClass OpenEVSE;

#endif
