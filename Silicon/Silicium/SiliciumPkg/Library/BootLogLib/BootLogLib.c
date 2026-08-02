#include <Uefi.h>

#include <Guid/FileSystemInfo.h>

#include <Protocol/PartitionInfo.h>
#include <Protocol/SimpleFileSystem.h>
#include <Library/PcdLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BootLogBuffer.h>
#include <Library/BootLogLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>

#define BOOT_LOG_FILE_NAME      L"bootlog.txt"
#define BOOT_LOG_TARGET_NAME    L"LOGFS"
#define BOOT_LOG_OVERFLOW_FLAG  1U

STATIC CONST CHAR8 mOverflowWarning[] =
  "\r\n"
  "=========================================================\r\n"
  "WARNING\r\n"
  "\r\n"
  "BootLog buffer overflowed. The saved log is truncated.\r\n"
  "=========================================================\r\n";

STATIC CONST CHAR8 mSaveSuccessMarker[] =
  "\r\n"
  "BootLog: saved successfully (LOGFS)\r\n";


#define BOOT_ANALYTICS_MAX_FAILED_IMAGES  8U
#define BOOT_ANALYTICS_MAX_WARNINGS       8U
#define BOOT_ANALYTICS_IMAGE_NAME_SIZE   64U
#define BOOT_ANALYTICS_WARNING_SIZE     160U

typedef struct {
  UINT32 DriversLoaded;
  UINT32 ImageStartFailures;
  UINT32 AcpiTablesInstalled;
  UINT32 WarningLines;
  UINT32 FailureLines;
  UINT32 StoredFailedImages;
  UINT32 StoredWarnings;
  UINT32 StoredFailures;
  CHAR8  FailedImages[BOOT_ANALYTICS_MAX_FAILED_IMAGES][BOOT_ANALYTICS_IMAGE_NAME_SIZE];
  CHAR8  Warnings[BOOT_ANALYTICS_MAX_WARNINGS][BOOT_ANALYTICS_WARNING_SIZE];
  CHAR8  Failures[BOOT_ANALYTICS_MAX_WARNINGS][BOOT_ANALYTICS_WARNING_SIZE];
} BOOT_LOG_ANALYTICS;

STATIC
BOOLEAN
AsciiBufferContains (
  IN CONST UINT8 *Buffer,
  IN UINTN        BufferSize,
  IN CONST CHAR8 *Needle
  )
{
  UINTN NeedleSize;
  UINTN Index;

  if ((Buffer == NULL) || (Needle == NULL)) {
    return FALSE;
  }

  NeedleSize = AsciiStrLen (Needle);
  if ((NeedleSize == 0) || (NeedleSize > BufferSize)) {
    return FALSE;
  }

  for (Index = 0; Index <= (BufferSize - NeedleSize); Index++) {
    if (CompareMem (
          Buffer + Index,
          Needle,
          NeedleSize
          ) == 0)
    {
      return TRUE;
    }
  }

  return FALSE;
}

STATIC
VOID
CopyAsciiSlice (
  OUT CHAR8       *Destination,
  IN  UINTN        DestinationSize,
  IN  CONST UINT8 *Source,
  IN  UINTN        SourceSize
  )
{
  UINTN CopySize;

  if ((Destination == NULL) || (DestinationSize == 0)) {
    return;
  }

  Destination[0] = '\0';

  if ((Source == NULL) || (SourceSize == 0)) {
    return;
  }

  CopySize = SourceSize;
  if (CopySize >= DestinationSize) {
    CopySize = DestinationSize - 1;
  }

  CopyMem (
    Destination,
    Source,
    CopySize
    );

  Destination[CopySize] = '\0';
}

STATIC
VOID
ExtractDriverName (
  IN  CONST UINT8 *Line,
  IN  UINTN        LineSize,
  OUT CHAR8       *DriverName,
  IN  UINTN        DriverNameSize
  )
{
  INTN End;
  INTN Start;

  if ((DriverName == NULL) || (DriverNameSize == 0)) {
    return;
  }

  DriverName[0] = '\0';

  if ((Line == NULL) || (LineSize == 0)) {
    return;
  }

  End = (INTN)LineSize - 1;

  while ((End >= 0) &&
         ((Line[End] == ' ') || (Line[End] == '\t')))
  {
    End--;
  }

  if (End < 0) {
    return;
  }

  Start = End;

  while ((Start >= 0) &&
         (Line[Start] != ' ') &&
         (Line[Start] != '\t'))
  {
    Start--;
  }

  Start++;

  if (Start > End) {
    return;
  }

  CopyAsciiSlice (
    DriverName,
    DriverNameSize,
    Line + Start,
    (UINTN)(End - Start + 1)
    );

  if (!AsciiBufferContains (
         (CONST UINT8 *)DriverName,
         AsciiStrLen (DriverName),
         ".efi"
         ))
  {
    DriverName[0] = '\0';
  }
}

STATIC
BOOLEAN
IsWarningLine (
  IN CONST UINT8 *Line,
  IN UINTN        LineSize
  )
{
  if ((Line == NULL) || (LineSize == 0)) {
    return FALSE;
  }

  return (BOOLEAN)(
           AsciiBufferContains (Line, LineSize, "WARNING") ||
           AsciiBufferContains (Line, LineSize, "Warning")
           );
}

STATIC
BOOLEAN
IsFailureLine (
  IN CONST UINT8 *Line,
  IN UINTN        LineSize
  )
{
  if ((Line == NULL) || (LineSize == 0)) {
    return FALSE;
  }

  //
  // Image-start records are handled separately below.
  //
  if (AsciiBufferContains (Line, LineSize, "Error: Image at ")) {
    return FALSE;
  }

  return (BOOLEAN)(
           AsciiBufferContains (Line, LineSize, " failed") ||
           AsciiBufferContains (Line, LineSize, " Failed") ||
           AsciiBufferContains (Line, LineSize, "FAILED") ||
           AsciiBufferContains (Line, LineSize, "Aborted")
           );
}

STATIC
VOID
AnalyzeBootLog (
  IN  CONST UINT8        *LogData,
  IN  UINTN               LogSize,
  OUT BOOT_LOG_ANALYTICS *Analytics
  )
{
  UINTN LineStart;
  UINTN LineEnd;
  UINTN LineSize;
  CHAR8 LastDriver[BOOT_ANALYTICS_IMAGE_NAME_SIZE];

  if (Analytics == NULL) {
    return;
  }

  ZeroMem (
    Analytics,
    sizeof (*Analytics)
    );

  ZeroMem (
    LastDriver,
    sizeof (LastDriver)
    );

  if ((LogData == NULL) || (LogSize == 0)) {
    return;
  }

  LineStart = 0;

  while (LineStart < LogSize) {
    LineEnd = LineStart;

    while ((LineEnd < LogSize) &&
           (LogData[LineEnd] != '\r') &&
           (LogData[LineEnd] != '\n'))
    {
      LineEnd++;
    }

    LineSize = LineEnd - LineStart;

    if (AsciiBufferContains (
          LogData + LineStart,
          LineSize,
          "Loading driver at "
          ))
    {
      Analytics->DriversLoaded++;

      ExtractDriverName (
        LogData + LineStart,
        LineSize,
        LastDriver,
        sizeof (LastDriver)
        );
    }

    if (AsciiBufferContains (
          LogData + LineStart,
          LineSize,
          "Error: Image at "
          ))
    {
      //
      // EFI_REQUEST_UNLOAD_IMAGE is a successful one-shot-driver exit,
      // even though DxeCore formats it as "start failed: 00000001".
      //
      if (!AsciiBufferContains (
             LogData + LineStart,
             LineSize,
             "start failed: 00000001"
             ))
      {
        Analytics->ImageStartFailures++;

        if (Analytics->StoredFailedImages < BOOT_ANALYTICS_MAX_FAILED_IMAGES) {
          if (LastDriver[0] != '\0') {
            AsciiStrCpyS (
              Analytics->FailedImages[Analytics->StoredFailedImages],
              BOOT_ANALYTICS_IMAGE_NAME_SIZE,
              LastDriver
              );
          } else {
            AsciiStrCpyS (
              Analytics->FailedImages[Analytics->StoredFailedImages],
              BOOT_ANALYTICS_IMAGE_NAME_SIZE,
              "<unknown image>"
              );
          }

          Analytics->StoredFailedImages++;
        }
      }
    }

    if (AsciiBufferContains (
          LogData + LineStart,
          LineSize,
          "[ACPI] "
          ) &&
        AsciiBufferContains (
          LogData + LineStart,
          LineSize,
          " Installed "
          ))
    {
      Analytics->AcpiTablesInstalled++;
    }

    if (IsWarningLine (
          LogData + LineStart,
          LineSize
          ))
    {
      Analytics->WarningLines++;

      if (Analytics->StoredWarnings < BOOT_ANALYTICS_MAX_WARNINGS) {
        CopyAsciiSlice (
          Analytics->Warnings[Analytics->StoredWarnings],
          BOOT_ANALYTICS_WARNING_SIZE,
          LogData + LineStart,
          LineSize
          );

        Analytics->StoredWarnings++;
      }
    }

    if (IsFailureLine (
          LogData + LineStart,
          LineSize
          ))
    {
      Analytics->FailureLines++;

      if (Analytics->StoredFailures < BOOT_ANALYTICS_MAX_WARNINGS) {
        CopyAsciiSlice (
          Analytics->Failures[Analytics->StoredFailures],
          BOOT_ANALYTICS_WARNING_SIZE,
          LogData + LineStart,
          LineSize
          );

        Analytics->StoredFailures++;
      }
    }

    while ((LineEnd < LogSize) &&
           ((LogData[LineEnd] == '\r') ||
            (LogData[LineEnd] == '\n')))
    {
      LineEnd++;
    }

    LineStart = LineEnd;
  }
}

STATIC
CHAR16
ToUpperAsciiChar16 (
  IN CHAR16 Character
  )
{
  if ((Character >= L'a') && (Character <= L'z')) {
    return (CHAR16)(Character - (L'a' - L'A'));
  }

  return Character;
}

STATIC
BOOLEAN
UnicodeStringEqualsInsensitive (
  IN CONST CHAR16 *Left,
  IN CONST CHAR16 *Right,
  IN UINTN         MaximumCharacters
  )
{
  UINTN Index;

  if ((Left == NULL) || (Right == NULL)) {
    return FALSE;
  }

  for (Index = 0; Index < MaximumCharacters; Index++) {
    if (ToUpperAsciiChar16 (Left[Index]) !=
        ToUpperAsciiChar16 (Right[Index]))
    {
      return FALSE;
    }

    if (Left[Index] == L'\0') {
      return TRUE;
    }
  }

  return FALSE;
}

STATIC
EFI_STATUS
ValidateBootLog (
  OUT BOOT_LOG_BUFFER_HEADER **Header,
  OUT UINT8                  **LogData,
  OUT UINTN                   *LogSize
  )
{
  EFI_STATUS              Status;
  BOOT_LOG_BUFFER_HEADER *LocalHeader;
  UINT8                  *LocalLogData;
  UINTN                   LocalLogCapacity;

  if ((Header == NULL) ||
      (LogData == NULL) ||
      (LogSize == NULL))
  {
    return EFI_INVALID_PARAMETER;
  }

  *Header  = NULL;
  *LogData = NULL;
  *LogSize = 0;

  LocalHeader      = NULL;
  LocalLogData     = NULL;
  LocalLogCapacity = 0;

  Status = GetBootLogBuffer (
             &LocalHeader,
             &LocalLogData,
             &LocalLogCapacity
             );

  if (EFI_ERROR (Status)) {
    return Status;
  }

  if ((LocalHeader == NULL) ||
      (LocalLogData == NULL) ||
      (LocalLogCapacity == 0) ||
      (LocalLogCapacity > MAX_UINT32))
  {
    return EFI_COMPROMISED_DATA;
  }

  if ((LocalHeader->Signature  != BOOT_LOG_BUFFER_SIGNATURE) ||
      (LocalHeader->Version    != BOOT_LOG_BUFFER_VERSION) ||
      (LocalHeader->HeaderSize != sizeof (BOOT_LOG_BUFFER_HEADER)) ||
      (LocalHeader->BufferSize != (UINT32)LocalLogCapacity))
  {
    return EFI_COMPROMISED_DATA;
  }

  if (LocalHeader->WriteOffset > LocalHeader->BufferSize) {
    return EFI_COMPROMISED_DATA;
  }

  if (LocalHeader->WriteOffset == 0) {
    return EFI_NOT_FOUND;
  }

  *Header  = LocalHeader;
  *LogData = LocalLogData;
  *LogSize = (UINTN)LocalHeader->WriteOffset;

  return EFI_SUCCESS;
}
STATIC
EFI_STATUS
GetLogFsVolumeMatch (
  IN  EFI_HANDLE         Handle,
  IN  EFI_FILE_PROTOCOL *Root,
  OUT BOOLEAN           *IsLogFs
  )
{
  EFI_STATUS                   Status;
  EFI_STATUS                   FileSystemInfoStatus;
  EFI_FILE_SYSTEM_INFO        *FileSystemInfo;
  EFI_PARTITION_INFO_PROTOCOL *PartitionInfo;
  UINTN                        InformationSize;
  BOOLEAN                      LabelMatches;
  BOOLEAN                      PartitionNameMatches;
  BOOLEAN                      IsReadOnly;

  if ((Handle == NULL) || (Root == NULL) || (IsLogFs == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  *IsLogFs            = FALSE;
  FileSystemInfo      = NULL;
  PartitionInfo       = NULL;
  InformationSize     = 0;
  LabelMatches        = FALSE;
  PartitionNameMatches = FALSE;
  IsReadOnly          = FALSE;

  FileSystemInfoStatus = Root->GetInfo (
                                  Root,
                                  &gEfiFileSystemInfoGuid,
                                  &InformationSize,
                                  NULL
                                  );

  if (FileSystemInfoStatus == EFI_BUFFER_TOO_SMALL) {
    FileSystemInfo = AllocateZeroPool (InformationSize);
    if (FileSystemInfo == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }

    FileSystemInfoStatus = Root->GetInfo (
                                    Root,
                                    &gEfiFileSystemInfoGuid,
                                    &InformationSize,
                                    FileSystemInfo
                                    );

    if (!EFI_ERROR (FileSystemInfoStatus)) {
      IsReadOnly = FileSystemInfo->ReadOnly;
      LabelMatches = UnicodeStringEqualsInsensitive (
                       FileSystemInfo->VolumeLabel,
                       BOOT_LOG_TARGET_NAME,
                       6
                       );
    }

    FreePool (FileSystemInfo);
    FileSystemInfo = NULL;
  }

  Status = gBS->HandleProtocol (
                  Handle,
                  &gEfiPartitionInfoProtocolGuid,
                  (VOID **)&PartitionInfo
                  );

  if (!EFI_ERROR (Status) &&
      (PartitionInfo != NULL) &&
      (PartitionInfo->Type == PARTITION_TYPE_GPT))
  {
    PartitionNameMatches = UnicodeStringEqualsInsensitive (
                             PartitionInfo->Info.Gpt.PartitionName,
                             BOOT_LOG_TARGET_NAME,
                             ARRAY_SIZE (PartitionInfo->Info.Gpt.PartitionName)
                             );
  }

  if (IsReadOnly) {
    return EFI_WRITE_PROTECTED;
  }

  if (LabelMatches || PartitionNameMatches) {
    *IsLogFs = TRUE;
    return EFI_SUCCESS;
  }

  if (EFI_ERROR (FileSystemInfoStatus) && EFI_ERROR (Status)) {
    return FileSystemInfoStatus;
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
RecreateLogFile (
  IN  EFI_FILE_PROTOCOL  *Root,
  OUT EFI_FILE_PROTOCOL **File
  )
{
  EFI_STATUS         Status;
  EFI_FILE_PROTOCOL *ExistingFile;

  if ((Root == NULL) || (File == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  *File        = NULL;
  ExistingFile = NULL;

  Status = Root->Open (
                   Root,
                   &ExistingFile,
                   BOOT_LOG_FILE_NAME,
                   EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE,
                   0
                   );

  if (!EFI_ERROR (Status)) {
    Status = ExistingFile->Delete (ExistingFile);
    ExistingFile = NULL;

    if (EFI_ERROR (Status)) {
      return Status;
    }
  } else if (Status != EFI_NOT_FOUND) {
    return Status;
  }

  return Root->Open (
                 Root,
                 File,
                 BOOT_LOG_FILE_NAME,
                 EFI_FILE_MODE_READ |
                 EFI_FILE_MODE_WRITE |
                 EFI_FILE_MODE_CREATE,
                 0
                 );
}

STATIC
EFI_STATUS
WriteBuffer (
  IN EFI_FILE_PROTOCOL *File,
  IN CONST VOID        *Buffer,
  IN UINTN              BufferSize
  )
{
  EFI_STATUS Status;
  UINTN      BytesToWrite;

  if ((File == NULL) ||
      ((Buffer == NULL) && (BufferSize != 0)))
  {
    return EFI_INVALID_PARAMETER;
  }

  BytesToWrite = BufferSize;

  Status = File->Write (
                   File,
                   &BytesToWrite,
                   (VOID *)Buffer
                   );

  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (BytesToWrite != BufferSize) {
    return EFI_DEVICE_ERROR;
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
WriteBootLogFile (
  IN EFI_FILE_PROTOCOL      *Root,
  IN BOOT_LOG_BUFFER_HEADER *Header,
  IN CONST UINT8            *LogData,
  IN UINTN                   LogSize
  )
{
  EFI_STATUS                    Status;
  EFI_FILE_PROTOCOL            *File;
  EFI_MEMORY_REGION_DESCRIPTOR  Region;
  CHAR8                         FileHeader[2048];
  CHAR8                         AnalyticsText[2048];
  UINTN                         FileHeaderSize;
  UINTN                         AnalyticsTextSize;
  UINTN                         AnalyticsOffset;
  UINTN                         AnalyticsIndex;
  UINTN                         FreeBytes;
  UINT64                        UsageX100;
  CONST CHAR8                  *Manufacturer;
  CONST CHAR8                  *SystemModel;
  CONST CHAR8                  *Codename;
  CONST CHAR8                  *ProcessorModel;
  CONST CHAR8                  *ProcessorPartNumber;
  BOOT_LOG_ANALYTICS            Analytics;

  if ((Root == NULL) ||
      (Header == NULL) ||
      ((LogData == NULL) && (LogSize != 0)))
  {
    return EFI_INVALID_PARAMETER;
  }

  File = NULL;

  Region.Address = 0;
  Region.Length  = 0;

  //
  // Retrieve full information about the platform memory region.
  // ValidateBootLog() has already confirmed that the buffer is usable.
  //
  Status = GetBootLogRegion (&Region);

  if (EFI_ERROR (Status) ||
      (Region.Address == 0) ||
      (Region.Length <= sizeof (BOOT_LOG_BUFFER_HEADER)))
  {
    return EFI_NOT_FOUND;
  }

  Manufacturer = (CONST CHAR8 *)FixedPcdGetPtr (
                                      PcdSmbiosSystemManufacturer
                                      );
  SystemModel = (CONST CHAR8 *)FixedPcdGetPtr (
                                   PcdSmbiosSystemModel
                                   );
  Codename = (CONST CHAR8 *)FixedPcdGetPtr (
                                PcdSmbiosSystemRetailModel
                                );
  ProcessorModel = (CONST CHAR8 *)FixedPcdGetPtr (
                                      PcdSmbiosProcessorModel
                                      );
  ProcessorPartNumber = (CONST CHAR8 *)FixedPcdGetPtr (
                                           PcdSmbiosProcessorPartNumber
                                           );

  if (LogSize < Header->BufferSize) {
    FreeBytes = Header->BufferSize - LogSize;
  } else {
    FreeBytes = 0;
  }

  //
  // Percentage multiplied by 100:
  // 4692 means 46.92%.
  // UINT64 arithmetic avoids overflow and does not require floating point.
  //
  if (Header->BufferSize != 0) {
    UsageX100 = DivU64x32 (
                  MultU64x32 (
                    (UINT64)LogSize,
                    10000U
                    ),
                  Header->BufferSize
                  );
  } else {
    UsageX100 = 0;
  }

  AnalyzeBootLog (
    LogData,
    LogSize,
    &Analytics
    );

  Status = RecreateLogFile (
             Root,
             &File
             );

  if (EFI_ERROR (Status)) {
    return Status;
  }

  FileHeaderSize = AsciiSPrint (
                     FileHeader,
                     sizeof (FileHeader),
                     "=========================================================\r\n"
                     "Mu-Silicium BootLog\r\n"
                     "=========================================================\r\n"
                     "\r\n"
                     "Signature      : BLOG\r\n"
                     "Version        : %u\r\n"
                     "\r\n"
                     "=========================================================\r\n"
                     "PLATFORM INFORMATION\r\n"
                     "=========================================================\r\n"
                     "\r\n"
                     "Device         : %a %a\r\n"
                     "Codename       : %a\r\n"
                     "\r\n"
                     "Processor      : %a\r\n"
                     "Platform       : %a\r\n"
                     "=========================================================\r\n"
                     "BOOT LOG INFO\r\n"
                     "=========================================================\r\n"
                     "\r\n"
                     "Memory Region  : %a\r\n"
                     "Address        : 0x%016Lx\r\n"
                     "Length         : 0x%016Lx\r\n"
                     "\r\n"
                     "Capacity       : %u bytes\r\n"
                     "Used           : %u bytes\r\n"
                     "Free           : %u bytes\r\n"
                     "Usage          : %Lu.%02Lu %%\r\n"
                     "Total Written  : %u bytes\r\n"
                     "Overflow       : %a\r\n"
                     "\r\n"
                     "Target         : LOGFS\r\n"
                     "File           : \\bootlog.txt\r\n"
                     "\r\n"
                     "=========================================================\r\n"
                     "BOOT LOG\r\n"
                     "=========================================================\r\n"
                     "\r\n",
                     Header->Version,
                     Manufacturer,
                     SystemModel,
                     Codename,
                     ProcessorModel,
                     ProcessorPartNumber,
                     BOOT_LOG_REGION_NAME,
                     (UINT64)Region.Address,
                     (UINT64)Region.Length,
                     Header->BufferSize,
                     (UINT32)LogSize,
                     (UINT32)FreeBytes,
                     UsageX100 / 100U,
                     UsageX100 % 100U,
                     Header->TotalWritten,
                     ((Header->Flags & BOOT_LOG_OVERFLOW_FLAG) != 0) ?
                       "YES" :
                       "NO"
                     );

  Status = WriteBuffer (
             File,
             FileHeader,
             FileHeaderSize
             );

  if (!EFI_ERROR (Status)) {
    Status = WriteBuffer (
               File,
               LogData,
               LogSize
               );
  }

  if (!EFI_ERROR (Status) &&
      ((Header->Flags & BOOT_LOG_OVERFLOW_FLAG) != 0))
  {
    Status = WriteBuffer (
               File,
               mOverflowWarning,
               sizeof (mOverflowWarning) - 1
               );
  }

  AnalyticsOffset = AsciiSPrint (
                      AnalyticsText,
                      sizeof (AnalyticsText),
                      "\r\n"
                      "=========================================================\r\n"
                      "BOOT ANALYTICS\r\n"
                      "=========================================================\r\n"
                      "\r\n"
                      "Drivers loaded       : %u\r\n"
                      "Image start failures : %u\r\n"
                      "ACPI tables installed: %u\r\n"
                      "Warning lines        : %u\r\n"
                      "Failure lines        : %u\r\n"
                      "Log usage            : %u / %u bytes\r\n"
                      "Overflow             : %a\r\n"
                      "\r\n"
                      "Failed images:\r\n",
                      Analytics.DriversLoaded,
                      Analytics.ImageStartFailures,
                      Analytics.AcpiTablesInstalled,
                      Analytics.WarningLines,
                      Analytics.FailureLines,
                      (UINT32)LogSize,
                      Header->BufferSize,
                      ((Header->Flags & BOOT_LOG_OVERFLOW_FLAG) != 0) ?
                        "YES" :
                        "NO"
                      );

  if (Analytics.StoredFailedImages == 0) {
    AnalyticsOffset += AsciiSPrint (
                         AnalyticsText + AnalyticsOffset,
                         sizeof (AnalyticsText) - AnalyticsOffset,
                         "  (none)\r\n"
                         );
  } else {
    for (AnalyticsIndex = 0;
         (AnalyticsIndex < Analytics.StoredFailedImages) &&
         (AnalyticsOffset < sizeof (AnalyticsText));
         AnalyticsIndex++)
    {
      AnalyticsOffset += AsciiSPrint (
                           AnalyticsText + AnalyticsOffset,
                           sizeof (AnalyticsText) - AnalyticsOffset,
                           "  - %a\r\n",
                           Analytics.FailedImages[AnalyticsIndex]
                           );
    }
  }

  AnalyticsOffset += AsciiSPrint (
                       AnalyticsText + AnalyticsOffset,
                       sizeof (AnalyticsText) - AnalyticsOffset,
                       "\r\n"
                       "Warnings:\r\n"
                       );

  if (Analytics.StoredWarnings == 0) {
    AnalyticsOffset += AsciiSPrint (
                         AnalyticsText + AnalyticsOffset,
                         sizeof (AnalyticsText) - AnalyticsOffset,
                         "  (none)\r\n"
                         );
  } else {
    for (AnalyticsIndex = 0;
         (AnalyticsIndex < Analytics.StoredWarnings) &&
         (AnalyticsOffset < sizeof (AnalyticsText));
         AnalyticsIndex++)
    {
      AnalyticsOffset += AsciiSPrint (
                           AnalyticsText + AnalyticsOffset,
                           sizeof (AnalyticsText) - AnalyticsOffset,
                           "  - %a\r\n",
                           Analytics.Warnings[AnalyticsIndex]
                           );
    }
  }

  AnalyticsOffset += AsciiSPrint (
                       AnalyticsText + AnalyticsOffset,
                       sizeof (AnalyticsText) - AnalyticsOffset,
                       "\r\n"
                       "Failures:\r\n"
                       );

  if (Analytics.StoredFailures == 0) {
    AnalyticsOffset += AsciiSPrint (
                         AnalyticsText + AnalyticsOffset,
                         sizeof (AnalyticsText) - AnalyticsOffset,
                         "  (none)\r\n"
                         );
  } else {
    for (AnalyticsIndex = 0;
         (AnalyticsIndex < Analytics.StoredFailures) &&
         (AnalyticsOffset < sizeof (AnalyticsText));
         AnalyticsIndex++)
    {
      AnalyticsOffset += AsciiSPrint (
                           AnalyticsText + AnalyticsOffset,
                           sizeof (AnalyticsText) - AnalyticsOffset,
                           "  - %a\r\n",
                           Analytics.Failures[AnalyticsIndex]
                           );
    }
  }

  AnalyticsOffset += AsciiSPrint (
                       AnalyticsText + AnalyticsOffset,
                       sizeof (AnalyticsText) - AnalyticsOffset,
                       "\r\n"
                       "=========================================================\r\n"
                       );

  AnalyticsTextSize = AnalyticsOffset;

  if (!EFI_ERROR (Status)) {
    Status = WriteBuffer (
               File,
               AnalyticsText,
               AnalyticsTextSize
               );
  }

  if (!EFI_ERROR (Status)) {
    Status = WriteBuffer (
               File,
               mSaveSuccessMarker,
               sizeof (mSaveSuccessMarker) - 1
               );
  }

  if (!EFI_ERROR (Status)) {
    Status = File->Flush (File);
  }

  File->Close (File);

  return Status;
}

EFI_STATUS
EFIAPI
SaveBootLogToFile (
  VOID
  )
{
  EFI_STATUS                       Status;
  EFI_STATUS                       LastStatus;
  BOOT_LOG_BUFFER_HEADER          *Header;
  UINT8                           *LogData;
  UINTN                            LogSize;
  EFI_HANDLE                      *Handles;
  UINTN                            HandleCount;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem;
  EFI_FILE_PROTOCOL               *Root;
  BOOLEAN                          IsLogFs;
  UINTN                            Index;

  Header      = NULL;
  LogData     = NULL;
  LogSize     = 0;
  Handles     = NULL;
  HandleCount = 0;
  LastStatus  = EFI_NOT_FOUND;

  Status = ValidateBootLog (
             &Header,
             &LogData,
             &LogSize
             );

  if (EFI_ERROR (Status)) {
    if (Status != EFI_NOT_FOUND) {
      DEBUG ((
        DEBUG_ERROR,
        "BootLogLib: invalid RAM log: %r\n",
        Status
        ));
    }

    return Status;
  }

  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiSimpleFileSystemProtocolGuid,
                  NULL,
                  &HandleCount,
                  &Handles
                  );

  if (EFI_ERROR (Status)) {
    if (Status != EFI_NOT_FOUND) {
      DEBUG ((
        DEBUG_ERROR,
        "BootLogLib: filesystem discovery failed: %r\n",
        Status
        ));
    }

    return Status;
  }

  for (Index = 0; Index < HandleCount; Index++) {
    FileSystem = NULL;
    Root       = NULL;
    IsLogFs    = FALSE;

    Status = gBS->HandleProtocol (
                    Handles[Index],
                    &gEfiSimpleFileSystemProtocolGuid,
                    (VOID **)&FileSystem
                    );

    if (EFI_ERROR (Status) || (FileSystem == NULL)) {
      LastStatus = EFI_ERROR (Status) ? Status : EFI_DEVICE_ERROR;
      continue;
    }

    Status = FileSystem->OpenVolume (
                           FileSystem,
                           &Root
                           );

    if (EFI_ERROR (Status) || (Root == NULL)) {
      LastStatus = EFI_ERROR (Status) ? Status : EFI_DEVICE_ERROR;
      continue;
    }

    Status = GetLogFsVolumeMatch (
               Handles[Index],
               Root,
               &IsLogFs
               );

    if (EFI_ERROR (Status)) {
      LastStatus = Status;
      Root->Close (Root);
      continue;
    }

    if (!IsLogFs) {
      Root->Close (Root);
      continue;
    }

    Status = WriteBootLogFile (
               Root,
               Header,
               LogData,
               LogSize
               );

    Root->Close (Root);
    gBS->FreePool (Handles);

    if (EFI_ERROR (Status)) {
      DEBUG ((
        DEBUG_ERROR,
        "BootLogLib: LOGFS rejected the log: %r\n",
        Status
        ));

      return Status;
    }

    return EFI_SUCCESS;
  }

  gBS->FreePool (Handles);

  if ((LastStatus != EFI_NOT_FOUND) && EFI_ERROR (LastStatus)) {
    DEBUG ((
      DEBUG_ERROR,
      "BootLogLib: LOGFS scan failed: %r\n",
      LastStatus
      ));
  }

  return EFI_NOT_FOUND;
}
