#include <map>
#include <memory>

enum PairingState {
    Uninitialized,
    Ready,
    Scanning,
};

struct BtDeviceInfo {
    char name[0xf9];
    BtdrvAddress btaddr;
    bool has_ssp_request;
    s32 ssp_passkey;
    bool wants_pair;
    bool paired;
};

typedef std::map<BtdrvAddress, BtDeviceInfo> DeviceInfoMap;

class BtPairingManager {
public:
    BtPairingManager();
    Result Initialize();
    Result BeginScan();
    void PollEvents();
    void Pair(BtdrvAddress addr);
    void Unpair(BtdrvAddress addr);
    DeviceInfoMap* GetScanResults();
    const char* GetState();
    ~BtPairingManager();

private:
    bool m_is_initialized;
    Event m_btevent;
    PairingState m_state;
    DeviceInfoMap m_devices;
};
