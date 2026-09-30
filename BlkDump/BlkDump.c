#include <Uefi.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/ShellLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#include <Protocol/BlockIo.h>
#include <Protocol/ShellParameters.h>

#define MAX_BUFFER_BLOCKS  128U
#define MAX_READ_BYTES     (1024U * 1024U)
#define BLKDUMP_BUILD_DATE __DATE__ " " __TIME__

typedef enum {
  OverwritePrompt,
  OverwriteExisting,
  KeepExisting
} OVERWRITE_MODE;

VOID *
memcpy (
  OUT VOID        *Destination,
  IN  CONST VOID  *Source,
  IN  UINTN       Length
  )
{
  return CopyMem (Destination, Source, Length);
}

typedef struct {
  UINTN        DeviceIndex;
  UINT64       StartLba;
  UINT64       BlockCount;
  CONST CHAR16 *OutputPath;
  CONST CHAR16 *OutputDirectory;
  BOOLEAN      HasDevice;
  BOOLEAN      HasStart;
  BOOLEAN      HasCount;
  BOOLEAN      AllBlocks;
  BOOLEAN      AllDevices;
  BOOLEAN      PauseListing;
  BOOLEAN      VerboseListing;
  BOOLEAN      PromptForAll;
  OVERWRITE_MODE OverwriteMode;
  BOOLEAN      HasOutput;
} OPTIONS;

STATIC
VOID
PrintUsage (
  VOID
  )
{
  Print (L"Usage: BlkDump.efi -l\n");
  Print (L"       BlkDump.efi -l -v\n");
  Print (L"       BlkDump.efi -d <index> [-s <lba>] [-n <blocks>] -o <file>\n\n");
  Print (L"       BlkDump.efi -d <index> -a -o <file>\n\n");
  Print (L"       BlkDump.efi -d <index> --all-devices <directory>\n\n");
  Print (L"       BlkDump.efi --version\n");
  Print (L"  --version       print build version\n");
  Print (L"  -l              list EFI_BLOCK_IO_PROTOCOL handles\n");
  Print (L"  -v              with -l, show full listing details\n");
  Print (L"  -b              pause after each page of list output\n");
  Print (L"  -d <index>      select device by the number shown by -l\n");
  Print (L"  -s <lba>        starting LBA (default: 0)\n");
  Print (L"  -n <blocks>     block count to dump (default: 128)\n");
  Print (L"  -a              dump the entire device (with confirmation)\n");
  Print (L"  -o <file>       regular filesystem output path\n");
  Print (L"  --all-devices <directory>  dump children of -d as blkN.bin\n");
  Print (L"  --overwrite     replace existing output files without prompting\n");
  Print (L"  --no-overwrite  skip existing output files\n");
}

STATIC
EFI_STATUS
ParseDeviceIndex (
  IN  CONST CHAR16 *Argument,
  OUT UINTN        *DeviceIndex
  )
{
  CONST CHAR16 *Digits;
  UINTN Length;
  UINTN Position;
  UINT64 ParsedValue;
  UINTN Digit;

  Digits = Argument;
  Length = StrLen (Argument);

  if (Length == 0) {
    return EFI_INVALID_PARAMETER;
  }

  ParsedValue = 0;
  for (Position = 0; Position < Length; ++Position) {
    if (Digits[Position] < L'0' || Digits[Position] > L'9') {
      Print (L"Invalid device index '%s': expected a decimal number.\n", Argument);
      return EFI_INVALID_PARAMETER;
    }
    Digit = (UINTN)(Digits[Position] - L'0');
    if (ParsedValue > (MAX_UINT64 - Digit) / 10) {
      Print (L"Invalid device index '%s': value is too large.\n", Argument);
      return EFI_INVALID_PARAMETER;
    }
    ParsedValue = (ParsedValue * 10) + Digit;
  }

  if (ParsedValue > MAX_UINTN) {
    Print (L"Invalid device index '%s': value is too large for this platform.\n", Argument);
    return EFI_INVALID_PARAMETER;
  }

  *DeviceIndex = (UINTN)ParsedValue;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
ParseOptions (
  IN  EFI_SHELL_PARAMETERS_PROTOCOL *ShellParameters,
  OUT OPTIONS                       *Options,
  OUT BOOLEAN                       *ListOnly,
  OUT BOOLEAN                       *VersionOnly
  )
{
  UINTN Index;
  CONST CHAR16 *Argument;

  ZeroMem (Options, sizeof (*Options));
  Options->StartLba = 0;
  Options->BlockCount = MAX_BUFFER_BLOCKS;
  Options->PromptForAll = TRUE;
  Options->OverwriteMode = OverwritePrompt;
  *ListOnly = FALSE;
  *VersionOnly = FALSE;

  for (Index = 1; Index < ShellParameters->Argc; ++Index) {
    Argument = ShellParameters->Argv[Index];

    if (StrCmp (Argument, L"-l") == 0 || StrCmp (Argument, L"--list") == 0) {
      *ListOnly = TRUE;
      continue;
    }

    if (StrCmp (Argument, L"-v") == 0 || StrCmp (Argument, L"--verbose") == 0) {
      Options->VerboseListing = TRUE;
      continue;
    }

    if (StrCmp (Argument, L"-V") == 0 || StrCmp (Argument, L"--version") == 0) {
      *VersionOnly = TRUE;
      continue;
    }

    if (StrCmp (Argument, L"-b") == 0 || StrCmp (Argument, L"--break") == 0) {
      Options->PauseListing = TRUE;
      continue;
    }

    if (StrCmp (Argument, L"-d") == 0 || StrCmp (Argument, L"--device") == 0) {
      if (++Index >= ShellParameters->Argc) {
        Print (L"Missing value for %s.\n", Argument);
        return EFI_INVALID_PARAMETER;
      }
      if (EFI_ERROR (ParseDeviceIndex (ShellParameters->Argv[Index], &Options->DeviceIndex))) {
        return EFI_INVALID_PARAMETER;
      }
      Options->HasDevice = TRUE;
      continue;
    }

    if (StrCmp (Argument, L"-s") == 0 || StrCmp (Argument, L"--start") == 0) {
      if (++Index >= ShellParameters->Argc) {
        Print (L"Missing value for %s.\n", Argument);
        return EFI_INVALID_PARAMETER;
      }
      Options->StartLba = StrDecimalToUint64 (ShellParameters->Argv[Index]);
      Options->HasStart = TRUE;
      continue;
    }

    if (StrCmp (Argument, L"-n") == 0 || StrCmp (Argument, L"--count") == 0) {
      if (++Index >= ShellParameters->Argc) {
        Print (L"Missing value for %s.\n", Argument);
        return EFI_INVALID_PARAMETER;
      }
      Options->BlockCount = StrDecimalToUint64 (ShellParameters->Argv[Index]);
      Options->HasCount = TRUE;
      continue;
    }

    if (StrCmp (Argument, L"-a") == 0 || StrCmp (Argument, L"--all") == 0) {
      Options->AllBlocks = TRUE;
      continue;
    }

    if (StrCmp (Argument, L"-o") == 0 || StrCmp (Argument, L"--output") == 0) {
      if (++Index >= ShellParameters->Argc) {
        Print (L"Missing value for %s.\n", Argument);
        return EFI_INVALID_PARAMETER;
      }
      Options->OutputPath = ShellParameters->Argv[Index];
      Options->HasOutput = TRUE;
      continue;
    }

    if (StrCmp (Argument, L"--all-devices") == 0) {
      if (++Index >= ShellParameters->Argc) {
        Print (L"Missing output directory for --all-devices.\n");
        return EFI_INVALID_PARAMETER;
      }
      Options->OutputDirectory = ShellParameters->Argv[Index];
      Options->AllDevices = TRUE;
      continue;
    }

    if (StrCmp (Argument, L"--overwrite") == 0) {
      if (Options->OverwriteMode == KeepExisting) {
        Print (L"--overwrite and --no-overwrite cannot be used together.\n");
        return EFI_INVALID_PARAMETER;
      }
      Options->OverwriteMode = OverwriteExisting;
      continue;
    }

    if (StrCmp (Argument, L"--no-overwrite") == 0) {
      if (Options->OverwriteMode == OverwriteExisting) {
        Print (L"--overwrite and --no-overwrite cannot be used together.\n");
        return EFI_INVALID_PARAMETER;
      }
      Options->OverwriteMode = KeepExisting;
      continue;
    }

    Print (L"Unknown option or argument '%s'.\n", Argument);
    return EFI_INVALID_PARAMETER;
  }

  if (Options->BlockCount == 0) {
    Print (L"Block count must be greater than zero.\n");
    return EFI_INVALID_PARAMETER;
  }

  if (Options->AllBlocks && (Options->HasStart || Options->HasCount)) {
    Print (L"--all cannot be combined with --start or --count.\n");
    return EFI_INVALID_PARAMETER;
  }

  if (Options->AllDevices && !Options->HasDevice) {
    Print (L"--all-devices requires --device.\n");
    return EFI_INVALID_PARAMETER;
  }

  if (Options->VerboseListing && !*ListOnly) {
    Print (L"--verbose requires --list.\n");
    return EFI_INVALID_PARAMETER;
  }

  if (Options->AllDevices &&
      (Options->HasStart || Options->HasCount || Options->HasOutput)) {
    Print (L"--all-devices cannot be combined with --start, --count, or --output.\n");
    return EFI_INVALID_PARAMETER;
  }

  return EFI_SUCCESS;
}

STATIC
VOID
FormatByteSize (
  IN  UINT64 ByteCount,
  OUT CHAR16 *Buffer,
  IN  UINTN  BufferSize
  )
{
  STATIC CONST CHAR16 *Units[] = { L"B", L"KiB", L"MiB", L"GiB", L"TiB" };
  UINTN UnitIndex;
  UINT64 UnitSize;
  UINT64 Whole;
  UINTN Tenths;

  UnitIndex = 0;
  UnitSize = 1;
  while (UnitIndex < 4 && ByteCount / UnitSize >= 1024) {
    UnitSize *= 1024;
    ++UnitIndex;
  }

  Whole = ByteCount / UnitSize;
  if (UnitIndex == 0) {
    UnicodeSPrint (Buffer, BufferSize, L"%Lu B", Whole);
    return;
  }

  Tenths = (UINTN)(((ByteCount % UnitSize) * 10 + UnitSize / 2) / UnitSize);
  if (Tenths == 10) {
    ++Whole;
    Tenths = 0;
  }
  if (Tenths == 0) {
    UnicodeSPrint (Buffer, BufferSize, L"%Lu %s", Whole, Units[UnitIndex]);
  } else {
    UnicodeSPrint (Buffer, BufferSize, L"%Lu.%u %s", Whole, Tenths, Units[UnitIndex]);
  }
}

STATIC
UINTN
GetDevicePathPrefixLength (
  IN EFI_DEVICE_PATH_PROTOCOL *DevicePath
  )
{
  EFI_DEVICE_PATH_PROTOCOL *Node;
  UINTN Length;

  Length = 0;
  Node = DevicePath;
  while (Node != NULL && !IsDevicePathEnd (Node)) {
    Length += DevicePathNodeLength (Node);
    Node = NextDevicePathNode (Node);
  }
  return Length;
}

STATIC
BOOLEAN
IsChildDevicePath (
  IN EFI_DEVICE_PATH_PROTOCOL *ParentPath,
  IN EFI_DEVICE_PATH_PROTOCOL *CandidatePath
  );

STATIC
UINTN
FindParentDiskIndex (
  IN EFI_HANDLE *Handles,
  IN UINTN      HandleCount,
  IN UINTN      CandidateIndex
  )
{
  EFI_BLOCK_IO_PROTOCOL *ParentBlockIo;
  EFI_DEVICE_PATH_PROTOCOL *ParentPath;
  EFI_DEVICE_PATH_PROTOCOL *CandidatePath;
  EFI_STATUS Status;
  UINTN Index;
  UINTN ParentIndex;
  UINTN ParentLength;

  CandidatePath = DevicePathFromHandle (Handles[CandidateIndex]);
  if (CandidatePath == NULL) {
    return MAX_UINTN;
  }

  ParentIndex = MAX_UINTN;
  ParentLength = 0;
  for (Index = 0; Index < HandleCount; ++Index) {
    if (Index == CandidateIndex) {
      continue;
    }
    Status = gBS->HandleProtocol (Handles[Index], &gEfiBlockIoProtocolGuid, (VOID **)&ParentBlockIo);
    if (EFI_ERROR (Status) || ParentBlockIo == NULL || ParentBlockIo->Media == NULL ||
        ParentBlockIo->Media->LogicalPartition) {
      continue;
    }
    ParentPath = DevicePathFromHandle (Handles[Index]);
    if (ParentPath != NULL && IsChildDevicePath (ParentPath, CandidatePath) &&
        GetDevicePathPrefixLength (ParentPath) > ParentLength) {
      ParentIndex = Index;
      ParentLength = GetDevicePathPrefixLength (ParentPath);
    }
  }
  return ParentIndex;
}

STATIC
VOID
GetScreenSize (
  UINTN *ScreenColumns,
  UINTN *ScreenRows
  )
{
  *ScreenColumns = 80;
  *ScreenRows = 25;
  if (gST != NULL && gST->ConOut != NULL && gST->ConOut->Mode != NULL) {
    if (!EFI_ERROR (gST->ConOut->QueryMode (
                      gST->ConOut,
                      gST->ConOut->Mode->Mode,
                      ScreenColumns,
                      ScreenRows
                      ))) {
      if (ScreenColumns == 0) {
        *ScreenColumns = 80;
      }
      if (ScreenRows == 0) {
        *ScreenRows = 25;
      }
    }
  }
}

STATIC
BOOLEAN
CheckContinueListing (
  IN UINTN ScreenRows,
  IN OUT UINTN *PrintedLines,
  IN BOOLEAN PauseEnabled
  )
{
  SHELL_PROMPT_RESPONSE *Response;
  EFI_STATUS Status;

  if (!PauseEnabled) {
    return TRUE;
  }

  if (*PrintedLines < ScreenRows - 1) {
    return TRUE;
  }

  Print (L"-- More -- (Enter to continue, Q to quit) ");

  Status = ShellPromptForResponse (
             ShellPromptResponseTypeQuitContinue,
             NULL,
             (VOID **)&Response
             );

  if (EFI_ERROR (Status) || Response == NULL) {
    if (Response != NULL) {
      FreePool (Response);
    }
    return FALSE;
  }

  if (*Response == ShellPromptResponseQuit) {
    FreePool (Response);
    return FALSE;
  }

  FreePool (Response);

  *PrintedLines = 0;
  return TRUE;
}

STATIC
VOID
PrintDevicesGrouped (
  IN EFI_HANDLE *Handles,
  IN UINTN      HandleCount,
  IN BOOLEAN    PauseEnabled
)
{
  EFI_STATUS Status;
  UINTN ScreenColumns = 80;
  UINTN ScreenRows = 25;
  UINTN PrintedLines = 0;

  UINTN ParentIndex;
  EFI_BLOCK_IO_PROTOCOL *ParentBlockIo = NULL;
  EFI_DEVICE_PATH_PROTOCOL *ParentPath = NULL;
  CHAR16 *ParentPathText = NULL;
  UINTN ParentPathLines = 1;

  UINTN ChildIndex;
  EFI_BLOCK_IO_PROTOCOL *ChildBlockIo = NULL;
  EFI_DEVICE_PATH_PROTOCOL *ChildPath = NULL;
  CHAR16 *ChildPathText = NULL;
  UINTN ChildPathLines = 1;


  // these are used for the parent and then reused for the child
  UINT64 TotalBytes;
  CHAR16 SizeText[32];
  CHAR16 BlockSizeText[32];

  /*
  * Devices:
  *    1: whole disk, ...
  *       path: ACPI(...)/PCI(...)
  *          2: partition, ...
  *             path: HD(...)
  * Ruler strings below use:
  * OOOO = parent ("outer") index field
  * IIII = child ("inner") index field
  */

  CONST UINTN ParentIndexWidth    = StrLen(L"OOOO");
  CONST UINTN ChildIndexWidth     = StrLen(L"IIII");
  CONST UINTN ParentPathPrefixLen = StrLen(L"OOOO: path: ");
  CONST UINTN ChildPathPrefixLen  = StrLen(L"OOOO: IIII: path: ");

  GetScreenSize(&ScreenColumns, &ScreenRows);

  Print (L"Devices:\n");
  PrintedLines++;

  for (ParentIndex = 0; ParentIndex < HandleCount; ++ParentIndex) {
    Status = gBS->HandleProtocol (Handles[ParentIndex], &gEfiBlockIoProtocolGuid, (VOID **)&ParentBlockIo);
    if (EFI_ERROR (Status) || ParentBlockIo == NULL || ParentBlockIo->Media == NULL) {
      continue;
    }
    if (ParentBlockIo->Media->LogicalPartition &&
        FindParentDiskIndex (Handles, HandleCount, ParentIndex) != MAX_UINTN) {
      continue;
    }

    ParentPath = DevicePathFromHandle (Handles[ParentIndex]);
    if (ParentPath != NULL) {
      ParentPathText = ConvertDevicePathToText (ParentPath, TRUE, TRUE);
      if (ParentPathText != NULL) {
        ParentPathLines = (ParentPathPrefixLen + StrLen (ParentPathText) + ScreenColumns - 1) / ScreenColumns;
      }
    }
    TotalBytes = 0;
    if (ParentBlockIo->Media->BlockSize != 0 && ParentBlockIo->Media->LastBlock != MAX_UINT64 &&
        ParentBlockIo->Media->LastBlock + 1ULL <= MAX_UINT64 / ParentBlockIo->Media->BlockSize) {
      TotalBytes = (ParentBlockIo->Media->LastBlock + 1ULL) * ParentBlockIo->Media->BlockSize;
    }
    FormatByteSize (TotalBytes, SizeText, sizeof (SizeText));
    FormatByteSize (ParentBlockIo->Media->BlockSize, BlockSizeText, sizeof (BlockSizeText));
    Print (L"%*u: %s, %s, %s, block size %s%s\n",
      ParentIndexWidth,
      ParentIndex,
      ParentBlockIo->Media->LogicalPartition ? L"partition" : L"whole disk",
      ParentBlockIo->Media->ReadOnly ? L"RO" : L"RW",
      SizeText,
      BlockSizeText,
      ParentBlockIo->Media->RemovableMedia ? L", removable" : L""
    );
    PrintedLines++;

    Print (L"%*s  path: %s\n",
      ParentIndexWidth,
      L"",
      ParentPathText == NULL ? L"<none>" : ParentPathText
    );
    PrintedLines += ParentPathLines;

    if (ParentPathText != NULL) {
      FreePool (ParentPathText);
      ParentPathText = NULL;
    }
    if (!CheckContinueListing (ScreenRows, &PrintedLines, PauseEnabled)) {
      return;
    }

    if (ParentBlockIo->Media->LogicalPartition) {
      // don't check for children of partitions
      continue;
    }

    for (ChildIndex = 0; ChildIndex < HandleCount; ++ChildIndex) {
      Status = gBS->HandleProtocol (Handles[ChildIndex], &gEfiBlockIoProtocolGuid, (VOID **)&ChildBlockIo);
      if (EFI_ERROR (Status) || ChildBlockIo == NULL || ChildBlockIo->Media == NULL ||
          !ChildBlockIo->Media->LogicalPartition ||
          FindParentDiskIndex (Handles, HandleCount, ChildIndex) != ParentIndex) {
        continue;
      }
      ChildPath = DevicePathFromHandle (Handles[ChildIndex]);
      ChildPathText = NULL;
      ChildPathLines = 1;
      if (ChildPath != NULL && ParentPath != NULL) {
        ChildPathText = ConvertDevicePathToText(
            (EFI_DEVICE_PATH_PROTOCOL *)((UINT8 *)ChildPath + GetDevicePathPrefixLength(ParentPath)),
            TRUE,
            TRUE);
      }
      TotalBytes = 0;
      if (ChildBlockIo->Media->BlockSize != 0 && ChildBlockIo->Media->LastBlock != MAX_UINT64 &&
          ChildBlockIo->Media->LastBlock + 1ULL <= MAX_UINT64 / ChildBlockIo->Media->BlockSize) {
        TotalBytes = (ChildBlockIo->Media->LastBlock + 1ULL) * ChildBlockIo->Media->BlockSize;
      }
      FormatByteSize (TotalBytes, SizeText, sizeof (SizeText));

      Print (L"%*s  %*u: partition, %s, %s\n",
        ParentIndexWidth,
        L"",
        ChildIndexWidth,
        ChildIndex,
        ChildBlockIo->Media->ReadOnly ? L"RO" : L"RW",
        SizeText
      );
      PrintedLines += 1;

      Print (L"%*s  %*s  path: %s\n",
        ParentIndexWidth,
        L"",
        ChildIndexWidth,
        L"",
        ChildPathText == NULL ? L"<none>" : ChildPathText
      );
      if (ChildPathText != NULL) {
        ChildPathLines = (ChildPathPrefixLen + StrLen (ChildPathText) + ScreenColumns - 1) / ScreenColumns;
      }
      PrintedLines += ChildPathLines;

      if (ChildPathText != NULL) {
        FreePool (ChildPathText);
        ChildPathText = NULL;
      }

      if (!CheckContinueListing (ScreenRows, &PrintedLines, PauseEnabled)) {
        return;
      }
    }
  }
}

STATIC
VOID
PrintDevicesVerbose (
  IN EFI_HANDLE *Handles,
  IN UINTN      HandleCount,
  IN BOOLEAN    PauseEnabled
  )
{
  EFI_STATUS Status;
  UINTN ScreenColumns = 80;
  UINTN ScreenRows = 25;
  UINTN PrintedLines = 0;

  UINTN Index;
  EFI_BLOCK_IO_PROTOCOL *BlockIo = NULL;
  EFI_DEVICE_PATH_PROTOCOL *DevicePath = NULL;
  CHAR16 *DevicePathText = NULL;
  UINTN PathLines = 1;
  UINT64 TotalBytes;

  CONST UINTN IndexWidth    = StrLen(L"   1");
  CONST UINTN PathPrefixLen = StrLen(L"   1: path: ");

  GetScreenSize (&ScreenColumns, &ScreenRows);

  Print (L"Devices:\n");
  PrintedLines++;

  for (Index = 0; Index < HandleCount; ++Index) {
    Status = gBS->HandleProtocol (Handles[Index], &gEfiBlockIoProtocolGuid, (VOID **)&BlockIo);
    if (EFI_ERROR (Status) || BlockIo == NULL || BlockIo->Media == NULL) {
      continue;
    }

    TotalBytes = 0;
    if (BlockIo->Media->BlockSize != 0 && BlockIo->Media->LastBlock != MAX_UINT64 &&
        BlockIo->Media->LastBlock + 1ULL <= MAX_UINT64 / BlockIo->Media->BlockSize) {
      TotalBytes = (BlockIo->Media->LastBlock + 1ULL) * BlockIo->Media->BlockSize;
    }

    DevicePath = DevicePathFromHandle (Handles[Index]);
    if (DevicePath != NULL) {
      DevicePathText = DevicePath == NULL ? NULL : ConvertDevicePathToText (DevicePath, TRUE, TRUE);
    }

    Print (L"%*u: %s, %s, media id 0x%lx, block size %u%s\n",
      IndexWidth,
      Index,
      BlockIo->Media->LogicalPartition ? L"partition" : L"whole disk",
      BlockIo->Media->ReadOnly ? L"RO" : L"RW",
      BlockIo->Media->MediaId,
      BlockIo->Media->BlockSize,
      BlockIo->Media->RemovableMedia ? L", removable" : L""
    );
    PrintedLines++;

    Print (L"%*s  io align %u, last LBA 0x%lx, %Lu bytes\n",
      IndexWidth,
      L"",
      BlockIo->Media->IoAlign,
      BlockIo->Media->LastBlock,
      TotalBytes
    );
    PrintedLines++;

    Print (L"%*s  path: %s\n",
      IndexWidth,
      L"",
      DevicePathText == NULL ? L"<none>" : DevicePathText
    );
    if (DevicePathText != NULL) {
      PathLines = (PathPrefixLen + StrLen (DevicePathText) + ScreenColumns - 1) / ScreenColumns;
    }
    PrintedLines += PathLines;

    if (DevicePathText != NULL) {
      FreePool (DevicePathText);
      DevicePathText = NULL;
    }
    if (!CheckContinueListing (ScreenRows, &PrintedLines, PauseEnabled)) {
      break;
    }
  }
}

STATIC
EFI_STATUS
ValidateOutputFile (
  IN SHELL_FILE_HANDLE File,
  OUT BOOLEAN          *IsRegularFile
  )
{
  EFI_FILE_INFO *FileInfo;
  EFI_STATUS Status;

  *IsRegularFile = FALSE;
  FileInfo = gEfiShellProtocol->GetFileInfo (File);
  if (FileInfo == NULL) {
    return EFI_UNSUPPORTED;
  }

  Status = EFI_SUCCESS;
  *IsRegularFile = ((FileInfo->Attribute & EFI_FILE_DIRECTORY) == 0);
  FreePool (FileInfo);
  return Status;
}

STATIC
EFI_STATUS
ConfirmOutputOverwrite (
  IN CONST CHAR16 *OutputPath,
  IN OVERWRITE_MODE OverwriteMode
  )
{
  SHELL_FILE_HANDLE ExistingFile;
  SHELL_PROMPT_RESPONSE *Response;
  EFI_STATUS Status;
  BOOLEAN IsRegularFile;

  ExistingFile = NULL;
  Response = NULL;
  Status = gEfiShellProtocol->OpenFileByName (
             OutputPath,
             &ExistingFile,
             EFI_FILE_MODE_READ
             );
  if (EFI_ERROR (Status)) {
    if (Status == EFI_NOT_FOUND) {
      return EFI_SUCCESS;
    }
    Print (L"Unable to inspect output path '%s': %r\n", OutputPath, Status);
    return Status;
  }

  if (OverwriteMode == KeepExisting) {
    gEfiShellProtocol->CloseFile (ExistingFile);
    Print (L"Skipping existing output file '%s'.\n", OutputPath);
    return EFI_ALREADY_STARTED;
  }

  Status = ValidateOutputFile (ExistingFile, &IsRegularFile);
  gEfiShellProtocol->CloseFile (ExistingFile);
  ExistingFile = NULL;
  if (EFI_ERROR (Status) || !IsRegularFile) {
    Print (L"Output path must be a regular filesystem file.\n");
    return EFI_INVALID_PARAMETER;
  }

  if (OverwriteMode == OverwriteExisting) {
    return EFI_SUCCESS;
  }

  Print (L"Output file '%s' already exists. Overwrite? (Y/N) ", OutputPath);
  Status = ShellPromptForResponse (ShellPromptResponseTypeYesNo, NULL, (VOID **)&Response);
  if (EFI_ERROR (Status) || Response == NULL || *Response != ShellPromptResponseYes) {
    if (Response != NULL) {
      FreePool (Response);
    }
    Print (L"\nDump cancelled.\n");
    return EFI_ABORTED;
  }

  FreePool (Response);
  Print (L"\n");
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
DumpRange (
  IN EFI_BLOCK_IO_PROTOCOL *BlockIo,
  IN OPTIONS               *Options
  )
{
  SHELL_FILE_HANDLE OutputFile;
  EFI_STATUS Status;
  UINTN BufferBlocks;
  UINTN BufferBytes;
  UINTN WriteCount;
  UINTN ChunkBlocks;
  UINT64 CurrentLba;
  UINT64 RemainingBlocks;
  UINT64 TotalBytes;
  UINT8 *Buffer;
  UINT8 *AlignedBuffer;
  UINTN AlignmentMask;
  SHELL_PROMPT_RESPONSE *Response;
  BOOLEAN IsRegularFile;
  EFI_STATUS FlushStatus;

  OutputFile = NULL;
  Buffer = NULL;
  AlignedBuffer = NULL;
  Response = NULL;
  Status = EFI_SUCCESS;

  if (BlockIo == NULL || BlockIo->Media == NULL || Options->OutputPath == NULL) {
    Print (L"Cannot dump: block device, media, or output path is invalid.\n");
    return EFI_INVALID_PARAMETER;
  }

  if (BlockIo->Media->BlockSize == 0) {
    Print (L"Cannot dump: block device has an invalid block size.\n");
    return EFI_INVALID_PARAMETER;
  }

  if (Options->AllBlocks) {
    if (BlockIo->Media->LastBlock == MAX_UINT64) {
      Print (L"Cannot dump all blocks: device capacity is unavailable.\n");
      return EFI_INVALID_PARAMETER;
    }
    Options->StartLba = 0;
    Options->BlockCount = BlockIo->Media->LastBlock + 1ULL;
  }

  if (Options->StartLba > BlockIo->Media->LastBlock) {
    Print (L"Invalid start LBA 0x%lx: beyond the last device LBA 0x%lx.\n",
      Options->StartLba,
      BlockIo->Media->LastBlock
      );
    return EFI_INVALID_PARAMETER;
  }

  if ((UINT64)Options->BlockCount > (BlockIo->Media->LastBlock - Options->StartLba + 1ULL)) {
    Print (L"Invalid block count %Lu: range exceeds the device capacity.\n", Options->BlockCount);
    return EFI_INVALID_PARAMETER;
  }

  if (Options->BlockCount > MAX_UINT64 / BlockIo->Media->BlockSize) {
    Print (L"Invalid block count %Lu: byte count overflows.\n", Options->BlockCount);
    return EFI_INVALID_PARAMETER;
  }
  TotalBytes = Options->BlockCount * BlockIo->Media->BlockSize;
  if (TotalBytes > MAX_UINTN) {
    Print (L"Dump size %Lu bytes is too large for this platform.\n", TotalBytes);
    return EFI_INVALID_PARAMETER;
  }

  if (Options->AllBlocks && Options->PromptForAll) {
    Print (
      L"This will create '%s' containing %Lu bytes (%Lu MiB). Continue? (Y/N) ",
      Options->OutputPath,
      TotalBytes,
      TotalBytes / (1024ULL * 1024ULL)
      );
    Status = ShellPromptForResponse (ShellPromptResponseTypeYesNo, NULL, (VOID **)&Response);
    if (EFI_ERROR (Status) || Response == NULL ||
        (*Response != ShellPromptResponseYes)) {
      Print (L"\nDump cancelled.\n");
      Status = EFI_ABORTED;
      goto Done;
    }
    FreePool (Response);
    Response = NULL;
    Print (L"\n");
  }

  Status = ConfirmOutputOverwrite (Options->OutputPath, Options->OverwriteMode);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gEfiShellProtocol->OpenFileByName (
             Options->OutputPath,
             &OutputFile,
             EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE
             );
  if (EFI_ERROR (Status)) {
    Print (L"Unable to open output file '%s': %r\n", Options->OutputPath, Status);
    return Status;
  }

  Status = ValidateOutputFile (OutputFile, &IsRegularFile);
  if (EFI_ERROR (Status) || !IsRegularFile) {
    Print (L"Output path must be a regular filesystem file.\n");
    Status = EFI_INVALID_PARAMETER;
    goto Done;
  }

  if (BlockIo->Media->IoAlign == 0) {
    BlockIo->Media->IoAlign = 1;
  }
  AlignmentMask = BlockIo->Media->IoAlign - 1;
  BufferBlocks = (MAX_READ_BYTES / BlockIo->Media->BlockSize);
  if (BufferBlocks == 0) {
    BufferBlocks = 1;
  }
  BufferBytes = BufferBlocks * BlockIo->Media->BlockSize;
  Buffer = AllocatePool (BufferBytes + BlockIo->Media->IoAlign);
  if (Buffer == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Done;
  }
  AlignedBuffer = (UINT8 *) (((UINTN) Buffer + (UINTN) BlockIo->Media->IoAlign - 1U) & ~(UINTN) AlignmentMask);

  CurrentLba = Options->StartLba;
  RemainingBlocks = Options->BlockCount;
  while (RemainingBlocks > 0) {
    ChunkBlocks = (RemainingBlocks > BufferBlocks) ? BufferBlocks : (UINTN) RemainingBlocks;
    if (ChunkBlocks > 1) {
      ChunkBlocks = 1;
    }
    WriteCount = ChunkBlocks * BlockIo->Media->BlockSize;

    Status = BlockIo->ReadBlocks (
                         BlockIo,
                         BlockIo->Media->MediaId,
                         CurrentLba,
                         WriteCount,
                         AlignedBuffer
                         );
    if (EFI_ERROR (Status)) {
      Print (L"Read failed at LBA 0x%lx: %r\n", CurrentLba, Status);
      goto Done;
    }

    Status = gEfiShellProtocol->WriteFile (OutputFile, &WriteCount, AlignedBuffer);
    if (EFI_ERROR (Status) || WriteCount != ChunkBlocks * BlockIo->Media->BlockSize) {
      Print (L"Write failed at LBA 0x%lx: %r\n", CurrentLba, Status);
      if (!EFI_ERROR (Status)) {
        Status = EFI_DEVICE_ERROR;
      }
      goto Done;
    }

    CurrentLba += ChunkBlocks;
    RemainingBlocks -= ChunkBlocks;
    Print (L"\rRead 0x%lx / 0x%lx blocks", CurrentLba, Options->StartLba + Options->BlockCount);
  }

  Print (L"\nDump complete: %Lu bytes written.\n", TotalBytes);
  FlushStatus = gEfiShellProtocol->FlushFile (OutputFile);
  if (EFI_ERROR (FlushStatus)) {
    Print (L"Unable to flush output file: %r\n", FlushStatus);
    Status = FlushStatus;
  }

Done:
  if (Response != NULL) {
    FreePool (Response);
  }
  if (OutputFile != NULL) {
    gEfiShellProtocol->CloseFile (OutputFile);
  }
  if (Buffer != NULL) {
    FreePool (Buffer);
  }
  return Status;
}

STATIC
BOOLEAN
IsChildDevicePath (
  IN EFI_DEVICE_PATH_PROTOCOL *ParentPath,
  IN EFI_DEVICE_PATH_PROTOCOL *CandidatePath
  )
{
  EFI_DEVICE_PATH_PROTOCOL *Node;
  UINTN ParentLength;
  UINTN CandidateLength;

  if (ParentPath == NULL || CandidatePath == NULL) {
    return FALSE;
  }

  ParentLength = 0;
  Node = ParentPath;
  while (!IsDevicePathEnd (Node)) {
    ParentLength += DevicePathNodeLength (Node);
    Node = NextDevicePathNode (Node);
  }

  CandidateLength = GetDevicePathSize (CandidatePath);
  return CandidateLength > ParentLength &&
         CompareMem (ParentPath, CandidatePath, ParentLength) == 0;
}

STATIC
CHAR16 *
BuildBulkOutputPath (
  IN CONST CHAR16 *Directory,
  IN UINTN        DeviceIndex
  )
{
  UINTN DirectoryLength;
  UINTN PathCharacters;
  BOOLEAN HasSeparator;
  CHAR16 *Path;

  DirectoryLength = StrLen (Directory);
  HasSeparator = DirectoryLength > 0 &&
                 (Directory[DirectoryLength - 1] == L'\\' ||
                  Directory[DirectoryLength - 1] == L'/');
  PathCharacters = DirectoryLength + 32;
  Path = AllocateZeroPool ((PathCharacters + 1) * sizeof (CHAR16));
  if (Path == NULL) {
    return NULL;
  }

  UnicodeSPrint (
    Path,
    (PathCharacters + 1) * sizeof (CHAR16),
    HasSeparator ? L"%sblk%u.bin" : L"%s\\blk%u.bin",
    Directory,
    DeviceIndex
    );
  return Path;
}

STATIC
EFI_STATUS
ConfirmBulkDump (
  IN CONST CHAR16 *Directory
  )
{
  SHELL_PROMPT_RESPONSE *Response;
  EFI_STATUS Status;

  Response = NULL;
  Print (
    L"This will dump every child block handle into '%s'. Continue? (Y/N) ",
    Directory
    );
  Status = ShellPromptForResponse (ShellPromptResponseTypeYesNo, NULL, (VOID **)&Response);
  if (EFI_ERROR (Status) || Response == NULL || *Response != ShellPromptResponseYes) {
    if (Response != NULL) {
      FreePool (Response);
    }
    Print (L"\nBulk dump cancelled.\n");
    return EFI_ABORTED;
  }

  FreePool (Response);
  Print (L"\n");
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_SHELL_PARAMETERS_PROTOCOL *ShellParameters;
  EFI_STATUS Status;
  EFI_HANDLE *Handles;
  UINTN HandleCount;
  OPTIONS Options;
  OPTIONS ChildOptions;
  BOOLEAN ListOnly;
  BOOLEAN VersionOnly;
  EFI_BLOCK_IO_PROTOCOL *BlockIo;
  EFI_DEVICE_PATH_PROTOCOL *ParentPath;
  EFI_DEVICE_PATH_PROTOCOL *CandidatePath;
  CHAR16 *OutputPath;
  UINTN Index;
  UINTN ChildCount;
  EFI_STATUS BulkStatus;

  ShellParameters = NULL;
  Handles = NULL;
  BlockIo = NULL;
  ParentPath = NULL;
  BulkStatus = EFI_SUCCESS;
  ChildCount = 0;

  Status = gBS->HandleProtocol (ImageHandle, &gEfiShellParametersProtocolGuid, (VOID **)&ShellParameters);
  if (EFI_ERROR (Status) || ShellParameters == NULL) {
    PrintUsage ();
    return EFI_UNSUPPORTED;
  }

  Status = ParseOptions (ShellParameters, &Options, &ListOnly, &VersionOnly);
  if (EFI_ERROR (Status)) {
    PrintUsage ();
    return Status;
  }

  if (VersionOnly) {
    Print (L"BlkDump build %a\n", BLKDUMP_BUILD_DATE);
    return EFI_SUCCESS;
  }

  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiBlockIoProtocolGuid,
                  NULL,
                  &HandleCount,
                  &Handles
                  );
  if (EFI_ERROR (Status)) {
    Print (L"Unable to locate block devices: %r\n", Status);
    return Status;
  }

  if (ListOnly) {
    if (Options.VerboseListing) {
      PrintDevicesVerbose (Handles, HandleCount, Options.PauseListing);
    } else {
      PrintDevicesGrouped (Handles, HandleCount, Options.PauseListing);
    }
    FreePool (Handles);
    return EFI_SUCCESS;
  }

  if (Options.AllDevices) {
    if (Options.DeviceIndex >= HandleCount) {
      PrintUsage ();
      FreePool (Handles);
      return EFI_INVALID_PARAMETER;
    }

    ParentPath = DevicePathFromHandle (Handles[Options.DeviceIndex]);
    if (ParentPath == NULL) {
      Print (L"Selected handle has no device path; cannot identify children.\n");
      FreePool (Handles);
      return EFI_UNSUPPORTED;
    }

    Status = ConfirmBulkDump (Options.OutputDirectory);
    if (EFI_ERROR (Status)) {
      FreePool (Handles);
      return Status;
    }

    for (Index = 0; Index < HandleCount; ++Index) {
      if (Index == Options.DeviceIndex) {
        continue;
      }

      CandidatePath = DevicePathFromHandle (Handles[Index]);
      if (!IsChildDevicePath (ParentPath, CandidatePath)) {
        continue;
      }

      OutputPath = BuildBulkOutputPath (Options.OutputDirectory, Index);
      if (OutputPath == NULL) {
        Print (L"Unable to allocate output path for blk%u.\n", Index);
        BulkStatus = EFI_OUT_OF_RESOURCES;
        continue;
      }

      Status = gBS->HandleProtocol (Handles[Index], &gEfiBlockIoProtocolGuid, (VOID **)&BlockIo);
      if (EFI_ERROR (Status)) {
        Print (L"Unable to open blk%u: %r\n", Index, Status);
        FreePool (OutputPath);
        BulkStatus = Status;
        continue;
      }

      ChildOptions = Options;
      ChildOptions.AllBlocks = TRUE;
      ChildOptions.PromptForAll = FALSE;
      ChildOptions.OutputPath = OutputPath;
      Status = DumpRange (BlockIo, &ChildOptions);
      FreePool (OutputPath);
      if (EFI_ERROR (Status) && Status != EFI_ALREADY_STARTED) {
        BulkStatus = Status;
      }
      ++ChildCount;
    }

    if (ChildCount == 0) {
      Print (L"No child block handles found for blk%u.\n", Options.DeviceIndex);
      FreePool (Handles);
      return EFI_NOT_FOUND;
    }

    FreePool (Handles);
    return EFI_ERROR (BulkStatus) ? BulkStatus : EFI_SUCCESS;
  }

  if (!Options.HasDevice || !Options.HasOutput || Options.DeviceIndex >= HandleCount) {
    PrintUsage ();
    FreePool (Handles);
    return EFI_INVALID_PARAMETER;
  }

  Status = gBS->HandleProtocol (Handles[Options.DeviceIndex], &gEfiBlockIoProtocolGuid, (VOID **)&BlockIo);
  if (EFI_ERROR (Status)) {
    FreePool (Handles);
    return Status;
  }

  Status = DumpRange (BlockIo, &Options);
  FreePool (Handles);
  return Status;
}
