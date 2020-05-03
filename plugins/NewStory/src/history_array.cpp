#include "stdafx.h"

bool Filter::check(ItemData *item)
{
	if (!item) return false;
	if (!(flags & EVENTONLY)) {
		if (item->dbe.flags & DBEF_SENT) {
			if (!(flags & OUTGOING))
				return false;
		}
		else {
			if (!(flags & INCOMING))
				return false;
		}
		switch (item->dbe.eventType) {
		case EVENTTYPE_MESSAGE:
			if (!(flags & MESSAGES))
				return false;
			break;
		case EVENTTYPE_FILE:
			if (!(flags & FILES))
				return false;
			break;
		case EVENTTYPE_STATUSCHANGE:
			if (!(flags & STATUS))
				return false;
			break;
		default:
			if (!(flags & OTHER))
				return false;
		}
	}

	if (flags & (EVENTTEXT | EVENTONLY)) {
		item->loadInline(ItemData::ELM_DATA);
		return CheckFilter(item->getWBuf(), text);
	}
	return true;
};

// Event
bool ItemData::load(EventLoadMode mode)
{
	if (mode == ItemData::ELM_NOTHING)
		return true;

	if ((mode == ItemData::ELM_INFO) && !dbeOk) {
		dbeOk = true;
		dbe.cbBlob = 0;
		dbe.pBlob = 0;
		db_event_get(hEvent, &dbe);
		return true;
	}

	if ((mode == ItemData::ELM_DATA) && (!dbeOk || !dbe.cbBlob)) {
		dbeOk = true;
		dbe.cbBlob = db_event_getBlobSize(hEvent);
		dbe.pBlob = (PBYTE)mir_calloc(dbe.cbBlob + 1);
		db_event_get(hEvent, &dbe);

		wtext = 0;

		switch (dbe.eventType) {
		case EVENTTYPE_STATUSCHANGE:
		case EVENTTYPE_MESSAGE:
			wtext = mir_utf8decodeW((char*)dbe.pBlob);
			wtext_del = false;
			break;

		case EVENTTYPE_JABBER_PRESENCE:
			wtext = DbEvent_GetTextW(&dbe, CP_ACP);
			wtext_del = false;
			break;

		case EVENTTYPE_AUTHREQUEST:
			wtext = new wchar_t[512];
			wtext_del = true;
			if ((dbe.cbBlob > 8) && *(dbe.pBlob + 8)) {
				mir_snwprintf(wtext, 512, L"%s requested authorization", Utf2T((char*)dbe.pBlob + 8).get());
			}
			else {
				mir_snwprintf(wtext, 512, L"%d requested authorization", *(DWORD *)(dbe.pBlob));
			}
			break;

		case EVENTTYPE_ADDED:
			wtext = new wchar_t[512];
			wtext_del = true;
			if ((dbe.cbBlob > 8) && *(dbe.pBlob + 8)) {
				mir_snwprintf(wtext, 512, L"%s added you to the contact list", Utf2T((char *)dbe.pBlob + 8).get());
			}
			else {
				mir_snwprintf(wtext, 512, L"%d added you to the contact list", *(DWORD *)(dbe.pBlob));
			}
			break;
		}

		return true;
	}

	return false;
}

bool ItemData::isGrouped() const
{
	if (pPrev && g_plugin.bMsgGrouping) {
		if (!pPrev->dbeOk)
			pPrev->load(EventLoadMode::ELM_DATA);

		if (pPrev->hContact == hContact && (pPrev->dbe.flags & DBEF_SENT) == (dbe.flags & DBEF_SENT))
			return true;
	}
	return false;
}

ItemData::~ItemData()
{
	if (dbeOk && dbe.pBlob) {
		mir_free(dbe.pBlob);
		dbe.pBlob = 0;
	}
	if (wtext && wtext_del) delete[] wtext;
	if (data) MTextDestroy(data);
}

// Array
HistoryArray::HistoryArray() :
	pages(50),
	strings(50, wcscmp)
{
	pages.insert(new ItemBlock());
}

HistoryArray::~HistoryArray()
{
	clear();
}

void HistoryArray::clear()
{
	for (auto &str : strings)
		mir_free(str);
	strings.destroy();

	pages.destroy();
	iLastPageCounter = 0;
}

void HistoryArray::addChatEvent(SESSION_INFO *si, LOGINFO *lin)
{
	if (si == nullptr)
		return;

	CMStringW wszText;
	bool bTextUsed = Chat_GetDefaultEventDescr(si, lin, wszText);
	if (!bTextUsed && lin->ptszText) {
		if (!wszText.IsEmpty())
			wszText.Append(L": ");
		wszText.Append(g_chatApi.RemoveFormatting(lin->ptszText));
	}

	auto &p = allocateItem();
	p.hContact = si->hContact;
	p.wtext = wszText.Detach();
	p.dbeOk = true;
	p.dbe.cbBlob = 1;
	p.dbe.pBlob = (BYTE *)p.wtext;
	p.dbe.eventType = EVENTTYPE_MESSAGE;
	p.dbe.timestamp = lin->time;

	if (lin->ptszNick) {
		p.wszNick = strings.find(lin->ptszNick);
		if (p.wszNick == nullptr) {
			p.wszNick = mir_wstrdup(lin->ptszNick);
			strings.insert(p.wszNick);
		}
	}
}

bool HistoryArray::addEvent(MCONTACT hContact, MEVENT hEvent, int count)
{
	if (count == -1)
		count = MAXINT;

	int numItems = getCount();
	auto *pPrev = (numItems == 0) ? nullptr : get(numItems - 1);

	for (int i = 0; hEvent && i < count; i++) {
		auto &p = allocateItem();
		p.hContact = hContact;
		p.hEvent = hEvent;
		p.pPrev = pPrev; pPrev = &p;

		hEvent = db_event_next(hContact, hEvent);
	}

	return true;
}

ItemData& HistoryArray::allocateItem()
{
	if (iLastPageCounter == HIST_BLOCK_SIZE - 1) {
		pages.insert(new ItemBlock());
		iLastPageCounter = 0;
	}

	auto &p = pages[pages.getCount() - 1];
	return p.data[iLastPageCounter++];
}

/*
bool HistoryArray::preloadEvents(int count)
{
	for (int i = 0; i < count; ++i)
	{
		preBlock->items[preIndex].load(ItemData::ELM_DATA);
		if (++preIndex == preBlock->count)
		{
			preBlock = preBlock->next;
			if (!preBlock)
				return false;
			preIndex = 0;
		}
	}
	return true;
}
*/

ItemData* HistoryArray::get(int id, ItemData::EventLoadMode mode)
{
	int pageNo = id / HIST_BLOCK_SIZE;
	if (pageNo >= pages.getCount())
		return nullptr;

	auto *p = &pages[pageNo].data[id % HIST_BLOCK_SIZE];
	if (mode != ItemData::ELM_NOTHING)
		p->load(mode);
	return p;
}

int HistoryArray::getCount() const
{
	return (pages.getCount() - 1) * HIST_BLOCK_SIZE + iLastPageCounter;
}
