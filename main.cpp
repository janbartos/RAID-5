
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <iostream>
#include <cassert>
#include <set>

using namespace std;

const int SECTOR_SIZE                                      =             512;
const int MAX_RAID_DEVICES                                 =              16;
const int MIN_RAID_DEVICES                                 =               3;
const int MAX_DEVICE_SECTORS                               = 1024 * 1024 * 2;
const int MIN_DEVICE_SECTORS                               =    1 * 1024 * 2;

const int RAID_STOPPED                                     = 0;
const int RAID_OK                                          = 1;
const int RAID_DEGRADED                                    = 2;
const int RAID_FAILED                                      = 3;


const unsigned char NO_FAILED_DEVICE                         = 255;

struct TBlkDev
{
    int              m_Devices;
    int              m_Sectors;
    int           (* m_Read )  ( int, int, void *, int );
    int           (* m_Write ) ( int, int, const void *, int );
};


class CRaidVolume
{
public:
    CRaidVolume();
    bool                     Create                        ( const TBlkDev   & dev );
    int                      Start                         ( const TBlkDev   & dev );
    int                      Stop                          ( void );
    int                      Resync                        ( void );
    int                      Status                        ( void ) const;
    int                      Size                          ( void ) const;
    bool                     Read                          ( int               secNr,
                                                             void            * data,
                                                             int               secCnt );
    bool                     Write                         ( int               secNr,
                                                             const void      * data,
                                                             int               secCnt );
private:
    int                     CalculateSectorLocation         ( int               secNr) const;
    int                     CalculateParityLocation         ( int               secNr) const;
    void                    InitIO                          (const TBlkDev    & dev);
    bool                    ReadServiceData                 (const TBlkDev    & dev,
                                                             int                diskIndex,
                                                             unsigned char    * buffer,
                                                             int                (&service)[3]);
    bool                    CheckInitialDisks               (const TBlkDev    & dev,
                                                             int (&serviceData)[16][3],
                                                             int &failed,
                                                             int &goodTimeStamp);
    bool                    CheckAdditionalDisks            (const TBlkDev    & dev,
                                                             int                startIdx,
                                                             int                goodTimeStamp,
                                                             int                (&serviceData)[16][3],
                                                             int              & failed);
    void                    GetDeviceAndOffset              (int                secNr,
                                                             int              & device,
                                                             int              & offset) const;
    void                    GetParityDeviceAndOffset        (int                secNr,
                                                             int              & device,
                                                             int              & offset) const;
    void                    CopyBuffer                      (unsigned char    *& dest,
                                                             const unsigned char* src);
    bool                    ReadSectorNormal                (int                 secNr,
                                                             unsigned char    *& dest);
    bool                    ReadSectorDegraded              (int                 secNr,
                                                             unsigned char    *& dest);
    bool                    RecoverSector                   (int                 secNr,
                                                             unsigned char    *& dest);
    bool                    ComputeParityExcluding          (int                 secNr,
                                                             const set<int>    & excludedDevices,
                                                             unsigned char     * parityOut);
    bool                    WriteToDevice                   (int                 device,
                                                             int                 offset,
                                                             const unsigned char* data);
    bool                    WriteNormal                     (int                 secNr,
                                                             const unsigned char* data);
    bool                    WriteDegraded                   (int                 secNr,
                                                             const unsigned char* data);


    TBlkDev io;
    int currentState;
    int timeStamp;
    int failedDevice = - 1;
};
CRaidVolume::CRaidVolume(){
    currentState = RAID_STOPPED;
    timeStamp = 0;
}

int CRaidVolume::CalculateSectorLocation(int secNr) const{
    int row = secNr / (io.m_Devices - 1);
    int column  = secNr % (io.m_Devices - 1 );

    if ( row % io.m_Devices  <= column ){
        column ++;
    }

    return (row * io.m_Devices) + column;
}

int CRaidVolume::CalculateParityLocation(int secNr) const{
    int row = secNr / (io.m_Devices - 1);
    return (row * io.m_Devices) + (row % io.m_Devices);
}

void CRaidVolume::InitIO(const TBlkDev &dev) {
    io.m_Devices = dev.m_Devices;
    io.m_Sectors = dev.m_Sectors;
    io.m_Read = dev.m_Read;
    io.m_Write = dev.m_Write;
}

bool CRaidVolume::Create ( const TBlkDev & dev ){
    int counter = 0;

    unsigned char buffer[SECTOR_SIZE];

    buffer[0] = 0;
    buffer[1] = NO_FAILED_DEVICE;
    buffer[2] = RAID_STOPPED;


    io.m_Devices = dev.m_Devices;
    io.m_Sectors = dev.m_Sectors;
    io.m_Read = dev.m_Read;
    io.m_Write = dev.m_Write;

    timeStamp = 0;
    currentState = RAID_STOPPED;

    for( int i = 0; i < io.m_Devices; i++){
        if ( io.m_Write(i, io.m_Sectors - 1, buffer, 1) == 0) {
            counter ++;
            if ( counter > 1 ){
                return false;
            }
        }
    }
    return true;
}

bool CRaidVolume::ReadServiceData(const TBlkDev &dev, int diskIndex, unsigned char* buffer, int (&service)[3]) {
    if (dev.m_Read(diskIndex, dev.m_Sectors - 1, buffer, 1) == 0)
        return false;

    service[0] = buffer[0];
    service[1] = buffer[1];
    service[2] = buffer[2];
    return true;
}

bool CRaidVolume::CheckInitialDisks(const TBlkDev &dev, int (&serviceData)[16][3], int &failed, int &goodTimeStamp) {
    unsigned char buffer[SECTOR_SIZE];

    for (int i = 0; i < MIN_RAID_DEVICES; i++) {
        if (!ReadServiceData(dev, i, buffer, serviceData[i])) {
            failed++;
            failedDevice = i;
            if (failed > 1) {
                currentState = RAID_FAILED;
                return false;
            }
            currentState = RAID_DEGRADED;
            continue;
        }

        if (serviceData[i][1] == RAID_FAILED) {
            currentState = RAID_FAILED;
            return false;
        }
    }

    if (failed > 0) {
        int t0 = serviceData[(failedDevice + 1) % MIN_RAID_DEVICES][0];
        int t1 = serviceData[(failedDevice + 2) % MIN_RAID_DEVICES][0];

        if (t0 != t1) {
            currentState = RAID_FAILED;
            return false;
        }

        goodTimeStamp = t0;
    } else {
        int t0 = serviceData[0][0];
        int t1 = serviceData[1][0];
        int t2 = serviceData[2][0];

        if (t0 == t1 && t0 == t2) {
            currentState = RAID_OK;
            goodTimeStamp = t0;
        } else if (t0 == t1) {
            failedDevice = 2;
            goodTimeStamp = t0;
            currentState = RAID_DEGRADED;
        } else if (t0 == t2) {
            failedDevice = 1;
            goodTimeStamp = t0;
            currentState = RAID_DEGRADED;
        } else if (t1 == t2) {
            failedDevice = 0;
            goodTimeStamp = t1;
            currentState = RAID_DEGRADED;
        } else {
            currentState = RAID_FAILED;
            return false;
        }
    }

    return true;
}

bool CRaidVolume::CheckAdditionalDisks(const TBlkDev &dev, int startIdx, int goodTimeStamp, int (&serviceData)[16][3], int &failed) {
    unsigned char buffer[SECTOR_SIZE];

    for (int i = startIdx; i < dev.m_Devices; i++) {
        if (!ReadServiceData(dev, i, buffer, serviceData[i])) {
            failed++;
            failedDevice = i;
            if (failed > 1) {
                currentState = RAID_FAILED;
                return false;
            }
            currentState = RAID_DEGRADED;
            continue;
        }

        if (serviceData[i][0] != goodTimeStamp) {
            failed++;
            failedDevice = i;
            currentState = RAID_DEGRADED;
        }

        if (failed > 1) {
            currentState = RAID_FAILED;
            return false;
        }
    }
    return true;
}

int CRaidVolume::Start(const TBlkDev &dev) {
    InitIO(dev);

    int goodTimeStamp = -1;
    int failed = 0;
    unsigned char buffer[SECTOR_SIZE];
    int serviceData[16][3];

    if (!CheckInitialDisks(dev, serviceData, failed, goodTimeStamp))
        return currentState;

    if (!CheckAdditionalDisks(dev, MIN_RAID_DEVICES, goodTimeStamp, serviceData, failed))
        return currentState;

    timeStamp = goodTimeStamp;
    return currentState;
}

int CRaidVolume::Size() const {
    return (io.m_Devices - 1 ) * (io.m_Sectors - 1);
}

int CRaidVolume::Status() const {
    return currentState;
}

int CRaidVolume::Stop() {

    timeStamp++;
    unsigned char buffer [SECTOR_SIZE];
    buffer[0] = timeStamp;
    buffer[1] = currentState;
    buffer[2] = failedDevice;

    for(int i = 0; i < io.m_Devices; i++){
        if (i != failedDevice ){
            io.m_Write(i, io.m_Sectors - 1 , buffer, 1);
        }
    }
    currentState = RAID_STOPPED;
    return RAID_STOPPED;
}

int CRaidVolume::Resync() {
    unsigned char buffer[SECTOR_SIZE];
    unsigned char bufferToRead[SECTOR_SIZE];
    memset(buffer, 0, SECTOR_SIZE);
    if ( currentState == RAID_OK){
        return 0;
    }
    if( currentState == RAID_FAILED || currentState == RAID_STOPPED){
        return 0;
    }
    if ( currentState == RAID_DEGRADED){
        for(int i = 0; i  < io.m_Sectors; i++){
            memset(buffer, 0, SECTOR_SIZE);
            for( int j = 0; j < io.m_Devices; j++){
                if (failedDevice == j ){
                    continue;

                }
                if ( io.m_Read(j, i, bufferToRead, 1)  != 1 ){
                    return 0;
                }
                for(int k = 0; k < SECTOR_SIZE; k++){
                    buffer[k] = buffer[k] ^ bufferToRead[k];
                }

            }
            if (io.m_Write(failedDevice, i, buffer, 1) != 1){
                return 0;
            }
        }
    }
    failedDevice = -1;
    currentState = RAID_OK;
    return 1;
}

void CRaidVolume::GetDeviceAndOffset(int secNr, int& device, int& offset) const {
    int loc = CalculateSectorLocation(secNr);
    device = loc % io.m_Devices;
    offset = loc / io.m_Devices;
}

void CRaidVolume::GetParityDeviceAndOffset(int secNr, int& device, int& offset) const {
    int loc = CalculateParityLocation(secNr);
    device = loc % io.m_Devices;
    offset = loc / io.m_Devices;
}

void CRaidVolume::CopyBuffer(unsigned char*& dest, const unsigned char* src) {
    memcpy(dest, src, SECTOR_SIZE);
}

bool CRaidVolume::ReadSectorNormal(int secNr, unsigned char*& dest) {
    int device, offset;
    GetDeviceAndOffset(secNr, device, offset);

    unsigned char buffer[SECTOR_SIZE];
    if (io.m_Read(device, offset, buffer, 1) != 1) {
        failedDevice = device;
        return false;
    }

    CopyBuffer(dest, buffer);
    dest += SECTOR_SIZE;
    return true;
}

bool CRaidVolume::ReadSectorDegraded(int secNr, unsigned char*& dest) {
    int device, offset;
    GetDeviceAndOffset(secNr, device, offset);

    if (device != failedDevice) {
        unsigned char buffer[SECTOR_SIZE];
        if (io.m_Read(device, offset, buffer, 1) != 1) {
            return false;
        }
        CopyBuffer(dest, buffer);
        dest += SECTOR_SIZE;
        return true;
    } else {
        return RecoverSector(secNr, dest);
    }
}

bool CRaidVolume::RecoverSector(int secNr, unsigned char*& dest) {
    int _, offset;
    GetDeviceAndOffset(secNr, _, offset);

    unsigned char repairBuffer[SECTOR_SIZE] = {0};
    unsigned char buffer[SECTOR_SIZE];

    for (int dev = 0; dev < io.m_Devices; ++dev) {
        if (dev == failedDevice) continue;

        if (io.m_Read(dev, offset, buffer, 1) != 1) {
            return false;
        }

        for (int i = 0; i < SECTOR_SIZE; ++i) {
            repairBuffer[i] ^= buffer[i];
        }
    }

    CopyBuffer(dest, repairBuffer);
    dest += SECTOR_SIZE;
    return true;
}

bool CRaidVolume::Read(int secNr, void *data, int secCnt) {
    auto * castedData = (unsigned char *) data;

    for (int i = 0; i < secCnt; ++i) {
        bool success = false;

        if (currentState == RAID_OK) {
            success = ReadSectorNormal(secNr, castedData);
            if (!success) {
                currentState = RAID_DEGRADED;
                success = ReadSectorDegraded(secNr, castedData);
            }
        } else if (currentState == RAID_DEGRADED) {
            success = ReadSectorDegraded(secNr, castedData);
        }

        if (!success) {
            currentState = RAID_FAILED;
            return false;
        }

        secNr++;
    }

    return true;
}

bool CRaidVolume::ComputeParityExcluding(int secNr, const set<int>& excludedDevices, unsigned char* parityOut) {
    memset(parityOut, 0, SECTOR_SIZE);
    int offset = CalculateSectorLocation(secNr) / io.m_Devices;
    unsigned char buffer[SECTOR_SIZE];

    for (int dev = 0; dev < io.m_Devices; ++dev) {
        if (excludedDevices.count(dev)) continue;

        if (io.m_Read(dev, offset, buffer, 1) != 1) {
            return false;
        }

        for (int i = 0; i < SECTOR_SIZE; ++i) {
            parityOut[i] ^= buffer[i];
        }
    }

    return true;
}

bool CRaidVolume::WriteToDevice(int device, int offset, const unsigned char* data) {
    return io.m_Write(device, offset, data, 1) == 1;
}

bool CRaidVolume::WriteNormal(int secNr, const unsigned char* data) {
    int dataDev, dataOff;
    int parityDev, parityOff;

    GetDeviceAndOffset(secNr, dataDev, dataOff);
    GetParityDeviceAndOffset(secNr, parityDev, parityOff);


    std::set<int> exclude = {dataDev, parityDev};
    unsigned char parityBuffer[SECTOR_SIZE];
    if (!ComputeParityExcluding(secNr, exclude, parityBuffer)) {
        failedDevice = dataDev;
        currentState = RAID_DEGRADED;
        return false;
    }

    for (int i = 0; i < SECTOR_SIZE; ++i) {
        parityBuffer[i] ^= data[i];
    }

    if (!WriteToDevice(dataDev, dataOff, data)) {
        failedDevice = dataDev;
        currentState = RAID_DEGRADED;
        return false;
    }

    if (!WriteToDevice(parityDev, parityOff, parityBuffer)) {
        failedDevice = parityDev;
        currentState = RAID_DEGRADED;
        return false;
    }

    return true;
}

bool CRaidVolume::WriteDegraded(int secNr, const unsigned char* data) {
    int dataDev, dataOff;
    int parityDev, parityOff;

    GetDeviceAndOffset(secNr, dataDev, dataOff);
    GetParityDeviceAndOffset(secNr, parityDev, parityOff);

    // Parity device failed
    if (failedDevice == parityDev) {
        if (!WriteToDevice(dataDev, dataOff, data)) {
            currentState = RAID_FAILED;
            return false;
        }
        return true;
    }

    // Data device failed
    if (failedDevice == dataDev) {
        std::set<int> exclude = {dataDev, parityDev};
        unsigned char parityBuffer[SECTOR_SIZE];
        if (!ComputeParityExcluding(secNr, exclude, parityBuffer)) {
            currentState = RAID_FAILED;
            return false;
        }

        for (int i = 0; i < SECTOR_SIZE; ++i) {
            parityBuffer[i] ^= data[i];
        }

        if (!WriteToDevice(parityDev, parityOff, parityBuffer)) {
            currentState = RAID_FAILED;
            return false;
        }

        return true;
    }

    // Other device failed, parity calculation needed
    std::set<int> excludeFailed = {failedDevice};
    unsigned char failedBuffer[SECTOR_SIZE];
    if (!ComputeParityExcluding(secNr, excludeFailed, failedBuffer)) {
        currentState = RAID_FAILED;
        return false;
    }

    std::set<int> excludeParity = {failedDevice, dataDev, parityDev};
    unsigned char parityBuffer[SECTOR_SIZE];
    if (!ComputeParityExcluding(secNr, excludeParity, parityBuffer)) {
        currentState = RAID_FAILED;
        return false;
    }

    for (int i = 0; i < SECTOR_SIZE; ++i) {
        parityBuffer[i] ^= data[i];
        parityBuffer[i] ^= failedBuffer[i];
    }

    if (!WriteToDevice(dataDev, dataOff, data)) {
        currentState = RAID_FAILED;
        return false;
    }

    if (!WriteToDevice(parityDev, parityOff, parityBuffer)) {
        currentState = RAID_FAILED;
        return false;
    }

    return true;
}

bool CRaidVolume::Write(int secNr, const void *data, int secCnt) {

    auto * castedData = (unsigned char *) data;

    for (int i = 0; i < secCnt; ++i) {
        bool success = false;

        if (currentState == RAID_OK) {
            success = WriteNormal(secNr, castedData);
        }

        if (!success && currentState == RAID_DEGRADED) {
            success = WriteDegraded(secNr, castedData);
        }

        if (!success) {
            currentState = RAID_FAILED;
            return false;
        }

        secNr++;
        castedData += SECTOR_SIZE;
    }

    return true;
}

