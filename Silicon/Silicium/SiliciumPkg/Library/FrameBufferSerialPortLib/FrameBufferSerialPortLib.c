#include <Library/BaseMemoryLib.h>
#include <Library/BootLogBuffer.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/MemoryMapHelperLib.h>
#include <Library/SerialPortLib.h>
#include <Library/TimerLib.h>

#include "FrameBuffer.h"
#include "Font.h"

inline
STATIC
UINT8*
DrawPixel (
  IN UINT8  *Pixels,
  IN UINT32  PixelColor,
  IN UINT32  FontScale)
{
  // Repeat Action
  for (UINT8 i = 0; i < FontScale; i++) {
    // Save Pixel Color
    UINT32 Color = PixelColor;

    // Go theu each Byte
    for (UINT8 j = 0; j < FB_BPP; j++) {
      // Update Pixel Color
      *Pixels++ = (UINT8)Color;

      // Shift Pixel Color
      Color >>= 8;
    }
  }

  return Pixels;
}

inline
STATIC
UINT8*
DrawRow (
  IN UINT8  *Pixels,
  IN UINT32  RowData,
  IN UINT32  FontScale,
  IN UINTN   Stride)
{
  // Repeat Action
  for (UINT8 i = 0; i < FontScale; i++) {
    // Save Row Data
    UINT32 Data = RowData;

    // Go thru each Pixel
    for (UINT8 j = 0; j < FONT_WIDTH; j++) {
      // Set Pixel Color
      UINT32 PixelColor = (Data & 1) ? 0xFFFFFFFF : 0x00000000;
      
      // Draw Pixel Color
      Pixels = DrawPixel (Pixels, PixelColor, FontScale);

      // Shift Row
      Data >>= 1;
    }

    // Append Stride
    Pixels += Stride;
  }
  
  return Pixels;
}

VOID
DrawGlyph (
  IN UINT8  *Pixels,
  IN UINT64  Glyph,
  IN UINT32  FontScale)
{
  // Calculate Stride
  UINTN Stride = (FB_WIDTH - (FONT_WIDTH * FontScale)) * FB_BPP;

  // Split Glyph
  UINT32 TopGlyph    = (UINT32)(Glyph >> 32);
  UINT32 BottomGlyph = (UINT32)Glyph;

  // Set Inital Row Data
  UINT32 RowData = TopGlyph;

  // Go thru each Row
  for (UINT8 Row = 0; Row < 12; Row++) {
    // Switch Row Data
    if (Row == 6) {
      RowData = BottomGlyph;
    }

    // Draw Row
    Pixels = DrawRow (Pixels, RowData, FontScale, Stride);

    // Shift to next Row
    RowData >>= FONT_WIDTH;
  }
}

VOID
AdvanceNewLine (
  IN EFI_PHYSICAL_ADDRESS       FbBase,
  IN UINT64                     FbLength,
  IN EFI_FRAME_BUFFER_POSITION *CurrentPosition,
  IN EFI_FRAME_BUFFER_POSITION  MaxPosition
  )
{
  MicroSecondDelay (FB_DELAY_US);

  CurrentPosition->XPos = 0;

  if (CurrentPosition->YPos < MaxPosition.YPos) {
    CurrentPosition->YPos++;
  }

  if (CurrentPosition->YPos >= MaxPosition.YPos) {
    ZeroMem (
      (VOID *)(UINTN)FbBase,
      FB_WIDTH * FB_HEIGHT * FB_BPP
      );

    CurrentPosition->XPos = 0;
    CurrentPosition->YPos = 0;
  }
}

UINT8
GetFontScale ()
{
  // Get Smaller Display Dimension
  UINT32 ShorterDimension = (FB_WIDTH < FB_HEIGHT) ? FB_WIDTH : FB_HEIGHT;

  // Return Calculated Font Scale
  return (ShorterDimension < 426) ? 1 : (UINT8)(ShorterDimension / 426);
}

VOID
GetFbPositions (
  IN  EFI_PHYSICAL_ADDRESS        FbBase,
  IN  UINT64                      FbLength,
  IN  UINT8                       FontScale,
  OUT EFI_FRAME_BUFFER_POSITION **CurrentPosition,
  OUT EFI_FRAME_BUFFER_POSITION  *MaxPosition)
{
  EFI_FRAME_BUFFER_POSITION LocalMaxPosition;

  // Get Current Position
  *CurrentPosition = (EFI_FRAME_BUFFER_POSITION *)(FbBase + FbLength - sizeof (EFI_FRAME_BUFFER_POSITION));

  // Calculate Max Positions
  LocalMaxPosition.XPos = FB_WIDTH  / ((FONT_WIDTH  + 1) * FontScale);
  LocalMaxPosition.YPos = FB_HEIGHT / ((FONT_HEIGHT - 4) * FontScale);

  // Pass Max Position
  *MaxPosition = LocalMaxPosition;
}

VOID
WriteFrameBuffer (
  IN EFI_PHYSICAL_ADDRESS FbBase,
  IN UINT64               FbLength,
  IN UINT8                Character
  )
{
  EFI_FRAME_BUFFER_POSITION *CurrentPosition;
  EFI_FRAME_BUFFER_POSITION  MaxPosition;
  UINT8                     *Pixels;
  UINT8                      GlyphFontScale;
  UINT64                     Glyph;
  UINTN                      GlyphWidth;
  UINTN                      GlyphHeight;

  STATIC CONST UINT64 GlyphFont[] = GLYPH_FONT;

  GlyphFontScale = GetFontScale ();

  GetFbPositions (
    FbBase,
    FbLength,
    GlyphFontScale,
    &CurrentPosition,
    &MaxPosition
    );

  //
  // На холодном старте последние байты framebuffer могут содержать мусор.
  // Не используем XPos/YPos, пока не проверим их границы.
  //
  if ((MaxPosition.XPos == 0) ||
      (MaxPosition.YPos == 0) ||
      (CurrentPosition->XPos >= MaxPosition.XPos) ||
      (CurrentPosition->YPos >= MaxPosition.YPos)) {
    CurrentPosition->XPos = 0;
    CurrentPosition->YPos = 0;
  }

  if (Character >= 127) {
    return;
  }

  if (Character < 32) {
    if (Character == '\n') {
      AdvanceNewLine (
        FbBase,
        FbLength,
        CurrentPosition,
        MaxPosition
        );
    } else if (Character == '\r') {
      CurrentPosition->XPos = 0;
    }

    return;
  }

  if ((CurrentPosition->XPos == 0) && (Character == ' ')) {
    return;
  }

  Glyph = GlyphFont[Character - 32];

  GlyphWidth  = (FONT_WIDTH + 1) * GlyphFontScale;
  GlyphHeight = (FONT_HEIGHT - 4) * GlyphFontScale;

  Pixels  = (UINT8 *)(UINTN)FbBase;
  Pixels += (UINTN)CurrentPosition->YPos *
            GlyphHeight *
            (FB_WIDTH * FB_BPP);
  Pixels += (UINTN)CurrentPosition->XPos *
            GlyphWidth *
            FB_BPP;

  DrawGlyph (
    Pixels,
    Glyph,
    GlyphFontScale
    );

  CurrentPosition->XPos++;

  if (CurrentPosition->XPos >= MaxPosition.XPos) {
    AdvanceNewLine (
      FbBase,
      FbLength,
      CurrentPosition,
      MaxPosition
      );
  }
}
EFI_STATUS
GetFrameBufferMemory (
  OUT EFI_PHYSICAL_ADDRESS *Base,
  OUT UINT64               *Length)
{
  EFI_MEMORY_REGION_DESCRIPTOR FbRegion;

  // Locate "Display Reserved" Memory Region
  LocateMemoryRegionByName ("Display Reserved", &FbRegion);
  LocateMemoryRegionByName ("Display_Reserved", &FbRegion);
  if (!FbRegion.Address && !FbRegion.Length) {
    return EFI_NOT_FOUND;
  }

  // Pass Frame Buffer Memory
  *Base   = FbRegion.Address;
  *Length = FbRegion.Length;

  return EFI_SUCCESS;
}

UINTN
EFIAPI
SerialPortWrite (
  IN UINT8 *Buffer,
  IN UINTN  NumberOfBytes
  )
{
  EFI_STATUS              Status;
  BOOT_LOG_BUFFER_HEADER *Header;
  UINT8                  *LogData;
  UINTN                   LogDataSize;
  BOOLEAN                 BootLogAvailable;

#if defined (FRAMEBUFFER_DEBUG_OUTPUT)
  EFI_PHYSICAL_ADDRESS FbBase;
  UINT64               FbLength;
  BOOLEAN              FrameBufferAvailable;
#endif

  if ((Buffer == NULL) || (NumberOfBytes == 0)) {
    return 0;
  }

  Header           = NULL;
  LogData          = NULL;
  LogDataSize      = 0;
  BootLogAvailable = FALSE;

  //
  // Locate the platform memory region named "Log Buffer".
  // Failure must not interrupt UEFI boot or framebuffer output.
  //
  Status = GetBootLogBuffer (
             &Header,
             &LogData,
             &LogDataSize
             );

  if (!EFI_ERROR (Status) &&
      (Header != NULL) &&
      (LogData != NULL) &&
      (LogDataSize != 0) &&
      (LogDataSize <= MAX_UINT32))
  {
    BootLogAvailable = TRUE;

    if ((Header->Signature  != BOOT_LOG_BUFFER_SIGNATURE) ||
        (Header->Version    != BOOT_LOG_BUFFER_VERSION) ||
        (Header->HeaderSize != sizeof (BOOT_LOG_BUFFER_HEADER)) ||
        (Header->BufferSize != (UINT32)LogDataSize))
    {
      Header->Signature    = BOOT_LOG_BUFFER_SIGNATURE;
      Header->Version      = BOOT_LOG_BUFFER_VERSION;
      Header->HeaderSize   = sizeof (BOOT_LOG_BUFFER_HEADER);
      Header->BufferSize   = (UINT32)LogDataSize;
      Header->WriteOffset  = 0;
      Header->TotalWritten = 0;
      Header->Flags        = 0;
      Header->Reserved     = 0;
    }
  }

#if defined (FRAMEBUFFER_DEBUG_OUTPUT)
  FbBase               = 0;
  FbLength             = 0;
  FrameBufferAvailable = FALSE;

  Status = GetFrameBufferMemory (
             &FbBase,
             &FbLength
             );

  if (!EFI_ERROR (Status) &&
      (FbBase != 0) &&
      (FbLength != 0))
  {
    FrameBufferAvailable = TRUE;
  }
#endif

  for (UINTN Index = 0; Index < NumberOfBytes; Index++) {
    UINT8 Character;

    Character = Buffer[Index];

#if defined (FRAMEBUFFER_DEBUG_OUTPUT)
    if (FrameBufferAvailable) {
      WriteFrameBuffer (
        FbBase,
        FbLength,
        Character
        );
    }
#endif

    if (BootLogAvailable) {
      if (Header->WriteOffset < Header->BufferSize) {
        LogData[Header->WriteOffset++] = Character;
      } else {
        Header->Flags |= 1U;
      }

      Header->TotalWritten++;
    }
  }

  if (BootLogAvailable) {
    WriteBackInvalidateDataCacheRange (
      Header,
      sizeof (BOOT_LOG_BUFFER_HEADER) + LogDataSize
      );
  }

  return NumberOfBytes;
}
UINTN
EFIAPI
SerialPortRead (
  OUT UINT8 *Buffer,
  IN  UINTN  NumberOfBytes)
{
  return 0;
}

BOOLEAN
EFIAPI
SerialPortPoll ()
{
  return FALSE;
}

EFI_STATUS
EFIAPI
SerialPortSetControl (IN UINT32 Control)
{
  return EFI_UNSUPPORTED;
}

EFI_STATUS
EFIAPI
SerialPortGetControl (OUT UINT32 *Control)
{
  return EFI_UNSUPPORTED;
}

EFI_STATUS
EFIAPI
SerialPortSetAttributes (
  IN OUT UINT64             *BaudRate,
  IN OUT UINT32             *ReceiveFifoDepth,
  IN OUT UINT32             *Timeout,
  IN OUT EFI_PARITY_TYPE    *Parity,
  IN OUT UINT8              *DataBits,
  IN OUT EFI_STOP_BITS_TYPE *StopBits)
{
  return EFI_UNSUPPORTED;
}

EFI_STATUS
EFIAPI
SerialPortInitialize ()
{
  return EFI_SUCCESS;
}
