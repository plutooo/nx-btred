#include <stdio.h>
#include <string.h>
#include <switch.h>
#include "bt_audio_manager.h"
#include "bt_config.h"
#include "audrec.h"

Mutex g_btdrv_mutex;

void NORETURN fatalThrowWithPc(Result err);


int main(int argc, char *argv[])
{
    Result rc;

    mutexInit(&g_btdrv_mutex);

    // Temporary workaround for race condition during boot.
    // This is probably an issue in an official Nintendo sysmodule.
    // TODO: Investigate deeper
    svcSleepThread(5000000000ULL);

    rc = g_audio_manager.Initialize();

    if (R_FAILED(rc))
        fatalThrowWithPc(rc);

    rc = g_config.Initialize();

    if (R_FAILED(rc))
        fatalThrowWithPc(rc);

    rc = setsysInitialize();

    if (R_FAILED(rc))
        fatalThrowWithPc(rc);

    while (1)
        g_audio_manager.PollEvents();

    return 0;
}

void NORETURN fatalThrowWithPc(Result err)
{
    FatalCpuContext ctx;
    extern int _start;

    memset(&ctx, 0, sizeof(ctx));
    ctx.is_aarch32 = false;
    ctx.aarch64_ctx.pc = (u64)__builtin_return_address(0) - 4;
    ctx.aarch64_ctx.start_address = (u64)&_start;

    fatalThrowWithContext(err, FatalPolicy_ErrorScreen, &ctx);
    svcExitProcess();
    __builtin_unreachable();
}

extern "C" {
u32 __nx_applet_type = AppletType_None;
u32 __nx_fs_num_sessions = 1;

void __libnx_initheap(void)
{
    static char g_heap[0x100000];
    extern char *fake_heap_start;
    extern char *fake_heap_end;
    fake_heap_start = &g_heap[0];
    fake_heap_end = &g_heap[sizeof g_heap];
}
}
