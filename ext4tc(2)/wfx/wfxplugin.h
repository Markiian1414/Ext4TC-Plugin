#pragma once
#ifndef WFXPLUGIN_H
#define WFXPLUGIN_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// -------------------------------------------------------
//  Total Commander WFX Plugin API v2.0
//  Based on official wfxplugin.h by Christian Ghisler
// -------------------------------------------------------

#define FS_MAX_PATH 260

typedef int (__stdcall *tProgressProc)(int PluginNr,
    char* SourceName, char* TargetName, int PercentDone);
typedef void (__stdcall *tLogProc)(int PluginNr, int MsgType,
    char* LogString);
typedef BOOL (__stdcall *tRequestProc)(int PluginNr, int RequestType,
    char* CustomTitle, char* CustomText, char* ReturnedText, int MaxLen);

typedef struct {
    DWORD SizeLow;
    DWORD SizeHigh;
    int   Attr;
    FILETIME LastWriteTime;
    int   Reserved;
} RemoteInfoStruct;

// Return codes FsGetFile/FsPutFile/FsRenMovFile
#define FS_FILE_OK              0
#define FS_FILE_EXISTS          1
#define FS_FILE_NOTFOUND        2
#define FS_FILE_READERROR       3
#define FS_FILE_WRITEERROR      4
#define FS_FILE_USERABORT       5
#define FS_FILE_NOTSUPPORTED    6
#define FS_FILE_EXISTSRESUMEALLOWED 7

// FsExecuteFile
#define FS_EXEC_OK          0
#define FS_EXEC_ERROR       1
#define FS_EXEC_YOURSELF    -1
#define FS_EXEC_SYMLINK     -2

// Background flags
#define BG_NONE     0
#define BG_DOWNLOAD 1
#define BG_UPLOAD   2
#define BG_ASK_USER 4

// Log types
#define MSGTYPE_CONNECT         1
#define MSGTYPE_DISCONNECT      2
#define MSGTYPE_DETAILS         3
#define MSGTYPE_TRANSFERCOMPLETE 4
#define MSGTYPE_CONNECTCOMPLETE  5
#define MSGTYPE_IMPORTANTERROR   6
#define MSGTYPE_OPERATIONCOMPLETE 7

// Copy flags
#define FS_COPYFLAGS_OVERWRITE          1
#define FS_COPYFLAGS_RESUME             2
#define FS_COPYFLAGS_MOVE               4
#define FS_COPYFLAGS_EXISTS_SAMECASE    8
#define FS_COPYFLAGS_EXISTS_DIFFERENTCASE 16

#ifdef __cplusplus
extern "C" {
#endif

int  __stdcall FsInit(int PluginNr, tProgressProc pProgressProc,
                      tLogProc pLogProc, tRequestProc pRequestProc);
void __stdcall FsGetDefRootName(char* DefRootName, int maxlen);
HANDLE __stdcall FsFindFirst(char* Path, WIN32_FIND_DATAA* FindData);
BOOL   __stdcall FsFindNext(HANDLE Hdl, WIN32_FIND_DATAA* FindData);
int    __stdcall FsFindClose(HANDLE Hdl);
int    __stdcall FsGetFile(char* RemoteName, char* LocalName, int CopyFlags, RemoteInfoStruct* ri);
int    __stdcall FsPutFile(char* LocalName, char* RemoteName, int CopyFlags);
BOOL   __stdcall FsDeleteFile(char* RemoteName);
BOOL   __stdcall FsRemoveDir(char* RemoteName);
BOOL   __stdcall FsMkDir(char* Path);
int    __stdcall FsExecuteFile(HWND MainWin, char* RemoteName, char* Verb);
int    __stdcall FsRenMovFile(char* OldName, char* NewName, BOOL Move,
                               BOOL OverWrite, RemoteInfoStruct* ri);
BOOL   __stdcall FsSetAttr(char* RemoteName, int NewAttr);
BOOL   __stdcall FsSetTime(char* RemoteName, FILETIME* CreationTime,
                            FILETIME* LastAccessTime, FILETIME* LastWriteTime);
void   __stdcall FsStatusInfo(char* RemoteDir, int InfoStartEnd, int Operation);
int    __stdcall FsGetBackgroundFlags(void);
BOOL   __stdcall FsLinksToLocalFiles(void);
BOOL   __stdcall FsContentGetDefaultView(char* ViewContents, char* ViewHeaders,
                                          char* ViewWidths, char* ViewOptions, int maxlen);

#ifdef __cplusplus
}
#endif

#endif // WFXPLUGIN_H
