#include "stdafx.h"

int TplMeasureVars(TemplateVars* vars, wchar_t* str);

wchar_t *TplFormatStringEx(int tpl, wchar_t *sztpl, MCONTACT hContact, HistoryArray::ItemData *item)
{
	if ((tpl < 0) || (tpl >= TPL_COUNT) || !sztpl)
		return mir_wstrdup(L"");

	TemplateVars vars;
	memset(&vars, 0, sizeof(vars));

	for (int i = 0; i < TemplateInfo::VF_COUNT; i++)
		if (templates[tpl].vf[i])
			templates[tpl].vf[i](VFM_VARS, &vars, hContact, item);

	wchar_t *buf = (wchar_t *)mir_alloc(sizeof(wchar_t) * (TplMeasureVars(&vars, sztpl) + 1));
	wchar_t *bufptr = buf;
	for (wchar_t *p = sztpl; *p; p++) {
		if (*p == '%') {
			wchar_t *var = vars.GetVar((p[1] & 0xff));
			if (var) {
				mir_wstrcpy(bufptr, var);
				bufptr += mir_wstrlen(var);
			}
			p++;
		}
		else *bufptr++ = *p;
	}
	*bufptr = 0;
	return buf;
}

wchar_t *TplFormatString(int tpl, MCONTACT hContact, HistoryArray::ItemData *item)
{
	if ((tpl < 0) || (tpl >= TPL_COUNT))
		return mir_wstrdup(L"");

	if (!templates[tpl].value)
		templates[tpl].value = mir_wstrdup(templates[tpl].defvalue);

	int i;
	TemplateVars vars;
	for (i = 0; i < 256; i++) {
		vars.del[i] = false;
		vars.val[i] = 0;
	}

	for (i = 0; i < TemplateInfo::VF_COUNT; i++)
		if (templates[tpl].vf[i])
			templates[tpl].vf[i](VFM_VARS, &vars, hContact, item);

	wchar_t *buf = (wchar_t *)mir_alloc(sizeof(wchar_t) * (TplMeasureVars(&vars, templates[tpl].value) + 1));
	wchar_t *bufptr = buf;
	for (wchar_t *p = templates[tpl].value; *p; p++) {
		if (*p == '%') {
			wchar_t *var = vars.GetVar((p[1] & 0xff));
			if (var) {
				mir_wstrcpy(bufptr, var);
				bufptr += mir_wstrlen(var);
			}
			p++;
		}
		else *bufptr++ = *p;
	}
	*bufptr = 0;
	return buf;
}

// Variable management
void TplInitVars(TemplateVars *vars)
{
	for (int i = 0; i < 256; i++) {
		vars->val[i] = 0;
		vars->del[i] = false;
	}
}

void TplCleanVars(TemplateVars *vars)
{
	for (int i = 0; i < 256; i++)
		if (vars->val[i] && vars->del[i]) {
			mir_free(vars->val[i]);
			vars->val[i] = 0;
			vars->del[i] = false;
		}
}

int TplMeasureVars(TemplateVars *vars, wchar_t *str)
{
	int res = 0;
	for (wchar_t *p = str; *p; p++) {
		if (*p == '%') {
			wchar_t *var = vars->GetVar(p[1] & 0xff);
			if (var)
				res += (int)mir_wstrlen(var);
			p++;
		}
		else res++;
	}
	return res;
}

// Loading variables
void vfGlobal(int, TemplateVars *vars, MCONTACT hContact, HistoryArray::ItemData *)
{
	//  %%: simply % character
	vars->SetVar('%', L"%", false);

	//  %n: line break
	vars->SetVar('n', L"\x0d\x0a", false);

	// %S: my nick (not for messages)
	char* proto = Proto_GetBaseAccountName(hContact);
	ptrW nick(Contact_GetInfo(CNF_DISPLAY, 0, proto));
	vars->SetVar('S', nick, false);
}

void vfContact(int, TemplateVars *vars, MCONTACT hContact, HistoryArray::ItemData *)
{
	// %N: buddy's nick (not for messages)
	wchar_t *nick = Clist_GetContactDisplayName(hContact, 0);
	vars->SetVar('N', nick, false);

	wchar_t buf[20];
	// %c: event count
	mir_snwprintf(buf, L"%d", db_event_count(hContact));
	vars->SetVar('c', buf, false);
}

void vfSystem(int, TemplateVars *vars, MCONTACT hContact, HistoryArray::ItemData *)
{
	// %N: buddy's nick (not for messages)
	vars->SetVar('N', LPGENW("System event"), false);

	// %c: event count
	wchar_t  buf[20];
	mir_snwprintf(buf, L"%d", db_event_count(hContact));
	vars->SetVar('c', buf, false);
}

void vfEvent(int, TemplateVars *vars, MCONTACT, HistoryArray::ItemData *item)
{
	HICON hIcon;
	wchar_t buf[100];

	//  %N: Nickname
	if (item->dbe.flags & DBEF_SENT) {
		char *proto = Proto_GetBaseAccountName(item->hContact);
		ptrW nick(Contact_GetInfo(CNF_DISPLAY, 0, proto));
		vars->SetVar('N', nick, false);
	}
	else {
		wchar_t *nick = Clist_GetContactDisplayName(item->hContact, 0);
		vars->SetVar('N', nick, false);
	}

	//  %I: Icon
	switch (item->dbe.eventType) {
	case EVENTTYPE_MESSAGE:
		hIcon = g_plugin.getIcon(ICO_SENDMSG);
		break;
	case EVENTTYPE_FILE:
		hIcon = g_plugin.getIcon(ICO_FILE);
		break;
	case EVENTTYPE_STATUSCHANGE:
		hIcon = g_plugin.getIcon(ICO_SIGNIN);
		break;
	default:
		hIcon = g_plugin.getIcon(ICO_UNKNOWN);
		break;
	}
	mir_snwprintf(buf, L"[$hicon=%d$]", hIcon);
	vars->SetVar('I', buf, true);

	//  %i: Direction icon
	if (item->dbe.flags & DBEF_SENT)
		hIcon = g_plugin.getIcon(ICO_MSGOUT);
	else
		hIcon = g_plugin.getIcon(ICO_MSGIN);

	mir_snwprintf(buf, L"[$hicon=%d$]", hIcon);
	vars->SetVar('i', buf, true);

	// %D: direction symbol
	if (item->dbe.flags & DBEF_SENT)
		vars->SetVar('D', L"<<", false);
	else
		vars->SetVar('D', L">>", false);

	//  %t: timestamp
	_tcsftime(buf, _countof(buf), L"%d.%m.%Y, %H:%M", _localtime32((__time32_t *)&item->dbe.timestamp));
	vars->SetVar('t', buf, true);

	//  %h: hour (24 hour format, 0-23)
	_tcsftime(buf, _countof(buf), L"%H", _localtime32((__time32_t *)&item->dbe.timestamp));
	vars->SetVar('h', buf, true);

	//  %a: hour (12 hour format)
	_tcsftime(buf, _countof(buf), L"%h", _localtime32((__time32_t *)&item->dbe.timestamp));
	vars->SetVar('a', buf, true);

	//  %m: minute
	_tcsftime(buf, _countof(buf), L"%M", _localtime32((__time32_t *)&item->dbe.timestamp));
	vars->SetVar('m', buf, true);

	//  %s: second
	_tcsftime(buf, _countof(buf), L"%S", _localtime32((__time32_t *)&item->dbe.timestamp));
	vars->SetVar('s', buf, true);

	//  %o: month
	_tcsftime(buf, _countof(buf), L"%m", _localtime32((__time32_t *)&item->dbe.timestamp));
	vars->SetVar('o', buf, true);

	//  %d: day of month
	_tcsftime(buf, _countof(buf), L"%d", _localtime32((__time32_t *)&item->dbe.timestamp));
	vars->SetVar('d', buf, true);

	//  %y: year
	_tcsftime(buf, _countof(buf), L"%Y", _localtime32((__time32_t *)&item->dbe.timestamp));
	vars->SetVar('y', buf, true);

	//  %w: day of week (Sunday, Monday... translatable)
	_tcsftime(buf, _countof(buf), L"%A", _localtime32((__time32_t *)&item->dbe.timestamp));
	vars->SetVar('w', TranslateW(buf), false);

	//  %p: AM/PM symbol
	_tcsftime(buf, _countof(buf), L"%p", _localtime32((__time32_t *)&item->dbe.timestamp));
	vars->SetVar('p', buf, true);

	//  %O: Name of month, translatable
	_tcsftime(buf, _countof(buf), L"%B", _localtime32((__time32_t *)&item->dbe.timestamp));
	vars->SetVar('O', TranslateW(buf), false);
}

void vfMessage(int, TemplateVars *vars, MCONTACT, HistoryArray::ItemData *item)
{
	//  %M: the message string itself
	vars->SetVar('M', item->getWBuf(), false);
}

void vfFile(int, TemplateVars *vars, MCONTACT, HistoryArray::ItemData *item)
{
	//  %M: the message string itself
	vars->SetVar('M', item->getWBuf(), false);
}

void vfUrl(int, TemplateVars *vars, MCONTACT, HistoryArray::ItemData *item)
{
	//  %M: the message string itself
	vars->SetVar('M', item->getWBuf(), false);
}

void vfSign(int, TemplateVars *vars, MCONTACT, HistoryArray::ItemData *item)
{
	//  %M: the message string itself
	vars->SetVar('M', item->getWBuf(), false);
}

void vfAuth(int, TemplateVars *vars, MCONTACT, HistoryArray::ItemData *item)
{
	//  %M: the message string itself
	vars->SetVar('M', item->getWBuf(), false);
}

void vfAdded(int, TemplateVars *vars, MCONTACT, HistoryArray::ItemData *item)
{
	//  %M: the message string itself
	vars->SetVar('M', item->getWBuf(), false);
}

void vfPresence(int, TemplateVars* vars, MCONTACT, HistoryArray::ItemData* item)
{
	//  %M: the message string itself
	vars->SetVar('M', item->getWBuf(), false);
}

void vfDeleted(int, TemplateVars *vars, MCONTACT, HistoryArray::ItemData *item)
{
	//  %M: the message string itself
	vars->SetVar('M', item->getWBuf(), false);
}

void vfOther(int, TemplateVars *vars, MCONTACT, HistoryArray::ItemData *)
{
	//  %M: the message string itself
	vars->SetVar('M', LPGENW("Unknown event"), false);
}

/////////////////////////////////////////////////////////////////////////////////////////

TemplateInfo templates[TPL_COUNT] =
{
	{ "tpl/interface/title", LPGENW("Interface"), ICO_NEWSTORY, LPGENW("Window title"),
		L"%N - History [%c messages total]", 0, 0,
		{ vfGlobal, vfContact, 0, 0, 0 } },

	{ "tpl/msglog/msg", LPGENW("Message log"), ICO_SENDMSG, LPGENW("Messages"),
		L"%I%i[b]%N, %t:[/b]\x0d\x0a%M", 0, 0,
		{ vfGlobal, vfContact, vfEvent, vfMessage, 0 } },
	{ "tpl/msglog/file", LPGENW("Message log"), ICO_FILE, LPGENW("Files"),
		L"%I%i[b]%N, %t:[/b]%n%M", 0, 0,
		{ vfGlobal, vfContact, vfEvent, vfFile, 0 } },
	{ "tpl/msglog/status", LPGENW("Message log"), ICO_SIGNIN, LPGENW("Status changes"),
		L"%I%i[b]%N, %t:[/b]%n%M", 0, 0,
		{ vfGlobal, vfContact, vfEvent, vfSign, 0 } },
	{ "tpl/msglog/presense", LPGENW("Message log"), ICO_UNKNOWN, LPGENW("Presence requests"),
		L"%I%i[b]%N, %t:[/b]%n%M", 0, 0,
		{ vfGlobal, vfContact, vfEvent, vfPresence, 0 } },
	{ "tpl/msglog/other", LPGENW("Message log"), ICO_UNKNOWN, LPGENW("Other events"),
		L"%I%i[b]%N, %t:[/b]%n%M", 0, 0,
		{ vfGlobal, vfContact, vfEvent, vfOther, 0 } },

	{ "tpl/msglog/authrq", LPGENW("Message log"), ICO_UNKNOWN, LPGENW("Authorization requests"),
		L"%I%i[b]%N, %t:[/b]%n%M", 0, 0,
		{ vfGlobal, vfEvent, vfSystem, vfAuth, 0 } },
	{ "tpl/msglog/added", LPGENW("Message log"), ICO_UNKNOWN, LPGENW("'You were added' events"),
		L"%I%i[b]%N, %t:[/b]%n%M", 0, 0,
		{ vfGlobal, vfEvent, vfSystem, vfAdded, 0 } },
	{ "tpl/msglog/deleted", LPGENW("Message log"), ICO_UNKNOWN, LPGENW("'You were deleted' events"),
		L"%I%i[b]%N, %t:[/b]%n%M", 0, 0,
		{ vfGlobal, vfEvent, vfSystem, vfDeleted, 0 } },

	{ "tpl/copy/msg", LPGENW("Clipboard"), ICO_SENDMSG, LPGENW("Messages"),
		L"%N, %t:\x0d\x0a%M%n", 0, 0,
		{ vfGlobal, vfContact, vfEvent, vfMessage, 0 } },
	{ "tpl/copy/file", LPGENW("Clipboard"), ICO_FILE, LPGENW("Files"),
		L"%N, %t:\x0d\x0a%M%n", 0, 0,
		{ vfGlobal, vfContact, vfEvent, vfFile, 0 } },
	{ "tpl/copy/url", LPGENW("Clipboard"), ICO_URL, LPGENW("URLs"),
		L"%N, %t:\x0d\x0a%M%n", 0, 0,
		{ vfGlobal, vfContact, vfEvent, vfUrl, 0 } },
	{ "tpl/copy/status", LPGENW("Clipboard"), ICO_SIGNIN, LPGENW("Status changes"),
		L"%N, %t:\x0d\x0a%M%n", 0, 0,
		{ vfGlobal, vfContact, vfEvent, vfSign, 0 } },
	{ "tpl/copy/presence", LPGENW("Clipboard"), ICO_UNKNOWN, LPGENW("Presence requests"),
		L"%N, %t:\x0d\x0a%M%n", 0, 0,
		{ vfGlobal, vfContact, vfEvent, vfPresence, 0 } },
	{ "tpl/copy/other", LPGENW("Clipboard"), ICO_UNKNOWN, LPGENW("Other events"),
		L"%N, %t:\x0d\x0a%M%n", 0, 0,
		{ vfGlobal, vfContact, vfEvent, vfOther, 0 } },

	{ "tpl/copy/authrq", LPGENW("Clipboard"), ICO_UNKNOWN, LPGENW("Authorization requests"),
		L"%N, %t:\x0d\x0a%M%n", 0, 0,
		{ vfGlobal, vfEvent, vfSystem, vfAuth, 0 } },
	{ "tpl/copy/added", LPGENW("Clipboard"), ICO_UNKNOWN, LPGENW("'You were added' events"),
		L"%N, %t:\x0d\x0a%M%n", 0, 0,
		{ vfGlobal, vfEvent, vfSystem, vfAdded, 0 } },
	{ "tpl/copy/deleted", LPGENW("Clipboard"), ICO_UNKNOWN, LPGENW("'You were deleted' events"),
		L"%N, %t:\x0d\x0a%M%n", 0, 0,
		{ vfGlobal, vfEvent, vfSystem, vfDeleted, 0 } }
};

void LoadTemplates()
{
	for (int i = 0; i < TPL_COUNT; i++) {
		DBVARIANT dbv = { 0 };
		db_get_ws(0, MODULENAME, templates[i].setting, &dbv);
		if (templates[i].value)
			mir_free(templates[i].value);
		if (dbv.pwszVal) {
			templates[i].value = mir_wstrdup(dbv.pwszVal);
		}
		else {
			templates[i].value = 0;
		}
		db_free(&dbv);
	}
}

void SaveTemplates()
{
	for (auto &it : templates)
		if (it.value)
			db_set_ws(0, MODULENAME, it.setting, it.value);
}
