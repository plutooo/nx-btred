#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <switch.h>
#include "bt_config.h"

BtConfig g_config;
#define NE(x, y) (memcmp(&(x), &(y), sizeof(x)) != 0)


BtConfig::BtConfig():
    m_btsettings{},
    m_btaddr{}
{ }

Result BtConfig::Initialize()
{
    bool needs_update = false;

    FILE* fd = fopen("config/btred/settings.bin", "rb");

    if (fd != NULL) {
        SetSysBluetoothDevicesSettings settings_file{};
        SetSysBluetoothDevicesSettings settings_current{};
        SetSysBluetoothDevicesSettings settings_empty{};

        fread(&settings_file, sizeof(SetSysBluetoothDevicesSettings), 1, fd);

        Result rc;
        rc = btdrvGetPairedDeviceInfo(settings_file.addr, &settings_current);

        if (R_SUCCEEDED(rc)) {
            m_btsettings = settings_current;
            needs_update = needs_update || NE(settings_current, settings_file);
        }
        else {
            m_btsettings = settings_file;

            if (NE(settings_file, settings_empty)) {
                rc = btdrvAddPairedDeviceInfo(&settings_file);

                if (R_FAILED(rc))
                    fatalThrow(rc);
            }
        }

        m_btaddr = m_btsettings.addr;
        fclose(fd);
    }

    if (needs_update) {
        SaveConfig();
    }

    return 0;
}

void BtConfig::SaveConfig()
{
    mkdir("config", 0666);
    mkdir("config/btred", 0666);

    FILE* fd = fopen("config/btred/settings.bin", "wb");

    if (fd != NULL) {
        fwrite(&m_btsettings, sizeof(m_btsettings), 1, fd);
        fclose(fd);
    }
}

void BtConfig::SetHeadphonesBtAddress(BtdrvAddress btaddr)
{
    bool dirty = NE(btaddr, m_btaddr);

    SetSysBluetoothDevicesSettings settings;
    Result rc;

    rc = btdrvGetPairedDeviceInfo(btaddr, &settings);

    if (R_SUCCEEDED(rc)) {
        dirty = dirty || NE(m_btsettings, settings);
        m_btsettings = settings;
        m_btaddr = btaddr;
    }

    if (dirty) {
        SaveConfig();
    }
}

BtdrvAddress BtConfig::GetHeadphonesBtAddress()
{
    return m_btaddr;
}

SetSysBluetoothDevicesSettings* BtConfig::GetHeadphonesBtSettings()
{
    return &m_btsettings;
}

bool BtConfig::HasHeadphonesBtAddress()
{
    BtdrvAddress empty{};
    return NE(empty, m_btaddr);
}
