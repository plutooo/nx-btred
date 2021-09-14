#pragma once

class BtConfig {
public:
    BtConfig();
    Result Initialize();
    void SaveConfig();

    bool HasHeadphonesBtAddress();
    void SetHeadphonesBtAddress(BtdrvAddress btaddr);
    BtdrvAddress GetHeadphonesBtAddress();
    SetSysBluetoothDevicesSettings* GetHeadphonesBtSettings();

private:
    SetSysBluetoothDevicesSettings m_btsettings;
    BtdrvAddress m_btaddr;
};

extern BtConfig g_config;
