#pragma once

class BtAudioManager;

class BtPscListener {
public:
    BtPscListener(BtAudioManager* parent);
    ~BtPscListener();

    Result Initialize();
    void   Finalize();

private:
    void HandleEvent();
    void WorkerThread();

    static void WorkerThreadTrampoline(BtPscListener* self) {
        self->WorkerThread();
    }

private:
    BtAudioManager* m_parent;

    bool   m_is_initialized;
    Thread m_workthread;
    void*  m_workthread_stack;
    UEvent m_workthread_exitsignal;
    PscPmModule m_psc_module;
    bool   m_is_suspended;
};
