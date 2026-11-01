#pragma once

#include "UserConfig.h"

class CTextDlg : public CDialogImpl<CTextDlg>
{
public:
	enum { IDD = IDD_TEXTDLG };

	BEGIN_MSG_MAP_EX(CTextDlg)
		MSG_WM_INITDIALOG(OnInitDialog)
		MSG_WM_DESTROY(OnDestroy)
		MSG_WM_CTLCOLORSTATIC(OnCtlColorStatic)
		MSG_WM_ACTIVATE(OnActivate)
		COMMAND_ID_HANDLER_EX(IDCANCEL, OnCancel)
		COMMAND_HANDLER_EX(IDC_EDIT, EN_CHANGE, OnTextChange)
		MSG_WM_EXITSIZEMOVE(OnExitSizeMove)
		MSG_WM_COMMAND(OnCommand)
		MSG_WM_NOTIFY(OnNotify)
		MSG_WM_GETMINMAXINFO(OnGetMinMaxInfo)
		MSG_WM_SIZE(OnSize)
		MESSAGE_HANDLER_EX(WM_NCHITTEST, OnNcHitTest)
	ALT_MSG_MAP(1)
		MSG_WM_KEYDOWN(OnKeyDown)
		MSG_WM_KEYUP(OnKeyUp)
		MSG_WM_LBUTTONUP(OnLButtonUp)
		MSG_WM_CHAR(OnChar)
	END_MSG_MAP()

	CTextDlg(
		const UserConfig& config,
		const CString& initialText,
		const CString& codeDefaultDestinationLabel,
		const CString& manualDefaultDestinationLabel) :
		m_wndEdit(this, 1),
		m_config(config),
		m_initialText(initialText),
		m_codeDefaultDestinationLabel(codeDefaultDestinationLabel),
		m_manualDefaultDestinationLabel(manualDefaultDestinationLabel) {}

	const CString& GetCodeDefaultDestinationLabel() const { return m_codeDefaultDestinationLabel; }
	const CString& GetManualDefaultDestinationLabel() const { return m_manualDefaultDestinationLabel; }

	BOOL OnIdle();

	BOOL OnInitDialog(CWindow wndFocus, LPARAM lInitParam);
	void OnDestroy();
	HBRUSH OnCtlColorStatic(CDCHandle dc, CStatic wndStatic);
	void OnActivate(UINT nState, BOOL bMinimized, CWindow wndOther);
	void OnCancel(UINT uNotifyCode, int nID, CWindow wndCtl);
	void OnCommand(UINT uNotifyCode, int nID, CWindow wndCtl);
	LRESULT OnNotify(int idCtrl, LPNMHDR pnmh);
	void OnGetMinMaxInfo(LPMINMAXINFO lpMMI);
	void OnSize(UINT nType, CSize size);
	LRESULT OnNcHitTest(UINT uMsg, WPARAM wParam, LPARAM lParam);
	void OnExitSizeMove();
	void OnTextChange(UINT uNotifyCode, int nID, CWindow wndCtl);
	void OnKeyDown(UINT vk, UINT nRepCnt, UINT nFlags);
	void OnKeyUp(UINT vk, UINT nRepCnt, UINT nFlags);
	void OnLButtonUp(UINT nFlags, CPoint point);
	void OnChar(UINT nChar, UINT nRepCnt, UINT nFlags);

private:
	enum class ClippingDestination
	{
		code,
		manual,
	};

	UserConfig m_config;
	CString m_initialText;
	CContainedWindowT<CEdit> m_wndEdit;
	CFont m_editFont;
	std::vector<int> m_editIndexes;
	int m_lastSelStart = 0, m_lastSelEnd = 0;
	CToolTipCtrl m_webButtonTooltip;
	std::vector<CIcon> m_webButtonIcons;
	bool m_pinned = false;
	bool m_showingModalBrowserHost = false;
	CSize m_minSize;
	CString m_codeDefaultDestinationLabel;
	CString m_manualDefaultDestinationLabel;

	void InitWebAppButtons();
	void InitAddToDbButtons();
	void UpdateAddToDbButtonLabels();
	void ShowClippingDestinationMenu(ClippingDestination destination, CWindow button);
	void AdjustWindowLocationAndSize(CPoint ptEvent, CRect rcAccObject, CString strText, CString strDefaultText);
	void OnSelectionMaybeChanged();
	bool SaveCurrentTextToDatabase(
		ClippingDestination destination,
		CString destinationLabel,
		bool makeDefault);
	void MakePinned();
};
