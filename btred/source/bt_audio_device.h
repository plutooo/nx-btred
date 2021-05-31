#pragma once

#define NUM_BUF 8
#define SAMPLES_PER_BUF 0x400 // 0x800
#define BUF_SIZE (SAMPLES_PER_BUF * sizeof(u16))
#define TOTAL_SIZE (NUM_BUF * BUF_SIZE)

class BtAudioDevice {
public:
    BtAudioDevice(BtdrvAddress addr);
    ~BtAudioDevice();

    Result Initialize();

private:
    Result InitializeBtdrv();
    void   FinalizeBtdrv();
    Result InitializeAudrec();
    void   FinalizeAudrec();
    Result InitializeBuffers();
    void   FinalizeBuffers();
    Result InitializeThread();
    void   FinalizeThread();

    Result QueueBuffer(void* buf);
    Result AudioReceived();
    Result SendAudio(void* buf);
    Result ApplyVolume(void* buf);
    Result RefreshAudrec();

    static void WorkerThreadTrampoline(BtAudioDevice* self) {
        self->WorkerThread();
    }
    void WorkerThread();

private:
    BtdrvAddress m_addr;

    bool   m_is_btdrv_initialized;
    u32    m_btdrv_handle;
    Event  m_btdrv_statechange_event;
    BtdrvAudioOutState m_btdrv_state;

    bool   m_is_audrec_initialized;
    AudrecRecorder m_audrec_recorder;
    Event  m_audrec_buffer_event;

    bool   m_are_buffers_initialized;
    void*  m_buffers[NUM_BUF];
    void*  m_buffer_mem;

    bool   m_is_thread_initialized;
    Thread m_workthread;
    void*  m_workthread_stack;
    UEvent m_workthread_exitsignal;
};

