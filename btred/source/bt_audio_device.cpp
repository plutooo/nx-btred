#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <switch.h>
#include <arm_neon.h>
#include <math.h>
#include "audrec.h"
#include "bt_audio_manager.h"
#include "bt_config.h"

//#define ENABLE_TRACE

#ifdef ENABLE_TRACE
#define TRACE(...) printf(__VA_ARGS__)
#else
#define TRACE(...)
#endif

void NORETURN fatalThrowWithPc(Result err);


BtAudioDevice::BtAudioDevice(BtdrvAddress addr):
    m_addr(addr),
    m_is_btdrv_initialized(false),
    m_is_audrec_initialized(false),
    m_are_buffers_initialized(false),
    m_is_thread_initialized(false)
{
    g_config.SetHeadphonesBtAddress(addr);
}

Result BtAudioDevice::Initialize()
{
    Result rc;

    rc = InitializeBtdrv();

    if (R_FAILED(rc)) {
        return rc;
    }

    rc = InitializeAudrec();

    if (R_FAILED(rc)) {
        FinalizeBtdrv();
        return rc;
    }

    rc = InitializeBuffers();

    if (R_FAILED(rc)) {
        FinalizeAudrec();
        FinalizeBtdrv();
        return -1;
    }

    rc = InitializeThread();

    if (R_FAILED(rc)) {
        FinalizeBuffers();
        FinalizeAudrec();
        FinalizeBtdrv();
        return rc;
    }

    return rc;
}

Result BtAudioDevice::InitializeBtdrv()
{
    Result rc;

    mutexLock(&g_btdrv_mutex);
    rc = btdrvOpenAudioOut(m_addr, &m_btdrv_handle);

    if (R_FAILED(rc)) {
        mutexUnlock(&g_btdrv_mutex);
        return rc;
    }

    rc = btdrvAcquireAudioOutStateChangedEvent(m_btdrv_handle, &m_btdrv_statechange_event, true);

    if (R_FAILED(rc)) {
        btdrvCloseAudioOut(m_btdrv_handle);
        mutexUnlock(&g_btdrv_mutex);
        return rc;
    }

    rc = btdrvGetAudioOutState(m_btdrv_handle, &m_btdrv_state);

    if (R_FAILED(rc)) {
        eventClose(&m_btdrv_statechange_event);
        btdrvCloseAudioOut(m_btdrv_handle);
        mutexUnlock(&g_btdrv_mutex);
        return rc;
    }

    BtdrvPcmParameter param;
    param.unk_x0 = 1;
    param.sample_rate = 48000;
    param.bits_per_sample = 16;

    s64 latency = 4000000LL;
    u64 out1;

    rc = btdrvStartAudioOut(m_btdrv_handle, &param, latency, &latency, &out1);

    // TODO: Maybe not necessary
    #define BtdrvErrorAlreadyStarted 0x190071
    if (rc == BtdrvErrorAlreadyStarted)
        rc = 0;

    if (R_FAILED(rc)) {
        eventClose(&m_btdrv_statechange_event);
        btdrvCloseAudioOut(m_btdrv_handle);
        mutexUnlock(&g_btdrv_mutex);
        return rc;
    }

    m_is_btdrv_initialized = true;
    mutexUnlock(&g_btdrv_mutex);
    return rc;
}

void BtAudioDevice::FinalizeBtdrv()
{
    if (m_is_btdrv_initialized) {
        mutexLock(&g_btdrv_mutex);
        btdrvStopAudioOut(m_btdrv_handle);
        eventClose(&m_btdrv_statechange_event);
        btdrvCloseAudioOut(m_btdrv_handle);
        mutexUnlock(&g_btdrv_mutex);
        m_is_btdrv_initialized = false;
    }

    mutexLock(&g_btdrv_mutex);
    btdrvCloseAudioConnection(m_addr);
    mutexUnlock(&g_btdrv_mutex);
}

Result BtAudioDevice::InitializeAudrec()
{
    Result rc;

    rc = _audrecInitialize();

    if (R_FAILED(rc)) {
        return rc;
    }

    FinalOutputRecorderParameter param_in;
    param_in.sample_rate = 48000;
    param_in.padding = 0;

    FinalOutputRecorderParameterInternal param_out;
    rc = audrecOpenFinalOutputRecorder(&m_audrec_recorder, &param_in, 0, &param_out);

    if (R_FAILED(rc)) {
        _audrecCleanup();
        return rc;
    }

    TRACE("sample_rate: %u\n", param_out.sample_rate);
    TRACE("channel_count: %u\n", param_out.channel_count);
    TRACE("sample_format: %u\n", param_out.sample_format);
    TRACE("state: %u\n", param_out.state);

    rc = audrecRecorderStart(&m_audrec_recorder);

    if (R_FAILED(rc)) {
        audrecRecorderClose(&m_audrec_recorder);
        _audrecCleanup();
        return rc;
    }

    rc = audrecRecorderRegisterBufferEvent(&m_audrec_recorder, &m_audrec_buffer_event);

    if (R_FAILED(rc)) {
        audrecRecorderStop(&m_audrec_recorder);
        audrecRecorderClose(&m_audrec_recorder);
        _audrecCleanup();
        return rc;
    }

    m_is_audrec_initialized = true;

    return rc;
}

void BtAudioDevice::FinalizeAudrec()
{
    if (m_is_audrec_initialized) {
        eventClose(&m_audrec_buffer_event);
        audrecRecorderStop(&m_audrec_recorder);
        audrecRecorderClose(&m_audrec_recorder);
        _audrecCleanup();
        m_is_audrec_initialized = false;
    }
}

Result BtAudioDevice::InitializeBuffers()
{
    m_buffer_mem = memalign(0x1000, TOTAL_SIZE);

    if (m_buffer_mem == NULL)
        return -1;

    size_t i;
    for (i=0; i<NUM_BUF; i++) {
        m_buffers[i] = (void*)((u16*)m_buffer_mem + i*SAMPLES_PER_BUF);
    }

    m_are_buffers_initialized = true;
    return 0;
}

void BtAudioDevice::FinalizeBuffers()
{
    if (m_are_buffers_initialized) {
        free(m_buffer_mem);
        m_are_buffers_initialized = false;
    }
}

Result BtAudioDevice::InitializeThread()
{
    #define DefaultStackSize 0x4000
    #define DefaultPrio 0x2C
    #define DefaultCore -2
    Result rc;

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

    rc = threadStart(&m_workthread);

    if (R_FAILED(rc)) {
        threadClose(&m_workthread);
        free(m_workthread_stack);
        return rc;
    }

    m_is_thread_initialized = true;

    return rc;
}

void BtAudioDevice::FinalizeThread()
{
    if (m_is_thread_initialized) {
        ueventSignal(&m_workthread_exitsignal);
        threadWaitForExit(&m_workthread);
        threadClose(&m_workthread);
        free(m_workthread_stack);
        m_is_thread_initialized = false;
    }
}

BtAudioDevice::~BtAudioDevice()
{
    FinalizeThread();
    FinalizeBuffers();
    FinalizeAudrec();
    FinalizeBtdrv();
}

Result BtAudioDevice::RefreshAudrec()
{
    Result rc;

    FinalizeAudrec();

    rc = InitializeAudrec();

    if (R_FAILED(rc))
        return rc;

    size_t i;
    for (i=0; i<NUM_BUF; i++) {
        QueueBuffer(m_buffers[i]);
    }

    return rc;
}

Result BtAudioDevice::QueueBuffer(void* buf)
{
    FinalOutputRecorderBuffer param;
    param.released_buffer_ptr = 0;
    param.next_buffer_ptr = 0;
    param.sample_buffer_ptr = (u64)buf;
    param.sample_buffer_capacity = BUF_SIZE;
    param.data_size = BUF_SIZE;
    param.data_offset = 0;

    return audrecRecorderAppendFinalOutputRecorderBuffer(&m_audrec_recorder, (u64)buf, &param);
}

Result BtAudioDevice::AudioReceived()
{
    FinalOutputRecorderBuffer params[NUM_BUF];
    u64 count = NUM_BUF;
    u64 released;
    Result rc;

    rc = audrecRecorderGetReleasedFinalOutputRecorderBuffers(&m_audrec_recorder, &params[0], &count, &released);

    if (R_FAILED(rc))
        return rc;

    size_t i;
    for (i=0; i<count; i++) {
        if (params[i].released_buffer_ptr == 0)
            continue;

        void* buf = (void*) params[i].released_buffer_ptr;

        ApplyVolume(buf);
        SendAudio(buf);
        QueueBuffer(buf);
    }

    #define TWO_PERIODS ((2*1000000000ULL*SAMPLES_PER_BUF)/48000)

    // If audrec gets out of sync (when switching apps), we need to refresh it.
    if ((armTicksToNs(svcGetSystemTick()) - released) > TWO_PERIODS) {
        return RefreshAudrec();
    }

    return rc;
}

Result BtAudioDevice::SendAudio(void* buf)
{
    u64 transferred = 0;
    Result rc;

    mutexLock(&g_btdrv_mutex);
    rc = btdrvSendAudioData(m_btdrv_handle, buf, BUF_SIZE, &transferred);
    mutexUnlock(&g_btdrv_mutex);

    return rc;
}

Result BtAudioDevice::ApplyVolume(void* buf)
{
    SetSysAudioVolume vol;
    Result rc = setsysGetAudioVolume(SetSysAudioDevice_Console, &vol);

    if (R_FAILED(rc))
        return rc;

    // Here's how I arrived at that number.
    // x^0 = 1
    // x^15 = 1/128
    // x = 0.7236346187201891

    float volume = powf(0.7236f, 15 - vol.volume);

    if (vol.volume == 0)
        volume = 0;

    s16* pcm = (s16*) buf;

    size_t i;
    for (i=0; i<SAMPLES_PER_BUF; i+=4) {
        int16x4_t   tmp0 = vld1_s16(pcm + i);         // Load four s16.
        int32x4_t   tmp1 = vmovl_s16(tmp0);           // Convert them into four s32.
        float32x4_t tmp2 = vcvtq_f32_s32(tmp1);       // Convert them into float.
        float32x4_t tmp3 = vmulq_n_f32(tmp2, volume); // Multiply each by volume.
        int32x4_t   tmp4 = vcvtq_s32_f32(tmp3);       // Convert back into s32.
        int16x4_t   tmp5 = vqmovn_s32(tmp4);          // Convert back into s16 (saturated!).
        vst1_s16(pcm + i, tmp5);                      // Store them back.
    }

    return rc;
}

void BtAudioDevice::WorkerThread()
{
    bool running = true;
    Result rc = 0;

    TRACE("BtAudioDevice::WorkerThread\n");

    size_t i;
    for (i=0; i<NUM_BUF; i++) {
        QueueBuffer(m_buffers[i]);
    }

    while (running)
    {
        int idx;

        rc = waitMulti(
            &idx, -1,
            waiterForUEvent(&m_workthread_exitsignal),
            waiterForEvent(&m_btdrv_statechange_event),
            waiterForEvent(&m_audrec_buffer_event));

        if (R_FAILED(rc)) {
            fatalThrow(rc);
        }

        switch (idx)
        {
            case 0: // m_workthread_exitsignal
                running = false;
                break;

            case 1: // m_btdrv_statechange_event
                mutexLock(&g_btdrv_mutex);
                rc = btdrvGetAudioOutState(m_btdrv_handle, &m_btdrv_state);
                mutexUnlock(&g_btdrv_mutex);
                break;

            case 2: // m_audrec_buffer_event
                rc = AudioReceived();
                break;
        }
    }
}
