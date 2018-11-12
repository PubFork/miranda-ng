/*
	Buddy Expectator+ plugin for Miranda-IM (www.miranda-im.org)
	(c)2005 Anar Ibragimoff (ai91@mail.ru)
	(c)2006 Scott Ellis (mail@scottellis.com.au)
	(c)2007,2008 Alexander Turyak (thief@miranda-im.org.ua)

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

CMPlugin g_plugin;

DWORD timer_id = 0;

HGENMENU hContactMenu;

HICON hIcon;
HANDLE hExtraIcon;

// Popup Actions
POPUPACTION missyouactions[1];
POPUPACTION hideactions[2];

extern int UserinfoInit(WPARAM wparam, LPARAM lparam);

static IconItem iconList[] =
{
	{ LPGEN("Tray/popup icon"), "main_icon", IDI_MAINICON },
	{ LPGEN("Enabled"), "enabled_icon", IDI_ENABLED },
	{ LPGEN("Disabled"), "disabled_icon", IDI_DISABLED },
	{ LPGEN("Hide"), "hide_icon", IDI_HIDE },
	{ LPGEN("Never hide"), "neverhide_icon", IDI_NEVERHIDE }
};

/////////////////////////////////////////////////////////////////////////////////////////

PLUGININFOEX pluginInfoEx = {
	sizeof(PLUGININFOEX),
	__PLUGIN_NAME,
	PLUGIN_MAKE_VERSION(__MAJOR_VERSION, __MINOR_VERSION, __RELEASE_NUM, __BUILD_NUM),
	__DESCRIPTION,
	__AUTHOR,
	__COPYRIGHT,
	__AUTHORWEB,
	UNICODE_AWARE,
	// {DDF8AEC9-7D37-49AF-9D22-BBBC920E6F05}
	{0xddf8aec9, 0x7d37, 0x49af, {0x9d, 0x22, 0xbb, 0xbc, 0x92, 0x0e, 0x6f, 0x05}}
};

CMPlugin::CMPlugin() :
	PLUGIN<CMPlugin>(MODULENAME, pluginInfoEx)
{}

/////////////////////////////////////////////////////////////////////////////////////////

time_t getLastSeen(MCONTACT hContact)
{
	return db_get_dw(hContact, MODULENAME, "LastSeen", db_get_dw(hContact, MODULENAME, "CreationTime", (DWORD)-1));
}

void setLastSeen(MCONTACT hContact)
{
	db_set_dw(hContact, MODULENAME, "LastSeen", (DWORD)time(0));
	if (db_get_b(hContact, MODULENAME, "StillAbsentNotified", 0))
		db_set_b(hContact, MODULENAME, "StillAbsentNotified", 0);
}

time_t getLastInputMsg(MCONTACT hContact)
{
	MEVENT hDbEvent = db_event_last(hContact);
	while (hDbEvent) {
		DBEVENTINFO dbei = {};
		db_event_get(hDbEvent, &dbei);
		if (dbei.eventType == EVENTTYPE_MESSAGE && !(dbei.flags & DBEF_SENT))
			return dbei.timestamp;
		hDbEvent = db_event_prev(hContact, hDbEvent);
	}
	return -1;
}

/**
 * Popup window procedures
 */

LRESULT CALLBACK HidePopupDlgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message) {
	case WM_COMMAND:
		if (HIWORD(wParam) == STN_CLICKED) {
			db_set_b(PUGetContact(hWnd), "CList", "Hidden", 1);
			PUDeletePopup(hWnd);
		}
		break;

	case WM_CONTEXTMENU:
		db_set_b(PUGetContact(hWnd), MODULENAME, "NeverHide", 1);
		PUDeletePopup(hWnd);
		break;

	case UM_POPUPACTION:
		if (wParam == 2) {
			db_set_b(PUGetContact(hWnd), "CList", "Hidden", 1);
			PUDeletePopup(hWnd);
		}
		if (wParam == 3) {
			db_set_b(PUGetContact(hWnd), MODULENAME, "NeverHide", 1);
			PUDeletePopup(hWnd);
		}
		break;

	case UM_FREEPLUGINDATA:
		return TRUE;
	}
	return DefWindowProc(hWnd, message, wParam, lParam);
}

LRESULT CALLBACK MissYouPopupDlgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message) {
	case WM_COMMAND:
		if (HIWORD(wParam) == STN_CLICKED) {
			CallServiceSync("BuddyExpectator/actionMissYou", (WPARAM)PUGetContact(hWnd), 0);
			if (!db_get_b(PUGetContact(hWnd), MODULENAME, "MissYouNotifyAlways", 0)) {
				db_set_b(PUGetContact(hWnd), MODULENAME, "MissYou", 0);
				ExtraIcon_Clear(hExtraIcon, PUGetContact(hWnd));
			}
			PUDeletePopup(hWnd);
		}
		break;

	case WM_CONTEXTMENU:
		PUDeletePopup(hWnd);
		break;

	case UM_POPUPACTION:
		if (wParam == 1) {
			db_set_b(PUGetContact(hWnd), MODULENAME, "MissYou", 0);
			ExtraIcon_Clear(hExtraIcon, PUGetContact(hWnd));
			PUDeletePopup(hWnd);
		}
		break;

	case UM_FREEPLUGINDATA:
		return TRUE;
	}
	return DefWindowProc(hWnd, message, wParam, lParam);
}

LRESULT CALLBACK PopupDlgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message) {
	case WM_COMMAND:
		if (HIWORD(wParam) == STN_CLICKED) {
			g_clistApi.pfnRemoveEvent(PUGetContact(hWnd), 0);
			CallServiceSync("BuddyExpectator/actionReturned", PUGetContact(hWnd), 0);
			PUDeletePopup(hWnd);
		}
		break;

	case WM_CONTEXTMENU:
		g_clistApi.pfnRemoveEvent(PUGetContact(hWnd), 0);
		setLastSeen(PUGetContact(hWnd));
		PUDeletePopup(hWnd);
		break;

	case UM_FREEPLUGINDATA:
		if (options.iShowEvent == 0)
			setLastSeen(PUGetContact(hWnd));
		return TRUE;
	}
	return DefWindowProc(hWnd, message, wParam, lParam);
}

LRESULT CALLBACK PopupDlgProcNoSet(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message) {
	case WM_COMMAND:
		if (HIWORD(wParam) == STN_CLICKED) {
			g_clistApi.pfnRemoveEvent(PUGetContact(hWnd), 0);
			CallServiceSync("BuddyExpectator/actionStillAbsent", (WPARAM)PUGetContact(hWnd), 0);
			PUDeletePopup(hWnd);
		}
		break;

	case WM_CONTEXTMENU:
		g_clistApi.pfnRemoveEvent(PUGetContact(hWnd), 0);
		PUDeletePopup(hWnd);
		break;

	case UM_FREEPLUGINDATA:
		return TRUE;
	}
	return DefWindowProc(hWnd, message, wParam, lParam);
}

/**
 * Checks - whether user has been gone for specified number of days
 */
bool isContactGoneFor(MCONTACT hContact, int days)
{
	time_t lastSeen = getLastSeen(hContact);
	time_t lastInputMsg = getLastInputMsg(hContact);
	time_t currentTime = time(0);

	int daysSinceOnline = -1;
	if (lastSeen != -1) daysSinceOnline = (int)((currentTime - lastSeen) / (60 * 60 * 24));

	int daysSinceMessage = -1;
	if (lastInputMsg != -1) daysSinceMessage = (int)((currentTime - lastInputMsg) / (60 * 60 * 24));

	if (options.hideInactive)
		if (daysSinceMessage >= options.iSilencePeriod)
			if (!db_get_b(hContact, "CList", "Hidden", 0) && !db_get_b(hContact, MODULENAME, "NeverHide", 0)) {
				POPUPDATAT_V2 ppd = { 0 };
				ppd.cbSize = sizeof(ppd);
				ppd.lchContact = hContact;
				ppd.lchIcon = IcoLib_GetIcon("enabled_icon");

				mir_snwprintf(ppd.lptzContactName, TranslateT("Hiding %s (%S)"), Clist_GetContactDisplayName(hContact), GetContactProto(hContact));
				mir_snwprintf(ppd.lptzText, TranslateT("%d days since last message"), daysSinceMessage);

				if (!options.iUsePopupColors) {
					ppd.colorBack = options.iPopupColorBack;
					ppd.colorText = options.iPopupColorFore;
				}
				ppd.PluginWindowProc = HidePopupDlgProc;
				ppd.iSeconds = -1;

				hideactions[0].flags = hideactions[1].flags = PAF_ENABLED;
				ppd.lpActions = hideactions;
				ppd.actionCount = 2;

				CallService(MS_POPUP_ADDPOPUPT, (WPARAM)&ppd, APF_NEWDATA);

				Skin_PlaySound("buddyExpectatorHide");
			}

	return (daysSinceOnline >= days && (daysSinceMessage == -1 || daysSinceMessage >= days));
}

void ReturnNotify(MCONTACT hContact, wchar_t *message)
{
	if (db_get_b(hContact, "CList", "NotOnList", 0) == 1 || db_get_b(hContact, "CList", "Hidden", 0) == 1)
		return;

	Skin_PlaySound("buddyExpectatorReturn");

	if (options.iShowPopup > 0) {
		// Display Popup
		POPUPDATAT ppd = { 0 };
		ppd.lchContact = hContact;
		ppd.lchIcon = hIcon;
		wcsncpy(ppd.lptzContactName, Clist_GetContactDisplayName(hContact), MAX_CONTACTNAME);
		wcsncpy(ppd.lptzText, message, MAX_SECONDLINE);
		if (!options.iUsePopupColors) {
			ppd.colorBack = options.iPopupColorBack;
			ppd.colorText = options.iPopupColorFore;
		}
		ppd.PluginWindowProc = PopupDlgProc;
		ppd.PluginData = nullptr;
		ppd.iSeconds = options.iPopupDelay;
		PUAddPopupT(&ppd);
	}

	if (options.iShowEvent > 0) {
		CLISTEVENT cle = {};
		cle.hContact = hContact;
		cle.hIcon = hIcon;
		cle.pszService = "BuddyExpectator/actionReturned";
		cle.flags = CLEF_UNICODE;

		wchar_t* nick = Clist_GetContactDisplayName(hContact);
		wchar_t tmpMsg[512];
		mir_snwprintf(tmpMsg, L"%s %s", nick, message);
		cle.szTooltip.w = tmpMsg;
		g_clistApi.pfnAddEvent(&cle);
	}
}

void GoneNotify(MCONTACT hContact, wchar_t *message)
{
	if (db_get_b(hContact, "CList", "NotOnList", 0) == 1 || db_get_b(hContact, "CList", "Hidden", 0) == 1)
		return;

	if (options.iShowPopup2 > 0) {
		// Display Popup
		POPUPDATAT ppd = { 0 };
		ppd.lchContact = hContact;
		ppd.lchIcon = hIcon;
		wcsncpy(ppd.lptzContactName, Clist_GetContactDisplayName(hContact), MAX_CONTACTNAME);
		wcsncpy(ppd.lptzText, message, MAX_SECONDLINE);
		if (!options.iUsePopupColors) {
			ppd.colorBack = options.iPopupColorBack;
			ppd.colorText = options.iPopupColorFore;
		}
		ppd.PluginWindowProc = PopupDlgProcNoSet;
		ppd.PluginData = nullptr;
		ppd.iSeconds = options.iPopupDelay;

		PUAddPopupT(&ppd);
	}

	if (options.iShowEvent2 > 0) {
		CLISTEVENT cle = {};
		cle.hContact = hContact;
		cle.hIcon = hIcon;
		cle.pszService = "BuddyExpectator/actionStillAbsent";

		wchar_t* nick = Clist_GetContactDisplayName(hContact);
		wchar_t tmpMsg[512];
		mir_snwprintf(tmpMsg, L"%s %s", nick, message);
		cle.szTooltip.w = tmpMsg;
		cle.flags = CLEF_UNICODE;
		g_clistApi.pfnAddEvent(&cle);
	}
}

/**
 * Miss you action (clist event click)
 *  when called from popup, wParam = (HANDLE)hContact and lParam == 0,
 *  when called from clist event, wParam = hWndCList, lParam = &CLISTEVENT
 */
INT_PTR MissYouAction(WPARAM wParam, LPARAM lParam)
{
	MCONTACT hContact;
	if (lParam) {
		CLISTEVENT* cle = (CLISTEVENT*)lParam;
		hContact = cle->hContact;
	}
	else hContact = wParam;

	CallService(MS_MSG_SENDMESSAGEW, hContact, 0);
	return 0;
}

/**
 * Contact returned action (clist event click)
 *  when called from popup, wParam = (HANDLE)hContact and lParam == 0,
 *  when called from clist event, wParam = hWndCList, lParam = &CLISTEVENT
 */
INT_PTR ContactReturnedAction(WPARAM hContact, LPARAM lParam)
{
	if (lParam) {
		CLISTEVENT *cle = (CLISTEVENT*)lParam;
		hContact = cle->hContact;
	}

	if (options.iShowMessageWindow > 0)
		CallService(MS_MSG_SENDMESSAGEW, hContact, 0);

	if (options.iShowUDetails > 0)
		CallService(MS_USERINFO_SHOWDIALOG, hContact, 0);

	setLastSeen(hContact);
	return 0;
}

/**
 * Contact not returned action (clist event click)
 *  when called from popup, wParam = (HANDLE)hContact and lParam == 0,
 *  when called from clist event, wParam = hWndCList, lParam = &CLISTEVENT
 */
INT_PTR ContactStillAbsentAction(WPARAM hContact, LPARAM lParam)
{
	if (lParam) {
		CLISTEVENT* cle = (CLISTEVENT*)lParam;
		hContact = cle->hContact;
	}

	switch (options.action2) {
	case GCA_DELETE:
		db_delete_contact(hContact);
		break;
	case GCA_UDETAILS:
		CallService(MS_USERINFO_SHOWDIALOG, hContact, 0);
		break;
	case GCA_MESSAGE:
		CallService(MS_MSG_SENDMESSAGE, hContact, 0);
		break;
	case GCA_NOACTION:
		break;
	}

	return 0;
}

/**
 * Load icons either from icolib or built in
 */
int onIconsChanged(WPARAM, LPARAM)
{
	hIcon = IcoLib_GetIcon("main_icon");
	return 0;
}

/**
 * Menu item click action
 */
INT_PTR MenuMissYouClick(WPARAM hContact, LPARAM)
{
	if (db_get_b(hContact, MODULENAME, "MissYou", 0)) {
		db_set_b(hContact, MODULENAME, "MissYou", 0);
		ExtraIcon_Clear(hExtraIcon, hContact);
	}
	else {
		db_set_b(hContact, MODULENAME, "MissYou", 1);
		ExtraIcon_SetIconByName(hExtraIcon, hContact, "enabled_icon");
	}

	return 0;
}

/**
 * Menu is about to appear
 */
int onPrebuildContactMenu(WPARAM hContact, LPARAM)
{
	char *proto = GetContactProto(hContact);
	if (!proto)
		return 0;

	if (db_get_b(hContact, MODULENAME, "MissYou", 0))
		Menu_ModifyItem(hContactMenu, LPGENW("Disable Miss You"), iconList[1].hIcolib);
	else
		Menu_ModifyItem(hContactMenu, LPGENW("Enable Miss You"), iconList[2].hIcolib);

	Menu_ShowItem(hContactMenu, !db_get_b(hContact, proto, "ChatRoom", 0) && (CallProtoService(proto, PS_GETCAPS, PFLAGNUM_1, 0) & PF1_IMSEND));
	return 0;
}

int onExtraImageApplying(WPARAM hContact, LPARAM)
{
	if (db_get_b(hContact, MODULENAME, "MissYou", 0))
		ExtraIcon_SetIconByName(hExtraIcon, hContact, "enabled_icon");

	return 0;
}

/**
 * ContactSettingChanged callback
 */
int SettingChanged(WPARAM hContact, LPARAM lParam)
{
	DBCONTACTWRITESETTING *inf = (DBCONTACTWRITESETTING*)lParam;
	if (hContact == NULL || inf->value.type == DBVT_DELETED || strcmp(inf->szSetting, "Status") != 0)
		return 0;

	if (db_get_b(hContact, "CList", "NotOnList", 0) == 1)
		return 0;

	char *proto = GetContactProto(hContact);
	if (proto == nullptr || (db_get_b(hContact, proto, "ChatRoom", 0) == 1)
		|| !(CallProtoService(proto, PS_GETCAPS, PFLAGNUM_1, 0) & PF1_IMSEND))
		return 0;

	int currentStatus = inf->value.wVal;
	int prevStatus = db_get_w(hContact, "UserOnline", "OldStatus", ID_STATUS_OFFLINE);

	if (currentStatus == prevStatus)
		return 0;

	// Last status
	db_set_dw(hContact, MODULENAME, "LastStatus", prevStatus);

	if (prevStatus == ID_STATUS_OFFLINE) {
		if (db_get_b(hContact, MODULENAME, "MissYou", 0)) {
			// Display Popup
			POPUPDATAT_V2 ppd = { 0 };
			ppd.cbSize = sizeof(ppd);

			ppd.lchContact = hContact;
			ppd.lchIcon = IcoLib_GetIcon("enabled_icon");
			wcsncpy(ppd.lptzContactName, Clist_GetContactDisplayName(hContact), MAX_CONTACTNAME);
			wcsncpy(ppd.lptzText, TranslateT("You awaited this contact!"), MAX_SECONDLINE);
			if (!options.iUsePopupColors) {
				ppd.colorBack = options.iPopupColorBack;
				ppd.colorText = options.iPopupColorFore;
			}
			ppd.PluginWindowProc = MissYouPopupDlgProc;
			ppd.PluginData = nullptr;
			ppd.iSeconds = -1;

			missyouactions[0].flags = PAF_ENABLED;
			ppd.lpActions = missyouactions;
			ppd.actionCount = 1;

			CallService(MS_POPUP_ADDPOPUPT, (WPARAM)&ppd, APF_NEWDATA);

			Skin_PlaySound("buddyExpectatorMissYou");
		}
	}

	if (currentStatus == ID_STATUS_OFFLINE) {
		setLastSeen(hContact);
		return 0;
	}

	if (db_get_dw(hContact, MODULENAME, "LastSeen", (DWORD)-1) == (DWORD)-1 && options.notifyFirstOnline) {
		ReturnNotify(hContact, TranslateT("has gone online for the first time."));
		setLastSeen(hContact);
	}

	unsigned int AbsencePeriod = db_get_dw(hContact, MODULENAME, "iAbsencePeriod", options.iAbsencePeriod);
	if (isContactGoneFor(hContact, AbsencePeriod)) {
		wchar_t* message = TranslateT("has returned after a long absence.");
		wchar_t tmpBuf[251] = { 0 };
		time_t tmpTime = getLastSeen(hContact);
		if (tmpTime != -1) {
			wcsftime(tmpBuf, 250, TranslateT("has returned after being absent since %#x"), gmtime(&tmpTime));
			message = tmpBuf;
		}
		else {
			tmpTime = getLastInputMsg(hContact);
			if (tmpTime != -1) {
				wcsftime(tmpBuf, 250, TranslateT("has returned after being absent since %#x"), gmtime(&tmpTime));
				message = tmpBuf;
			}
		}

		ReturnNotify(hContact, message);

		if ((options.iShowMessageWindow == 0 && options.iShowUDetails == 0) || (options.iShowEvent == 0 && options.iShowPopup == 0))
			setLastSeen(hContact);
	}
	else setLastSeen(hContact);

	return 0;
}

void CALLBACK TimerProc(HWND, UINT, UINT_PTR, DWORD)
{
	for (auto &hContact : Contacts()) {
		char *proto = GetContactProto(hContact);
		if (proto && (db_get_b(hContact, proto, "ChatRoom", 0) == 0) && (CallProtoService(proto, PS_GETCAPS, PFLAGNUM_1, 0) & PF1_IMSEND) && isContactGoneFor(hContact, options.iAbsencePeriod2) && (db_get_b(hContact, MODULENAME, "StillAbsentNotified", 0) == 0))
		{
			db_set_b(hContact, MODULENAME, "StillAbsentNotified", 1);
			Skin_PlaySound("buddyExpectatorStillAbsent");

			wchar_t* message = TranslateT("has not returned after a long absence.");
			time_t tmpTime;
			wchar_t tmpBuf[251] = { 0 };
			tmpTime = getLastSeen(hContact);
			if (tmpTime != -1)
			{
				wcsftime(tmpBuf, 250, TranslateT("has not returned after being absent since %#x"), gmtime(&tmpTime));
				message = tmpBuf;
			}
			else
			{
				tmpTime = getLastInputMsg(hContact);
				if (tmpTime != -1)
				{
					wcsftime(tmpBuf, 250, TranslateT("has not returned after being absent since %#x"), gmtime(&tmpTime));
					message = tmpBuf;
				}
			}

			GoneNotify(hContact, message);
		}
	}
}
/**
 * Called when all the modules have had their modules loaded event handlers called (dependence of popups on fontservice :( )
 */
int ModulesLoaded2(WPARAM, LPARAM)
{
	// check for 'still absent' contacts  on startup
	TimerProc(nullptr, 0, 0, 0);
	return 0;
}

/**
 * Called when all the modules are loaded
 */

int ModulesLoaded(WPARAM, LPARAM)
{
	HookEvent(ME_USERINFO_INITIALISE, UserinfoInit);

	// add sounds support
	g_plugin.addSound("buddyExpectatorReturn", LPGENW("BuddyExpectator"), LPGENW("Contact returned"));
	g_plugin.addSound("buddyExpectatorStillAbsent", LPGENW("BuddyExpectator"), LPGENW("Contact still absent"));
	g_plugin.addSound("buddyExpectatorMissYou", LPGENW("BuddyExpectator"), LPGENW("Miss you event"));
	g_plugin.addSound("buddyExpectatorHide", LPGENW("BuddyExpectator"), LPGENW("Hide contact event"));

	timer_id = SetTimer(nullptr, 0, 1000 * 60 * 60 * 4, TimerProc); // check every 4 hours

	HookEvent(ME_SYSTEM_MODULESLOADED, ModulesLoaded2);

	////////////////////////////////////////////////////////////////////////////

	if (options.enableMissYou) {
		HookEvent(ME_CLIST_PREBUILDCONTACTMENU, onPrebuildContactMenu);

		CMenuItem mi(&g_plugin);
		SET_UID(mi, 0xc48c31d4, 0x56b6, 0x48c6, 0x8e, 0xe9, 0xe6, 0x57, 0xb5, 0x80, 0xb8, 0x1e);
		mi.flags = CMIF_UNICODE;
		mi.hIcolibItem = iconList[2].hIcolib;
		mi.position = 200000;
		mi.name.w = LPGENW("Enable Miss You");
		mi.pszService = "BuddyExpectator/actionMissYouClick";
		hContactMenu = Menu_AddContactMenuItem(&mi);
	}

	missyouactions[0].cbSize = sizeof(POPUPACTION);
	missyouactions[0].lchIcon = IcoLib_GetIcon("disabled_icon");
	strncpy_s(missyouactions[0].lpzTitle, LPGEN("Disable Miss You"), _TRUNCATE);
	missyouactions[0].wParam = missyouactions[0].lParam = 1;

	hideactions[0].cbSize = sizeof(POPUPACTION);
	hideactions[0].lchIcon = IcoLib_GetIcon("hide_icon");
	strncpy_s(hideactions[0].lpzTitle, LPGEN("Hide contact"), _TRUNCATE);
	hideactions[0].wParam = hideactions[0].lParam = 2;

	hideactions[1].cbSize = sizeof(POPUPACTION);
	hideactions[1].lchIcon = IcoLib_GetIcon("neverhide_icon");
	strncpy_s(hideactions[1].lpzTitle, LPGEN("Never hide this contact"), _TRUNCATE);
	hideactions[1].wParam = hideactions[1].lParam = 3;

	return 0;
}

int ContactAdded(WPARAM hContact, LPARAM)
{
	db_set_dw(hContact, MODULENAME, "CreationTime", (DWORD)time(0));
	return 0;
}

int onShutdown(WPARAM, LPARAM)
{
	IcoLib_ReleaseIcon(hIcon);
	return 0;
}

int CMPlugin::Load()
{
	LoadOptions();
	HookEvent(ME_OPT_INITIALISE, OptionsInit);

	CreateServiceFunction("BuddyExpectator/actionReturned", ContactReturnedAction);
	CreateServiceFunction("BuddyExpectator/actionStillAbsent", ContactStillAbsentAction);
	CreateServiceFunction("BuddyExpectator/actionMissYou", MissYouAction);
	CreateServiceFunction("BuddyExpectator/actionMissYouClick", MenuMissYouClick);

	HookEvent(ME_DB_CONTACT_SETTINGCHANGED, SettingChanged);
	HookEvent(ME_SYSTEM_MODULESLOADED, ModulesLoaded);
	HookEvent(ME_SYSTEM_PRESHUTDOWN, onShutdown);

	HookEvent(ME_DB_CONTACT_ADDED, ContactAdded);

	// ensure all contacts are timestamped
	DWORD current_time = (DWORD)time(0);
	for (auto &hContact : Contacts())
		if (!db_get_dw(hContact, MODULENAME, "CreationTime"))
			db_set_dw(hContact, MODULENAME, "CreationTime", current_time);

	g_plugin.registerIcon("BuddyExpectator", iconList);

	HookEvent(ME_SKIN2_ICONSCHANGED, onIconsChanged);

	onIconsChanged(0, 0);

	hExtraIcon = ExtraIcon_RegisterIcolib("buddy_exp", LPGEN("Buddy Expectator"), "enabled_icon");

	return 0;
}

int CMPlugin::Unload()
{
	KillTimer(nullptr, timer_id);

	return 0;
}
