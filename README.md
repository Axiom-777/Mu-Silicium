# Mu-Silicium BootLog Framework

## Overview

BootLog is an optional logging framework for Mu-Silicium that captures the complete UEFI DEBUG output and stores a persistent boot report for offline analysis.

Instead of relying on on-screen debug messages, BootLog automatically saves the complete firmware log to:

```text
LOGFS:\bootlog.txt
```

The framework is completely optional and is enabled only by the `-l` build option.

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

The `-l` option enables the complete BootLog framework while keeping the firmware as a normal production build.

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

The default build behavior remains unchanged unless `-l` is explicitly specified.

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

If both are available, BootLog operates automatically without additional configuration.

---

## Developers

* mero
* Axiom

