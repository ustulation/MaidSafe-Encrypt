/*

Copyright (c) 2007, 2008 Hiroki Asakawa info@dokan-dev.net


Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#include "fs/w_fuse/fswin.h"

#include <stdio.h>
#include <stdlib.h>

#include "fs/w_fuse/dokan/fileinfo.h"

namespace fs = boost::filesystem;
namespace fs_w_fuse {

BOOL g_UseStdErr;
BOOL g_DebugMode;

static void DbgPrint(LPCWSTR format, ...) {
  if (g_DebugMode) {
    WCHAR buffer[512];
    va_list argp;
    va_start(argp, format);
    // vswprintf_s(buffer, sizeof(buffer)/sizeof(WCHAR), format, argp);
    va_end(argp);
    if (g_UseStdErr) {
      fwprintf(stderr, buffer);
    } else {
      // OutputDebugStringW(buffer);
      wprintf(buffer);
    }
  }
}


std::list<ULONG64> to_encrypt_;
std::list<std::string> to_delete_;

static WCHAR RootDirectory[MAX_PATH];
// static WCHAR DokanDrive[3] = L"M:";

static void GetFilePath(PWCHAR filePath, LPCWSTR FileName) {
  RtlZeroMemory(filePath, MAX_PATH);
  wcsncpy(filePath, RootDirectory, wcslen(RootDirectory));
  wcsncat(filePath, FileName, wcslen(FileName));
}


//  static void GetDokanFilePath(PWCHAR filePath, LPCWSTR FileName) {
//    std::locale loc;
//    WCHAR DokanDrive[3];
//    DokanDrive[0] = std::use_facet< std::ctype<wchar_t> >(loc).widen(
//        maidsafe::ClientController::getInstance()->DriveLetter());
//    DokanDrive[1] = L':';
//    DokanDrive[2] = L'\0';
//    RtlZeroMemory(filePath, MAX_PATH);
//    wcsncpy(filePath, DokanDrive, wcslen(DokanDrive));
//    wcsncat(filePath, FileName, wcslen(FileName));
//  }


static void GetFilePath(std::string *filePathStr, LPCWSTR FileName) {
  std::ostringstream stm;
  const std::ctype<char> &ctfacet =
      std::use_facet< std::ctype<char> >(stm.getloc());
  for (size_t i = 0; i < wcslen(FileName); ++i)
    stm << ctfacet.narrow(FileName[i], 0);
  fs::path path_(stm.str());
  *filePathStr = base::TidyPath(path_.string());
}


static FILETIME GetFileTime(ULONGLONG linuxtime) {
  FILETIME filetime, ft;
  SYSTEMTIME systime;
  systime.wYear = 1970;
  systime.wMonth = systime.wDay = 1;
  systime.wHour = systime.wMinute = systime.wSecond = systime.wMilliseconds = 0;
  SystemTimeToFileTime(&systime, &ft);

  ULARGE_INTEGER g;
  g.HighPart = ft.dwHighDateTime;
  g.LowPart = ft.dwLowDateTime;

  g.QuadPart += linuxtime*10000000;

  filetime.dwHighDateTime = g.HighPart;
  filetime.dwLowDateTime = g.LowPart;

  return filetime;
}


#define WinCheckFlag(val, flag) if (val&flag) {DbgPrint(L"\t\t\" L#flag L\"\n"); }  // NOLINT


static int WinCreateFile(LPCWSTR FileName,
                         DWORD AccessMode,
                         DWORD ShareMode,
                         DWORD CreationDisposition,
                         DWORD FlagsAndAttributes,
                         PDOKAN_FILE_INFO   DokanFileInfo) {
  base::pd_scoped_lock guard(dokan_mutex);
#ifdef DEBUG
  printf("WinCreateFile\n");
#endif
  WCHAR filePath[MAX_PATH];
  HANDLE handle;
  std::string relPathStr, filePathStr, rootStr;
#ifdef DEBUG
  std::wcout << "FileName: " << FileName << std::endl;
#endif
  GetFilePath(filePath, FileName);
  //  std::wcout << "\tfilePath: " << filePath << std::endl;
  GetFilePath(&filePathStr, filePath);
  //  std::cout << "\tfilePathStr: " << filePathStr << std::endl;
  GetFilePath(&relPathStr, FileName);
  //  std::cout << "\trelPathStr: " << relPathStr << std::endl;
  GetFilePath(&rootStr, RootDirectory);
  //  std::cout << "\trootStr: " << rootStr << std::endl;
  // WCHAR DokanPath[MAX_PATH];
  // GetDokanFilePath(DokanPath, FileName);
  // build the required maidsafe branch dirs
  // (if we have the file already in the DA)
  bool created_cache_dir_ = false;
  fs::path rel_path_(relPathStr);
  fs::path branch_path_ = rel_path_.branch_path();

  //  std::cout << "relPathStr = " << relPathStr;
  //  std::cout << " and branch_path_ = " << branch_path_ << std::endl;

  // if path is not in an authorised dirs, return error "Permission denied"
  // TODO(Fraser#5#): set bool gui_private_share_ to true if gui has
  //                  requested a private share be set up.

  // CAN'T HAVE THIS CHECK HERE, BECAUSE THAT RENDERS ALL FILES INVISIBLE
//  bool gui_private_share_(false);
//  if (maidsafe::ClientController::getInstance()->ReadOnly(relPathStr,
//                                                          gui_private_share_))
//    return -5;

//  // if we're in root and not in one of the pre-loaded dirs, deny access
//  if (branch_path_.string()=="" && !(relPathStr=="\\" || relPathStr=="/" )) {
//    bool ok_=false;
//    for (int i=0; i<kRootSubdirSize; i++) {
//      if (relPathStr==base::TidyPath(kRootSubdir[i][0])) {
//        ok_=true;
//        break;
//      }
//    }
//    if (!ok_){
//      std::cout << "aaaaaaaaaaaaaaaaaaaaaa" << std::endl;
//      return -5; //  ERROR_ACCESS_DENIED
//    }
//  }
  fs::path full_branch_path_(rootStr);
  full_branch_path_ /= branch_path_;
  std::string ser_mdm = "";  // , ser_mdm_branch;
  maidsafe::MetaDataMap mdm;
  if (!maidsafe::ClientController::getInstance()->getattr(relPathStr,
                                                          ser_mdm)) {
    if (!fs::exists(full_branch_path_)) {
      try {
        fs::create_directories(full_branch_path_);
      }
      catch(const std::exception &e) {
#ifdef DEBUG
        printf("%s\n", e.what());
#endif
        DWORD error = GetLastError();
#ifdef DEBUG
        printf("bbbbbbbbbbbbbbbbbbb\n");
#endif
        return error * -1;
      }
      created_cache_dir_ = true;
    }
  }
  //  if it's a file, decrypt it to the maidsafe dir or else create dir
  if (!fs::exists(filePathStr) && ser_mdm != "") {
    mdm.ParseFromString(ser_mdm);
    if (mdm.type() < 3) {  // i.e. if this is a file
      printf("\tDecryption of %s: %i\n",
             relPathStr.c_str(),
             maidsafe::ClientController::getInstance()->read(relPathStr));
    } else if (mdm.type() == 4 || mdm.type() == 5) {  //  i.e. if this is a dir
#ifdef DEBUG
      printf("\tMaking dir %s\n", filePathStr.c_str());
#endif
      fs::create_directory(filePathStr);
    } else {
#ifdef DEBUG
      printf("cccccccccccccccccc\n");
#endif
      return -99999;
    }
  }
  WinCheckFlag(ShareMode, FILE_SHARE_READ);  // 0x00000001
  WinCheckFlag(ShareMode, FILE_SHARE_WRITE);  // 0x00000002
  WinCheckFlag(ShareMode, FILE_SHARE_DELETE);  // 0x00000004
  WinCheckFlag(AccessMode, GENERIC_READ);  // 0x80000000L
  WinCheckFlag(AccessMode, GENERIC_WRITE);  // 0x40000000L
  WinCheckFlag(AccessMode, GENERIC_EXECUTE);  // 0x20000000L
  WinCheckFlag(AccessMode, DELETE);  // 0x00010000L
  WinCheckFlag(AccessMode, FILE_READ_DATA);  // 0x0001 - file & pipe
  WinCheckFlag(AccessMode, FILE_READ_ATTRIBUTES);  // 0x0080 - all
  WinCheckFlag(AccessMode, FILE_READ_EA);  // 0x0008 - file & directory
  WinCheckFlag(AccessMode, READ_CONTROL);  // 0x00020000L
  WinCheckFlag(AccessMode, FILE_WRITE_DATA);  // 0x0002 - file & pipe
  WinCheckFlag(AccessMode, FILE_WRITE_ATTRIBUTES);  // 0x0100 - all
  WinCheckFlag(AccessMode, FILE_WRITE_EA);  // 0x0010 - file & directory
  WinCheckFlag(AccessMode, FILE_APPEND_DATA);  // 0x0004 - file
  WinCheckFlag(AccessMode, WRITE_DAC);  // 0x00040000L
  WinCheckFlag(AccessMode, WRITE_OWNER);  // 0x00080000L
  WinCheckFlag(AccessMode, SYNCHRONIZE);  // 0x00100000L
  WinCheckFlag(AccessMode, FILE_EXECUTE);  // 0x0020 - file
  WinCheckFlag(AccessMode, STANDARD_RIGHTS_READ);  // 0x00020000L
  WinCheckFlag(AccessMode, STANDARD_RIGHTS_WRITE);  // 0x00020000L
  WinCheckFlag(AccessMode, STANDARD_RIGHTS_EXECUTE);  // 0x00020000L
  if (fs::is_directory(filePathStr)) {
    DokanFileInfo->IsDirectory = TRUE;
//    handle = CreateFile(
//      filePath,
//      0,
//      FILE_SHARE_READ|FILE_SHARE_WRITE,
//      NULL,
//      OPEN_EXISTING,
//      FILE_FLAG_BACKUP_SEMANTICS,
//      NULL);
//    if (handle == INVALID_HANDLE_VALUE) {
//      DWORD error = GetLastError();
//      DbgPrint(L"\t\terror code = %ld\n\n", error);
//      std::cout << "fdsfdsafdsfdsf" << std::endl;
//      return error * -1;
//    }
//    DokanFileInfo->Context = (ULONG64)handle;
    return 0;
//  } else if (rel_path_.leaf() == "Thumbs.db" ||
//             rel_path_.leaf() == "desktop.ini") {
//    // TODO(Haiyang): treat thumbs.db in a proper way, this is temp solution!
//    std::cout << "Creating Thumbs.db, desktop.ini requested. No way!";
//    std::cout << std::endl;
//    return -5;
  }
  handle = CreateFile(
    filePath,
    AccessMode,  // GENERIC_READ|GENERIC_WRITE|GENERIC_EXECUTE,
    ShareMode,
    NULL,  // security attribute
    CreationDisposition,
    FlagsAndAttributes,  // |FILE_FLAG_NO_BUFFERING,
    NULL);  // template file handle

  if (handle == INVALID_HANDLE_VALUE) {
    DWORD error = GetLastError();
    if (created_cache_dir_ && fs::exists(full_branch_path_)) {
      fs::remove(full_branch_path_);
    }
#ifdef DEBUG
    printf("dddddddddddddddd\n");
    std::wcout << error << std::endl;
#endif
    if (CreationDisposition == OPEN_EXISTING)
      printf("OPEN_EXISTING\n");
    if (CreationDisposition == CREATE_NEW)
      printf("CREATE_NEW\n");
    return error * -1;  // error codes are negated val of Win Sys Error codes
  }
  DokanFileInfo->Context = (ULONG64)handle;
  to_delete_.push_back(relPathStr);
//  std::list<std::string>::iterator dit;
//  std::cout << "FileNames for deletion: ";
//  for (dit = to_delete_.begin(); dit != to_delete_.end(); ++dit) {
//   std::cout << *dit << "\t";
//  }
//  std::cout << std::endl << std::endl;

  if (CreationDisposition != CREATE_NEW)
    return 0;
#ifdef DEBUG
  printf("Encyption decider.\n");
#endif
  std::list<ULONG64>::iterator it;
  for (it = to_encrypt_.begin(); it != to_encrypt_.end(); ++it) {
    if (*it == DokanFileInfo->Context)
      return 0;
  }
#ifdef DEBUG
  printf("Adding to encryption list\n");
#endif
  to_encrypt_.push_back(DokanFileInfo->Context);
  return 0;
}


static int WinCreateDirectory(LPCWSTR FileName,
                              PDOKAN_FILE_INFO) {
  base::pd_scoped_lock guard(dokan_mutex);
#ifdef DEBUG
  printf("WinCreateDirectory\n");
  std::wcout << "FileName: " << FileName << std::endl;
#endif
  WCHAR filePath[MAX_PATH];
  GetFilePath(filePath, FileName);
  if (wcslen(FileName) == 1)
    return 0;
  std::string dir_path_str;
  GetFilePath(&dir_path_str, filePath);
  fs::path dir_path(dir_path_str);
#ifdef DEBUG
  printf("dir_path_str: %s\n", dir_path.string().c_str());
#endif

//  if (!fs::exists(dir_path)) {

  // must use CreateDirectory rather than boost::filesystem::create_directories
  // to avoid removing existing files from directory
  if (!CreateDirectory(filePath, NULL)) {
    DWORD error = GetLastError();
#ifdef DEBUG
    printf("ccccccccccccc\n");
#endif
    return error * -1;  // error code is negated val of Win Sys Error code
  }
//  }
  std::string filePathStr, relPathStr;
  GetFilePath(&filePathStr, filePath);
  GetFilePath(&relPathStr, FileName);
#ifdef DEBUG
  printf("\tms_mkdir PATH: %s\n", relPathStr.c_str());
#endif
  bool gui_private_share_(false);
  if (maidsafe::ClientController::getInstance()->ReadOnly(relPathStr,
      gui_private_share_))
    return -5;

  int n = maidsafe::ClientController::getInstance()->mkdir(relPathStr);
  if (n != 0)
    return n;
  return 0;
}


static int WinOpenDirectory(LPCWSTR FileName, PDOKAN_FILE_INFO DokanFileInfo) {
  base::pd_scoped_lock guard(dokan_mutex);
  printf("WinOpenDirectory\n");
  std::wcout << "FileName: " << FileName << std::endl;
  WCHAR filePath[MAX_PATH];
  HANDLE handle;
  DWORD attr;
  GetFilePath(filePath, FileName);
  attr = GetFileAttributes(filePath);
  if (attr == INVALID_FILE_ATTRIBUTES) {
    DWORD error = GetLastError();
#ifdef DEBUG
    printf("\terror code = %ld\n\n", error);
#endif
    return error * -1;
  }
  if (!(attr & FILE_ATTRIBUTE_DIRECTORY)) {
    return -1;
  }

  // WCHAR DokanPath[MAX_PATH];
  // GetDokanFilePath(DokanPath, FileName);
  handle = CreateFile(filePath,
                      0,
                      FILE_SHARE_READ|FILE_SHARE_WRITE,
                      NULL,
                      OPEN_EXISTING,
                      FILE_FLAG_BACKUP_SEMANTICS,
                      NULL);
  if (handle == INVALID_HANDLE_VALUE) {
    DWORD error = GetLastError();
    DbgPrint(L"\t\terror code = %ld\n\n", error);
    printf("bbbbbbbbbbbbbb\n");
    return error * -1;
  }
  DokanFileInfo->Context = (ULONG64)handle;
  return 0;
}


static int WinCloseFile(LPCWSTR FileName, PDOKAN_FILE_INFO DokanFileInfo) {
  base::pd_scoped_lock guard(dokan_mutex);
  printf("WinCloseFile\n");
  std::wcout << "\tFileName: " << FileName << std::endl;
  WCHAR filePath[MAX_PATH];
  std::string filePathStr;
  GetFilePath(filePath, FileName);
  GetFilePath(&filePathStr, filePath);
  if (DokanFileInfo->Context) {
    CloseHandle((HANDLE)DokanFileInfo->Context);
    DokanFileInfo->Context = 0;
  }
  return 0;
}


static int WinCleanup(LPCWSTR FileName, PDOKAN_FILE_INFO DokanFileInfo) {
  base::pd_scoped_lock guard(dokan_mutex);
  printf("WinCleanup\n");
  std::wcout << "FileName: " << FileName << std::endl;
  WCHAR filePath[MAX_PATH];
  std::string filePathStr, relPathStr;
  GetFilePath(filePath, FileName);
  GetFilePath(&filePathStr, filePath);
  GetFilePath(&relPathStr, FileName);
  // WCHAR DokanPath[MAX_PATH];
  // GetDokanFilePath(DokanPath, FileName);
  bool encrypt_ = false;
  std::list<ULONG64>::iterator it;
  for (it = to_encrypt_.begin(); it != to_encrypt_.end(); ++it) {
    if (*it == DokanFileInfo->Context) {
      encrypt_ = true;
      to_encrypt_.erase(it);
      break;
    }
  }
  if (DokanFileInfo->Context) {
    CloseHandle((HANDLE)DokanFileInfo->Context);
    DokanFileInfo->Context = 0;
    if (DokanFileInfo->DeleteOnClose) {
      if (DokanFileInfo->IsDirectory) {
        RemoveDirectory(filePath);
      } else {
        DeleteFile(filePath);
      }
    }
    if (relPathStr == "\\" || relPathStr == "/" )
      return 0;
    if (encrypt_) {
      if (maidsafe::ClientController::getInstance()->write(relPathStr) != 0) {
        printf("Encryption failed.\n");
        return -errno;
      } else {
        printf("Encryption succeeded.\n");
      }
    }
//     if (!to_delete_.size())
//       return 0;
//     bool delete_ = false;
//     for (std::list<std::string>::iterator dit = to_delete_.begin();
//          dit != to_delete_.end();
//          ++dit) {
//       if (*dit == relPathStr) {
//         to_delete_.erase(dit);
//         delete_ = true;
//         break;
//       }
//     }
//     if (!delete_)
//       return 0;
//     for (std::list<std::string>::iterator dit = to_delete_.begin();
//          dit != to_delete_.end();
//          ++dit) {
//       if (*dit == relPathStr)
//         return 0;
//     }
//     if (fs::is_directory(filePathStr)) {
//       std::cout << "THIS IS A DIRECTORY - I'M DONE HERE Y'ALL" << std::endl;
//       return 0;
//     }
//     std::cout << "Removing " << filePathStr << std::endl;
    // fs::remove(filePathStr);
  return 0;

  } else {
//    if (fs::is_directory(filePathStr)) {
//      return 0;
//    } else {
//      std::cout << "rrrrrrrrrrrrrrrrrrrrrrrrrr" << std::endl;
      return -1;
//    }
  }
  return 0;
}


static int WinReadFile(LPCWSTR FileName,
                       LPVOID Buffer,
                       DWORD BufferLength,
                       LPDWORD ReadLength,
                       LONGLONG Offset,
                       PDOKAN_FILE_INFO DokanFileInfo) {
  base::pd_scoped_lock guard(dokan_mutex);
  printf("WinReadFile\n");
  std::wcout << "FileName: " << FileName << std::endl;
  WCHAR   filePath[MAX_PATH];
  HANDLE   handle = (HANDLE)DokanFileInfo->Context;
  ULONG   offset = (ULONG)Offset;
  BOOL   opened = FALSE;
  GetFilePath(filePath, FileName);
  if (!handle || handle == INVALID_HANDLE_VALUE) {
    handle = CreateFile(filePath,
                        GENERIC_READ,
                        FILE_SHARE_READ,
                        NULL,
                        OPEN_EXISTING,
                        0,
                        NULL);
    if (handle == INVALID_HANDLE_VALUE) {
      printf("ddddddddddddddddd\n");
      DbgPrint(L"\t\tCreateFile error : %d\n\n", GetLastError());
      return -1;
    }
    opened = TRUE;
  }
  if (SetFilePointer(handle, offset, NULL, FILE_BEGIN) == 0xFFFFFFFF) {
    DbgPrint(L"\t\tseek error, offset = %d\n\n", offset);
    printf("mmmmmmmmmmmm\n");
    if (opened)
      CloseHandle(handle);
    return -1;
  }
  if (!ReadFile(handle, Buffer, BufferLength, ReadLength, NULL)) {
    DbgPrint(L"\t\tread error = %u, buffer length = %d, read length = %d\n\n",
             GetLastError(),
             BufferLength,
             *ReadLength);
    printf("nnnnnnnnnnnnn\n");
    if (opened)
      CloseHandle(handle);
    return -1;
  } else {
      printf("xxxxxxxxxxxx\n");
      // DbgPrint(L"\t\tread %d, offset %d\n\n", *ReadLength, offset);
  }
  if (opened)
    CloseHandle(handle);
  return 0;
}

static int WinWriteFile(LPCWSTR FileName,
                        LPCVOID Buffer,
                        DWORD NumberOfBytesToWrite,
                        LPDWORD NumberOfBytesWritten,
                        LONGLONG Offset,
                        PDOKAN_FILE_INFO DokanFileInfo) {
  base::pd_scoped_lock guard(dokan_mutex);
  printf("WinWriteFile\n");
  std::wcout << "FileName: " << FileName << std::endl;
  WCHAR filePath[MAX_PATH];
  HANDLE handle = (HANDLE)DokanFileInfo->Context;
  ULONG offset = (ULONG)Offset;
  BOOL opened = FALSE;
  GetFilePath(filePath, FileName);
  //  reopen the file
  if (!handle || handle == INVALID_HANDLE_VALUE) {
      // DbgPrint(L"\t\tinvalid handle, cleanuped?\n");
    handle = CreateFile(filePath,
                        GENERIC_WRITE,
                        FILE_SHARE_WRITE,
                        NULL,
                        OPEN_EXISTING,
                        0,
                        NULL);
    if (handle == INVALID_HANDLE_VALUE) {
      printf("eeeeeeeeeeeeeeeee\n");
      return -1;
    }
    opened = TRUE;
  }
  if (SetFilePointer(handle, offset, NULL, FILE_BEGIN) ==
      INVALID_SET_FILE_POINTER) {
    printf("fffffffffffffffffff\n");
    return -1;
  }
  if (!WriteFile(handle,
                 Buffer,
                 NumberOfBytesToWrite,
                 NumberOfBytesWritten,
                 NULL)) {
    printf("ggggggggggggggg\n");
    return -1;
  }
  //  close the file when it is reopened
  if (opened)
    CloseHandle(handle);
  std::list<ULONG64>::reverse_iterator rit;
  for (rit = to_encrypt_.rbegin(); rit != to_encrypt_.rend(); ++rit) {
    if (*rit == DokanFileInfo->Context)
      return 0;
  }
  to_encrypt_.push_back(DokanFileInfo->Context);
  return 0;
}


static int WinFlushFileBuffers(LPCWSTR FileName,
                               PDOKAN_FILE_INFO DokanFileInfo) {
  base::pd_scoped_lock guard(dokan_mutex);
  printf("WinFlushFileBuffers\n");
  std::wcout << "FileName: " << FileName << std::endl;
  WCHAR filePath[MAX_PATH];
  HANDLE handle = (HANDLE)DokanFileInfo->Context;
  GetFilePath(filePath, FileName);
  std::string filePathStr;
  GetFilePath(&filePathStr, filePath);
  if (!handle || handle == INVALID_HANDLE_VALUE)
    return 0;
  if (!FlushFileBuffers(handle))
    return -1;
  return 0;
}


static int WinGetFileInformation(
    LPCWSTR FileName,
    LPBY_HANDLE_FILE_INFORMATION HandleFileInformation,
    PDOKAN_FILE_INFO DokanFileInfo) {
  base::pd_scoped_lock guard(dokan_mutex);
  printf("WinGetFileInformation\n");
  std::wcout << "FileName: " << FileName << std::endl;
  WCHAR filePath[MAX_PATH];
  HANDLE handle = (HANDLE)DokanFileInfo->Context;
  BOOL opened = FALSE;
  GetFilePath(filePath, FileName);
  std::string relPathStr;
  GetFilePath(&relPathStr, FileName);
  if (!handle || handle == INVALID_HANDLE_VALUE) {
    //  If CreateDirectory returned FILE_ALREADY_EXISTS and
    //  it is called with FILE_OPEN_IF, that handle must be opened.
    printf("ooooooooooooooooo\n");
    handle = CreateFile(
      filePath,
      0,
      FILE_SHARE_READ,
      NULL,
      OPEN_EXISTING,
      FILE_FLAG_BACKUP_SEMANTICS,
      NULL);
    if (handle == INVALID_HANDLE_VALUE) {
      printf("eeeeeeeeeeeeeee\n");
      return -1;
    }
    opened = TRUE;
  }
  if (!GetFileInformationByHandle(handle, HandleFileInformation)) {
    // FileName is a root directory
    // in this case, FindFirstFile can't get directory information
    if (relPathStr == "\\" || relPathStr == "/") {
      HandleFileInformation->dwFileAttributes = GetFileAttributes(filePath);
    } else {
      maidsafe::MetaDataMap mdm;
      std::string ser_mdm;
      WIN32_FIND_DATAW find;
      ZeroMemory(&find, sizeof(WIN32_FIND_DATAW));
      handle = FindFirstFile(filePath, &find);
      if (handle == INVALID_HANDLE_VALUE) {
        printf("ffffffffffffff\n");
        return -1;
      }
      if (maidsafe::ClientController::getInstance()->getattr(relPathStr,
                                                             ser_mdm)) {
        printf("ggggggggggggggggggggggggg\n");
        return -1;
      }
      mdm.ParseFromString(ser_mdm);
      if (mdm.type() == maidsafe::EMPTY_FILE ||
          mdm.type() == maidsafe::REGULAR_FILE ||
          mdm.type() == maidsafe::SMALL_FILE) {
        HandleFileInformation->dwFileAttributes = find.dwFileAttributes;
        // HandleFileInformation->dwFileAttributes = 32;
            // find.dwFileAttributes;
        HandleFileInformation->ftCreationTime =
            GetFileTime(mdm.creation_time());  // find.ftCreationTime;
        HandleFileInformation->ftLastAccessTime =
            GetFileTime(mdm.last_access());  // find.ftLastWriteTime;
        HandleFileInformation->ftLastWriteTime =
            GetFileTime(mdm.last_modified());  // find.ftLastWriteTime;
        HandleFileInformation->nFileSizeHigh =
            mdm.file_size_high();  // find.nFileSizeHigh;
        HandleFileInformation->nFileSizeLow =
            mdm.file_size_low();  // find.nFileSizeLow;
        printf("\t\tFindFiles OK\n");
      } else if (mdm.type() == maidsafe::EMPTY_DIRECTORY ||
                 mdm.type() == maidsafe::DIRECTORY) {
      HandleFileInformation->dwFileAttributes = find.dwFileAttributes;
        // HandleFileInformation->dwFileAttributes = 16;
            // find.dwFileAttributes;
        HandleFileInformation->ftCreationTime =
            GetFileTime(mdm.creation_time());  // find.ftCreationTime;
        HandleFileInformation->ftLastAccessTime =
            GetFileTime(mdm.last_access());  // find.ftLastWriteTime;
        HandleFileInformation->ftLastWriteTime =
            GetFileTime(mdm.last_modified());  // find.ftLastWriteTime;
        HandleFileInformation->nFileSizeHigh = 0;  // find.nFileSizeHigh;
        HandleFileInformation->nFileSizeLow = 0;  // find.nFileSizeLow;
        DokanFileInfo->IsDirectory = TRUE;
        printf("\t\tFindFiles OK\n");
      }
    }
  }
  if (opened)
    CloseHandle(handle);
  return 0;
}


static int WinFindFiles(LPCWSTR FileName,
                        PFillFindData FillFindData,  // function postatic inter
                        PDOKAN_FILE_INFO DokanFileInfo) {
  base::pd_scoped_lock guard(dokan_mutex);
  printf("WinFindFiles\n");
  std::wcout << "FileName: " << FileName << std::endl;
  WCHAR filePath[MAX_PATH];
  WIN32_FIND_DATAW findData;
  PWCHAR yenStar = const_cast<PWCHAR>(L"\\*");
  int count = 0;
  GetFilePath(filePath, FileName);
  std::string filePathStr, relPathStr;
  GetFilePath(&filePathStr, filePath);
  GetFilePath(&relPathStr, FileName);
  wcscat(filePath, yenStar);
  std::map<std::string, maidsafe::itemtype> children;
  maidsafe::ClientController::getInstance()->readdir(relPathStr, children);
  while (!children.empty()) {
    std::string s = children.begin()->first;
    maidsafe::itemtype ityp = children.begin()->second;
    maidsafe::MetaDataMap mdm;
    std::string ser_mdm;
    fs::path path_(relPathStr);
    path_ /= s;
    if (maidsafe::ClientController::getInstance()->getattr(path_.string(),
                                                           ser_mdm)) {
        printf("hhhhhhhhhhhhhhh\n");
      return -1;
    }
    mdm.ParseFromString(ser_mdm);
    const char *charpath(s.c_str());
      memset(&findData, 0, sizeof(WIN32_FIND_DATAW));
    if (ityp == maidsafe::DIRECTORY || ityp == maidsafe::EMPTY_DIRECTORY) {
      findData.dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
      findData.ftCreationTime = GetFileTime(mdm.creation_time());
      findData.ftLastAccessTime = GetFileTime(mdm.last_access());
      findData.ftLastWriteTime = GetFileTime(mdm.last_modified());
      findData.nFileSizeHigh = mdm.file_size_high();
      findData.nFileSizeLow = mdm.file_size_low();
      // findData.cFileName[ MAX_PATH ];
      MultiByteToWideChar(CP_ACP,
                          0,
                          charpath,
                          strlen(charpath) + 1,
                          findData.cFileName,
                          MAX_PATH);
      std::wcout << "\tchild: " << findData.cFileName << "\ttype: ";
      std::wcout << ityp << std::endl;
      findData.cAlternateFileName[ 14 ] = NULL;
      // create children directories if they don't exist
      std::string root_dir;
      GetFilePath(&root_dir, RootDirectory);
      fs::path sub_dir(root_dir);
      sub_dir /= path_;
      if (!fs::exists(sub_dir)) {
        try {
          fs::create_directories(sub_dir);
        }
        catch(const std::exception &e) {
          printf("%s\n", e.what());
          DWORD error = GetLastError();
          printf("iiiiiiiiiiiiiii\n");
          return error * -1;
        }
      }
    } else {
      findData.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
      findData.ftCreationTime = GetFileTime(mdm.creation_time());
      findData.ftLastAccessTime = GetFileTime(mdm.last_access());
      findData.ftLastWriteTime = GetFileTime(mdm.last_modified());
      findData.nFileSizeHigh = mdm.file_size_high();
      findData.nFileSizeLow = mdm.file_size_low();
      // findData.cFileName[MAX_PATH];
      MultiByteToWideChar(CP_ACP,
                          0,
                          charpath,
                          strlen(charpath) + 1,
                          findData.cFileName,
                          MAX_PATH);
      std::wcout << "\tchild: " << findData.cFileName << "\ttype: ";
      std::wcout << ityp << std::endl;
      findData.cAlternateFileName[ 14 ] = NULL;
    }
    children.erase(children.begin());
    FillFindData(&findData, DokanFileInfo);
    count++;
  }
  return 0;
}


static int WinDeleteFile(LPCWSTR FileName, PDOKAN_FILE_INFO) {
  base::pd_scoped_lock guard(dokan_mutex);
  printf("WinDeleteFile\n");
  std::wcout << "FileName: " << FileName << std::endl;
  WCHAR filePath[MAX_PATH];
  // HANDLE handle = (HANDLE)DokanFileInfo->Context;
  GetFilePath(filePath, FileName);
  std::string relPathStr;
  GetFilePath(&relPathStr, FileName);
  if (to_delete_.size()) {
    for (std::list<std::string>::iterator dit = to_delete_.begin();
         dit != to_delete_.end();
         ++dit) {
      if (*dit == relPathStr) {
        to_delete_.erase(dit);
        break;
      }
    }
  }

  bool gui_private_share_(false);
  if (maidsafe::ClientController::getInstance()->ReadOnly(relPathStr,
      gui_private_share_))
    return -5;

  if (maidsafe::ClientController::getInstance()->unlink(relPathStr) != 0) {
    printf("jjjjjjjjjjjjjjjjjjjj\n");
    // return -errno;
  }
  return 0;
}


static int WinDeleteDirectory(LPCWSTR FileName,
                              PDOKAN_FILE_INFO) {
  base::pd_scoped_lock guard(dokan_mutex);
  printf("WinDeleteDirectory\n");
  std::wcout << "FileName: " << FileName << std::endl;
  WCHAR filePath[MAX_PATH];
  // HANDLE handle = (HANDLE)DokanFileInfo->Context;
  GetFilePath(filePath, FileName);
  std::string relPathStr;
  GetFilePath(&relPathStr, FileName);
  std::map<std::string, maidsafe::itemtype> children;
  if (maidsafe::ClientController::getInstance()->readdir(relPathStr, children))
    return -errno;
  printf("Directory %s has %i children.\n\n\n",
         relPathStr.c_str(),
         children.size());
  if (children.size()) {
    DbgPrint(L"\t\terror code = 145\n\n");
    printf("jjjjjjjjjjjjjj\n");
    return -145;
  }

  bool gui_private_share_(false);
  if (maidsafe::ClientController::getInstance()->ReadOnly(relPathStr,
      gui_private_share_))
    return -5;

  if (!RemoveDirectory(filePath)) {
    DWORD error = GetLastError();
    DbgPrint(L"\t\terror code = %ld\n\n", error);
#ifdef DEBUG
    printf("kkkkkkkkkkkkkk\n");
#endif
    return error * -1;
  }
  if (maidsafe::ClientController::getInstance()->rmdir(relPathStr) != 0) {
#ifdef DEBUG
    printf("lllllllllllllllll\n");
#endif
    return -errno;
  }
  return 0;
}


static int WinMoveFile(LPCWSTR FileName,
                       LPCWSTR NewFileName,
                       BOOL ReplaceIfExisting,
                       PDOKAN_FILE_INFO DokanFileInfo) {
  base::pd_scoped_lock guard(dokan_mutex);
#ifdef DEBUG
  printf("WinMovefile\n");
  std::wcout << "FileName: " << FileName << std::endl;
#endif
  WCHAR filePath[MAX_PATH];
  WCHAR newFilePath[MAX_PATH];
  BOOL status;
  GetFilePath(filePath, FileName);
  GetFilePath(newFilePath, NewFileName);
  std::string o_path_;
  std::string n_path_;
  GetFilePath(&o_path_, FileName);
  GetFilePath(&n_path_, NewFileName);
  if (DokanFileInfo->Context) {
    //  should close? or rename at closing?
    CloseHandle((HANDLE)DokanFileInfo->Context);
    DokanFileInfo->Context = 0;
  }

  bool gui_private_share_(false);
  if (maidsafe::ClientController::getInstance()->ReadOnly(n_path_,
      gui_private_share_))
    return -5;


  if (maidsafe::ClientController::getInstance()->rename(o_path_, n_path_) != 0)
    return -errno;
  if (ReplaceIfExisting) {
    status = MoveFileEx(filePath, newFilePath, MOVEFILE_REPLACE_EXISTING);
  } else {
    status = MoveFile(filePath, newFilePath);
  }
  if (status == FALSE) {
    DWORD error = GetLastError();
    DbgPrint(L"\t\tMoveFile failed status = %d, code = %d\n", status, error);
    return -static_cast<int>(error);
  }
  //  Need to move file in list of files needing deleted after cleanup.
#ifdef DEBUG
  printf("FileNames in movefile for deletion: ");
#endif
  if (!to_delete_.size())
    return status == TRUE ? 0 : -1;
  for (std::list<std::string>::iterator dit = to_delete_.begin();
       dit != to_delete_.end();
       ++dit) {
    if (*dit == o_path_) {
      to_delete_.erase(dit);
      to_delete_.insert(dit, n_path_);
      break;
    }
  }
  return status == TRUE ? 0 : -1;
}


static int WinLockFile(LPCWSTR FileName,
                       LONGLONG ByteOffset,
                       LONGLONG Length,
                       PDOKAN_FILE_INFO DokanFileInfo) {
  base::pd_scoped_lock guard(dokan_mutex);
  printf("WinLockFile\n");
  std::wcout << "FileName: " << FileName << std::endl;
  WCHAR filePath[MAX_PATH];
  HANDLE handle;
  LARGE_INTEGER offset;
  LARGE_INTEGER length;
  GetFilePath(filePath, FileName);
  handle = (HANDLE)DokanFileInfo->Context;
  if (!handle || handle == INVALID_HANDLE_VALUE) {
    printf("ooooooooooooooooooooo\n");
    return -1;
  }
  length.QuadPart = Length;
  offset.QuadPart = ByteOffset;
  if (LockFile(handle,
               offset.HighPart,
               offset.LowPart,
               length.HighPart,
               length.LowPart)) {
    return 0;
  } else {
    printf("ppppppppppppppppp\n");
    return -1;
  }
  return 0;
}


static int WinSetEndOfFile(LPCWSTR FileName,
                           LONGLONG ByteOffset,
                           PDOKAN_FILE_INFO DokanFileInfo) {
  base::pd_scoped_lock guard(dokan_mutex);
  printf("WinSetEndofFile\n");
  std::wcout << "FileName: " << FileName << std::endl;
  WCHAR filePath[MAX_PATH];
  HANDLE handle;
  LARGE_INTEGER offset;
  GetFilePath(filePath, FileName);
  handle = (HANDLE)DokanFileInfo->Context;
  if (!handle || handle == INVALID_HANDLE_VALUE) {
    printf("dddddddddddddddddddddddddddddddddd\n");
    return -1;
  }
  offset.QuadPart = ByteOffset;
  if (!SetFilePointerEx(handle, offset, NULL, FILE_BEGIN)) {
    printf("rrrrrrrrrrrrrrrrrr\n");
    return GetLastError() * -1;
  }
  if (!SetEndOfFile(handle)) {
    DWORD error = GetLastError();
    printf("\t\terror code = %ld\n\n", error);
    printf("ttttttttttttttttt\n");
    return error * -1;
  }
  return 0;
}


static int WinSetAllocationSize(LPCWSTR FileName,
                                LONGLONG AllocSize,
                                PDOKAN_FILE_INFO DokanFileInfo) {
  base::pd_scoped_lock guard(dokan_mutex);
  printf("WinSetAllocationSize\n");
  std::wcout << "FileName: " << FileName << std::endl;
  WCHAR filePath[MAX_PATH];
  HANDLE handle;
  LARGE_INTEGER fileSize;
  GetFilePath(filePath, FileName);
  handle = (HANDLE)DokanFileInfo->Context;
  if (!handle || handle == INVALID_HANDLE_VALUE) {
    printf("\tinvalid handle\n\n");
    return -1;
  }
  if (GetFileSizeEx(handle, &fileSize)) {
    if (AllocSize < fileSize.QuadPart) {
      fileSize.QuadPart = AllocSize;
      if (SetFilePointerEx(handle, fileSize, NULL, FILE_BEGIN)) {
        printf("\tSetAllocationSize: SetFilePointer error: %ld",
               GetLastError());
        printf(", offfset = %I64lld\n\n", AllocSize);
        return GetLastError() * -1;
      }
      if (!SetEndOfFile(handle)) {
        DWORD error = GetLastError();
        printf("\terror code = %ld\n\n", error);
        return error * -1;
      }
    }
  } else {
    DWORD error = GetLastError();
    printf("\terror code = %ld\n\n", error);
    return error * -1;
  }
  return 0;
}


static int WinSetFileAttributes(LPCWSTR FileName,
                                DWORD FileAttributes,
                                PDOKAN_FILE_INFO) {
  base::pd_scoped_lock guard(dokan_mutex);
  printf("WinSetFileAttributes\n");
  std::wcout << "FileName: " << FileName << std::endl;
  WCHAR filePath[MAX_PATH];
  GetFilePath(filePath, FileName);
  if (!SetFileAttributes(filePath, FileAttributes)) {
    DWORD error = GetLastError();
    DbgPrint(L"\t\terror code = %ld\n\n", error);
    printf("uuuuuuuuuuuuuuuuuuuu\n");
    return error * -1;
  }
  return 0;
}


static int WinSetFileTime(LPCWSTR FileName,
                          CONST FILETIME *CreationTime,
                          CONST FILETIME *LastAccessTime,
                          CONST FILETIME *LastWriteTime,
                          PDOKAN_FILE_INFO DokanFileInfo) {
  base::pd_scoped_lock guard(dokan_mutex);
  printf("WinSetFileTime\n");
  std::wcout << "FileName: " << FileName << std::endl;
  WCHAR   filePath[MAX_PATH];
  HANDLE   handle;
  GetFilePath(filePath, FileName);
  handle = (HANDLE)DokanFileInfo->Context;
  if (!handle || handle == INVALID_HANDLE_VALUE) {
    printf("vvvvvvvvvvvvvvvvvvv\n");
    return -1;
  }
  if (!SetFileTime(handle, CreationTime, LastAccessTime, LastWriteTime)) {
    DWORD error = GetLastError();
    printf("wwwwwwwwwwwwwwwww\n");
    DbgPrint(L"\t\terror code = %ld\n\n", error);
    return error * -1;
  }
  return 0;
}


static int WinUnlockFile(LPCWSTR FileName,
                         LONGLONG ByteOffset,
                         LONGLONG Length,
                         PDOKAN_FILE_INFO DokanFileInfo) {
  base::pd_scoped_lock guard(dokan_mutex);
  printf("WinUnLockFile\n");
  std::wcout << "FileName: " << FileName << std::endl;
  WCHAR filePath[MAX_PATH];
  HANDLE handle;
  LARGE_INTEGER length;
  LARGE_INTEGER offset;
  GetFilePath(filePath, FileName);
  handle = (HANDLE)DokanFileInfo->Context;
  if (!handle || handle == INVALID_HANDLE_VALUE) {
    printf("xxxxxxxxxxxxxxxx\n");
    return -1;
  }
  length.QuadPart = Length;
  offset.QuadPart = ByteOffset;
  if (UnlockFile(handle,
                 offset.HighPart,
                 offset.LowPart,
                 length.HighPart,
                 length.LowPart)) {
    return 0;
  } else {
    printf("yyyyyyyyyyyyyyyyy\n");
    return -1;
  }
  return 0;
}


static int WinUnmount(PDOKAN_FILE_INFO) {
  DbgPrint(L"\tUnmount\n");
  return 0;
}


static void CallMount(char drive) {
  printf("In CallMount()\n");
  int status;
  fs_w_fuse::PDOKAN_OPERATIONS Dokan_Operations =
      (fs_w_fuse::PDOKAN_OPERATIONS)malloc(sizeof(fs_w_fuse::DOKAN_OPERATIONS));
  fs_w_fuse::PDOKAN_OPTIONS Dokan_Options =
      (fs_w_fuse::PDOKAN_OPTIONS)malloc(sizeof(fs_w_fuse::DOKAN_OPTIONS));

  ZeroMemory(Dokan_Options, sizeof(fs_w_fuse::DOKAN_OPTIONS));

  file_system::FileSystem fsys_;
  std::string msHome_ = fsys_.MaidsafeHomeDir();
//   // repace '/' with '\\'
//   for (std::string::iterator it=msHome_.begin(); it != msHome_.end(); it++){
//     if ((*it) == '/')
//       msHome_.replace(it, it+1, "\\");
//   }
  printf("msHome= %s\n", msHome_.c_str());
  mbstowcs(fs_w_fuse::RootDirectory, msHome_.c_str(), msHome_.size());
  wprintf(L"RootDirectory: %ls\n", fs_w_fuse::RootDirectory);

  g_DebugMode = TRUE;
  g_UseStdErr = FALSE;

  Dokan_Options->DriveLetter = drive;
  Dokan_Options->ThreadCount = 3;
  if (g_DebugMode)
    Dokan_Options->Options |= DOKAN_OPTION_DEBUG;
  if (g_UseStdErr)
    Dokan_Options->Options |= DOKAN_OPTION_STDERR;
  Dokan_Options->Options |= DOKAN_OPTION_KEEP_ALIVE;

  ZeroMemory(Dokan_Operations, sizeof(fs_w_fuse::DOKAN_OPERATIONS));
  Dokan_Operations->CreateFile = WinCreateFile;
  Dokan_Operations->OpenDirectory = WinOpenDirectory;
  Dokan_Operations->CreateDirectory = WinCreateDirectory;
  Dokan_Operations->Cleanup = WinCleanup;
  Dokan_Operations->CloseFile = WinCloseFile;
  Dokan_Operations->ReadFile = WinReadFile;
  Dokan_Operations->WriteFile = WinWriteFile;
  Dokan_Operations->FlushFileBuffers = WinFlushFileBuffers;
  Dokan_Operations->GetFileInformation = WinGetFileInformation;
  Dokan_Operations->FindFiles = WinFindFiles;
  Dokan_Operations->FindFilesWithPattern = NULL;
  Dokan_Operations->SetFileAttributes = WinSetFileAttributes;
  Dokan_Operations->SetFileTime = WinSetFileTime;
  Dokan_Operations->DeleteFile = WinDeleteFile;
  Dokan_Operations->DeleteDirectory = WinDeleteDirectory;
  Dokan_Operations->MoveFile = WinMoveFile;
  Dokan_Operations->SetEndOfFile = WinSetEndOfFile;
  Dokan_Operations->SetAllocationSize = WinSetAllocationSize;
  Dokan_Operations->LockFile = WinLockFile;
  Dokan_Operations->UnlockFile = WinUnlockFile;
  Dokan_Operations->GetDiskFreeSpace = NULL;
  Dokan_Operations->GetVolumeInformation = NULL;
  Dokan_Operations->Unmount = WinUnmount;

  status = DokanMain(Dokan_Options, Dokan_Operations);
  maidsafe::SessionSingleton::getInstance()->SetMounted(status);
  switch (status) {
    case DOKAN_SUCCESS:
      printf("Dokan Success\n");
      break;
    case DOKAN_ERROR:
      printf("Dokan Error\n");
      break;
    case DOKAN_DRIVE_LETTER_ERROR:
      printf("Dokan Bad Drive letter\n");
      break;
    case DOKAN_DRIVER_INSTALL_ERROR:
      printf("Dokan Can't install driver\n");
      break;
    case DOKAN_START_ERROR:
      printf("Dokan Driver has something wrong\n");
      break;
    case DOKAN_MOUNT_ERROR:
      printf("Dokan Can't assign a drive letter\n");
      break;
    default:
      printf("Dokan Unknown error: %d\n", status);
      break;
  }
  free(Dokan_Options);
  free(Dokan_Operations);
}

void Mount(char drive) {
  printf("In Mount()\n");
  boost::thread thrd_(CallMount, drive);
}

}  // namespace fs_w_fuse
