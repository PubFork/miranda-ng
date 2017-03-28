/*
	NewXstatusNotify YM - Plugin for Miranda IM
	Copyright (c) 2001-2004 Luca Santarelli
	Copyright (c) 2005-2007 Vasilich
	Copyright (c) 2007-2011 yaho

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
	*/

#include "stdafx.h"

// From NewEventNotify :-)
bool CheckMsgWnd(MCONTACT hContact)
{
	MessageWindowData mwd;
	if (CallService(MS_MSG_GETWINDOWDATA, hContact, (LPARAM)&mwd) != NULL)
		return false;

	if (mwd.hwndWindow != NULL && (mwd.uState & MSG_WINDOW_STATE_EXISTS))
		return true;

	return false;
}

int DBGetStringDefault(MCONTACT hContact, const char *szModule, const char *szSetting, wchar_t *setting, int size, const wchar_t *defaultValue)
{
	DBVARIANT dbv;
	if (!db_get_ws(hContact, szModule, szSetting, &dbv)) {
		wcsncpy(setting, dbv.ptszVal, size);
		db_free(&dbv);
		return 0;
	}

	wcsncpy(setting, defaultValue, size);
	return 1;
}

void ShowLog(wchar_t *file)
{
	INT_PTR res = (INT_PTR)ShellExecute(NULL, L"open", file, NULL, NULL, SW_SHOW);
	if (res <= 32) // error
		MessageBox(0, TranslateT("Can't open the log file!"), TranslateT("NewXstatusNotify"), MB_OK | MB_ICONERROR);
}

BOOL StatusHasAwayMessage(char *szProto, int status)
{
	if (szProto != NULL) {
		unsigned long iSupportsSM = (unsigned long)CallProtoService(szProto, PS_GETCAPS, (WPARAM)PFLAGNUM_3, 0);
		return (iSupportsSM & Proto_Status2Flag(status)) ? TRUE : FALSE;
	}

	switch (status) {
	case ID_STATUS_AWAY:
	case ID_STATUS_NA:
	case ID_STATUS_OCCUPIED:
	case ID_STATUS_DND:
	case ID_STATUS_FREECHAT:
	case ID_STATUS_ONTHEPHONE:
	case ID_STATUS_OUTTOLUNCH:
		return TRUE;

	default:
		return FALSE;
	}
}

void LogToFile(wchar_t *stzText)
{
	FILE *fp = _wfopen(opt.LogFilePath, L"a+b, ccs=UTF-8");
	if (fp) {
		fprintf(fp, T2Utf(stzText));
		fclose(fp);
	}
}

void AddCR(CMStringW &str, const wchar_t *stzText)
{
	if (stzText == NULL)
		return;
	
	CMStringW res(stzText);
	res.Replace(L"\n", L"\r\n");
	res.Replace(L"\r\r\n", L"\r\n");
	str.Append(res);
}
