#include <Uefi.h>

#include <Library/BootLogBuffer.h>
#include <Library/MemoryMapHelperLib.h>

EFI_STATUS
EFIAPI
GetBootLogBuffer (
  OUT BOOT_LOG_BUFFER_HEADER **Header,
  OUT UINT8                  **LogData,
  OUT UINTN                   *LogSize
  )
{
  EFI_STATUS                   Status;
  EFI_MEMORY_REGION_DESCRIPTOR Region;

  if ((Header == NULL) || (LogData == NULL) || (LogSize == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  *Header  = NULL;
  *LogData = NULL;
  *LogSize = 0;

  Region.Address = 0;
  Region.Length  = 0;

  Status = LocateMemoryRegionByName (BOOT_LOG_REGION_NAME, &Region);
  if (EFI_ERROR (Status) || (Region.Address == 0) || (Region.Length == 0)) {
    return EFI_NOT_FOUND;
  }

  if (Region.Length <= sizeof (BOOT_LOG_BUFFER_HEADER)) {
    return EFI_BAD_BUFFER_SIZE;
  }

  *Header  = (BOOT_LOG_BUFFER_HEADER *)(UINTN)Region.Address;
  *LogData = (UINT8 *)(UINTN)(Region.Address + sizeof (BOOT_LOG_BUFFER_HEADER));
  *LogSize = (UINTN)(Region.Length - sizeof (BOOT_LOG_BUFFER_HEADER));

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
GetBootLogRegion (
  OUT EFI_MEMORY_REGION_DESCRIPTOR *Region
  )
{
  if (Region == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Region->Address = 0;
  Region->Length  = 0;

  return LocateMemoryRegionByName (
           BOOT_LOG_REGION_NAME,
           Region
           );
}
