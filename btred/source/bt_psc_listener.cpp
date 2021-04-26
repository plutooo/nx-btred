#include <malloc.h>
#include <switch.h>
#include "bt_audio_manager.h"
#include "bt_psc_listener.h"

BtPscListener::BtPscListener(BtAudioManager* parent):
    m_parent(parent),
    m_is_initialized(false),
    m_is_suspended(false)
{ }

BtPscListener::~BtPscListener()
{
    Finalize();
}

void BtPscListener::Finalize()
{
    if (m_is_initialized) {
        ueventSignal(&m_workthread_exitsignal);
        threadWaitForExit(&m_workthread);
        pscPmModuleFinalize(&m_psc_module);
        pscPmModuleClose(&m_psc_module);
        pscmExit();
        threadClose(&m_workthread);
        free(m_workthread_stack);

        m_is_initialized = false;
    }
}

Result BtPscListener::Initialize()
{
    Result rc;

    #define DefaultStackSize 0x4000
    #define DefaultPrio 0x2C
    #define DefaultCore -2

    m_workthread_stack = memalign(0x1000, DefaultStackSize);

    if (m_workthread_stack == NULL) {
        return -1;
    }

    rc = threadCreate(
        &m_workthread,
        (ThreadFunc) WorkerThreadTrampoline,
        (void*) this,
        m_workthread_stack,
        DefaultStackSize,
        DefaultPrio,
        DefaultCore);

    if (R_FAILED(rc)) {
        free(m_workthread_stack);
        return rc;
    }

    ueventCreate(&m_workthread_exitsignal, false);

    rc = pscmInitialize();

    if (R_FAILED(rc)) {
        threadClose(&m_workthread);
        free(m_workthread_stack);
        return rc;
    }

    u32 deps[] = {
        PscPmModuleId_Bluetooth,
        PscPmModuleId_Audio,
        PscPmModuleId_Fs,
        PscPmModuleId_Uart,
        PscPmModuleId_Btm,
        PscPmModuleId_Ns,
    };

    rc = pscmGetPmModule(&m_psc_module, (PscPmModuleId)123, deps, sizeof(deps)/sizeof(deps[0]), true);

    if (R_FAILED(rc)) {
        pscmExit();
        threadClose(&m_workthread);
        free(m_workthread_stack);
        return rc;
    }

    rc = threadStart(&m_workthread);

    if (R_FAILED(rc)) {
        pscPmModuleFinalize(&m_psc_module);
        pscPmModuleClose(&m_psc_module);
        pscmExit();
        threadClose(&m_workthread);
        free(m_workthread_stack);
        return rc;
    }

    m_is_initialized = true;
    return rc;
}

void BtPscListener::WorkerThread()
{
    // Purpose of this function is to wait for the ReadyToSleep event,
    // so that we may clean up properly before a suspend.

    // On wake-up, the bluetooth driver automatically reconnects, and
    // gives us an AudioConnectionEvent, so we don't need to handle it
    // here.

    bool running = true;
    Result rc = 0;

    while (running)
    {
        int idx;

        rc = waitMulti(
            &idx, -1,
            waiterForUEvent(&m_workthread_exitsignal),
            waiterForEvent(&m_psc_module.event));

        if (R_FAILED(rc))
            fatalThrow(rc);

        switch (idx)
        {
            case 0: // m_workthread_exitsignal
                running = false;
                break;

            case 1: // m_psc_module.event
                HandleEvent();
                break;
        }
    }
}

void BtPscListener::HandleEvent()
{
    Result rc;
    PscPmState state;
    u32 flags;

    rc = pscPmModuleGetRequest(&m_psc_module, &state, &flags);

    if (R_FAILED(rc))
        fatalThrow(rc);

    switch (state) {
        case PscPmState_Awake:
            if (m_is_suspended) {
                m_parent->OnResume();
                m_is_suspended = false;
            }

            break;

        case PscPmState_ReadyAwaken:
        case PscPmState_ReadySleep:
        case PscPmState_ReadySleepCritical:
        case PscPmState_ReadyAwakenCritical:
            if (!m_is_suspended) {
                m_parent->OnSuspend();
                m_is_suspended = true;
            }

            break;

        default:
            break;
    }

    rc = pscPmModuleAcknowledge(&m_psc_module, state);

    if (R_FAILED(rc))
        fatalThrow(rc);
}