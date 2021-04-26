#pragma once

#include <map>
#include <memory>
#include "bt_audio_device.h"
#include "bt_psc_listener.h"

typedef std::map<BtdrvAddress, std::shared_ptr<BtAudioDevice>> DeviceMap;

class BtAudioManager {
public:
    BtAudioManager();
    ~BtAudioManager();

    Result Initialize();
    void   PollEvents();

private:
    void RefreshDevices();

protected:
    friend class BtPscListener;
    void OnSuspend();
    void OnResume();

private:
    Mutex     m_suspend_mutex;

    bool      m_is_initialized;
    Event     m_btdrv_audio_info_event;
    Event     m_btdrv_audio_connection_event;
    DeviceMap m_devices;
    UTimer    m_reconnect_timer;
    BtPscListener m_psc_listener;
};

extern Mutex g_btdrv_mutex;
extern BtAudioManager g_audio_manager;
