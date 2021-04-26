#include <stdio.h>
#include <string.h>
#include <switch.h>
#include "bt_pairing_manager.h"

//#define ENABLE_TRACE

#ifdef ENABLE_TRACE
#define TRACE(...) printf(__VA_ARGS__)
#else
#define TRACE(...)
#endif


bool operator<(BtdrvAddress const &left, BtdrvAddress const &right);

Result btdrvMissionControlRedirectCoreEvents(bool enable)
{
    return serviceDispatchIn(btdrvGetServiceSession(), 65002, enable);
}

// TODO: move into libnx
Result btdrvStartInquiryNew(u32 service_mask, s64 duration)
{
    const struct {
        u32 service_mask;
        s64 duration;
    } in = {service_mask, duration};

    return serviceDispatchIn(btdrvGetServiceSession(), 8, in);
}

// TODO: move into libnx
Result btdrvRespondToSspRequestNew(BtdrvAddress addr, u32 variant, bool flag, u32 unk)
{
    const struct {
        BtdrvAddress addr;
        u8 flag;
        u32 variant;
        u32 unk;
    } in = {addr, flag != 0, variant, unk};

    return serviceDispatchIn(btdrvGetServiceSession(), 14, in);
}

const char *BtAddrToString(BtdrvAddress addr);

BtPairingManager::BtPairingManager():
    m_is_initialized(false), m_state(PairingState::Uninitialized)
{ }

Result BtPairingManager::Initialize()
{
    Result rc;

    rc = btdrvInitialize();

    if (R_FAILED(rc)) {
        TRACE("[!] btdrvInitialize %x\n", rc);
        return rc;
    }

    // TODO: If mission control is not installed, this will force terminate the IPC session.
    rc = btdrvMissionControlRedirectCoreEvents(true);

    if (R_FAILED(rc)) {
        TRACE("[!] btdrvMissionControlRedirectCoreEvents %x\n", rc);
        btdrvExit();
        return rc;
    }

    rc = btdrvInitializeBluetooth(&m_btevent);

    if (R_FAILED(rc)) {
        TRACE("[!] btdrvInitializeBluetooth %x\n", rc);
        btdrvMissionControlRedirectCoreEvents(false);
        btdrvExit();
        return rc;
    }

    m_state = PairingState::Ready;
    m_is_initialized = true;
    return rc;
}

Result BtPairingManager::BeginScan()
{
    Result rc;

    if (m_state != PairingState::Ready)
        return -1;

    rc = btdrvStartInquiryNew(0xffffffff, 10200000000ull);

    if (!R_SUCCEEDED(rc)) {
        TRACE("[!] btdrvStartInquiryNew %x\n", rc);
        return rc;
    }

    m_devices.clear();
    m_state = PairingState::Scanning;
    return rc;
}

void BtPairingManager::PollEvents()
{
    Result rc;

    // Check currently connected audio devices.
    int total_out;
    BtdrvAddress audio_addrs[8] = {0};

    rc = btdrvGetConnectedAudioDevice(audio_addrs, 8, &total_out);

    if (R_SUCCEEDED(rc))
    {
        // Reset every device to false.
        for (auto it=m_devices.begin(); it!=m_devices.end(); ++it)
            it->second.paired = false;

        // Set all active ones to true.
        for (int i=0; i<total_out; i++)
        {
            BtdrvAddress btaddr = audio_addrs[i];

            if (!m_devices.contains(btaddr))
                m_devices[btaddr] = BtDeviceInfo{};

            m_devices[btaddr].btaddr = btaddr;
            m_devices[btaddr].paired = true;
        }
    }

    // Check bluetooth events.
    rc = eventWait(&m_btevent, 0);

    if (R_FAILED(rc))
        return;

    BtdrvEventInfo info;
    BtdrvEventType type;

    rc = btdrvGetEventInfo(&info, sizeof(info), &type);
    TRACE("[?] btdrvGetEventInfo: %x %x\n", rc, type);

    if (R_FAILED(rc))
        return;

    BtdrvAddress btaddr;

    switch (type)
    {
    case BtdrvEventType_InquiryDevice:
        btaddr = info.inquiry_device.v12.addr;

        if (!m_devices.contains(btaddr))
            m_devices[btaddr] = BtDeviceInfo{};

        m_devices[btaddr].btaddr = btaddr;
        memcpy(m_devices[btaddr].name, info.inquiry_device.v12.name, sizeof(m_devices[btaddr].name));

        TRACE("[+] Discovered %s (%s)\n", m_devices[btaddr].name, BtAddrToString(btaddr));
        break;

    case BtdrvEventType_InquiryStatus:
        if ((info.inquiry_status.v12.status & 0xff) == 0)
            m_state = PairingState::Ready;

        break;

    case BtdrvEventType_SspRequest:
        btaddr = info.ssp_request.v12.addr;

        if (!m_devices.contains(btaddr))
            m_devices[btaddr] = BtDeviceInfo{};

        m_devices[btaddr].btaddr = btaddr;
        m_devices[btaddr].has_ssp_request = true;
        m_devices[btaddr].ssp_passkey = info.ssp_request.v12.passkey;

        if (m_devices[btaddr].wants_pair) {
            rc = btdrvRespondToSspRequestNew(btaddr, 0, true, info.ssp_request.v12.passkey);
            TRACE("[?] btdrvRespondToSspRequestNew: %x\n", rc);
        }

        TRACE("[+] Pairing request from %s (%s)\n", m_devices[btaddr].name, BtAddrToString(btaddr));
        break;

    default:
        break;
    }
}

void BtPairingManager::Pair(BtdrvAddress btaddr)
{
    if (!m_devices.contains(btaddr))
        m_devices[btaddr] = BtDeviceInfo{};

    m_devices[btaddr].btaddr = btaddr;
    m_devices[btaddr].wants_pair = true;

    Result rc;
    rc = btdrvCancelBond(btaddr);
    TRACE("btdrvCancelBond: %x\n", rc);

    rc = btdrvRemoveBond(btaddr);
    TRACE("btdrvRemoveBond: %x\n", rc);

    rc = btdrvCreateBond(btaddr, 0);
    TRACE("btdrvCreateBond: %x\n", rc);
}

void BtPairingManager::Unpair(BtdrvAddress btaddr)
{
    m_devices[btaddr] = BtDeviceInfo{};
    m_devices[btaddr].btaddr = btaddr;

    Result rc;

    rc = btdrvCancelBond(btaddr);
    TRACE("btdrvCancelBond: %x\n", rc);

    rc = btdrvRemoveBond(btaddr);
    TRACE("btdrvRemoveBond: %x\n", rc);
}

DeviceInfoMap* BtPairingManager::GetScanResults()
{
    return &m_devices;
}

const char* BtPairingManager::GetState()
{
    switch (m_state)
    {
        case Uninitialized:
            return "Uninitialized";
        case Ready:
            return "\e[0;32mReady\e[0;0m";
        case Scanning:
            return "\e[0;36mScanning\e[0;0m";
    }
    return "Unknown";
}

BtPairingManager::~BtPairingManager()
{
    if (m_is_initialized) {
        eventClose(&m_btevent);
        btdrvMissionControlRedirectCoreEvents(false);
        btdrvExit();
    }
}
