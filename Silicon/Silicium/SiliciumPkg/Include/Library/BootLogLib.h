#ifndef BOOT_LOG_LIB_H_
#define BOOT_LOG_LIB_H_

#include <Uefi.h>

/**
  Save the current RAM boot log to the first writable EFI filesystem.

  The file is recreated on each boot.

  @retval EFI_SUCCESS           The log was saved successfully.
  @retval EFI_NOT_FOUND         No valid log or writable filesystem was found.
  @retval EFI_COMPROMISED_DATA  The RAM log header is invalid.
  @retval Others                A filesystem or file operation failed.
**/
EFI_STATUS
EFIAPI
SaveBootLogToFile (
  VOID
  );

#endif
