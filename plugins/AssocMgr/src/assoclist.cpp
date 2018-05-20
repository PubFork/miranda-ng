/*

'File Association Manager'-Plugin for Miranda IM

Copyright (C) 2005-2007 H. Herkenrath

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License,  or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program (AssocMgr-License.txt); if not,  write to the Free Software
Foundation,  Inc.,  59 Temple Place - Suite 330,  Boston,  MA  02111-1307,  USA.
*/

#include "stdafx.h"

// Options
static HANDLE hHookOptInit;

/************************* Assoc List *****************************/

typedef struct
{
	char *pszClassName;    // class name as used in registry and db
	wchar_t *pszDescription;
	HINSTANCE hInstance;   // allowed to be NULL for miranda32.exe
	WORD nIconResID;
	char *pszService;
	WORD flags;            // set of FTDF_* and UTDF_* flags
	char *pszFileExt;      // file type: NULL for url type
	char *pszMimeType;     // file type: allowed to be NULL
	wchar_t *pszVerbDesc;    // file type: allowed to be NULL
} ASSOCDATA;

static ASSOCDATA *pAssocList; // protected by csAssocList
static int nAssocListCount;   // protected by csAssocList
static mir_cs csAssocList;

/************************* Assoc Enabled **************************/

static BOOL IsAssocEnabled(const ASSOCDATA *assoc)
{
	char szSetting[MAXMODULELABELLENGTH];
	mir_snprintf(szSetting, "enabled_%s", assoc->pszClassName);
	return db_get_b(NULL, MODULENAME, szSetting, (BYTE)!(assoc->flags&FTDF_DEFAULTDISABLED)) != 0;
}

static void SetAssocEnabled(const ASSOCDATA *assoc, BOOL fEnabled)
{
	char szSetting[MAXMODULELABELLENGTH];
	wchar_t szDLL[MAX_PATH], szBuf[MAX_PATH];
	mir_snprintf(szSetting, "enabled_%s", assoc->pszClassName);
	db_set_b(NULL, MODULENAME, szSetting, (BYTE)fEnabled);
	// dll name for uninstall
	if (assoc->hInstance != nullptr && assoc->hInstance != g_plugin.getInst() && assoc->hInstance != GetModuleHandle(nullptr))
		if (GetModuleFileName(assoc->hInstance, szBuf, _countof(szBuf)))
			if (PathToRelativeW(szBuf, szDLL)) {
				mir_snprintf(szSetting, "module_%s", assoc->pszClassName);
				db_set_ws(NULL, MODULENAME, szSetting, szDLL);
			}
}

static void DeleteAssocEnabledSetting(const ASSOCDATA *assoc)
{
	char szSetting[MAXMODULELABELLENGTH];
	mir_snprintf(szSetting, "enabled_%s", assoc->pszClassName);
	db_unset(NULL, MODULENAME, szSetting);
	// dll name for uninstall
	mir_snprintf(szSetting, "module_%s", assoc->pszClassName);
	db_unset(NULL, MODULENAME, szSetting);
}

void CleanupAssocEnabledSettings(void)
{
	int nSettingsCount;
	char **ppszSettings, *pszSuffix;
	DBVARIANT dbv;
	int i;
	HANDLE hFile;
	wchar_t szDLL[MAX_PATH];
	char szSetting[MAXMODULELABELLENGTH];

	// delete old enabled_* settings if associated plugin no longer present
	if (EnumDbPrefixSettings(MODULENAME, "enabled_", &ppszSettings, &nSettingsCount)) {
		mir_cslock lck(csAssocList);
		for (i = 0; i < nSettingsCount; ++i) {
			pszSuffix = &ppszSettings[i][8];
			mir_snprintf(szSetting, "module_%s", pszSuffix);
			if (!db_get_ws(NULL, MODULENAME, szSetting, &dbv)) {
				if (PathToAbsoluteW(dbv.ptszVal, szDLL)) {
					// file still exists?
					hFile = CreateFile(szDLL, 0, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
					if (hFile == INVALID_HANDLE_VALUE) {
						db_unset(NULL, MODULENAME, ppszSettings[i]);
						db_unset(NULL, MODULENAME, szSetting);
					}
					else CloseHandle(hFile);
				}
				mir_free(dbv.ptszVal);
			}
			mir_free(ppszSettings[i]);
		}
		mir_free(ppszSettings); // does NULL check
	}
}

/************************* Mime Reg *******************************/

static __inline void RememberMimeTypeAdded(const char *pszMimeType, const char *pszFileExt, BYTE fAdded)
{
	char szSetting[MAXMODULELABELLENGTH];
	mir_snprintf(szSetting, "mime_%s", pszMimeType);
	if (fAdded) db_set_s(NULL, MODULENAME, szSetting, pszFileExt);
	else db_unset(NULL, MODULENAME, szSetting);
}

static __inline BOOL WasMimeTypeAdded(const char *pszMimeType)
{
	char szSetting[MAXMODULELABELLENGTH];
	DBVARIANT dbv;
	BOOL fAdded = FALSE;
	mir_snprintf(szSetting, "mime_%s", pszMimeType);
	if (!db_get(NULL, MODULENAME, szSetting, &dbv)) fAdded = TRUE;
	else db_free(&dbv);
	return fAdded;
}

void CleanupMimeTypeAddedSettings(void)
{
	int nSettingsCount;
	char **ppszSettings, *pszSuffix;
	DBVARIANT dbv;
	int i, j;

	// delete old mime_* settings and unregister the associated mime type
	if (EnumDbPrefixSettings(MODULENAME, "mime_", &ppszSettings, &nSettingsCount)) {
		mir_cslock lck(csAssocList);
		for (i = 0; i < nSettingsCount; ++i) {
			pszSuffix = &ppszSettings[i][5];
			for (j = 0; j < nAssocListCount; ++j)
				if (!mir_strcmp(pszSuffix, pAssocList[j].pszMimeType))
					break; // mime type in current list
			if (j == nAssocListCount) { // mime type not in current list
				if (!db_get(NULL, MODULENAME, ppszSettings[i], &dbv)) {
					if (dbv.type == DBVT_ASCIIZ)
						RemoveRegMimeType(pszSuffix, dbv.pszVal);
					db_free(&dbv);
				}
				db_unset(NULL, MODULENAME, ppszSettings[i]);
			}
			mir_free(ppszSettings[i]);
		}
		mir_free(ppszSettings);
	}
}

/************************* Shell Notify ***************************/

#define SHELLNOTIFY_DELAY  3000  // time for which assoc changes are buffered

static UINT nNotifyTimerID; // protected by csNotifyTimer
static mir_cs csNotifyTimer;

static void CALLBACK NotifyTimerProc(HWND hwnd, UINT, UINT_PTR nTimerID, DWORD)
{
	mir_cslock lck(csNotifyTimer);
	KillTimer(hwnd, nTimerID);
	if (nNotifyTimerID == nTimerID) // might be stopped previously
		SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST | SHCNF_FLUSHNOWAIT, nullptr, nullptr);
	nNotifyTimerID = 0;
}

static void NotifyAssocChange(BOOL fNow)
{
	mir_cslock lck(csNotifyTimer);
	if (fNow) {
		nNotifyTimerID = 0; // stop previous timer
		SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST | SHCNF_FLUSH, nullptr, nullptr);
	}
	else nNotifyTimerID = SetTimer(nullptr, nNotifyTimerID, SHELLNOTIFY_DELAY, NotifyTimerProc);
}

/************************* Assoc List Utils ***********************/

// this function assumes it has got the csAssocList mutex
static int FindAssocItem(const char *pszClassName)
{
	int i;
	for (i = 0; i < nAssocListCount; ++i)
		if (!mir_strcmp(pszClassName, pAssocList[i].pszClassName))
			return i;
	return -1;
}

BOOL IsRegisteredAssocItem(const char *pszClassName)
{
	int index;
	mir_cslock lck(csAssocList);
	index = FindAssocItem(pszClassName);
	return index != -1;
}


// this function assumes it has got the csAssocList mutex
static ASSOCDATA* CopyAssocItem(const ASSOCDATA *assoc)
{
	ASSOCDATA *assoc2;
	assoc2 = (ASSOCDATA*)mir_alloc(sizeof(ASSOCDATA));
	if (assoc2 == nullptr) return nullptr;
	assoc2->pszClassName = mir_strdup(assoc->pszClassName);
	assoc2->pszDescription = mir_wstrdup(assoc->pszDescription);
	assoc2->hInstance = assoc->hInstance;
	assoc2->nIconResID = assoc->nIconResID;
	assoc2->pszService = mir_strdup(assoc->pszService);
	assoc2->flags = assoc->flags;
	assoc2->pszFileExt = mir_strdup(assoc->pszFileExt);
	assoc2->pszMimeType = mir_strdup(assoc->pszMimeType);
	assoc2->pszVerbDesc = mir_wstrdup(assoc->pszVerbDesc);
	if (assoc2->pszClassName == nullptr || assoc2->pszDescription == nullptr ||
		(assoc2->pszFileExt == nullptr && assoc->pszFileExt != nullptr)) {
		mir_free(assoc2->pszClassName);   // does NULL check
		mir_free(assoc2->pszDescription); // does NULL check
		mir_free(assoc2->pszService);     // does NULL check
		mir_free(assoc2->pszFileExt);     // does NULL check
		mir_free(assoc2->pszMimeType);    // does NULL check
		mir_free(assoc2->pszVerbDesc);    // does NULL check
		mir_free(assoc2);
		return nullptr;
	}
	return assoc2;
}

// this function assumes it has got the csAssocList mutex
// this function assumes CoInitialize() has been called before
static int ReplaceImageListAssocIcon(HIMAGELIST himl, const ASSOCDATA *assoc, int iPrevIndex)
{
	HICON hIcon = nullptr;
	int index;
	if (himl == nullptr) return -1;

	// load icon
	hIcon = LoadRegClassSmallIcon(assoc->pszClassName);
	if (hIcon == nullptr) {
		SHFILEINFOA sfi;
		if (SHGetFileInfoA((assoc->pszFileExt != nullptr) ? assoc->pszFileExt : "", FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES))
			hIcon = sfi.hIcon; // WinXP: this icon is not updated until the process exits
	}
	// add icon
	if (hIcon == nullptr) return -1;
	index = ImageList_ReplaceIcon(himl, iPrevIndex, hIcon);
	DestroyIcon(hIcon);
	return index;
}

// the return value does not need to be freed
// this function assumes it has got the csAssocList mutex
static wchar_t* GetAssocTypeDesc(const ASSOCDATA *assoc)
{
	static wchar_t szDesc[32];
	if (assoc->pszFileExt == nullptr)
		mir_snwprintf(szDesc, L"%hs:", assoc->pszClassName);
	else
		mir_snwprintf(szDesc, TranslateT("%hs files"), assoc->pszFileExt);
	return szDesc;
}

// this function assumes it has got the csAssocList mutex
static BOOL IsAssocRegistered(const ASSOCDATA *assoc)
{
	BOOL fSuccess = FALSE, fIsUrl, fUseMainCmdLine;

	fIsUrl = (assoc->pszFileExt == nullptr);
	fUseMainCmdLine = (assoc->pszService == nullptr);

	// class
	wchar_t *pszRunCmd = MakeRunCommand(fUseMainCmdLine, !fUseMainCmdLine);
	if (pszRunCmd != nullptr)
		fSuccess = IsRegClass(assoc->pszClassName, pszRunCmd);
	mir_free(pszRunCmd); // does NULL check
	// file ext
	if (!fIsUrl)
		fSuccess = IsRegFileExt(assoc->pszFileExt, assoc->pszClassName);

	return fSuccess;
}

// this function assumes it has got the csAssocList mutex
// call GetLastError() on error to get more error details
static BOOL EnsureAssocRegistered(const ASSOCDATA *assoc)
{
	BOOL fSuccess = FALSE, fIsUrl, fUseMainCmdLine;
	wchar_t *pszIconLoc, *pszRunCmd, *pszDdeCmd, *pszAppFileName;

	fIsUrl = (assoc->pszFileExt == nullptr);
	fUseMainCmdLine = (assoc->pszService == nullptr);

	pszRunCmd = MakeRunCommand(fUseMainCmdLine, !fUseMainCmdLine);
	if (pszRunCmd != nullptr) {
		fSuccess = TRUE; // tentatively
		// do not overwrite user customized settings
		if (!IsRegClass(assoc->pszClassName, pszRunCmd)) {
			// class icon
			if (!assoc->nIconResID && fIsUrl) pszIconLoc = MakeIconLocation(nullptr, 0); // miranda logo
			else if (!assoc->nIconResID) pszIconLoc = MakeIconLocation(g_plugin.getInst(), IDI_MIRANDAFILE); // generic file
			else pszIconLoc = MakeIconLocation(assoc->hInstance, assoc->nIconResID);
			// register class
			if (fUseMainCmdLine) pszDdeCmd = nullptr;
			else pszDdeCmd = fIsUrl ? DDEURLCMD : DDEFILECMD;
			fSuccess = AddRegClass(assoc->pszClassName, assoc->pszDescription, pszIconLoc, _A2W(MIRANDANAME), pszRunCmd, pszDdeCmd, DDEAPP, DDETOPIC, assoc->pszVerbDesc, assoc->flags&FTDF_BROWSERAUTOOPEN, fIsUrl, assoc->flags&FTDF_ISSHORTCUT);
			mir_free(pszIconLoc); // does NULL check
			// file type
			if (fSuccess && !fIsUrl) {
				// register mime type
				if (assoc->pszMimeType != nullptr)
					if (AddRegMimeType(assoc->pszMimeType, assoc->pszFileExt))
						RememberMimeTypeAdded(assoc->pszMimeType, assoc->pszFileExt, TRUE);
				// register file ext
				fSuccess = AddRegFileExt(assoc->pszFileExt, assoc->pszClassName, assoc->pszMimeType, assoc->flags&FTDF_ISTEXT);
				// register open-with
				pszAppFileName = MakeAppFileName(fUseMainCmdLine);
				if (pszAppFileName != nullptr)
					AddRegOpenWithExtEntry(pszAppFileName, assoc->pszFileExt, assoc->pszDescription);
				mir_free(pszAppFileName); // does NULL check
			}
		}
		mir_free(pszRunCmd);
	}
	else SetLastError(ERROR_OUTOFMEMORY);
	return fSuccess;
}

// this function assumes it has got the csAssocList mutex
// call GetLastError() on error to get more error details
static BOOL UnregisterAssoc(const ASSOCDATA *assoc)
{
	BOOL fIsUrl, fUseMainCmdLine;
	wchar_t *pszAppFileName;

	fIsUrl = (assoc->pszFileExt == nullptr);
	fUseMainCmdLine = (assoc->pszService == nullptr);

	// class might have been registered by another instance
	wchar_t *pszRunCmd = MakeRunCommand(fUseMainCmdLine, !fUseMainCmdLine);
	if (pszRunCmd != nullptr && !IsRegClass(assoc->pszClassName, pszRunCmd)) {
		mir_free(pszRunCmd);
		return TRUE; // succeed anyway
	}
	mir_free(pszRunCmd); // does NULL check

	// file type
	if (!fIsUrl) {
		// file extension
		RemoveRegFileExt(assoc->pszFileExt, assoc->pszClassName);
		// mime type
		if (assoc->pszMimeType != nullptr)
			if (WasMimeTypeAdded(assoc->pszMimeType)) {
				RemoveRegMimeType(assoc->pszMimeType, assoc->pszFileExt);
				RememberMimeTypeAdded(assoc->pszMimeType, assoc->pszFileExt, FALSE);
			}
		// open-with entry
		pszAppFileName = MakeAppFileName(fUseMainCmdLine);
		if (pszAppFileName != nullptr)
			RemoveRegOpenWithExtEntry(pszAppFileName, assoc->pszFileExt);
		mir_free(pszAppFileName); // does NULL check
	}
	return RemoveRegClass(assoc->pszClassName);
}

/************************* Assoc List Workers *********************/
// this structure represents the head of both
// * FILETYPEDESC and URLTYPEDESC structures.
// * the head is identical for both structures.

typedef struct
{
	int cbSize;  // either sizeof(FILETYPEDESC) or sizeof(URLTYPEDESC)
	const void *pszDescription;
	HINSTANCE hInstance;
	UINT nIconResID;
	const char *pszService;
	DWORD flags;
} TYPEDESCHEAD;

// ownership of pszClassName,  pszFileExt,  pszVerbDesc and pszMimeType is transfered
// to the storage list on success
static BOOL AddNewAssocItem_Worker(char *pszClassName, const TYPEDESCHEAD *tdh, char *pszFileExt, wchar_t *pszVerbDesc, char *pszMimeType)
{
	ASSOCDATA *pAssocListBuf, *assoc;

	// is already in list?
	mir_cslock lck(csAssocList);
	int index = FindAssocItem(pszClassName);
	if (index != -1) return FALSE;

	// resize storage array
	pAssocListBuf = (ASSOCDATA*)mir_realloc(pAssocList, (nAssocListCount + 1) * sizeof(ASSOCDATA));
	if (pAssocListBuf == nullptr)
		return FALSE;
	pAssocList = pAssocListBuf;

	// init new item
	assoc = &pAssocList[nAssocListCount];
	assoc->pszClassName = pszClassName; // no dup here
	assoc->pszDescription = s2t(tdh->pszDescription, tdh->flags&FTDF_UNICODE, TRUE); // does NULL check
	assoc->hInstance = tdh->hInstance; // hInstance is allowed to be NULL for miranda32.exe
	assoc->nIconResID = (WORD)tdh->nIconResID; // default icon selected later on
	assoc->pszService = mir_strdup(tdh->pszService); // does NULL check
	assoc->flags = (WORD)tdh->flags;
	assoc->pszFileExt = pszFileExt;
	assoc->pszMimeType = pszMimeType;
	assoc->pszVerbDesc = pszVerbDesc;

	// error check
	if (assoc->pszDescription == nullptr || (assoc->pszService == nullptr && tdh->pszService != nullptr)) {
		mir_free(assoc->pszService);     // does NULL check
		mir_free(assoc->pszDescription); // does NULL check
		return FALSE;
	}

	// add registry keys 
	if (IsAssocEnabled(assoc))
		EnsureAssocRegistered(assoc);

	++nAssocListCount;
	NotifyAssocChange(FALSE);
	return TRUE;
}

// ownership of pszClassName is *not* transferd to storage list
static BOOL RemoveAssocItem_Worker(const char *pszClassName)
{
	ASSOCDATA *pAssocListBuf, *assoc;

	// find index
	mir_cslock lck(csAssocList);
	int index = FindAssocItem(pszClassName);
	if (index == -1)
		return FALSE;
	assoc = &pAssocList[index];

	// delete registry keys and db setting
	UnregisterAssoc(assoc);
	if (assoc->pszMimeType != nullptr)
		RememberMimeTypeAdded(assoc->pszMimeType, assoc->pszFileExt, FALSE);
	DeleteAssocEnabledSetting(assoc);

	// free memory
	mir_free(assoc->pszClassName);
	mir_free(assoc->pszDescription);
	mir_free(assoc->pszService);
	mir_free(assoc->pszFileExt);  // does NULL check
	mir_free(assoc->pszVerbDesc); // does NULL check
	mir_free(assoc->pszMimeType); // does NULL check

	// resize storage array
	if ((index + 1) < nAssocListCount)
		memmove(assoc, &pAssocList[index + 1], ((nAssocListCount - index - 1) * sizeof(ASSOCDATA)));
	pAssocListBuf = (ASSOCDATA*)mir_realloc(pAssocList, (nAssocListCount - 1) * sizeof(ASSOCDATA));
	if (pAssocListBuf != nullptr) pAssocList = pAssocListBuf;
	--nAssocListCount;

	NotifyAssocChange(FALSE);
	return TRUE;
}

/************************* Services *******************************/

static INT_PTR ServiceAddNewFileType(WPARAM, LPARAM lParam)
{
	const FILETYPEDESC *ftd = (FILETYPEDESC*)lParam;
	if (ftd->cbSize < sizeof(FILETYPEDESC))
		return 1;
	if (ftd->pszFileExt == nullptr || ftd->pszFileExt[0] != '.')
		return 2;

	char *pszFileExt = mir_strdup(ftd->pszFileExt);
	char *pszClassName = MakeFileClassName(ftd->pszFileExt);
	if (pszFileExt != nullptr && pszClassName != nullptr) {
		wchar_t *pszVerbDesc = s2t(ftd->pwszVerbDesc, ftd->flags&FTDF_UNICODE, TRUE); // does NULL check
		char *pszMimeType = mir_strdup(ftd->pszMimeType); // does NULL check
		if (AddNewAssocItem_Worker(pszClassName, (TYPEDESCHEAD*)ftd, pszFileExt, pszVerbDesc, pszMimeType))
			// no need to free pszClassName,  pszFileExt, pszVerbDesc and pszMimeType, 
			// as their ownership got transfered to storage list
			return 0;
	}
	mir_free(pszClassName); // does NULL check
	mir_free(pszFileExt); // does NULL check
	return 3;
}

static INT_PTR ServiceRemoveFileType(WPARAM, LPARAM lParam)
{
	if ((char*)lParam == nullptr) return 2;
	char *pszClassName = MakeFileClassName((char*)lParam);
	if (pszClassName != nullptr)
		if (RemoveAssocItem_Worker(pszClassName)) {
			mir_free(pszClassName);
			return 0;
		}
	mir_free(pszClassName); // does NULL check
	return 3;
}

static INT_PTR ServiceAddNewUrlType(WPARAM, LPARAM lParam)
{
	const URLTYPEDESC *utd = (URLTYPEDESC*)lParam;

	if (utd->cbSize < sizeof(URLTYPEDESC))
		return 1;
	if (utd->pszService == nullptr)
		return 2;
	if (utd->pszProtoPrefix == nullptr || utd->pszProtoPrefix[mir_strlen(utd->pszProtoPrefix) - 1] != ':')
		return 2;

	char *pszClassName = MakeUrlClassName(utd->pszProtoPrefix);
	if (pszClassName != nullptr)
		if (AddNewAssocItem_Worker(pszClassName, (TYPEDESCHEAD*)utd, nullptr, nullptr, nullptr))
			// no need to free pszClassName, as its ownership got transferred to storage list
			return 0;
	mir_free(pszClassName); // does NULL check
	return 3;
}

static INT_PTR ServiceRemoveUrlType(WPARAM, LPARAM lParam)
{
	if ((char*)lParam == nullptr) return 2;
	char *pszClassName = MakeUrlClassName((char*)lParam);
	if (pszClassName != nullptr)
		if (RemoveAssocItem_Worker(pszClassName)) {
			mir_free(pszClassName);
			return 0;
		}
	mir_free(pszClassName); // does NULL check
	return 3;
}

/************************* Open Handler ***************************/

static BOOL InvokeHandler_Worker(const char *pszClassName, const wchar_t *pszParam, INT_PTR *res)
{
	void *pvParam;
	char *pszService;

	// find it in list
	mir_cslock lck(csAssocList);
	int index = FindAssocItem(pszClassName);
	if (index == -1)
		return FALSE;
	ASSOCDATA *assoc = &pAssocList[index];
	// no service specified? correct registry to use main commandline
	if (assoc->pszService == nullptr) {
		EnsureAssocRegistered(assoc);
		NotifyAssocChange(FALSE);
		// try main command line
		if ((INT_PTR)ShellExecute(nullptr, nullptr, pszParam, nullptr, nullptr, SW_SHOWNORMAL) >= 32)
			*res = 0; // success
		return TRUE;
	}
	// get params
	pszService = mir_strdup(assoc->pszService);
	pvParam = t2s(pszParam, assoc->flags&FTDF_UNICODE, FALSE);

	// call service
	if (pszService != nullptr && pvParam != nullptr)
		*res = CallService(pszService, 0, (LPARAM)pvParam);
	mir_free(pszService); // does NULL check
	mir_free(pvParam); // does NULL check
	return TRUE;
}

INT_PTR InvokeFileHandler(const wchar_t *pszFileName)
{
	char *pszClassName, *pszFileExt;
	INT_PTR res = CALLSERVICE_NOTFOUND;

	// find extension
	wchar_t *p = (wchar_t*)wcsrchr(pszFileName, '.');
	if (p != nullptr) {
		pszFileExt = t2a(p);
		if (pszFileExt != nullptr) {
			// class name
			pszClassName = MakeFileClassName(pszFileExt);
			if (pszClassName != nullptr)
				if (!InvokeHandler_Worker(pszClassName, pszFileName, &res)) {
					// correct registry on error (no longer in list)
					RemoveRegFileExt(pszFileExt, pszClassName);
					RemoveRegClass(pszClassName);
				}
			mir_free(pszClassName); // does NULL check
			mir_free(pszFileExt);
		}
	}
	return res;
}

INT_PTR InvokeUrlHandler(const wchar_t *pszUrl)
{
	char *pszClassName, *pszProtoPrefix, *p;
	INT_PTR res = CALLSERVICE_NOTFOUND;

	// find prefix
	pszProtoPrefix = t2a(pszUrl);
	if (pszProtoPrefix != nullptr) {
		p = strchr(pszProtoPrefix, ':');
		if (p != nullptr) {
			*(++p) = 0; // remove trailing :
			// class name
			pszClassName = MakeUrlClassName(pszProtoPrefix);
			if (pszClassName != nullptr)
				if (!InvokeHandler_Worker(pszClassName, pszUrl, &res))
					// correct registry on error (no longer in list)
					RemoveRegClass(pszClassName);
			mir_free(pszClassName); // does NULL check
		}
		mir_free(pszProtoPrefix);
	}
	return res;
}

/************************* Options ********************************/

static int CALLBACK ListViewSortDesc(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort)
{
	int cmp;
	if (((ASSOCDATA*)lParam1)->pszFileExt != nullptr && ((ASSOCDATA*)lParam2)->pszFileExt != nullptr)
		cmp = CompareStringA((LCID)lParamSort, 0, ((ASSOCDATA*)lParam1)->pszFileExt, -1, ((ASSOCDATA*)lParam2)->pszFileExt, -1);
	else if (((ASSOCDATA*)lParam1)->pszFileExt == ((ASSOCDATA*)lParam2)->pszFileExt) // both NULL
		cmp = CompareStringA((LCID)lParamSort, 0, ((ASSOCDATA*)lParam1)->pszClassName, -1, ((ASSOCDATA*)lParam2)->pszClassName, -1);
	else // different types,  incomparable
		cmp = (((ASSOCDATA*)lParam1)->pszFileExt == nullptr) ? CSTR_LESS_THAN : CSTR_GREATER_THAN;
	if (cmp == CSTR_EQUAL)
		cmp = CompareString((LCID)lParamSort, 0, ((ASSOCDATA*)lParam1)->pszDescription, -1, ((ASSOCDATA*)lParam2)->pszDescription, -1);
	if (cmp != 0) cmp -= 2; // maintain CRT conventions
	return cmp;
}

#define M_REFRESH_ICONS  (WM_APP+1)
static INT_PTR CALLBACK AssocListOptDlgProc(HWND hwndDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	LVITEM lvi;
	ASSOCDATA *assoc;

	switch (msg) {
	case WM_INITDIALOG:
	{
		TranslateDialogDefault(hwndDlg);
		CoInitialize(nullptr);
		HWND hwndList = GetDlgItem(hwndDlg, IDC_ASSOCLIST);

		ListView_SetUnicodeFormat(hwndList, TRUE);

		SendDlgItemMessage(hwndDlg, IDC_HEADERTEXT, WM_SETFONT, SendMessage(GetParent(hwndDlg), PSM_GETBOLDFONT, 0, 0), 0);
		// checkboxes won't show up on Win95 without IE3+ or 4.70 (plugin opts uses the same)
		ListView_SetExtendedListViewStyle(hwndList, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_LABELTIP);
		// columns
		{
			LVCOLUMN lvc;
			lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
			lvc.pszText = TranslateT("Type");
			lvc.cx = 170;
			ListView_InsertColumn(hwndList, lvc.iSubItem = 0, &lvc);
			lvc.pszText = TranslateT("Description");
			ListView_InsertColumn(hwndList, lvc.iSubItem = 1, &lvc);
		}
		// create image storage
		HIMAGELIST himl;
		mir_cslock lck(csAssocList);
		{
			HDC hdc = GetDC(hwndList);
			if (hdc != nullptr) { // BITSPIXEL is compatible with ILC_COLOR flags
				himl = ImageList_Create(GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), GetDeviceCaps(hdc, BITSPIXEL) | ILC_MASK, nAssocListCount, 0);
				ReleaseDC(hwndList, hdc);
			}
			else himl = nullptr;
		}
		ListView_SetImageList(hwndList, himl, LVSIL_SMALL); // autodestroyed
		// enum assoc list
		lvi.iSubItem = 0;
		lvi.mask = LVIF_TEXT | LVIF_PARAM | LVIF_IMAGE;
		for (int i = 0; i < nAssocListCount; ++i) {
			assoc = &pAssocList[i];
			lvi.iItem = 0;
			lvi.lParam = (LPARAM)CopyAssocItem(assoc);
			lvi.pszText = GetAssocTypeDesc(assoc);
			lvi.iImage = ReplaceImageListAssocIcon(himl, assoc, -1);
			lvi.iItem = ListView_InsertItem(hwndList, &lvi);
			if (lvi.iItem != -1) {
				ListView_SetItemText(hwndList, lvi.iItem, 1, assoc->pszDescription);
				ListView_SetCheckState(hwndList, lvi.iItem, IsAssocEnabled(assoc) && IsAssocRegistered(assoc));
			}
		}
		// sort items (before moving to groups)
		ListView_SortItems(hwndList, ListViewSortDesc, Langpack_GetDefaultLocale());
		// groups
		if (ListView_EnableGroupView(hwndList, TRUE) == 1) { // returns 0 on pre WinXP or if commctls6 are disabled
			LVGROUP lvg;
			int iItem;
			// dummy item for group
			lvi.iItem = ListView_GetItemCount(hwndList) - 1;
			lvi.iSubItem = 0;
			lvi.mask = LVIF_PARAM | LVIF_IMAGE;
			lvi.iImage = -1;
			lvi.lParam = 0;
			// insert groups
			lvg.cbSize = sizeof(lvg);
			lvg.mask = LVGF_HEADER | LVGF_GROUPID;
			lvg.iGroupId = 2;
			lvg.pszHeader = TranslateT("URLs on websites");
			lvi.iItem = ListView_InsertItem(hwndList, &lvi);
			if (lvi.iItem != -1) {
				ListView_InsertGroup(hwndList, lvi.iItem, &lvg);
				lvg.iGroupId = 1;
				lvg.pszHeader = TranslateT("File types");
				iItem = lvi.iItem = ListView_InsertItem(hwndList, &lvi);
				if (lvi.iItem != -1)
					ListView_InsertGroup(hwndList, lvi.iItem, &lvg);
				else ListView_DeleteItem(hwndList, iItem);
			}
			// move to group
			lvi.iSubItem = 0;
			lvi.mask = LVIF_PARAM | LVIF_GROUPID;
			for (lvi.iItem = 0; ListView_GetItem(hwndList, &lvi); ++lvi.iItem) {
				assoc = (ASSOCDATA*)lvi.lParam;
				if (assoc == nullptr) continue; // groups
				lvi.iGroupId = (assoc->pszFileExt == nullptr) + 1;
				ListView_SetItem(hwndList, &lvi);
			}
		}
		lvi.iItem = ListView_GetTopIndex(hwndList);
		ListView_SetItemState(hwndList, lvi.iItem, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		ListView_SetColumnWidth(hwndList, 1, LVSCW_AUTOSIZE_USEHEADER); // size to fit window
		// only while running
		CheckDlgButton(hwndDlg, IDC_ONLYWHILERUNNING, (BOOL)db_get_b(NULL, MODULENAME, "OnlyWhileRunning", SETTING_ONLYWHILERUNNING_DEFAULT) ? BST_CHECKED : BST_UNCHECKED);

		// autostart
		wchar_t *pszRunCmd = MakeRunCommand(TRUE, TRUE);
		if (pszRunCmd != nullptr) {
			CheckDlgButton(hwndDlg, IDC_AUTOSTART, IsRegRunEntry(L"MirandaNG", pszRunCmd) ? BST_CHECKED : BST_UNCHECKED);
			mir_free(pszRunCmd);
		}
	}
	return TRUE;

	case WM_SETTINGCHANGE:
	case M_REFRESH_ICONS:
	{
		HWND hwndList = GetDlgItem(hwndDlg, IDC_ASSOCLIST);
		HIMAGELIST himl = ListView_GetImageList(hwndList, LVSIL_SMALL);
		// enum items
		lvi.iSubItem = 0;
		lvi.mask = LVIF_PARAM | LVIF_IMAGE;
		for (lvi.iItem = 0; ListView_GetItem(hwndList, &lvi); ++lvi.iItem) {
			assoc = (ASSOCDATA*)lvi.lParam;
			if (assoc == nullptr) continue; // groups
			lvi.iImage = ReplaceImageListAssocIcon(himl, assoc, lvi.iImage);
			ListView_SetItem(hwndList, &lvi);
		}
		if (lvi.iItem) { // ListView_Update() blinks
			ListView_RedrawItems(hwndList, 0, lvi.iItem - 1);
			UpdateWindow(hwndList);
		}
	}
	return TRUE;

	case WM_CTLCOLORSTATIC:
		// use same text color for header as for group boxes (WinXP+)
		if (GetDlgCtrlID((HWND)lParam) == IDC_HEADERTEXT) {
			lParam = (LPARAM)GetDlgItem(hwndDlg, IDC_MISCLABEL);
			HBRUSH hBrush = (HBRUSH)SendMessage(hwndDlg, msg, wParam, lParam);
			COLORREF clr;
			HTHEME hTheme = GetWindowTheme((HWND)lParam);
			if (hTheme != nullptr && !GetThemeColor(hTheme, BP_GROUPBOX, GBS_NORMAL, TMT_TEXTCOLOR, &clr)) {
				SetBkMode((HDC)wParam, TRANSPARENT);
				SetTextColor((HDC)wParam, clr);
			}
			return (INT_PTR)hBrush;
		}
		break;

	case WM_NCDESTROY:
		CoUninitialize();
		return TRUE;

	case WM_COMMAND:
		// enable apply
		PostMessage(GetParent(hwndDlg), PSM_CHANGED, 0, 0);
		break;

	case WM_NOTIFYFORMAT:
		SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, NFR_UNICODE);
		return TRUE;

	case WM_NOTIFY:
		NMHDR * nmhdr = (NMHDR*)lParam;
		switch (nmhdr->idFrom) {
		case IDC_ASSOCLIST:
			switch (nmhdr->code) {
			case LVN_DELETEITEM: // also called on WM_DESTROY
				lvi.mask = LVIF_PARAM;
				lvi.iSubItem = 0;
				lvi.iItem = ((NMLISTVIEW*)lParam)->iItem;
				// free memory
				if (ListView_GetItem(nmhdr->hwndFrom, &lvi))
					mir_free((ASSOCDATA*)lvi.lParam); // does NULL check
				return TRUE;

			case LVN_ITEMCHANGED:
				// enable apply (not while loading)
				if (IsWindowVisible(nmhdr->hwndFrom))
					PostMessage(GetParent(hwndDlg), PSM_CHANGED, 0, 0);
				return TRUE;

			case LVN_KEYDOWN:
				// workaround for WinXP (ListView with groups):
				// eat keyboard navigation that goes beyond the first item in list
				// as it would scroll out of scope in this case
				// bug should not be present using WinVista and higher
				switch (((NMLVKEYDOWN*)lParam)->wVKey) {
				case VK_UP:
					lvi.iSubItem = 0;
					lvi.mask = LVIF_PARAM;
					lvi.iItem = ListView_GetNextItem(nmhdr->hwndFrom, -1, LVNI_FOCUSED);
					lvi.iItem = ListView_GetNextItem(nmhdr->hwndFrom, lvi.iItem, LVNI_ABOVE);
					if (lvi.iItem != -1)
						if (ListView_GetItem(nmhdr->hwndFrom, &lvi))
							if ((ASSOCDATA*)lvi.lParam == nullptr) // groups
								lvi.iItem = -1;
					if (lvi.iItem == -1) {
						SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, TRUE); // eat it
						return TRUE;
					}
					break;

				case VK_PRIOR:
					lvi.iSubItem = 0;
					lvi.mask = LVIF_PARAM;
					lvi.iItem = ListView_GetNextItem(nmhdr->hwndFrom, -1, LVNI_FOCUSED);
					lvi.iItem -= ListView_GetCountPerPage(nmhdr->hwndFrom);
					if (lvi.iItem >= 0)
						if (ListView_GetItem(nmhdr->hwndFrom, &lvi))
							if ((ASSOCDATA*)lvi.lParam == nullptr) // groups
								lvi.iItem = -1;
					if (lvi.iItem < 0) {
						ListView_SetItemState(nmhdr->hwndFrom, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
						SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, TRUE); // eat it
						return TRUE;
					}
					break;
				}
				break;
			}
			break;

		case 0:
			switch (nmhdr->code) {
			case PSN_APPLY:
				BOOL fEnabled, fRegFailed = FALSE;

				// only while running
				db_set_b(NULL, MODULENAME, "OnlyWhileRunning", (BYTE)(IsDlgButtonChecked(hwndDlg, IDC_ONLYWHILERUNNING) != 0));

				// save enabled assoc items
				HWND hwndList = GetDlgItem(hwndDlg, IDC_ASSOCLIST);
				lvi.iSubItem = 0;
				lvi.mask = LVIF_PARAM;
				mir_cslock lck(csAssocList);
				for (lvi.iItem = 0; ListView_GetItem(hwndList, &lvi); ++lvi.iItem) {
					assoc = (ASSOCDATA*)lvi.lParam;
					if (assoc == nullptr) continue; // groups
					fEnabled = ListView_GetCheckState(hwndList, lvi.iItem);
					SetAssocEnabled(assoc, fEnabled);

					// re-register registery keys
					if (fEnabled ? !EnsureAssocRegistered(assoc) : !UnregisterAssoc(assoc)) {
						char *pszErr = GetWinErrorDescription(GetLastError());
						ShowInfoMessage(NIIF_ERROR, Translate("File association error"), Translate("There was an error writing to the registry to modify the file/url associations.\nReason: %s"), (pszErr != nullptr) ? pszErr : Translate("Unknown"));
						mir_free(pszErr); // does NULL check
						fRegFailed = TRUE; // just show one time
					}
				}
				NotifyAssocChange(TRUE);
				PostMessage(hwndDlg, M_REFRESH_ICONS, 0, 0);

				// autostart
				wchar_t *pszRunCmd = MakeRunCommand(TRUE, TRUE);
				fRegFailed = FALSE;
				if (pszRunCmd != nullptr) {
					fEnabled = IsDlgButtonChecked(hwndDlg, IDC_AUTOSTART);
					if (fEnabled ? !AddRegRunEntry(L"MirandaNG", pszRunCmd) : !RemoveRegRunEntry(L"MirandaNG", pszRunCmd)) {
						char *pszErr;
						pszErr = GetWinErrorDescription(GetLastError());
						ShowInfoMessage(NIIF_ERROR, Translate("Autostart error"), Translate("There was an error writing to the registry to modify the autostart list.\n\nReason: %s"), (pszErr != nullptr) ? pszErr : Translate("Unknown"));
						mir_free(pszErr); // does NULL check
						fRegFailed = TRUE; // just show one time
					}
					mir_free(pszRunCmd);
				}
				return TRUE;
			}
		}
		break;
	}
	return FALSE;
}

static int AssocListOptInit(WPARAM wParam, LPARAM)
{
	OPTIONSDIALOGPAGE odp = { 0 };
	odp.hInstance = g_plugin.getInst();
	odp.pszTemplate = MAKEINTRESOURCEA(IDD_OPT_ASSOCLIST);
	odp.position = 900000100; // network opts  =  900000000
	odp.szGroup.a = LPGEN("Services"); // autotranslated
	odp.szTitle.a = LPGEN("Associations"); // autotranslated
	odp.flags = ODPF_BOLDGROUPS;
	odp.pfnDlgProc = AssocListOptDlgProc;
	g_plugin.addOptions(wParam, &odp);
	return 0;
}

/************************* Misc ***********************************/

void InitAssocList(void)
{
	// Options
	INITCOMMONCONTROLSEX icc;
	icc.dwSize = sizeof(icc);
	icc.dwICC = ICC_LISTVIEW_CLASSES;
	InitCommonControlsEx(&icc);
	hHookOptInit = HookEvent(ME_OPT_INITIALISE, AssocListOptInit);

	// Assoc List
	pAssocList = nullptr;
	nAssocListCount = 0;

	// Services
	CreateServiceFunction(MS_ASSOCMGR_ADDNEWFILETYPE, ServiceAddNewFileType);
	CreateServiceFunction(MS_ASSOCMGR_REMOVEFILETYPE, ServiceRemoveFileType);
	CreateServiceFunction(MS_ASSOCMGR_ADDNEWURLTYPE, ServiceAddNewUrlType);
	CreateServiceFunction(MS_ASSOCMGR_REMOVEURLTYPE, ServiceRemoveUrlType);

	// Notify Shell
	nNotifyTimerID = 0;

	// register open-with app
	{
		wchar_t *pszAppFileName, *pszIconLoc, *pszRunCmd;
		pszIconLoc = MakeIconLocation(nullptr, 0);

		// miranda32.exe
		pszAppFileName = MakeAppFileName(TRUE);
		pszRunCmd = MakeRunCommand(TRUE, FALSE);
		if (pszAppFileName != nullptr && pszRunCmd != nullptr)
			AddRegOpenWith(pszAppFileName, FALSE, _A2W(MIRANDANAME), pszIconLoc, pszRunCmd, nullptr, nullptr, nullptr);
		mir_free(pszRunCmd); // does NULL check
		mir_free(pszAppFileName); // does NULL check
		// assocmgr.dll
		pszAppFileName = MakeAppFileName(FALSE);
		pszRunCmd = MakeRunCommand(FALSE, TRUE);
		if (pszAppFileName != nullptr && pszRunCmd != nullptr)
			AddRegOpenWith(pszAppFileName, TRUE, _A2W(MIRANDANAME), pszIconLoc, pszRunCmd, DDEFILECMD, DDEAPP, DDETOPIC);
		mir_free(pszRunCmd); // does NULL check
		mir_free(pszAppFileName); // does NULL check

		mir_free(pszIconLoc); // does NULL check
	}

	// default items
	{
		FILETYPEDESC ftd;
		ftd.cbSize = sizeof(FILETYPEDESC);
		ftd.pszFileExt = ".dat";
		ftd.pszMimeType = nullptr;
		ftd.pwszDescription = TranslateT("Miranda NG database");
		ftd.hInstance = g_plugin.getInst();
		ftd.nIconResID = IDI_MIRANDAFILE;
		ftd.pwszVerbDesc = nullptr;
		ftd.pszService = nullptr;
		ftd.flags = FTDF_DEFAULTDISABLED | FTDF_UNICODE;
		ServiceAddNewFileType(0, (LPARAM)&ftd);
	}
}

void UninitAssocList(void)
{
	// Options
	UnhookEvent(hHookOptInit);

	// Assoc List
	BYTE fOnlyWhileRunning = db_get_b(NULL, MODULENAME, "OnlyWhileRunning", SETTING_ONLYWHILERUNNING_DEFAULT);
	for (int i = 0; i < nAssocListCount; ++i) {
		ASSOCDATA *assoc = &pAssocList[i];

		// remove registry keys
		if (fOnlyWhileRunning)
			UnregisterAssoc(assoc);

		mir_free(assoc->pszClassName);
		mir_free(assoc->pszDescription);
		mir_free(assoc->pszService);
		mir_free(assoc->pszFileExt);
		mir_free(assoc->pszVerbDesc);
		mir_free(assoc->pszMimeType);
	}
	mir_free(pAssocList);

	// Notify Shell
	if (fOnlyWhileRunning && nAssocListCount)
		NotifyAssocChange(TRUE);

	// unregister open-with app
	if (fOnlyWhileRunning) {
		// miranda32.exe
		ptrW pszAppFileName(MakeAppFileName(TRUE));
		if (pszAppFileName != NULL)
			RemoveRegOpenWith(pszAppFileName);

		// assocmgr.dll
		pszAppFileName = MakeAppFileName(FALSE);
		if (pszAppFileName != NULL)
			RemoveRegOpenWith(pszAppFileName);
	}
}
