#ifndef _OPTIONS_H_
#define _OPTIONS_H_

class COptionsMain : public CDlgBase
{
private:
	CCtrlListView m_feeds;
	CCtrlButton m_add;
	CCtrlButton m_change;
	CCtrlButton m_delete;
	CCtrlButton m_import;
	CCtrlButton m_export;
	CCtrlCheck m_checkonstartup;

protected:
	void OnInitDialog();
	void OnApply();

	void OnAddButtonClick(CCtrlBase*);
	void OnChangeButtonClick(CCtrlBase*);
	void OnDeleteButtonClick(CCtrlBase*);
	void OnImportButtonClick(CCtrlBase*);
	void OnExportButtonClick(CCtrlBase*);

	void OnFeedListItemChanged(CCtrlListView::TEventInfo *evt);
	void OnFeedListDoubleClick(CCtrlBase*);

	void UpdateList();

public:
	COptionsMain();
};

class CFeedEditor : public CDlgBase
{
	friend class CAuthRequest;

private:
	typedef CDlgBase CSuper;

	int m_iItem;
	CCtrlListView *m_list;
	MCONTACT m_hContact;

	CCtrlEdit m_feedtitle;
	CCtrlEdit m_feedurl;
	CCtrlEdit m_checktime; 
	CCtrlSpin m_checktimespin;
	CCtrlButton m_checkfeed;
	CCtrlEdit m_tagedit;
	CCtrlButton m_reset;
	CCtrlButton m_help;
	CCtrlButton m_ok;

protected:
	void OnInitDialog();
	void OnCheckFeed(CCtrlBase*);
	void OnReset(CCtrlBase*);
	void OnHelp(CCtrlBase*);
	void OnOk(CCtrlBase*);
	void OnClose();
	void OnUseAuth(CCtrlBase*);

public:
	CCtrlCheck m_useauth;
	CCtrlEdit m_login;
	CCtrlEdit m_password;

	CFeedEditor(int iItem, CCtrlListView *m_list, MCONTACT Contact);

	__inline MCONTACT getContact() const { return m_hContact; }
};

class CImportFeed : public CDlgBase
{
private:
	typedef CDlgBase CSuper;

	CCtrlListView *m_list;

	CCtrlEdit m_importfile;
	CCtrlButton m_browsefile;
	CCtrlListBox m_feedslist;
	CCtrlListBox m_feedsimportlist;
	CCtrlButton m_addfeed;
	CCtrlButton m_removefeed;
	CCtrlButton m_addallfeeds;
	CCtrlButton m_removeallfeeds;
	CCtrlButton m_ok;

protected:
	void OnInitDialog();
	void OnBrowseFile(CCtrlBase*);
	void OnAddFeed(CCtrlBase*);
	void OnRemoveFeed(CCtrlBase*);
	void OnAddAllFeeds(CCtrlBase*);
	void OnRemoveAllFeeds(CCtrlBase*);
	void OnOk(CCtrlBase*);
	void OnClose();

	void OnFeedsList(CCtrlBase*);
	void OnFeedsImportList(CCtrlBase*);

public:
	CImportFeed(CCtrlListView *m_list);
};

class CExportFeed : public CDlgBase
{
private:
	typedef CDlgBase CSuper;

	CCtrlListBox m_feedslist;
	CCtrlListBox m_feedsexportlist;
	CCtrlButton m_addfeed;
	CCtrlButton m_removefeed;
	CCtrlButton m_addallfeeds;
	CCtrlButton m_removeallfeeds;
	CCtrlButton m_ok;

protected:
	void OnInitDialog();
	void OnAddFeed(CCtrlBase*);
	void OnRemoveFeed(CCtrlBase*);
	void OnAddAllFeeds(CCtrlBase*);
	void OnRemoveAllFeeds(CCtrlBase*);
	void OnOk(CCtrlBase*);
	void OnClose();

	void OnFeedsList(CCtrlBase*);
	void OnFeedsExportList(CCtrlBase*);

public:
	CExportFeed();
};

class CAuthRequest : public CDlgBase
{
private:
	typedef CDlgBase CSuper;

	CFeedEditor *m_pDlg;
	MCONTACT m_hContact;

	CCtrlBase m_feedname;
	CCtrlEdit m_username;
	CCtrlEdit m_password;
	CCtrlButton m_ok;

protected:
	void OnInitDialog();
	void OnOk(CCtrlBase*);

public:
	CAuthRequest(CFeedEditor *pDlg, MCONTACT hContact);
};

#endif //_OPTIONS_H_