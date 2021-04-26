#include <stdio.h>
#include <string.h>
#include <switch.h>
#include <vector>
#include "bt_pairing_manager.h"

static BtPairingManager g_pairing_manager;


bool operator <(BtdrvAddress const& left, BtdrvAddress const& right) {
    return memcmp(&left, &right, sizeof(BtdrvAddress)) < 0;
}

const char* BtAddrToString(BtdrvAddress addr)
{
    static char buffer[16];
    snprintf(
        buffer, sizeof buffer, "%02x%02x%02x%02x%02x%02x",
        addr.address[0],
        addr.address[1],
        addr.address[2],
        addr.address[3],
        addr.address[4],
        addr.address[5]);
    return buffer;
}

static const char* Header =
    "\u001b[34m___.   .__                 __                 __  .__\n"
    "\\_ |__ |  |  __ __   _____/  |_  ____   _____/  |_|  |__\n"
    " | __ \\|  | |  |  \\_/ __ \\   __\\/  _ \\ /  _ \\   __\\  |  \\\n"
    " | \\_\\ \\  |_|  |  /\\  ___/|  | (  <_> |  <_> )  | |   Y  \\\n"
    " |___  /____/____/  \\___  >__|  \\____/ \\____/|__| |___|  /\n"
    "     \\/                 \\/                             \\/\u001b[0m\n";

#define Bar "\u001b[34m--------------------------------------------------------------------------------\u001b[0m"

int main(int argc, char *argv[])
{
    consoleInit(NULL);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);

    PadState pad;
    padInitializeDefault(&pad);


    Result rc;

    rc = btdrvInitialize();

    if (!R_SUCCEEDED(rc)) {
        printf("btdrvInitialize %x\n", rc);
        consoleUpdate(NULL);
        while(1);
    }

    rc = g_pairing_manager.Initialize();

    if (!R_SUCCEEDED(rc)) {
        printf("BtPairingManager Initialize %x\n", rc);
        consoleUpdate(NULL);
        while(1);
    }

    int cursor = 0;

    while (appletMainLoop())
    {
        consoleClear();
        printf(Header);
        printf("   plutoo 2021                        State: %s\n", g_pairing_manager.GetState());
        printf("\n");
        printf("\n");
        printf("  A: Begin pair     B: Unpair     X: Refresh     +: Exit\n\n");
        printf(Bar "\n\n");

        padUpdate(&pad);

        u64 kUp = padGetButtonsUp(&pad);

        g_pairing_manager.PollEvents();

        auto device_info_map = g_pairing_manager.GetScanResults();

        std::vector<BtDeviceInfo> device_list;

        for (auto it=device_info_map->begin(); it!=device_info_map->end(); ++it) {
            device_list.push_back(it->second);
        }

        if ((cursor < 0) || (cursor >= device_list.size()))
            cursor = 0;

        for (int i=0; i<device_list.size(); i++) {
            printf("[%s] %s (%s)\n", (cursor == i) ? ">" : " ", device_list[i].name, BtAddrToString(device_list[i].btaddr));

            if (device_list[i].paired)
                printf("        \e[0;32mCONNECTED\e[0;0m\n");
            else if (device_list[i].has_ssp_request)
                printf("        \e[0;36mGOT SSP...\e[0;0m\n");
            else if (device_list[i].wants_pair)
                printf("        \e[0;36mPAIRING\e[0;0m...\n");

            printf("\n");
        }

        printf("\n" Bar "\n");

        if (kUp & HidNpadButton_Plus)
            break;

        if (kUp & (HidNpadButton_A | HidNpadButton_B)) {
            if ((cursor >= 0) && (cursor < device_list.size())) {
                if (kUp & HidNpadButton_A)
                    g_pairing_manager.Pair(device_list[cursor].btaddr);

                if (kUp & HidNpadButton_B)
                    g_pairing_manager.Unpair(device_list[cursor].btaddr);
            }
        }

        if (kUp & HidNpadButton_X)
            g_pairing_manager.BeginScan();

        if (kUp & HidNpadButton_Up)
            cursor--;

        if (kUp & HidNpadButton_Down)
            cursor++;


        consoleUpdate(NULL);
    }

    btdrvExit();
    consoleExit(NULL);
    return 0;
}
