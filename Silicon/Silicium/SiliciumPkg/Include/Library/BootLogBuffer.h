#ifndef BOOT_LOG_BUFFER_H_
#define BOOT_LOG_BUFFER_H_

#include <Uefi.h>

#include <Library/MemoryMapHelperLib.h>

#define BOOT_LOG_REGION_NAME       "Log Buffer"
#define BOOT_LOG_BUFFER_SIGNATURE  SIGNATURE_32 ('B', 'L', 'O', 'G')
#define BOOT_LOG_BUFFER_VERSION    1U

typedef struct {
  UINT32 Signature;
  UINT16 Version;
  UINT16 HeaderSize;
  UINT32 BufferSize;
  UINT32 WriteOffset;
  UINT32 TotalWritten;
  UINT32 Flags;
  UINT32 Reserved;
} BOOT_LOG_BUFFER_HEADER;

EFI_STATUS
EFIAPI
GetBootLogBuffer (
  OUT BOOT_LOG_BUFFER_HEADER **Header,
  OUT UINT8                  **LogData,
  OUT UINTN                   *LogSize
  );

EFI_STATUS
EFIAPI
GetBootLogRegion (
  OUT EFI_MEMORY_REGION_DESCRIPTOR *Region
  );

#endif
