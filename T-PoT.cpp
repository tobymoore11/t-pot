//=============================================================================
// T-PoT - Total Commander file system plug-in for iPod and iPhone devices
//-----------------------------------------------------------------------------
// File:			T-PoT.cpp
// Purpose:			Main API implementation for Total Commander plug-in.
// Limitations:		Limitations of iTunes interface with iPod/iPhone
//					devices (no file attributes).
// Platform:		Win32
//-----------------------------------------------------------------------------
// Copyright (c) 2007-2009, Scythal
// All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without 
// modification, are permitted provided that the following conditions are met:
// * Redistributions of source code must retain the above copyright
//   notice, this list of conditions and the following disclaimer.
// * Redistributions in binary form must reproduce the above copyright
//   notice, this list of conditions and the following disclaimer in the
//   documentation and/or other materials provided with the distribution.
// * Neither the name of the software nor the names of its contributors may be 
//   used to endorse or promote products derived from this software without 
//   specific prior written permission.
// 
// THIS SOFTWARE IS PROVIDED BY ITS AUTHOR ``AS IS'' AND ANY EXPRESS OR 
// IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES 
// OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN 
// NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, 
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED 
// TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR 
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF 
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING 
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS 
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//=============================================================================

#pragma warning(disable:4996)

#include "stdafx.h"
#include <io.h>
#include <atlstr.h>			// To use CString
#include <shellapi.h>

#include "T-PoT.h"
#include "iPoTApi.h"

#ifdef _MANAGED
#pragma managed(push, off)
#endif

//-----------------------------------------------------------------------------
// Globals
//-----------------------------------------------------------------------------

int pluginId;
CiPoTApi iPodApi;
tProgressProc ProgressProc = NULL;
tLogProc LogProc = NULL;
tRequestProc RequestProc = NULL;
bool bAborted;
bool bTranslatePLIST = true;
bool bTranslatePNG = true;

//-----------------------------------------------------------------------------
// DllMain
//-----------------------------------------------------------------------------

extern "C" BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		break;
	case DLL_THREAD_ATTACH:
		break;
	case DLL_THREAD_DETACH:
		break;
	case DLL_PROCESS_DETACH:
		break;
	}
    return TRUE;
}

//-----------------------------------------------------------------------------
// Exported functions
//-----------------------------------------------------------------------------

// FsGetDefRootName is called only when the plugin is installed. It asks the plugin 
// for the default root name which should appear in the Network Neighborhood. This 
// root name is NOT part of the path passed to the plugin when Wincmd accesses the 
// plugin file system! The root will always be "\", and all subpaths will be built 
// from the directory names returned by the plugin.
//
void __stdcall FsGetDefRootName(char *DefRootName, int maxlen)
{
	strncpy(DefRootName, FSPLUGIN_CAPTION, maxlen - 1);
}

// FsInit is called when loading the plugin. The passed values should be stored in 
// the plugin for later use.
//
int	__stdcall FsInit(int PluginNr, tProgressProc pProgressProc,
                     tLogProc pLogProc, tRequestProc pRequestProc)
{
	pluginId = PluginNr;
	ProgressProc = pProgressProc;
	LogProc = pLogProc;
	RequestProc = pRequestProc;
	iPodApi.SetProgressCallBack(Progress);
	while (iPodApi.AttachDLL() != IPOD_ERR_OK) {
		// Could not attach the DLL
		int ret = MessageBox(NULL, 
			"Could not load iTunesMobileDevice.dll, check your iTunes installation.\n", 
			"Missing library",
			MB_RETRYCANCEL);
		if (ret == IDCANCEL)
			return 1;
	}
	return 0;
}

// FsFindFirst is called to retrieve the first file in a directory of the plugin's file system.
//
HANDLE __stdcall FsFindFirst(char* Path, WIN32_FIND_DATA *FindData)
{
	t_iPodError status = iPodApi.OpenSession();
	
	if (status == IPOD_ERR_OK) {
		bool bFile;
		t_iPodFileInfo *piPodFileInfo = new t_iPodFileInfo;
		CString BSDPath(Path);

		BSDPath.Replace('\\', '/');
		bFile = iPodApi.FindFirst(BSDPath.GetBuffer(), piPodFileInfo);
		if (bFile) {
			*FindData = piPodFileInfo->findData;
			return (HANDLE)piPodFileInfo;
		} else 
			SetLastError(ERROR_NO_MORE_FILES);
	} else
		SetLastError(ERROR_BAD_UNIT);
	return INVALID_HANDLE_VALUE;
}

// FsFindNext is called to retrieve the next file in a directory of the plugin's file system.
//
BOOL __stdcall FsFindNext(HANDLE Hdl, WIN32_FIND_DATA *FindData)
{
	t_iPodFileInfo *piPodFileInfo = (t_iPodFileInfo*)Hdl;
	bool bFile = iPodApi.FindNext(piPodFileInfo);
	if (bFile) {
		*FindData = piPodFileInfo->findData;
		return TRUE;
	}
	SetLastError(ERROR_NO_MORE_FILES);
	return FALSE;
}

// FsFindClose is called to end a FsFindFirst/FsFindNext loop, 
// either after retrieving all files, or when the user aborts it.
//
int __stdcall FsFindClose(HANDLE Hdl)
{
	t_iPodFileInfo *piPodFileInfo = (t_iPodFileInfo*)Hdl;
	iPodApi.FindClose(piPodFileInfo);
	delete piPodFileInfo;
	return 0;
}

// Callback function to inform the API of the progression status
// of the current FsGetFile or FsPutFile transfer.
//
// Returns:
//		0	if the operation can continue
//		1	if the operation should be bAborted
//
int __stdcall Progress(char* sourceName, char* targetName, int percentDone)
{
	if (ProgressProc) {
		int abort = ProgressProc(pluginId, sourceName, targetName, percentDone);
		bAborted = (abort == 1);
		return abort;
	} else
		return 0;
}

// FsGetFile is called to transfer a file from the plugin's file system 
// to the normal file system (drive letters or UNC).
//
int __stdcall FsGetFile(char *RemoteName, char *LocalName, int CopyFlags, RemoteInfoStruct *ri)
{
	t_iPodError status = iPodApi.OpenSession();

	bAborted = false;
	if (status == IPOD_ERR_OK) {
		CString BSDPath(RemoteName), LocalCopy(LocalName);
		BSDPath.Replace('\\', '/');
		t_MachError ret;
		
		if (~CopyFlags & FS_COPYFLAGS_OVERWRITE) {
			// Check if the destination file exists
			int result = access(LocalName, 0);
			if (result != -1) {
				// Files exists, checks the write permission
				result = access(LocalName, 2);
				if (result == -1) {
					// Access denied, no need to go further
					return FS_FILE_WRITEERROR;
				}
				// Files exists, asks user's decision
				return FS_FILE_EXISTSRESUMEALLOWED;
			}
		}
		// Checks if the file needs to be converted
		if (bTranslatePNG && !BSDPath.Right(4).CompareNoCase(".png")) {
			iPodApi.GetTempFilename(LocalCopy);
			ret = iPodApi.FileRead(BSDPath.GetBuffer(), LocalCopy.GetBuffer());
			iPodApi.TranslatePNG(LocalCopy.GetBuffer(), LocalName);
		} else if (bTranslatePLIST && iPodApi.CanTranslatePLIST() && !BSDPath.Right(6).CompareNoCase(".plist")) {
			iPodApi.GetTempFilename(LocalCopy);
			ret = iPodApi.FileRead(BSDPath.GetBuffer(), LocalCopy.GetBuffer());
			if (!iPodApi.TranslatePLIST(LocalCopy.GetBuffer(), LocalName)) {
				// Failure
				MessageBox(NULL, 
					"Could not translate the plist file.\n",
					"Translation Failure",
					MB_ICONEXCLAMATION);
			}
		} else {
			ret = iPodApi.FileRead(BSDPath.GetBuffer(), LocalName);
		}
		switch (ret) {
			case MDERR_OK:
				if (CopyFlags & FS_COPYFLAGS_MOVE)
					// TODO: remember if some of the files could not be deleted
					//       and display a pop-up at the end of the group transfer
					iPodApi.Remove(BSDPath.GetBuffer());
				return FS_FILE_OK;
			case MDERR_AFC_NOT_FOUND:
			case MDERR_AFC_ACCESS_DENIED:
				return FS_FILE_NOTSUPPORTED;
			default:
				if (bAborted) {
					// Remove the partial file
					_unlink(LocalName);
					return FS_FILE_USERABORT;
				}
				return FS_FILE_READERROR;
		}
	}
	return FS_FILE_READERROR;
}                        

// FsPutFile is called to transfer a file from the normal file system 
// (drive letters or UNC) to the plugin's file system.
//
int __stdcall FsPutFile(char *LocalName, char *RemoteName, int CopyFlags)
{
	t_iPodError status = iPodApi.OpenSession();

	bAborted = false;
	if (status == IPOD_ERR_OK) {
		CString BSDPath(RemoteName);
		BSDPath.Replace('\\', '/');
		t_MachError ret;
		
		if (~CopyFlags & FS_COPYFLAGS_OVERWRITE) {
			// Check if the destination file exists
			if (iPodApi.FileExists(BSDPath.GetBuffer()))
				// Files exists, unfortunately we can't check the write permission
				// so asks the user's decision
				return FS_FILE_EXISTSRESUMEALLOWED;			
		}
		ret = iPodApi.FileWrite(BSDPath.GetBuffer(), LocalName);
		switch (ret) {
			case MDERR_OK:
				if (CopyFlags & FS_COPYFLAGS_MOVE)
					// TODO: remember if some of the files could not be deleted
					//       and display a pop-up at the end of the group transfer
					_unlink(LocalName);
				return FS_FILE_OK;
			case MDERR_AFC_NOT_FOUND:
				return FS_FILE_READERROR;
			case MDERR_AFC_ACCESS_DENIED:
				return FS_FILE_NOTSUPPORTED;
			default:
				if (bAborted) {
					// Remove the partial file
					iPodApi.Remove(BSDPath.GetBuffer());
					return FS_FILE_USERABORT;
				}
				return FS_FILE_WRITEERROR;
		}
	}
	return FS_FILE_WRITEERROR;
}

// FsRenMovFile is called to transfer (copy or move) a file within the 
// plugin's file system.
//
int __stdcall FsRenMovFile(char *OldName, char *NewName, BOOL Move, BOOL OverWrite, RemoteInfoStruct *ri)
{
	t_iPodError status = iPodApi.OpenSession();
	CString BSDOldPath(OldName), BSDNewPath(NewName);
	t_MachError ret;

	BSDOldPath.Replace('\\', '/');
	BSDNewPath.Replace('\\', '/');
	if (!Move) {
		// Does a combination of AFCFileRefRead and AFCFileRefWrite,
		// a little silly as we lose the file attributes on the way.
		CString LocalCopy;
		
		iPodApi.GetTempFilename(LocalCopy);
		if (iPodApi.FileRead(BSDOldPath.GetBuffer(), LocalCopy.GetBuffer()) != FS_FILE_OK)
			return FS_FILE_READERROR;
		ret = iPodApi.FileWrite(BSDNewPath.GetBuffer(), LocalCopy.GetBuffer());
		_unlink(LocalCopy);
		return ret;
	}
	bAborted = false;
	if (status == IPOD_ERR_OK) {
		
		if (!OverWrite) {
			if (iPodApi.FileExists(BSDNewPath.GetBuffer()))
				// Files exists, unfortunately we can't check the write permission
				// so asks the user's decision
				return FS_FILE_EXISTSRESUMEALLOWED;			
		}
		ret = iPodApi.Move(BSDOldPath.GetBuffer(), BSDNewPath.GetBuffer());
		return (ret == FS_FILE_OK) ? FS_FILE_OK : FS_FILE_READERROR;
	}
	return FS_FILE_READERROR;
}

// FsMkDir is called to create a directory on the plugin's file system.
//
BOOL __stdcall FsMkDir(char *Path)
{
	t_iPodError status = iPodApi.OpenSession();
	
	if (status == IPOD_ERR_OK) {
		CString BSDPath(Path);
		BSDPath.Replace('\\', '/');

		return iPodApi.MakeDir(BSDPath.GetBuffer()) == MDERR_OK;
	}
	return FALSE;
}

// FsDeleteFile is called to delete a file from the plugin's file system.
//
BOOL __stdcall FsDeleteFile(char *RemoteName)
{
	t_iPodError status = iPodApi.OpenSession();
	
	if (status == IPOD_ERR_OK) {
		CString BSDPath(RemoteName);
		BSDPath.Replace('\\', '/');

		return iPodApi.Remove(BSDPath.GetBuffer()) == MDERR_OK;
	}
	return FALSE;
}

// FsRemoveDir is called to remove a directory from the plugin's file system.
//
BOOL __stdcall FsRemoveDir(char* RemoteName)
{
	return FsDeleteFile(RemoteName);
}

// FsExecuteFile is called to execute a file on the plugin's file system, or show
// its property sheet. It is also called to show a plugin configuration dialog
// when the user right clicks on the plugin root and chooses 'properties'.
// The plugin is then called with RemoteName="\" and Verb="properties" (requires TC>=5.51).
//
int __stdcall FsExecuteFile(HWND MainWin, char *RemoteName, char *Verb)
{
	CString BSDPath(RemoteName);
	BSDPath.Replace('\\', '/');

	if (!strcmp(Verb, "open"))
		return FS_EXEC_YOURSELF;
	else if (!strcmp(Verb, "properties")) {
		t_iPodFileInfo info;
		CString information, caption;
			
		caption.Format("Properties of %s", RemoteName);
		if (iPodApi.GetFileInfo(&info, BSDPath.GetBuffer()) == MDERR_OK)
		{
			if (info.findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
				information.Format("Remote name: %s\n%s\nContains %d elements\nUses %d blocks",
					RemoteName,
					(info.findData.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) ? "system Directory" : "Directory",
					(int)(info.iPodSize/34 - 2),
					info.iPodBlocks
				);
			} else {
				information.Format("Remote name: %s\n%s\nSize: %d bytes\nUses %d blocks",
					RemoteName,
					(info.findData.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) ? "system File" : "File",
					info.iPodSize,
					info.iPodBlocks
				);
			}
		} else
			information = "Broken link";
		MessageBox(NULL, 
			information.GetBuffer(), 
			caption.GetBuffer(),
			MB_OK | MB_ICONINFORMATION);
		return FS_EXEC_OK;
	} else if (!strncmp(Verb, "chmod", 5))
		return FS_EXEC_ERROR;
	else if (!strncmp(Verb, "quote", 5)) {
		// TODO: Handle a few commands ?
		return FS_EXEC_ERROR;
	} else if (!strncmp(Verb, "MODE", 4))
		// Undocumented command, sets the mode to binary, text or auto (?)
		return FS_EXEC_ERROR;
	else
		return FS_EXEC_ERROR;
}

#ifdef _MANAGED
#pragma managed(pop)
#endif
