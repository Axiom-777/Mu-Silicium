# Mu-Silicium BootLog Framework

## Overview

BootLog is an optional firmware logging system for UEFI-based projects.

It captures the complete UEFI DEBUG output during boot and automatically generates a persistent boot report for offline analysis.

Instead of relying on on-screen debug messages, BootLog automatically saves the complete firmware log to:

```text
LOGFS:\bootlog.txt
```

---

## Architecture

```text
                  DEBUG()

                     │

                     ▼

        FrameBufferSerialPortLib

                     │

                     ▼

          BootLog Buffer (RAM)

                     │

                     ▼

              BootLog Library

          ┌─────────┼─────────┐
          │         │         │

          ▼         ▼         ▼

   Boot Analytics  ACPI   Report Builder

                     │

                     ▼

               BootLogDxe

                     │

                     ▼

            LOGFS:\bootlog.txt
```

---

## Advantages

* Complete UEFI DEBUG log capture
* Persistent boot report stored on LOGFS
* Dynamic Log Buffer discovery (no fixed RAM addresses)
* Automatic LOGFS detection
* Platform-independent implementation
* Boot Analytics
* ACPI Table Tracker
* Failed image reporting
* Warning and failure summaries
* Optional build-time feature
* No additional tools required for log retrieval

---

## Current Limitation

BootLog stores the report after a writable filesystem becomes available.

If the boot process does not reach this stage, a persistent boot log cannot be generated.

---

## Device Integration

Before using the `-l` build option, BootLog must be added to the target device configuration.

### 1. Update `DeviceBuild.py`

Find:

```python
self.env.SetValue ("BLD_*_ENABLE_SECUREBOOT", self.env.GetValue("ENABLE_SECUREBOOT"), "Default")
```

Add the following line immediately after it:

```python
self.env.SetValue ("BLD_*_ENABLE_BOOTLOG", self.env.GetValue("ENABLE_BOOTLOG", "0"), "BootLog")
```

After the change:

```python
self.env.SetValue ("BLD_*_ENABLE_SECUREBOOT", self.env.GetValue("ENABLE_SECUREBOOT"), "Default")
self.env.SetValue ("BLD_*_ENABLE_BOOTLOG", self.env.GetValue("ENABLE_BOOTLOG", "0"), "BootLog")
self.env.SetValue ("BLD_*_FD_BASE", self.env.GetValue("FD_BASE"), "Default")
```

### 2. Update `APRIORI.inc` and `DXE.inc`

Find:

```ini
INF FatPkg/EnhancedFatDxe/Fat.inf
```

Insert the following lines immediately after it:

```ini
!if $(ENABLE_BOOTLOG) == 1
  INF SiliciumPkg/Drivers/BootLogDxe/BootLogDxe.inf
!endif
```

After the change:

```ini
INF FatPkg/EnhancedFatDxe/Fat.inf

!if $(ENABLE_BOOTLOG) == 1
  INF SiliciumPkg/Drivers/BootLogDxe/BootLogDxe.inf
!endif
```

---

## Build

Default build

```bash
python3 build_uefi.py -d <device>
```

Build with BootLog

```bash
python3 build_uefi.py -d <device> -l
```

Debug build

```bash
python3 build_uefi.py -d <device> -r DEBUG
```

---

## What does `-l` enable?

The `-l` option enables the complete BootLog implementation.

It enables:

* BootLogDxe
* BootLogLib
* BootLogBufferLib
* FrameBufferSerialPortLib transport
* DEBUG_PRINT
* DEBUG_CODE
* `/DEBUG:GHASH`
* ACPI Table Tracker
* Boot Analytics

The `-l` option enables BootLog only for devices where the **Device Integration** steps described above have already been completed.

---

## Retrieving the Log

After the firmware has finished booting:

1. Boot Android or TWRP.
2. Mount the LOGFS partition.
3. Copy:

```text
LOGFS:\bootlog.txt
```

No additional tools are required.

---

## Report Contents

Each generated report includes:

* Platform Information
* Boot Log Information
* Complete firmware debug log
* Installed ACPI tables
* Boot Analytics
* Failed Images
* Warning Summary
* Failure Summary

---

## Runtime Requirements

* Log Buffer memory region
* Writable LOGFS partition

---


