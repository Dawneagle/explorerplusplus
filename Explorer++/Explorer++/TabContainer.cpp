// Copyright (C) Explorer++ Project
// SPDX-License-Identifier: GPL-3.0-only
// See LICENSE in the top level directory

#include "stdafx.h"
#include "TabContainer.h"
#include "Bookmarks/BookmarkHelper.h"
#include "Config.h"
#include "CoreInterface.h"
#include "DarkModeHelper.h"
#include "Icon.h"
#include "IconResourceLoader.h"
#include "MainResource.h"
#include "PreservedTab.h"
#include "RenameTabDialog.h"
#include "ResourceHelper.h"
#include "ShellBrowser/ShellBrowser.h"
#include "ShellBrowser/ShellNavigationController.h"
#include "TabBacking.h"
#include "TabRestorer.h"
#include "../Helper/CachedIcons.h"
#include "../Helper/Controls.h"
#include "../Helper/DpiCompatibility.h"
#include "../Helper/IconFetcher.h"
#include "../Helper/ImageHelper.h"
#include "../Helper/MenuHelper.h"
#include "../Helper/ShellHelper.h"
#include "../Helper/TabHelper.h"
#include "../Helper/WindowHelper.h"
#include "../Helper/iDirectoryMonitor.h"
#include <boost/algorithm/string.hpp>
#include <boost/range/adaptor/map.hpp>

const UINT TAB_CONTROL_STYLES = WS_VISIBLE | WS_CHILD | TCS_FOCUSNEVER | TCS_SINGLELINE
	| TCS_TOOLTIPS | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;

// clang-format off
const std::map<UINT, Icon> TAB_RIGHT_CLICK_MENU_IMAGE_MAPPINGS = {
	{ IDM_FILE_NEWTAB, Icon::NewTab },
	{ IDM_TAB_REFRESH, Icon::Refresh },
	{ IDM_TAB_CLOSETAB, Icon::CloseTab }
};
// clang-format on

TabContainer *TabContainer::Create(HWND parent, TabNavigationInterface *tabNavigation,
	CoreInterface *coreInterface, FileActionHandler *fileActionHandler, CachedIcons *cachedIcons,
	BookmarkTree *bookmarkTree, HINSTANCE resourceInstance, std::shared_ptr<Config> config)
{
	return new TabContainer(parent, tabNavigation, coreInterface, fileActionHandler, cachedIcons,
		bookmarkTree, resourceInstance, config);
}

TabContainer::TabContainer(HWND parent, TabNavigationInterface *tabNavigation,
	CoreInterface *coreInterface, FileActionHandler *fileActionHandler, CachedIcons *cachedIcons,
	BookmarkTree *bookmarkTree, HINSTANCE resourceInstance, std::shared_ptr<Config> config) :
	ShellDropTargetWindow(CreateTabControl(parent, config->forceSameTabWidth.get())),
	m_tabNavigation(tabNavigation),
	m_coreInterface(coreInterface),
	m_fileActionHandler(fileActionHandler),
	m_cachedIcons(cachedIcons),
	m_bookmarkTree(bookmarkTree),
	m_resourceInstance(resourceInstance),
	m_config(config),
	m_bTabBeenDragged(FALSE),
	m_iPreviousTabSelectionId(-1),
	m_iconFetcher(m_hwnd, cachedIcons),
	m_defaultFolderIconSystemImageListIndex(GetDefaultFolderIconIndex()),
	m_dropTargetIndex(-1)
{
	Initialize(parent);
}

HWND TabContainer::CreateTabControl(HWND parent, BOOL forceSameTabWidth)
{
	UINT styles = TAB_CONTROL_STYLES;

	if (forceSameTabWidth)
	{
		styles |= TCS_FIXEDWIDTH;
	}

	return ::CreateTabControl(parent, styles);
}

void TabContainer::Initialize(HWND parent)
{
	auto &dpiCompat = DpiCompatibility::GetInstance();
	UINT dpi = dpiCompat.GetDpiForWindow(m_hwnd);

	NONCLIENTMETRICS ncm;
	ncm.cbSize = sizeof(ncm);
	dpiCompat.SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICS), &ncm, 0,
		dpi);
	m_tabFont.reset(CreateFontIndirect(&ncm.lfSmCaptionFont));

	if (m_tabFont)
	{
		SendMessage(m_hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(m_tabFont.get()),
			MAKELPARAM(TRUE, 0));
	}

	SHGetImageList(SHIL_SYSSMALL, IID_PPV_ARGS(&m_systemImageList));

	int dpiScaledSize = MulDiv(ICON_SIZE_96DPI, dpi, USER_DEFAULT_SCREEN_DPI);
	m_tabCtrlImageList.reset(
		ImageList_Create(dpiScaledSize, dpiScaledSize, ILC_COLOR32 | ILC_MASK, 0, 100));
	TabCtrl_SetImageList(m_hwnd, m_tabCtrlImageList.get());

	AddDefaultTabIcons(m_tabCtrlImageList.get());

	auto &darkModeHelper = DarkModeHelper::GetInstance();

	if (darkModeHelper.IsDarkModeEnabled())
	{
		HWND tooltips = TabCtrl_GetToolTips(m_hwnd);
		darkModeHelper.AllowDarkModeForWindow(tooltips, true);
		SetWindowTheme(tooltips, L"Explorer", nullptr);
	}

	m_windowSubclasses.push_back(std::make_unique<WindowSubclassWrapper>(m_hwnd, WndProcStub,
		reinterpret_cast<DWORD_PTR>(this)));
	m_windowSubclasses.push_back(std::make_unique<WindowSubclassWrapper>(parent, ParentWndProcStub,
		reinterpret_cast<DWORD_PTR>(this)));

	m_connections.push_back(
		tabCreatedSignal.AddObserver(std::bind_front(&TabContainer::OnTabCreated, this)));
	m_connections.push_back(tabNavigationCommittedSignal.AddObserver(
		std::bind_front(&TabContainer::OnNavigationCommitted, this)));
	m_connections.push_back(
		tabSelectedSignal.AddObserver(std::bind_front(&TabContainer::OnTabSelected, this)));
	m_connections.push_back(
		tabRemovedSignal.AddObserver(std::bind_front(&TabContainer::OnTabRemoved, this)));

	m_connections.push_back(m_config->alwaysShowTabBar.addObserver(
		std::bind_front(&TabContainer::OnAlwaysShowTabBarUpdated, this)));
	m_connections.push_back(m_config->forceSameTabWidth.addObserver(
		std::bind_front(&TabContainer::OnForceSameTabWidthUpdated, this)));
}

void TabContainer::AddDefaultTabIcons(HIMAGELIST himlTab)
{
	UINT dpi = DpiCompatibility::GetInstance().GetDpiForWindow(m_hwnd);
	wil::unique_hbitmap bitmap = m_coreInterface->GetIconResourceLoader()->LoadBitmapFromPNGForDpi(
		Icon::Lock, ICON_SIZE_96DPI, ICON_SIZE_96DPI, dpi);
	m_tabIconLockIndex = ImageList_Add(himlTab, bitmap.get(), nullptr);

	m_defaultFolderIconIndex = ImageHelper::CopyImageListIcon(m_tabCtrlImageList.get(),
		reinterpret_cast<HIMAGELIST>(m_systemImageList.get()),
		m_defaultFolderIconSystemImageListIndex);
}

bool TabContainer::IsDefaultIcon(int iconIndex)
{
	if (iconIndex == m_tabIconLockIndex || iconIndex == m_defaultFolderIconIndex)
	{
		return true;
	}

	return false;
}

TabContainer::~TabContainer()
{
	RevokeDragDrop(m_hwnd);
}

LRESULT CALLBACK TabContainer::WndProcStub(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
	UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
	UNREFERENCED_PARAMETER(uIdSubclass);

	auto *tabContainer = reinterpret_cast<TabContainer *>(dwRefData);
	return tabContainer->WndProc(hwnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK TabContainer::WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
		HANDLE_MSG(hwnd, WM_MOUSEWHEEL, OnMouseWheel);

	case WM_LBUTTONDOWN:
	{
		POINT pt;
		POINTSTOPOINT(pt, MAKEPOINTS(lParam));
		OnTabCtrlLButtonDown(&pt);
	}
	break;

	case WM_LBUTTONUP:
		OnTabCtrlLButtonUp();
		break;

	case WM_MOUSEMOVE:
	{
		POINT pt;
		POINTSTOPOINT(pt, MAKEPOINTS(lParam));
		OnTabCtrlMouseMove(&pt);
	}
	break;

	case WM_LBUTTONDBLCLK:
	{
		POINT pt;
		POINTSTOPOINT(pt, MAKEPOINTS(lParam));
		OnLButtonDoubleClick(pt);
	}
	break;

	case WM_MBUTTONUP:
	{
		POINT pt;
		POINTSTOPOINT(pt, MAKEPOINTS(lParam));
		OnTabCtrlMButtonUp(&pt);
	}
	break;

	case WM_RBUTTONUP:
	{
		POINT pt;
		POINTSTOPOINT(pt, MAKEPOINTS(lParam));
		OnTabCtrlRButtonUp(&pt);
	}
	break;

	case WM_TIMER:
		if (wParam == DROP_SWITCH_TAB_TIMER_ID)
		{
			OnDropSwitchTabTimer();
		}
		else if (wParam == DROP_SCROLL_TIMER_ID)
		{
			OnDropScrollTimer();
		}
		break;

	case WM_MENUSELECT:
		/* Forward the message to the main window so it can
		handle menu help. */
		SendMessage(m_coreInterface->GetMainWindow(), WM_MENUSELECT, wParam, lParam);
		break;

	case WM_CAPTURECHANGED:
	{
		if ((HWND) lParam != hwnd)
		{
			ReleaseCapture();
		}

		m_bTabBeenDragged = FALSE;
	}
	break;

	case WM_NCDESTROY:
		delete this;
		return 0;
	}

	return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

void TabContainer::OnTabCtrlLButtonDown(POINT *pt)
{
	TCHITTESTINFO info;
	info.pt = *pt;
	int itemNum = TabCtrl_HitTest(m_hwnd, &info);

	if (info.flags != TCHT_NOWHERE)
	{
		/* Save the bounds of the dragged tab. */
		TabCtrl_GetItemRect(m_hwnd, itemNum, &m_rcDraggedTab);

		/* Capture mouse movement exclusively until
		the mouse button is released. */
		SetCapture(m_hwnd);

		m_bTabBeenDragged = TRUE;
		m_draggedTabStartIndex = itemNum;
		m_draggedTabEndIndex = itemNum;
	}
}

void TabContainer::OnTabCtrlLButtonUp()
{
	if (!m_bTabBeenDragged)
	{
		return;
	}

	ReleaseCapture();

	m_bTabBeenDragged = FALSE;

	if (m_draggedTabEndIndex != m_draggedTabStartIndex)
	{
		const Tab &tab = GetTabByIndex(m_draggedTabEndIndex);
		tabMovedSignal.m_signal(tab, m_draggedTabStartIndex, m_draggedTabEndIndex);
	}
}

void TabContainer::OnTabCtrlMouseMove(POINT *pt)
{
	if (!m_bTabBeenDragged)
	{
		return;
	}

	/* Dragged tab. */
	int iSelected = TabCtrl_GetCurFocus(m_hwnd);

	TCHITTESTINFO hitTestInfo;
	hitTestInfo.pt = *pt;
	int iSwap = TabCtrl_HitTest(m_hwnd, &hitTestInfo);

	/* Check:
	- If the cursor is over an item.
	- If the cursor is not over the dragged item itself.
	- If the cursor has passed to the left of the dragged tab, or
	- If the cursor has passed to the right of the dragged tab. */
	if (hitTestInfo.flags != TCHT_NOWHERE && iSwap != iSelected
		&& (pt->x < m_rcDraggedTab.left || pt->x > m_rcDraggedTab.right))
	{
		RECT rcSwap;

		TabCtrl_GetItemRect(m_hwnd, iSwap, &rcSwap);

		/* These values need to be adjusted, since
		tabs are adjusted whenever the dragged tab
		passes a boundary, not when the cursor is
		released. */
		if (pt->x > m_rcDraggedTab.right)
		{
			/* Cursor has gone past the right edge of
			the dragged tab. */
			m_rcDraggedTab.left = m_rcDraggedTab.right;
			m_rcDraggedTab.right = rcSwap.right;
		}
		else
		{
			/* Cursor has gone past the left edge of
			the dragged tab. */
			m_rcDraggedTab.right = m_rcDraggedTab.left;
			m_rcDraggedTab.left = rcSwap.left;
		}

		/* Swap the dragged tab with the tab the cursor
		finished up on. */
		TabCtrl_SwapItems(m_hwnd, iSelected, iSwap);

		/* The index of the selected tab has now changed
		(but the actual tab/browser selected remains the
		same). */
		TabCtrl_SetCurFocus(m_hwnd, iSwap);

		m_draggedTabEndIndex = iSwap;
	}
}

void TabContainer::OnMouseWheel(HWND hwnd, int xPos, int yPos, int delta, UINT keys)
{
	UNREFERENCED_PARAMETER(hwnd);
	UNREFERENCED_PARAMETER(xPos);
	UNREFERENCED_PARAMETER(yPos);
	UNREFERENCED_PARAMETER(keys);

	auto scrollDirection = delta < 0 ? ScrollDirection::Left : ScrollDirection::Right;

	for (int i = 0; i < abs(delta / WHEEL_DELTA); i++)
	{
		ScrollTabControl(scrollDirection);
	}
}

void TabContainer::OnLButtonDoubleClick(const POINT &pt)
{
	TCHITTESTINFO info;
	info.pt = pt;
	const int index = TabCtrl_HitTest(m_hwnd, &info);

	if (info.flags != TCHT_NOWHERE && m_config->doubleClickTabClose)
	{
		const Tab &tab = GetTabByIndex(index);
		CloseTab(tab);
	}
}

void TabContainer::OnTabCtrlMButtonUp(POINT *pt)
{
	TCHITTESTINFO htInfo;
	htInfo.pt = *pt;

	/* Find the tab that the click occurred over. */
	int iTabHit = TabCtrl_HitTest(m_hwnd, &htInfo);

	if (iTabHit != -1)
	{
		const Tab &tab = GetTabByIndex(iTabHit);
		CloseTab(tab);
	}
}

void TabContainer::OnTabCtrlRButtonUp(POINT *pt)
{
	TCHITTESTINFO tcHitTest;
	tcHitTest.pt = *pt;
	const int tabHitIndex = TabCtrl_HitTest(m_hwnd, &tcHitTest);

	if (tcHitTest.flags == TCHT_NOWHERE)
	{
		return;
	}

	POINT ptCopy = *pt;
	BOOL res = ClientToScreen(m_hwnd, &ptCopy);

	if (!res)
	{
		return;
	}

	Tab &tab = GetTabByIndex(tabHitIndex);

	CreateTabContextMenu(tab, ptCopy);
}

void TabContainer::CreateTabContextMenu(Tab &tab, const POINT &pt)
{
	auto parentMenu =
		wil::unique_hmenu(LoadMenu(m_resourceInstance, MAKEINTRESOURCE(IDR_TAB_RCLICK)));

	if (!parentMenu)
	{
		return;
	}

	HMENU menu = GetSubMenu(parentMenu.get(), 0);

	std::vector<wil::unique_hbitmap> menuImages;
	AddImagesToTabContextMenu(menu, menuImages);

	MenuHelper::EnableItem(menu, IDM_TAB_OPENPARENTINNEWTAB,
		!IsNamespaceRoot(tab.GetShellBrowser()->GetDirectoryIdl().get()));
	MenuHelper::CheckItem(menu, IDM_TAB_LOCKTAB, tab.GetLockState() == Tab::LockState::Locked);
	MenuHelper::CheckItem(menu, IDM_TAB_LOCKTABANDADDRESS,
		tab.GetLockState() == Tab::LockState::AddressLocked);
	MenuHelper::EnableItem(menu, IDM_TAB_CLOSETAB, tab.GetLockState() == Tab::LockState::NotLocked);

	UINT command =
		TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_VERTICAL | TPM_RETURNCMD, pt.x,
			pt.y, 0, m_hwnd, nullptr);

	ProcessTabCommand(command, tab);
}

void TabContainer::AddImagesToTabContextMenu(HMENU menu,
	std::vector<wil::unique_hbitmap> &menuImages)
{
	UINT dpi = DpiCompatibility::GetInstance().GetDpiForWindow(m_hwnd);

	for (const auto &mapping : TAB_RIGHT_CLICK_MENU_IMAGE_MAPPINGS)
	{
		ResourceHelper::SetMenuItemImage(menu, mapping.first,
			m_coreInterface->GetIconResourceLoader(), mapping.second, dpi, menuImages);
	}
}

void TabContainer::ProcessTabCommand(UINT uMenuID, Tab &tab)
{
	switch (uMenuID)
	{
	case IDM_FILE_NEWTAB:
		/* Send the resulting command back to the main window for processing. */
		SendMessage(m_coreInterface->GetMainWindow(), WM_COMMAND, MAKEWPARAM(uMenuID, 0), 0);
		break;

	case IDM_TAB_DUPLICATETAB:
		DuplicateTab(tab);
		break;

	case IDM_TAB_OPENPARENTINNEWTAB:
		OnOpenParentInNewTab(tab);
		break;

	case IDM_TAB_REFRESH:
		OnRefreshTab(tab);
		break;

	case IDM_TAB_REFRESHALL:
		OnRefreshAllTabs();
		break;

	case IDM_TAB_RENAMETAB:
		OnRenameTab(tab);
		break;

	case IDM_TAB_LOCKTAB:
		OnLockTab(tab);
		break;

	case IDM_TAB_LOCKTABANDADDRESS:
		OnLockTabAndAddress(tab);
		break;

	case IDM_TAB_CLOSETAB:
		CloseTab(tab);
		break;

	case IDM_TAB_CLOSEOTHERTABS:
		OnCloseOtherTabs(GetTabIndex(tab));
		break;

	case IDM_TAB_CLOSETABSTORIGHT:
		OnCloseTabsToRight(GetTabIndex(tab));
		break;
	}
}

void TabContainer::OnOpenParentInNewTab(const Tab &tab)
{
	auto pidlCurrent = tab.GetShellBrowser()->GetDirectoryIdl();

	PIDLIST_ABSOLUTE pidlParent = nullptr;
	HRESULT hr = GetVirtualParentPath(pidlCurrent.get(), &pidlParent);

	if (SUCCEEDED(hr))
	{
		CreateNewTab(pidlParent, TabSettings(_selected = true));
		CoTaskMemFree(pidlParent);
	}
}

void TabContainer::OnRefreshTab(Tab &tab)
{
	tab.GetShellBrowser()->GetNavigationController()->Refresh();
}

void TabContainer::OnRefreshAllTabs()
{
	for (auto &tab : GetAllTabs() | boost::adaptors::map_values)
	{
		tab->GetShellBrowser()->GetNavigationController()->Refresh();
	}
}

void TabContainer::OnRenameTab(const Tab &tab)
{
	RenameTabDialog renameTabDialog(m_resourceInstance, m_coreInterface->GetMainWindow(),
		tab.GetId(), this);
	renameTabDialog.ShowModalDialog();
}

void TabContainer::OnLockTab(Tab &tab)
{
	if (tab.GetLockState() == Tab::LockState::Locked)
	{
		tab.SetLockState(Tab::LockState::NotLocked);
	}
	else
	{
		tab.SetLockState(Tab::LockState::Locked);
	}
}

void TabContainer::OnLockTabAndAddress(Tab &tab)
{
	if (tab.GetLockState() == Tab::LockState::AddressLocked)
	{
		tab.SetLockState(Tab::LockState::NotLocked);
	}
	else
	{
		tab.SetLockState(Tab::LockState::AddressLocked);
	}
}

void TabContainer::OnCloseOtherTabs(int index)
{
	const int nTabs = GetNumTabs();

	/* Close all tabs except the
	specified one. */
	for (int i = nTabs - 1; i >= 0; i--)
	{
		if (i != index)
		{
			const Tab &tab = GetTabByIndex(i);
			CloseTab(tab);
		}
	}
}

void TabContainer::OnCloseTabsToRight(int index)
{
	int nTabs = GetNumTabs();

	for (int i = nTabs - 1; i > index; i--)
	{
		const Tab &currentTab = GetTabByIndex(i);
		CloseTab(currentTab);
	}
}

LRESULT CALLBACK TabContainer::ParentWndProcStub(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
	UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
	UNREFERENCED_PARAMETER(uIdSubclass);

	auto *tabContainer = reinterpret_cast<TabContainer *>(dwRefData);
	return tabContainer->ParentWndProc(hwnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK TabContainer::ParentWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_LBUTTONDBLCLK:
		CreateNewTabInDefaultDirectory(TabSettings(_selected = true));
		break;

	case WM_RBUTTONUP:
	{
		POINT ptClient;
		POINTSTOPOINT(ptClient, MAKEPOINTS(lParam));
		ShowBackgroundContextMenu(ptClient);
	}
	break;

	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case TABTOOLBAR_CLOSE:
			CloseTab(GetSelectedTab());
			break;
		}
		break;

	case WM_NOTIFY:
		switch (reinterpret_cast<LPNMHDR>(lParam)->code)
		{
		case TTN_GETDISPINFO:
			OnGetDispInfo(reinterpret_cast<NMTTDISPINFO *>(lParam));
			break;

		case TCN_SELCHANGE:
			tabSelectedSignal.m_signal(GetSelectedTab());
			break;
		}
		break;
	}

	return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

void TabContainer::ShowBackgroundContextMenu(const POINT &ptClient)
{
	wil::unique_hmenu parentMenu(
		LoadMenu(m_resourceInstance, MAKEINTRESOURCE(IDR_TAB_CONTAINER_CONTEXT_MENU)));

	if (!parentMenu)
	{
		return;
	}

	HMENU menu = GetSubMenu(parentMenu.get(), 0);

	if (m_coreInterface->GetTabRestorer()->GetClosedTabs().empty())
	{
		MenuHelper::EnableItem(menu, IDM_TAB_CONTAINER_REOPEN_CLOSED_TAB, FALSE);
	}

	POINT ptScreen = ptClient;
	ClientToScreen(m_hwnd, &ptScreen);

	int menuItemId = TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_RETURNCMD, ptScreen.x, ptScreen.y, 0,
		m_hwnd, nullptr);

	if (menuItemId != 0)
	{
		OnBackgroundMenuItemSelected(menuItemId);
	}
}

void TabContainer::OnBackgroundMenuItemSelected(int menuItemId)
{
	switch (menuItemId)
	{
	case IDM_TAB_CONTAINER_NEW_TAB:
		CreateNewTabInDefaultDirectory(TabSettings(_selected = true));
		break;

	case IDM_TAB_CONTAINER_REOPEN_CLOSED_TAB:
		m_coreInterface->GetTabRestorer()->RestoreLastTab();
		break;

	case IDM_TAB_CONTAINER_BOOKMARK_ALL_TABS:
		BookmarkHelper::BookmarkAllTabs(m_bookmarkTree, m_resourceInstance, m_hwnd,
			m_coreInterface);
		break;

	default:
		assert(false);
		break;
	}
}

void TabContainer::OnGetDispInfo(NMTTDISPINFO *dispInfo)
{
	HWND toolTipControl = TabCtrl_GetToolTips(m_hwnd);

	if (dispInfo->hdr.hwndFrom != toolTipControl)
	{
		return;
	}

	static TCHAR tabToolTip[512];

	const Tab &tab = GetTabByIndex(static_cast<int>(dispInfo->hdr.idFrom));

	auto pidlDirectory = tab.GetShellBrowser()->GetDirectoryIdl();
	auto path = GetFolderPathForDisplay(pidlDirectory.get());

	if (!path)
	{
		return;
	}

	StringCchCopy(tabToolTip, SIZEOF_ARRAY(tabToolTip), path->c_str());

	dispInfo->lpszText = tabToolTip;
}

void TabContainer::OnTabCreated(int tabId, BOOL switchToNewTab)
{
	UNREFERENCED_PARAMETER(tabId);
	UNREFERENCED_PARAMETER(switchToNewTab);

	if (!m_config->alwaysShowTabBar.get() && (GetNumTabs() > 1))
	{
		m_coreInterface->ShowTabBar();
	}
}

void TabContainer::OnTabRemoved(int tabId)
{
	UNREFERENCED_PARAMETER(tabId);

	if (!m_config->alwaysShowTabBar.get() && (GetNumTabs() == 1))
	{
		m_coreInterface->HideTabBar();
	}
}

void TabContainer::OnTabSelected(const Tab &tab)
{
	if (m_iPreviousTabSelectionId != -1)
	{
		m_tabSelectionHistory.push_back(m_iPreviousTabSelectionId);
	}

	m_iPreviousTabSelectionId = tab.GetId();
}

void TabContainer::OnAlwaysShowTabBarUpdated(BOOL newValue)
{
	if (newValue)
	{
		m_coreInterface->ShowTabBar();
	}
	else
	{
		if (GetNumTabs() > 1)
		{
			m_coreInterface->ShowTabBar();
		}
		else
		{
			m_coreInterface->HideTabBar();
		}
	}
}

void TabContainer::OnForceSameTabWidthUpdated(BOOL newValue)
{
	AddWindowStyle(m_hwnd, TCS_FIXEDWIDTH, newValue);
}

void TabContainer::OnNavigationCommitted(const Tab &tab, PCIDLIST_ABSOLUTE pidl,
	bool addHistoryEntry)
{
	UNREFERENCED_PARAMETER(pidl);
	UNREFERENCED_PARAMETER(addHistoryEntry);

	UpdateTabNameInWindow(tab);
	SetTabIcon(tab);
}

void TabContainer::OnTabUpdated(const Tab &tab, Tab::PropertyType propertyType)
{
	switch (propertyType)
	{
	case Tab::PropertyType::LockState:
		SetTabIcon(tab);
		break;

	case Tab::PropertyType::Name:
		UpdateTabNameInWindow(tab);
		break;
	}

	tabUpdatedSignal.m_signal(tab, propertyType);
}

void TabContainer::UpdateTabNameInWindow(const Tab &tab)
{
	std::wstring name = tab.GetName();
	boost::replace_all(name, L"&", L"&&");

	int index = GetTabIndex(tab);
	TabCtrl_SetItemText(m_hwnd, index, name.c_str());
}

void TabContainer::SetTabIcon(const Tab &tab)
{
	/* If the tab is locked, use a lock icon. */
	if (tab.GetLockState() == Tab::LockState::Locked
		|| tab.GetLockState() == Tab::LockState::AddressLocked)
	{
		SetTabIconFromImageList(tab, m_tabIconLockIndex);
	}
	else
	{
		auto itr = m_cachedIcons->findByPath(tab.GetShellBrowser()->GetDirectory());

		if (itr != m_cachedIcons->end())
		{
			SetTabIconFromSystemImageList(tab, itr->iconIndex);
		}
		else
		{
			SetTabIconFromImageList(tab, m_defaultFolderIconIndex);
		}

		auto pidlDirectory = tab.GetShellBrowser()->GetDirectoryIdl();

		m_iconFetcher.QueueIconTask(pidlDirectory.get(),
			[this, tabId = tab.GetId(), folderId = tab.GetShellBrowser()->GetUniqueFolderId()](
				int iconIndex)
			{
				auto tab = GetTabOptional(tabId);

				if (!tab)
				{
					return;
				}

				if (tab->GetShellBrowser()->GetUniqueFolderId() != folderId)
				{
					return;
				}

				SetTabIconFromSystemImageList(*tab, iconIndex);
			});
	}
}

void TabContainer::SetTabIconFromSystemImageList(const Tab &tab, int systemIconIndex)
{
	if (systemIconIndex == m_defaultFolderIconSystemImageListIndex)
	{
		SetTabIconFromImageList(tab, m_defaultFolderIconIndex);
		return;
	}

	int index = ImageHelper::CopyImageListIcon(m_tabCtrlImageList.get(),
		reinterpret_cast<HIMAGELIST>(m_systemImageList.get()), systemIconIndex);

	if (index != -1)
	{
		SetTabIconFromImageList(tab, index);
	}
}

void TabContainer::SetTabIconFromImageList(const Tab &tab, int imageIndex)
{
	int tabIndex = GetTabIndex(tab);

	TCITEM tcItem;
	tcItem.mask = TCIF_IMAGE;
	BOOL res = TabCtrl_GetItem(m_hwnd, tabIndex, &tcItem);

	if (!res)
	{
		return;
	}

	int previousImageIndex = tcItem.iImage;

	tcItem.mask = TCIF_IMAGE;
	tcItem.iImage = imageIndex;
	res = TabCtrl_SetItem(m_hwnd, tabIndex, &tcItem);

	if (!res)
	{
		return;
	}

	if (!IsDefaultIcon(previousImageIndex))
	{
		TabCtrl_RemoveImage(m_hwnd, previousImageIndex);
	}
}

void TabContainer::CreateNewTabInDefaultDirectory(const TabSettings &tabSettings)
{
	CreateNewTab(m_config->defaultTabDirectory, tabSettings);
}

// Note that although it's guaranteed that the tab will be created, it's not guaranteed that it will
// be navigated to the directory that's provided. For example, the directory may not exist, or the
// user may not have access to it, or the path may be temporarily unavailable (e.g. because a
// network location has been disconnected). Therefore, it's not safe to make any assumptions about
// the actual directory the tab is in once it's been created.
Tab &TabContainer::CreateNewTab(const std::wstring &directory, const TabSettings &tabSettings,
	const FolderSettings *folderSettings, const FolderColumns *initialColumns)
{
	unique_pidl_absolute pidl;
	HRESULT hr = SHParseDisplayName(directory.c_str(), nullptr, wil::out_param(pidl), 0, nullptr);

	if (FAILED(hr))
	{
		hr = SHParseDisplayName(m_config->defaultTabDirectory.c_str(), nullptr,
			wil::out_param(pidl), 0, nullptr);

		if (FAILED(hr))
		{
			hr = SHParseDisplayName(m_config->defaultTabDirectoryStatic.c_str(), nullptr,
				wil::out_param(pidl), 0, nullptr);
		}
	}

	return CreateNewTab(pidl.get(), tabSettings, folderSettings, initialColumns);
}

Tab &TabContainer::CreateNewTab(const PreservedTab &preservedTab)
{
	PreservedHistoryEntry *entry = preservedTab.history.at(preservedTab.currentEntry).get();

	auto tabTemp =
		std::make_unique<Tab>(preservedTab, m_coreInterface, m_tabNavigation, m_fileActionHandler);
	auto item = m_tabs.insert({ tabTemp->GetId(), std::move(tabTemp) });

	Tab &tab = *item.first->second;

	TabSettings tabSettings(_index = preservedTab.index, _selected = true);

	return SetUpNewTab(tab, entry->pidl.get(), tabSettings, false);
}

Tab &TabContainer::CreateNewTab(PCIDLIST_ABSOLUTE pidlDirectory, const TabSettings &tabSettings,
	const FolderSettings *folderSettings, const FolderColumns *initialColumns)
{
	auto tabTemp = std::make_unique<Tab>(m_coreInterface, m_tabNavigation, m_fileActionHandler,
		folderSettings, initialColumns);
	auto item = m_tabs.insert({ tabTemp->GetId(), std::move(tabTemp) });

	Tab &tab = *item.first->second;

	if (tabSettings.lockState)
	{
		tab.SetLockState(*tabSettings.lockState);
	}

	if (tabSettings.name && !tabSettings.name->empty())
	{
		tab.SetCustomName(*tabSettings.name);
	}

	return SetUpNewTab(tab, pidlDirectory, tabSettings, true);
}

Tab &TabContainer::SetUpNewTab(Tab &tab, PCIDLIST_ABSOLUTE pidlDirectory,
	const TabSettings &tabSettings, bool addHistoryEntry)
{
	int index;

	if (tabSettings.index)
	{
		index = *tabSettings.index;
	}
	else
	{
		// When the application is first started, the number of tabs will be 0 initially, so there
		// won't be any selected tab. In that case, the openNewTabNextToCurrent setting should
		// effectively be ignored.
		if (m_config->openNewTabNextToCurrent && GetSelectedTabIndexOptional())
		{
			index = GetSelectedTabIndex() + 1;
		}
		else
		{
			index = TabCtrl_GetItemCount(m_hwnd);
		}
	}

	/* Browse folder sends a message back to the main window, which
	attempts to contact the new tab (needs to be created before browsing
	the folder). */
	InsertNewTab(index, tab.GetId(), pidlDirectory, tabSettings.name);

	// Note that for the listview window to be shown, it has to have a non-zero
	// size. If the size is zero at the point it's shown, it will instead remain
	// hidden, even if it's later resized. Currently, if the tab is selected,
	// the listview is shown during the OnTabSelected() call below. Therefore,
	// the listview needs to have a non-zero size before that point.
	m_coreInterface->SetListViewInitialPosition(tab.GetShellBrowser()->GetListView());

	bool selected = false;

	if (tabSettings.selected)
	{
		selected = *tabSettings.selected;
	}

	tab.GetShellBrowser()->AddNavigationStartedObserver(
		[this, &tab](PCIDLIST_ABSOLUTE pidl)
		{
			tabNavigationStartedSignal.m_signal(tab, pidl);
		});

	tab.GetShellBrowser()->AddNavigationCommittedObserver(
		[this, &tab](PCIDLIST_ABSOLUTE pidl, bool addHistoryEntry)
		{
			tabNavigationCommittedSignal.m_signal(tab, pidl, addHistoryEntry);
		});

	// Capturing the tab by reference here is safe, since the tab object is
	// guaranteed to exist whenever this method is called.
	tab.GetShellBrowser()->AddNavigationCompletedObserver(
		[this, &tab](PCIDLIST_ABSOLUTE pidlDirectory)
		{
			UNREFERENCED_PARAMETER(pidlDirectory);

			// Re-broadcast the event. This allows other classes to be notified of
			// navigations in any tab, without having to observe navigation events
			// for each tab individually.
			tabNavigationCompletedSignal.m_signal(tab);
		});

	tab.GetShellBrowser()->AddNavigationFailedObserver(
		[this, &tab]()
		{
			tabNavigationFailedSignal.m_signal(tab);
		});

	tab.GetShellBrowser()->directoryModified.AddObserver(
		[this, &tab]()
		{
			tabDirectoryModifiedSignal.m_signal(tab);
		});

	tab.GetShellBrowser()->listViewSelectionChanged.AddObserver(
		[this, &tab]()
		{
			tabListViewSelectionChangedSignal.m_signal(tab);
		});

	tab.GetShellBrowser()->columnsChanged.AddObserver(
		[this, &tab]()
		{
			tabColumnsChangedSignal.m_signal(tab);
		});

	HRESULT hr = tab.GetShellBrowser()->GetNavigationController()->BrowseFolder(pidlDirectory,
		addHistoryEntry);

	if (FAILED(hr))
	{
		hr = tab.GetShellBrowser()->GetNavigationController()->BrowseFolder(
			m_config->defaultTabDirectory, addHistoryEntry);

		if (FAILED(hr))
		{
			// The computer folder should always exist, so this call shouldn't fail.
			tab.GetShellBrowser()->GetNavigationController()->BrowseFolder(
				m_config->defaultTabDirectoryStatic, addHistoryEntry);
		}
	}

	if (selected)
	{
		int previousIndex = TabCtrl_SetCurSel(m_hwnd, index);

		if (previousIndex != -1)
		{
			tabSelectedSignal.m_signal(tab);
		}
	}

	// There's no need to manually disconnect this. Either it will be
	// disconnected when the tab is closed and the tab object (and
	// associated signal) is destroyed or when the tab is destroyed
	// during application shutdown.
	tab.AddTabUpdatedObserver(std::bind_front(&TabContainer::OnTabUpdated, this));

	tabCreatedSignal.m_signal(tab.GetId(), selected);

	return tab;
}

void TabContainer::InsertNewTab(int index, int tabId, PCIDLIST_ABSOLUTE pidlDirectory,
	std::optional<std::wstring> customName)
{
	std::wstring name;

	if (customName && !customName->empty())
	{
		name = *customName;
	}
	else
	{
		GetDisplayName(pidlDirectory, SHGDN_INFOLDER, name);
	}

	boost::replace_all(name, L"&", L"&&");

	TCITEM tcItem;
	tcItem.mask = TCIF_TEXT | TCIF_PARAM;
	tcItem.pszText = name.data();
	tcItem.lParam = tabId;
	TabCtrl_InsertItem(m_hwnd, index, &tcItem);
}

bool TabContainer::CloseTab(const Tab &tab)
{
	const int nTabs = GetNumTabs();

	if (nTabs == 1)
	{
		if (m_config->closeMainWindowOnTabClose)
		{
			m_coreInterface->CloseApplication();
			return true;
		}
		else
		{
			return false;
		}
	}

	/* The tab is locked. Don't close it. */
	if (tab.GetLockState() == Tab::LockState::Locked
		|| tab.GetLockState() == Tab::LockState::AddressLocked)
	{
		return false;
	}

	tabPreRemovalSignal.m_signal(tab);

	RemoveTabFromControl(tab);

	auto dirMonitorId = tab.GetShellBrowser()->GetDirMonitorId();

	if (dirMonitorId)
	{
		m_coreInterface->GetDirectoryMonitor()->StopDirectoryMonitor(*dirMonitorId);
	}

	// This is needed, as the erase() call below will remove the element
	// from the tabs container (which will invalidate the reference
	// passed to the function).
	int tabId = tab.GetId();

	m_tabs.erase(tab.GetId());

	tabRemovedSignal.m_signal(tabId);

	return true;
}

void TabContainer::RemoveTabFromControl(const Tab &tab)
{
	m_tabSelectionHistory.erase(
		std::remove(m_tabSelectionHistory.begin(), m_tabSelectionHistory.end(), tab.GetId()),
		m_tabSelectionHistory.end());

	const int index = GetTabIndex(tab);

	if (IsTabSelected(tab))
	{
		int newIndex;

		/* If there was a previously active tab, the focus
		should be switched back to it. */
		if (!m_tabSelectionHistory.empty())
		{
			const int lastTabId = m_tabSelectionHistory.back();
			m_tabSelectionHistory.pop_back();

			const Tab &lastTab = GetTab(lastTabId);
			newIndex = GetTabIndex(lastTab);
		}
		else
		{
			// If the last tab in the control is what's being closed,
			// the tab before it will be selected.
			if (index == (GetNumTabs() - 1))
			{
				newIndex = index - 1;
			}
			else
			{
				newIndex = index + 1;
			}
		}

		SelectTabAtIndex(newIndex);

		// This is somewhat hacky. Switching the tab will cause the
		// previously selected tab (i.e. the tab that's about to be
		// closed) to be added to the history list. That's not
		// desirable, so the last entry will be removed here.
		m_tabSelectionHistory.pop_back();
	}

	TCITEM tcItemRemoved;
	tcItemRemoved.mask = TCIF_IMAGE;
	TabCtrl_GetItem(m_hwnd, index, &tcItemRemoved);

	TabCtrl_DeleteItem(m_hwnd, index);

	if (!IsDefaultIcon(tcItemRemoved.iImage))
	{
		TabCtrl_RemoveImage(m_hwnd, tcItemRemoved.iImage);
	}
}

Tab &TabContainer::GetTab(int tabId)
{
	return *m_tabs.at(tabId);
}

Tab *TabContainer::GetTabOptional(int tabId)
{
	auto itr = m_tabs.find(tabId);

	if (itr == m_tabs.end())
	{
		return nullptr;
	}

	return itr->second.get();
}

void TabContainer::SelectTab(const Tab &tab)
{
	int index = GetTabIndex(tab);
	SelectTabAtIndex(index);
}

void TabContainer::SelectAdjacentTab(BOOL bNextTab)
{
	int nTabs = GetNumTabs();
	int newIndex = GetSelectedTabIndex();

	if (bNextTab)
	{
		/* If this is the last tab in the order,
		wrap the selection back to the start. */
		if (newIndex == (nTabs - 1))
		{
			newIndex = 0;
		}
		else
		{
			newIndex++;
		}
	}
	else
	{
		/* If this is the first tab in the order,
		wrap the selection back to the end. */
		if (newIndex == 0)
		{
			newIndex = nTabs - 1;
		}
		else
		{
			newIndex--;
		}
	}

	SelectTabAtIndex(newIndex);
}

void TabContainer::SelectTabAtIndex(int index)
{
	assert(index >= 0 && index < GetNumTabs());

	int previousIndex = TabCtrl_SetCurSel(m_hwnd, index);

	if (previousIndex == -1)
	{
		return;
	}

	tabSelectedSignal.m_signal(GetTabByIndex(index));
}

Tab &TabContainer::GetSelectedTab()
{
	int index = GetSelectedTabIndex();
	return GetTabByIndex(index);
}

int TabContainer::GetSelectedTabIndex() const
{
	int index = TabCtrl_GetCurSel(m_hwnd);

	if (index == -1)
	{
		throw std::runtime_error("No selected tab");
	}

	return index;
}

std::optional<int> TabContainer::GetSelectedTabIndexOptional() const
{
	int index = TabCtrl_GetCurSel(m_hwnd);

	if (index == -1)
	{
		return std::nullopt;
	}

	return index;
}

bool TabContainer::IsTabSelected(const Tab &tab)
{
	const Tab &selectedTab = GetSelectedTab();
	return tab.GetId() == selectedTab.GetId();
}

Tab &TabContainer::GetTabByIndex(int index)
{
	TCITEM tcItem;
	tcItem.mask = TCIF_PARAM;
	BOOL res = TabCtrl_GetItem(m_hwnd, index, &tcItem);

	if (!res)
	{
		throw std::runtime_error("Tab lookup failed");
	}

	return GetTab(static_cast<int>(tcItem.lParam));
}

int TabContainer::GetTabIndex(const Tab &tab) const
{
	int numTabs = TabCtrl_GetItemCount(m_hwnd);

	for (int i = 0; i < numTabs; i++)
	{
		TCITEM tcItem;
		tcItem.mask = TCIF_PARAM;
		BOOL res = TabCtrl_GetItem(m_hwnd, i, &tcItem);

		if (res && (tcItem.lParam == tab.GetId()))
		{
			return i;
		}
	}

	// All internal tab objects should have an index.
	throw std::runtime_error("Couldn't determine index for tab");
}

int TabContainer::GetNumTabs() const
{
	return static_cast<int>(m_tabs.size());
}

int TabContainer::MoveTab(const Tab &tab, int newIndex)
{
	int index = GetTabIndex(tab);
	return TabCtrl_MoveItem(m_hwnd, index, newIndex);
}

std::unordered_map<int, std::unique_ptr<Tab>> &TabContainer::GetTabs()
{
	return m_tabs;
}

const std::unordered_map<int, std::unique_ptr<Tab>> &TabContainer::GetAllTabs() const
{
	return m_tabs;
}

std::vector<std::reference_wrapper<const Tab>> TabContainer::GetAllTabsInOrder() const
{
	std::vector<std::reference_wrapper<const Tab>> sortedTabs;

	for (const auto &tab : m_tabs | boost::adaptors::map_values)
	{
		sortedTabs.emplace_back(*tab);
	}

	// The Tab class is non-copyable, so there are essentially two ways of
	// retrieving a sorted list of tabs, as far as I can tell:
	//
	// 1. The first is to maintain a sorted list of tabs while the program is
	// running. I generally don't think that's a good idea, since it would be
	// redundant (the tab control already stores that information) and it risks
	// possible issues (if the two sets get out of sync).
	//
	// 2. The second is to sort the tabs when needed. Because they're
	// non-copyable, that can't be done directly. std::reference_wrapper allows
	// it to be done relatively easily, though. Sorting a set of pointers would
	// accomplish the same thing.
	std::sort(sortedTabs.begin(), sortedTabs.end(),
		[this](const auto &tab1, const auto &tab2)
		{
			return GetTabIndex(tab1.get()) < GetTabIndex(tab2.get());
		});

	return sortedTabs;
}

void TabContainer::DuplicateTab(const Tab &tab)
{
	auto folderSettings = tab.GetShellBrowser()->GetFolderSettings();
	auto folderColumns = tab.GetShellBrowser()->ExportAllColumns();
	CreateNewTab(tab.GetShellBrowser()->GetDirectoryIdl().get(), {}, &folderSettings,
		&folderColumns);
}

int TabContainer::GetDropTargetItem(const POINT &pt)
{
	POINT ptClient = pt;
	BOOL res = ScreenToClient(m_hwnd, &ptClient);

	if (!res)
	{
		return -1;
	}

	TCHITTESTINFO hitTestInfo;
	hitTestInfo.pt = ptClient;
	return TabCtrl_HitTest(m_hwnd, &hitTestInfo);
}

unique_pidl_absolute TabContainer::GetPidlForTargetItem(int targetItem)
{
	if (targetItem == -1)
	{
		return nullptr;
	}

	const auto &tab = GetTabByIndex(targetItem);
	return tab.GetShellBrowser()->GetDirectoryIdl();
}

IUnknown *TabContainer::GetSiteForTargetItem(PCIDLIST_ABSOLUTE targetItemPidl)
{
	UNREFERENCED_PARAMETER(targetItemPidl);

	return nullptr;
}

bool TabContainer::IsTargetSourceOfDrop(int targetItem, IDataObject *dataObject)
{
	UNREFERENCED_PARAMETER(targetItem);
	UNREFERENCED_PARAMETER(dataObject);

	return false;
}

void TabContainer::UpdateUiForDrop(int targetItem, const POINT &pt)
{
	UpdateUiForTargetItem(targetItem);
	ScrollTabControlForDrop(pt);
}

void TabContainer::UpdateUiForTargetItem(int targetItem)
{
	if (targetItem != -1)
	{
		if (m_dropTargetIndex != targetItem)
		{
			SetTimer(m_hwnd, DROP_SWITCH_TAB_TIMER_ID, DROP_SWITCH_TAB_TIMER_ELAPSE, nullptr);
		}
	}
	else
	{
		KillTimer(m_hwnd, DROP_SWITCH_TAB_TIMER_ID);
	}

	m_dropTargetIndex = targetItem;
}

void TabContainer::ScrollTabControlForDrop(const POINT &pt)
{
	POINT ptClient = pt;
	BOOL res = ScreenToClient(m_hwnd, &ptClient);

	if (!res)
	{
		return;
	}

	RECT rc;
	res = GetClientRect(m_hwnd, &rc);

	if (!res)
	{
		return;
	}

	UINT dpi = DpiCompatibility::GetInstance().GetDpiForWindow(m_hwnd);
	std::optional<ScrollDirection> scrollDirection;

	if (ptClient.x < MulDiv(DROP_SCROLL_MARGIN_X_96DPI, dpi, USER_DEFAULT_SCREEN_DPI))
	{
		scrollDirection = ScrollDirection::Left;
	}
	else if (ptClient.x
		> (rc.right - MulDiv(DROP_SCROLL_MARGIN_X_96DPI, dpi, USER_DEFAULT_SCREEN_DPI)))
	{
		scrollDirection = ScrollDirection::Right;
	}

	if (!scrollDirection)
	{
		KillTimer(m_hwnd, DROP_SCROLL_TIMER_ID);
		return;
	}

	if (!m_dropScrollDirection || scrollDirection != *m_dropScrollDirection)
	{
		m_dropScrollDirection = scrollDirection;
		SetTimer(m_hwnd, DROP_SCROLL_TIMER_ID, DROP_SCROLL_TIMER_ELAPSE, nullptr);
	}
}

void TabContainer::ResetDropUiState()
{
	m_dropTargetIndex = -1;
	KillTimer(m_hwnd, DROP_SWITCH_TAB_TIMER_ID);

	m_dropScrollDirection.reset();
	KillTimer(m_hwnd, DROP_SCROLL_TIMER_ID);
}

void TabContainer::OnDropSwitchTabTimer()
{
	assert(m_dropTargetIndex != -1);

	if (m_dropTargetIndex != GetSelectedTabIndex())
	{
		SelectTabAtIndex(m_dropTargetIndex);
	}

	KillTimer(m_hwnd, DROP_SWITCH_TAB_TIMER_ID);
}

void TabContainer::OnDropScrollTimer()
{
	assert(m_dropScrollDirection);
	ScrollTabControl(*m_dropScrollDirection);

	m_dropScrollDirection.reset();
	KillTimer(m_hwnd, DROP_SCROLL_TIMER_ID);
}

void TabContainer::ScrollTabControl(ScrollDirection direction)
{
	HWND upDownControl = FindWindowEx(m_hwnd, nullptr, UPDOWN_CLASS, nullptr);

	// It's valid for the control not to exist if all the tabs fit within the window.
	if (!upDownControl)
	{
		return;
	}

	int lowerLimit;
	int upperLimit;
	SendMessage(upDownControl, UDM_GETRANGE32, reinterpret_cast<WPARAM>(&lowerLimit),
		reinterpret_cast<WPARAM>(&upperLimit));

	BOOL positionRetrieved;
	auto position =
		SendMessage(upDownControl, UDM_GETPOS32, 0, reinterpret_cast<LPARAM>(&positionRetrieved));

	if (!positionRetrieved)
	{
		return;
	}

	switch (direction)
	{
	case ScrollDirection::Left:
		position--;
		break;

	case ScrollDirection::Right:
		position++;
		break;
	}

	if (position < lowerLimit || position > upperLimit)
	{
		return;
	}

	SendMessage(m_hwnd, WM_HSCROLL, MAKEWPARAM(SB_THUMBPOSITION, position), NULL);
}
