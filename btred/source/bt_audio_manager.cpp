#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <switch.h>
#include "bt_audio_manager.h"
#include "bt_config.h"

//#define ENABLE_TRACE

#ifdef ENABLE_TRACE
#define TRACE(...) printf(__VA_ARGS__)
#else
#define TRACE(...)
#endif

void NORETURN fatalThrowWithPc(Result err);

bool operator <(BtdrvAddress const& left, BtdrvAddress const& right) {
    return memcmp(&left, &right, sizeof(BtdrvAddress)) < 0;
}

BtAudioManager g_audio_manager;


BtAudioManager::BtAudioManager():
    m_is_initialized(false),
    m_psc_listener(this),
    m_is_first_connect(true)
{
    utimerCreate(&m_reconnect_timer, 1000000000ULL * 10, TimerType_Repeating);
    utimerStart(&m_reconnect_timer);

    utimerCreate(&m_connect_workaround_timer, 1000000000ULL * 5, TimerType_OneShot);

    mutexInit(&m_suspend_mutex);
}

Result BtAudioManager::Initialize()
{
    Result rc;

    rc = btdrvInitialize();

    if (R_FAILED(rc)) {
        return rc;
    }

    rc = btdrvAcquireAudioConnectionStateChangedEvent(&m_btdrv_audio_connection_event, true);
    TRACE("[?] btdrvAcquireAudioConnectionStateChangedEvent: %x\n", rc);

    if (R_FAILED(rc)) {
        btdrvExit();
        return rc;
    }

    rc = btdrvAcquireAudioEvent(&m_btdrv_audio_info_event, true);
    TRACE("[?] btdrvAcquireAudioEvent: %x\n", rc);

    if (R_FAILED(rc)) {
        eventClose(&m_btdrv_audio_connection_event);
        btdrvExit();
        return rc;
    }

    rc = audctlInitialize();

    if (R_FAILED(rc)) {
        eventClose(&m_btdrv_audio_info_event);
        eventClose(&m_btdrv_audio_connection_event);
        btdrvExit();
        return rc;
    }

    rc = m_psc_listener.Initialize();

    if (R_FAILED(rc)) {
        audctlExit();
        eventClose(&m_btdrv_audio_info_event);
        eventClose(&m_btdrv_audio_connection_event);
        btdrvExit();
        return rc;
    }

    RefreshDevices();

    m_is_initialized = true;

    return rc;
}

BtAudioManager::~BtAudioManager()
{
    m_devices.clear();

    if (m_is_initialized) {
        m_psc_listener.Finalize();
        audctlExit();
        eventClose(&m_btdrv_audio_info_event);
        eventClose(&m_btdrv_audio_connection_event);
        btdrvExit();
        m_is_initialized = false;
    }
}

void BtAudioManager::PollEvents()
{
    Result rc;
    int idx;

    rc = waitMulti(
        &idx, -1,
        waiterForEvent(&m_btdrv_audio_connection_event),
        waiterForEvent(&m_btdrv_audio_info_event),
        waiterForUTimer(&m_reconnect_timer),
        waiterForUTimer(&m_connect_workaround_timer));

    if (R_FAILED(rc))
        fatalThrowWithPc(rc);

    mutexLock(&m_suspend_mutex);

    switch (idx)
    {
        case 0: // m_audio_connection_event
            RefreshDevices();
            break;

        case 1: // m_audio_info_event
            break;

        case 2: // m_reconnect_timer
            if (m_devices.size() == 0) {
                if (g_config.HasHeadphonesBtAddress()) {
                    mutexLock(&g_btdrv_mutex);
                    btdrvOpenAudioConnection(g_config.GetHeadphonesBtAddress());
                    mutexUnlock(&g_btdrv_mutex);
                }
            }
            break;

        case 3: // m_connect_workaround_timer:
            mutexLock(&g_btdrv_mutex);
            btdrvCloseAudioConnection(g_config.GetHeadphonesBtAddress());
            mutexUnlock(&g_btdrv_mutex);
            break;
    }

    mutexUnlock(&m_suspend_mutex);
}

void BtAudioManager::RefreshDevices()
{
    // When we receive the AudioConnectionEvent signal, we need to fetch
    // the device list and diff it against our own understanding.

    int total_out;
    BtdrvAddress audio_addrs[8] = {0};
    Result rc;

    mutexLock(&g_btdrv_mutex);
    rc = btdrvGetConnectedAudioDevice(audio_addrs, 8, &total_out);
    mutexUnlock(&g_btdrv_mutex);

    TRACE("[?] btdrvGetConnectedAudioDevice: 0x%x, %d\n", rc, total_out);

    if (R_FAILED(rc))
        return;

    // Check whether we can find any new audio devices.
    // These would then in turn each get their own BtAudioDevice object.
    for (int i = 0; i < total_out; i++) {
        BtdrvAddress btaddr = audio_addrs[i];

        if (!m_devices.contains(btaddr)) {
            // Mute speakers during bluetooth initialization.
            // This will be undone, at the end of the function.
            audctlSetSystemOutputMasterVolume(0);

            TRACE("[+] New audio source\n");
            auto device = std::make_shared<BtAudioDevice>(btaddr);

            rc = device->Initialize();

            if (R_FAILED(rc)) {
                TRACE("[!] Failed to initialize device\n");
                continue;
            }

            m_devices[btaddr] = device;

            if (m_is_first_connect) {
                // For some headphones, a reconnect is required after the
                // first connect.
                // TODO: Investigate deeper.
                m_is_first_connect = false;
                utimerStart(&m_connect_workaround_timer);
            }
        }
    }

    // Check if any audio devices were removed. When they are removed from
    // the device map, the destructor cleans them up properly.
    for (auto it = m_devices.begin(); it != m_devices.end(); ) {
        bool was_found = false;

        for (int i = 0; i < total_out; i++) {
            if (memcmp(&audio_addrs[i], &it->first, sizeof(BtdrvAddress)) == 0) {
                was_found = true;
                break;
            }
        }

        if (!was_found) {
            TRACE("[-] Removed audio source\n");
            it = m_devices.erase(it);
        }
        else {
            ++it;
        }
    }

    // Here we mute speakers if we have a bluetooth headset connected.
    audctlSetSystemOutputMasterVolume(m_devices.size() ? 0 : 1);
}

void BtAudioManager::OnSuspend()
{
    // Warning: This function is executed in the PSC event listener thread.

    // The bluetooth driver will enter a weird state if we don't
    // remove our connection before sleeping.

    // Similar/same issue arises if we open an audio connection during
    // suspend. Therefore we must hold this lock to pause the main thread
    // throughout the entire suspend.

    mutexLock(&m_suspend_mutex);

    for (auto it = m_devices.begin(); it != m_devices.end(); ) {
        it = m_devices.erase(it);
    }
}

void BtAudioManager::OnResume()
{
    // Warning: This function is executed in the PSC event listener thread.

    mutexUnlock(&m_suspend_mutex);
}