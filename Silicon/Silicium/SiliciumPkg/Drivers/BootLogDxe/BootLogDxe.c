#include <Uefi.h>

#include <Guid/EventGroup.h>

#include <Protocol/SimpleFileSystem.h>

#include <Library/BootLogLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>

STATIC EFI_EVENT mReadyToBootEvent       = NULL;
STATIC EFI_EVENT mFileSystemEvent        = NULL;
STATIC VOID     *mFileSystemRegistration = NULL;
STATIC BOOLEAN   mSaveCompleted          = FALSE;

STATIC
VOID
CloseBootLogEvents (
  VOID
  )
{
  if (mReadyToBootEvent != NULL) {
    gBS->CloseEvent (mReadyToBootEvent);
    mReadyToBootEvent = NULL;
  }

  if (mFileSystemEvent != NULL) {
    gBS->CloseEvent (mFileSystemEvent);
    mFileSystemEvent = NULL;
  }

  mFileSystemRegistration = NULL;
}

STATIC
EFI_STATUS
TrySaveBootLog (
  IN CONST CHAR8 *Reason
  )
{
  EFI_STATUS Status;

  if (mSaveCompleted) {
    return EFI_ALREADY_STARTED;
  }

  Status = SaveBootLogToFile ();

  if (EFI_ERROR (Status)) {
    //
    // EFI_NOT_FOUND is expected until LOGFS becomes available.
    //
    if (Status != EFI_NOT_FOUND) {
      DEBUG ((
        DEBUG_ERROR,
        "BootLogDxe: save failed, reason=%a status=%r\n",
        Reason,
        Status
        ));
    }

    return Status;
  }

  mSaveCompleted = TRUE;

  //
  // The success marker is appended directly to bootlog.txt
  // by BootLogLib before the file is flushed.
  //
  CloseBootLogEvents ();

  return EFI_SUCCESS;
}

STATIC
VOID
EFIAPI
FileSystemNotifyHandler (
  IN EFI_EVENT Event,
  IN VOID      *Context
  )
{
  //
  // SaveBootLogToFile() scans all currently installed filesystem handles.
  // EFI_NOT_FOUND is expected until LOGFS becomes available.
  //
  TrySaveBootLog ("SimpleFileSystem installed");
}

STATIC
VOID
EFIAPI
ReadyToBootHandler (
  IN EFI_EVENT Event,
  IN VOID      *Context
  )
{
  EFI_STATUS Status;

  Status = TrySaveBootLog ("ReadyToBoot");

  if (Status == EFI_NOT_FOUND) {
    DEBUG ((
      DEBUG_INFO,
      "BootLog: LOGFS partition not found\n"
      ));

    return;
  }

  if (EFI_ERROR (Status) &&
      (Status != EFI_ALREADY_STARTED))
  {
    DEBUG ((
      DEBUG_ERROR,
      "BootLogDxe: final save failed: %r\n",
      Status
      ));
  }
}

EFI_STATUS
EFIAPI
BootLogDxeEntryPoint (
  IN EFI_HANDLE       ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
  )
{
  EFI_STATUS Status;
  EFI_STATUS SaveStatus;

  //
  // First attempt: LOGFS may already be available.
  //
  SaveStatus = TrySaveBootLog ("driver entry");

  if (!EFI_ERROR (SaveStatus) ||
      (SaveStatus == EFI_ALREADY_STARTED))
  {
    return EFI_SUCCESS;
  }

  //
  // Retry whenever a Simple File System protocol is installed.
  //
  Status = gBS->CreateEvent (
                  EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  FileSystemNotifyHandler,
                  NULL,
                  &mFileSystemEvent
                  );

  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_ERROR,
      "BootLogDxe: filesystem event creation failed: %r\n",
      Status
      ));

    mFileSystemEvent = NULL;
  } else {
    Status = gBS->RegisterProtocolNotify (
                    &gEfiSimpleFileSystemProtocolGuid,
                    mFileSystemEvent,
                    &mFileSystemRegistration
                    );

    if (EFI_ERROR (Status)) {
      DEBUG ((
        DEBUG_ERROR,
        "BootLogDxe: RegisterProtocolNotify failed: %r\n",
        Status
        ));

      gBS->CloseEvent (mFileSystemEvent);
      mFileSystemEvent        = NULL;
      mFileSystemRegistration = NULL;
    }
  }

  //
  // Final fallback before control reaches the operating-system loader.
  //
  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  ReadyToBootHandler,
                  NULL,
                  &gEfiEventReadyToBootGuid,
                  &mReadyToBootEvent
                  );

  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_ERROR,
      "BootLogDxe: ReadyToBoot event creation failed: %r\n",
      Status
      ));

    mReadyToBootEvent = NULL;
  }

  //
  // The driver remains useful when at least one retry mechanism exists.
  //
  if ((mFileSystemEvent == NULL) &&
      (mReadyToBootEvent == NULL))
  {
    return EFI_DEVICE_ERROR;
  }

  return EFI_SUCCESS;
}
