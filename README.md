# RAID 5 Software Implementation – `CRaidVolume`

## Overview

This project implements a software-level RAID 5 controller in C++. The goal is to simulate the behavior of a block device backed by multiple physical drives, providing fault tolerance and striped data layout with rotating parity.

The core logic is encapsulated in the `CRaidVolume` class, which manages reading, writing, device failures, and data recovery. This implementation interacts with simulated drives via provided low-level read/write function pointers.

---

## Key Features

- Supports any number of devices `n`, where `n >= 3`.
- Tolerates failure of **one device** without data loss (`RAID_DEGRADED` mode).
- Uniform distribution of parity (rotating parity across devices).
- Logical capacity is approximately `(n - 1) * device capacity` (plus a small overhead).
- Sector-based block operations: read/write occurs in full sector units.

---

## Implementation Details

- **Sector size**: Defined by `SECTOR_SIZE` (e.g., 512 bytes).
- **Devices**: Managed through the `TBlkDev` structure, which provides:
    - `m_Devices`: Number of physical devices.
    - `m_Sectors`: Number of sectors per device.
    - `m_Read` and `m_Write`: Function pointers to simulate I/O.

---

## RAID States

- `RAID_OK`: All devices operational.
- `RAID_DEGRADED`: One device has failed – reads and writes still succeed, but with recovery logic.
- `RAID_FAILED`: Two or more devices have failed – device unusable.
- `RAID_STOPPED`: RAID is uninitialized or explicitly stopped.

---

## Key Methods in `CRaidVolume`

| Method | Description |
|--------|-------------|
| `create` | Initializes metadata on all devices. Called during initial RAID creation. |
| `start` | Loads metadata and prepares the volume for I/O. Transitions RAID to `OK`, `DEGRADED`, or `FAILED`. |
| `stop` | Flushes data and stores metadata. Transitions RAID to `STOPPED`. |
| `status` | Returns current RAID state. |
| `size` | Returns usable sector count (excludes internal metadata). |
| `read(secNr, data, secCnt)` | Reads one or more sectors. Performs recovery if in `DEGRADED` mode. |
| `write(secNr, data, secCnt)` | Writes one or more sectors. Calculates parity and handles failure scenarios. |
| `resync()` | Rebuilds data on a previously failed disk if a replacement is inserted. Updates RAID state accordingly. |

---

## RAID Logic

- **Data and parity mapping** are handled via:
    - `CalculateSectorLocation(secNr)`: Maps logical sector to physical data sector.
    - `CalculateParityLocation(secNr)`: Determines parity sector location (rotating parity).
- **Parity** is calculated using XOR across all participating devices (excluding the failed or write-target device).
- In degraded mode, reads reconstruct missing data on-the-fly, and writes compute correct parity using available sectors.

---

## Notes

- All read/write operations are **sector-aligned and sector-sized**.

---

## Example Usage

```cpp
TBlkDev dev;
dev.m_Devices = 4;
dev.m_Sectors = 1000;
dev.m_Read = YourReadFunction;
dev.m_Write = YourWriteFunction;

CRaidVolume raid;
raid.start(&dev);

// Writing data
unsigned char buffer[SECTOR_SIZE];
raid.write(0, buffer, 1);

// Reading data
raid.read(0, buffer, 1);

// Stop the RAID volume
raid.stop();
