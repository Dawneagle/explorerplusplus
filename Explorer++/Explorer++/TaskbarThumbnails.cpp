// Copyright (C) Explorer++ Project
// SPDX-License-Identifier: GPL-3.0-only
// See LICENSE in the top level directory

/*
 * The methods in this file manage the tab proxy windows. A tab
 * proxy is created when a taskbar thumbnail needs to be shown.
 */

#include "stdafx.h"
#include "TaskbarThumbnails.h"
#include "CommandLine.h"
#include "Config.h"
#include "CoreInterface.h"
#include "MainResource.h"
#include "ResourceHelper.h"
#include "ShellBrowser/ShellBrowserImpl.h"
#include "TabContainer.h"
#include "../Helper/Macros.h"
#include "../Helper/ProcessHelper.h"
#include "../Helper/ShellHelper.h"
#include "../Helper/WindowHelper.h"
#include <dwmapi.h>

namespace
{
struct TabProxy
{
	TaskbarThumbnails *taskbarThumbnails;
	int iTabId;
};
}

TaskbarThumbnails *TaskbarThumbnails::Create(CoreInterface *coreInterface,
	TabContainer *tabContainer, HINSTANCE resourceInstance, std::shared_ptr<Config> config)
{
	return new TaskbarThumbnails(coreInterface, tabContainer, resourceInstance, config);
}

TaskbarThumbnails::TaskbarThumbnails(CoreInterface *coreInterface, TabContainer *tabContainer,
	HINSTANCE resourceInstance, std::shared_ptr<Config> config) :
	m_coreInterface(coreInterface),
	m_tabContainer(tabContainer),
	m_instance(resourceInstance),
	m_enabled(config->showTaskbarThumbnails)
{
	Initialize();
}

void TaskbarThumbnails::Initialize()
{
	if (!m_enabled)
	{
		return;
	}

	m_uTaskbarButtonCreatedMessage = RegisterWindowMessage(_T("TaskbarButtonCreated"));

	ChangeWindowMessageFilter(m_uTaskbarButtonCreatedMessage, MSGFLT_ADD);
	ChangeWindowMessageFilter(WM_DWMSENDICONICTHUMBNAIL, MSGFLT_ADD);
	ChangeWindowMessageFilter(WM_DWMSENDICONICLIVEPREVIEWBITMAP, MSGFLT_ADD);

	/* Subclass the main window until the above message (TaskbarButtonCreated) is caught. */
	SetWindowSubclass(m_coreInterface->GetMainWindow(), MainWndProcStub, 0,
		reinterpret_cast<DWORD_PTR>(this));
}

LRESULT CALLBACK TaskbarThumbnails::MainWndProcStub(HWND hwnd, UINT uMsg, WPARAM wParam,
	LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
	UNREFERENCED_PARAMETER(uIdSubclass);

	auto *taskbarThumbnails = reinterpret_cast<TaskbarThumbnails *>(dwRefData);

	return taskbarThumbnails->MainWndProc(hwnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK TaskbarThumbnails::MainWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	// Note that this message won't be received in environments like Windows PE, where there is no
	// shell/taskbar.
	if (uMsg == m_uTaskbarButtonCreatedMessage)
	{
		OnTaskbarButtonCreated();
		RemoveWindowSubclass(hwnd, MainWndProcStub, 0);
		return 0;
	}

	return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

void TaskbarThumbnails::OnTaskbarButtonCreated()
{
	HRESULT hr = CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER,
		IID_PPV_ARGS(&m_taskbarList));

	if (FAILED(hr))
	{
		return;
	}

	hr = m_taskbarList->HrInit();

	if (FAILED(hr))
	{
		return;
	}

	SetupJumplistTasks();

	for (const auto &tab : m_tabContainer->GetAllTabsInOrder())
	{
		CreateTabProxy(tab.get().GetId(), m_tabContainer->IsTabSelected(tab.get()));
	}

	SetUpObservers();
}

// Sets up the observers required to manage the taskbar thumbnails (e.g. to keep the thumbnails in
// sync with the list of tabs). These observers are only needed if the taskbar thumbnails are
// enabled and the functionality is available.
void TaskbarThumbnails::SetUpObservers()
{
	m_connections.push_back(m_tabContainer->tabCreatedSignal.AddObserver(
		std::bind_front(&TaskbarThumbnails::CreateTabProxy, this)));
	m_connections.push_back(m_tabContainer->tabNavigationCommittedSignal.AddObserver(
		std::bind_front(&TaskbarThumbnails::OnNavigationCommitted, this)));
	m_connections.push_back(m_tabContainer->tabNavigationCompletedSignal.AddObserver(
		std::bind_front(&TaskbarThumbnails::OnNavigationCompleted, this)));
	m_connections.push_back(m_tabContainer->tabSelectedSignal.AddObserver(
		std::bind_front(&TaskbarThumbnails::OnTabSelectionChanged, this)));
	m_connections.push_back(m_tabContainer->tabRemovedSignal.AddObserver(
		std::bind_front(&TaskbarThumbnails::RemoveTabProxy, this)));

	m_connections.push_back(m_coreInterface->AddApplicationShuttingDownObserver(
		std::bind_front(&TaskbarThumbnails::OnApplicationShuttingDown, this)));
}

void TaskbarThumbnails::SetupJumplistTasks()
{
	TCHAR szCurrentProcess[MAX_PATH];
	GetProcessImageName(GetCurrentProcessId(), szCurrentProcess, SIZEOF_ARRAY(szCurrentProcess));

	std::wstring name = ResourceHelper::LoadString(m_instance, IDS_TASKS_NEWTAB);

	/* New tab task. */
	JumpListTaskInformation jlti;
	jlti.pszName = name.c_str();
	jlti.pszPath = szCurrentProcess;
	jlti.pszArguments = CommandLine::JUMPLIST_TASK_NEWTAB_ARGUMENT;
	jlti.pszIconPath = szCurrentProcess;
	jlti.iIcon = 1;

	std::list<JumpListTaskInformation> taskList;
	taskList.push_back(jlti);

	AddJumpListTasks(taskList);
}

ATOM TaskbarThumbnails::RegisterTabProxyClass(const TCHAR *szClassName)
{
	WNDCLASSEX wcex;
	wcex.cbSize = sizeof(wcex);
	wcex.style = 0;
	wcex.lpfnWndProc = TabProxyWndProcStub;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = sizeof(TabProxy *);
	wcex.hInstance = GetModuleHandle(nullptr);
	wcex.hIcon = nullptr;
	wcex.hIconSm = nullptr;
	wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wcex.hbrBackground = nullptr;
	wcex.lpszMenuName = nullptr;
	wcex.lpszClassName = szClassName;

	return RegisterClassEx(&wcex);
}

/* The Display Window Manager (DWM) will only interact with top-level
windows. Therefore, we'll need to create a top-level proxy window for
every tab. This top-level window will be hidden, and will handle the
thumbnail preview for the tab.
References:
http://dotnet.dzone.com/news/windows-7-taskbar-tabbed
http://channel9.msdn.com/learn/courses/Windows7/Taskbar/Win7TaskbarNative/Exercise-Experiment-with-the-New-Windows-7-Taskbar-Features/
*/
void TaskbarThumbnails::CreateTabProxy(int iTabId, BOOL bSwitchToNewTab)
{
	static int iCount = 0;
	TCHAR szClassName[512];
	StringCchPrintf(szClassName, SIZEOF_ARRAY(szClassName), _T("Explorer++TabProxy%d"), iCount++);

	ATOM aRet = RegisterTabProxyClass(szClassName);

	if (aRet == 0)
	{
		return;
	}

	TabProxy *ptp = new TabProxy();
	ptp->taskbarThumbnails = this;
	ptp->iTabId = iTabId;

	HWND hTabProxy = CreateWindow(szClassName, EMPTY_STRING, WS_OVERLAPPEDWINDOW, 0, 0, 0, 0,
		nullptr, nullptr, GetModuleHandle(nullptr), (LPVOID) ptp);

	if (!hTabProxy)
	{
		return;
	}

	BOOL bValue = TRUE;
	DwmSetWindowAttribute(hTabProxy, DWMWA_FORCE_ICONIC_REPRESENTATION, &bValue, sizeof(BOOL));
	DwmSetWindowAttribute(hTabProxy, DWMWA_HAS_ICONIC_BITMAP, &bValue, sizeof(BOOL));
	RegisterTab(hTabProxy, EMPTY_STRING, bSwitchToNewTab);

	TabProxyInfo tpi;
	tpi.hProxy = hTabProxy;
	tpi.iTabId = iTabId;
	tpi.atomClass = aRet;

	m_TabProxyList.push_back(std::move(tpi));

	const Tab &tab = m_tabContainer->GetTab(iTabId);
	SetTabProxyIcon(tab);
	UpdateTaskbarThumbnailTitle(tab);
}

void TaskbarThumbnails::RemoveTabProxy(int iTabId)
{
	auto tabProxy = std::find_if(m_TabProxyList.begin(), m_TabProxyList.end(),
		[iTabId](const TabProxyInfo &currentTabProxy) { return currentTabProxy.iTabId == iTabId; });

	if (tabProxy == m_TabProxyList.end())
	{
		assert(false);
		return;
	}

	DestroyTabProxy(*tabProxy);

	m_TabProxyList.erase(tabProxy);
}

void TaskbarThumbnails::DestroyTabProxy(TabProxyInfo &tabProxy)
{
	m_taskbarList->UnregisterTab(tabProxy.hProxy);

	auto *ptp = reinterpret_cast<TabProxy *>(GetWindowLongPtr(tabProxy.hProxy, GWLP_USERDATA));
	DestroyWindow(tabProxy.hProxy);
	delete ptp;

	UnregisterClass(reinterpret_cast<LPCWSTR>(MAKEWORD(tabProxy.atomClass, 0)),
		GetModuleHandle(nullptr));
}

void TaskbarThumbnails::InvalidateTaskbarThumbnailBitmap(const Tab &tab)
{
	for (auto itr = m_TabProxyList.begin(); itr != m_TabProxyList.end(); itr++)
	{
		if (itr->iTabId == tab.GetId())
		{
			DwmInvalidateIconicBitmaps(itr->hProxy);
			break;
		}
	}
}

void TaskbarThumbnails::RegisterTab(HWND hTabProxy, const TCHAR *szDisplayName, BOOL bTabActive)
{
	/* Register and insert the tab into the current list of
	taskbar thumbnails. */
	m_taskbarList->RegisterTab(hTabProxy, m_coreInterface->GetMainWindow());
	m_taskbarList->SetTabOrder(hTabProxy, nullptr);

	m_taskbarList->SetThumbnailTooltip(hTabProxy, szDisplayName);

	if (bTabActive)
	{
		m_taskbarList->SetTabActive(hTabProxy, m_coreInterface->GetMainWindow(), 0);
	}
}

LRESULT CALLBACK TaskbarThumbnails::TabProxyWndProcStub(HWND hwnd, UINT Msg, WPARAM wParam,
	LPARAM lParam)
{
	auto *ptp = (TabProxy *) GetWindowLongPtr(hwnd, GWLP_USERDATA);

	switch (Msg)
	{
	case WM_CREATE:
	{
		ptp = (TabProxy *) ((CREATESTRUCT *) lParam)->lpCreateParams;

		SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR) ptp);
	}
	break;
	}

	if (ptp != nullptr)
	{
		return ptp->taskbarThumbnails->TabProxyWndProc(hwnd, Msg, wParam, lParam, ptp->iTabId);
	}
	else
	{
		return DefWindowProc(hwnd, Msg, wParam, lParam);
	}
}

LRESULT CALLBACK TaskbarThumbnails::TabProxyWndProc(HWND hwnd, UINT Msg, WPARAM wParam,
	LPARAM lParam, int iTabId)
{
	const Tab *tab = m_tabContainer->GetTabOptional(iTabId);

	switch (Msg)
	{
	case WM_ACTIVATE:
		/* Restore the main window if necessary, and switch
		to the actual tab. */
		if (IsIconic(m_coreInterface->GetMainWindow()))
		{
			ShowWindow(m_coreInterface->GetMainWindow(), SW_RESTORE);
		}

		m_tabContainer->SelectTab(*tab);
		return 0;

	case WM_SETFOCUS:
		SetFocus(tab->GetShellBrowser()->GetListView());
		break;

	case WM_SYSCOMMAND:
		switch (wParam)
		{
		case SC_CLOSE:
			break;

		default:
			SendMessage(tab->GetShellBrowser()->GetListView(), WM_SYSCOMMAND, wParam, lParam);
			break;
		}
		break;

	/* Generate a thumbnail of the current tab. Basic procedure:
	1. Generate a full-scale bitmap of the main window.
	2. Overlay a bitmap of the specified tab onto the main
	window bitmap.
	3. Shrink the resulting bitmap down to the correct thumbnail size.

	A thumbnail will be dynamically generated, provided the main window
	is not currently minimized (as we won't be able to grab a screenshot
	of it). If the main window is minimized, we'll use a cached screenshot
	of the tab (taken before the main window was minimized). */
	case WM_DWMSENDICONICTHUMBNAIL:
		OnDwmSendIconicThumbnail(hwnd, *tab, HIWORD(lParam), LOWORD(lParam));
		return 0;

	case WM_DWMSENDICONICLIVEPREVIEWBITMAP:
	{
		if (IsIconic(m_coreInterface->GetMainWindow()))
		{
			/* TODO: Show an image here... */
		}
		else
		{
			wil::unique_hbitmap bitmap = GetTabLivePreviewBitmap(*tab);

			RECT rcTab;
			GetClientRect(tab->GetShellBrowser()->GetListView(), &rcTab);
			MapWindowPoints(tab->GetShellBrowser()->GetListView(), m_coreInterface->GetMainWindow(),
				reinterpret_cast<LPPOINT>(&rcTab), 2);

			MENUBARINFO mbi;
			mbi.cbSize = sizeof(mbi);
			GetMenuBarInfo(m_coreInterface->GetMainWindow(), OBJID_MENU, 0, &mbi);

			POINT ptOrigin;

			/* The operating system will automatically draw the main window. Therefore,
			we'll just shift the tab into it's proper position. */
			ptOrigin.x = rcTab.left;

			/* Need to include the menu bar in the offset. */
			ptOrigin.y = rcTab.top + mbi.rcBar.bottom - mbi.rcBar.top;

			DwmSetIconicLivePreviewBitmap(hwnd, bitmap.get(), &ptOrigin, 0);
		}

		return 0;
	}

	case WM_CLOSE:
	{
		int nTabs = m_tabContainer->GetNumTabs();

		if (nTabs == 1)
		{
			/* If this is the last tab, we'll close
			the whole application. */
			SendMessage(m_coreInterface->GetMainWindow(), WM_CLOSE, 0, 0);
		}
		else
		{
			m_tabContainer->CloseTab(*tab);
		}
	}
	break;
	}

	return DefWindowProc(hwnd, Msg, wParam, lParam);
}

void TaskbarThumbnails::OnDwmSendIconicThumbnail(HWND tabProxy, const Tab &tab, int maxWidth,
	int maxHeight)
{
	wil::unique_hbitmap hbmTab;

	/* If the main window is minimized, it won't be possible
	to generate a thumbnail for any of the tabs. In that
	case, use a static 'No Preview Available' bitmap. */
	if (IsIconic(m_coreInterface->GetMainWindow()))
	{
		hbmTab.reset(static_cast<HBITMAP>(LoadImage(GetModuleHandle(nullptr),
			MAKEINTRESOURCE(IDB_NOPREVIEWAVAILABLE), IMAGE_BITMAP, 0, 0, 0)));

		SetBitmapDimensionEx(hbmTab.get(), 223, 130, nullptr);
	}
	else
	{
		hbmTab = CaptureTabScreenshot(tab);
	}

	SIZE currentSize;
	GetBitmapDimensionEx(hbmTab.get(), &currentSize);

	/* Shrink the bitmap. */
	wil::unique_hdc_window hdc = wil::GetDC(m_coreInterface->GetMainWindow());
	wil::unique_hdc hdcSrc(CreateCompatibleDC(hdc.get()));

	auto previousTabBitmap = wil::SelectObject(hdcSrc.get(), hbmTab.get());

	wil::unique_hdc hdcThumbnailSrc(CreateCompatibleDC(hdc.get()));

	int finalWidth;
	int finalHeight;

	/* If the current height of the main window
	is less than the width, we'll create a thumbnail
	of maximum width; else maximum height. */
	if (((double) currentSize.cx / (double) maxWidth)
		> ((double) currentSize.cy / (double) maxHeight))
	{
		finalWidth = maxWidth;
		finalHeight = (int) ceil(maxWidth * ((double) currentSize.cy / (double) currentSize.cx));
	}
	else
	{
		finalHeight = maxHeight;
		finalWidth = (int) ceil(maxHeight * ((double) currentSize.cx / (double) currentSize.cy));
	}

	wil::unique_hbitmap hbmThumbnail;
	Gdiplus::Color color(0, 0, 0);
	Gdiplus::Bitmap bmpThumbnail(finalWidth, finalHeight, PixelFormat32bppARGB);
	bmpThumbnail.GetHBITMAP(color, &hbmThumbnail);

	auto previousThumbnailBitmap = wil::SelectObject(hdcThumbnailSrc.get(), hbmThumbnail.get());

	/* Finally, shrink the full-scale bitmap down into a thumbnail. */
	POINT pt;
	SetStretchBltMode(hdcThumbnailSrc.get(), HALFTONE);
	SetBrushOrgEx(hdcThumbnailSrc.get(), 0, 0, &pt);
	StretchBlt(hdcThumbnailSrc.get(), 0, 0, finalWidth, finalHeight, hdcSrc.get(), 0, 0,
		currentSize.cx, currentSize.cy, SRCCOPY);

	DwmSetIconicThumbnail(tabProxy, hbmThumbnail.get(), 0);
}

wil::unique_hbitmap TaskbarThumbnails::CaptureTabScreenshot(const Tab &tab)
{
	wil::unique_hdc_window hdc = wil::GetDC(m_coreInterface->GetMainWindow());
	wil::unique_hdc hdcSrc(CreateCompatibleDC(hdc.get()));

	RECT rcMain;
	GetClientRect(m_coreInterface->GetMainWindow(), &rcMain);

	/* Any bitmap sent back to the operating system will need to be in 32-bit
	ARGB format. */
	wil::unique_hbitmap hBitmap;
	Gdiplus::Color color(0, 0, 0);
	Gdiplus::Bitmap bi(GetRectWidth(&rcMain), GetRectHeight(&rcMain), PixelFormat32bppARGB);
	bi.GetHBITMAP(color, &hBitmap);

	/* Draw the main window into the bitmap. */
	auto mainWindowPreviousBitmap = wil::SelectObject(hdcSrc.get(), hBitmap.get());
	BitBlt(hdcSrc.get(), 0, 0, GetRectWidth(&rcMain), GetRectHeight(&rcMain), hdc.get(), 0, 0,
		SRCCOPY);

	/* Now draw the tab onto the main window. */
	RECT rcTab;
	GetClientRect(tab.GetShellBrowser()->GetListView(), &rcTab);

	wil::unique_hdc_window hdcTab = wil::GetDC(tab.GetShellBrowser()->GetListView());
	wil::unique_hdc hdcTabSrc(CreateCompatibleDC(hdcTab.get()));
	wil::unique_hbitmap hbmTab(
		CreateCompatibleBitmap(hdcTab.get(), GetRectWidth(&rcTab), GetRectHeight(&rcTab)));

	auto tabPreviousBitmap = wil::SelectObject(hdcTabSrc.get(), hbmTab.get());

	BOOL bVisible = IsWindowVisible(tab.GetShellBrowser()->GetListView());

	if (!bVisible)
	{
		ShowWindow(tab.GetShellBrowser()->GetListView(), SW_SHOW);
	}

	PrintWindow(tab.GetShellBrowser()->GetListView(), hdcTabSrc.get(), PW_CLIENTONLY);

	if (!bVisible)
	{
		ShowWindow(tab.GetShellBrowser()->GetListView(), SW_HIDE);
	}

	MapWindowPoints(tab.GetShellBrowser()->GetListView(), m_coreInterface->GetMainWindow(),
		reinterpret_cast<LPPOINT>(&rcTab), 2);
	BitBlt(hdcSrc.get(), rcTab.left, rcTab.top, GetRectWidth(&rcTab), GetRectHeight(&rcTab),
		hdcTabSrc.get(), 0, 0, SRCCOPY);

	/* Shrink the bitmap. */
	wil::unique_hdc hdcThumbnailSrc(CreateCompatibleDC(hdc.get()));

	wil::unique_hbitmap hbmThumbnail;
	Gdiplus::Bitmap bmpThumbnail(GetRectWidth(&rcMain), GetRectHeight(&rcMain),
		PixelFormat32bppARGB);
	bmpThumbnail.GetHBITMAP(color, &hbmThumbnail);

	auto thumbnailPreviousBitmap = wil::SelectObject(hdcThumbnailSrc.get(), hbmThumbnail.get());

	/* Finally, shrink the full-scale bitmap down into a thumbnail. */
	POINT pt;
	SetStretchBltMode(hdcThumbnailSrc.get(), HALFTONE);
	SetBrushOrgEx(hdcThumbnailSrc.get(), 0, 0, &pt);
	BitBlt(hdcThumbnailSrc.get(), 0, 0, GetRectWidth(&rcMain), GetRectHeight(&rcMain), hdcSrc.get(),
		0, 0, SRCCOPY);

	SetBitmapDimensionEx(hbmThumbnail.get(), GetRectWidth(&rcMain), GetRectHeight(&rcMain),
		nullptr);

	return hbmThumbnail;
}

wil::unique_hbitmap TaskbarThumbnails::GetTabLivePreviewBitmap(const Tab &tab)
{
	wil::unique_hdc_window hdcTab = wil::GetDC(tab.GetShellBrowser()->GetListView());
	wil::unique_hdc hdcTabSrc(CreateCompatibleDC(hdcTab.get()));

	RECT rcTab;
	GetClientRect(tab.GetShellBrowser()->GetListView(), &rcTab);

	wil::unique_hbitmap hbmTab;
	Gdiplus::Color color(0, 0, 0);
	Gdiplus::Bitmap bi(GetRectWidth(&rcTab), GetRectHeight(&rcTab), PixelFormat32bppARGB);
	bi.GetHBITMAP(color, &hbmTab);

	auto tabPreviousBitmap = wil::SelectObject(hdcTabSrc.get(), hbmTab.get());

	BOOL bVisible = IsWindowVisible(tab.GetShellBrowser()->GetListView());

	if (!bVisible)
	{
		ShowWindow(tab.GetShellBrowser()->GetListView(), SW_SHOW);
	}

	PrintWindow(tab.GetShellBrowser()->GetListView(), hdcTabSrc.get(), PW_CLIENTONLY);

	if (!bVisible)
	{
		ShowWindow(tab.GetShellBrowser()->GetListView(), SW_HIDE);
	}

	SetStretchBltMode(hdcTabSrc.get(), HALFTONE);
	SetBrushOrgEx(hdcTabSrc.get(), 0, 0, nullptr);
	StretchBlt(hdcTabSrc.get(), 0, 0, GetRectWidth(&rcTab), GetRectHeight(&rcTab), hdcTabSrc.get(),
		0, 0, GetRectWidth(&rcTab), GetRectHeight(&rcTab), SRCCOPY);

	return hbmTab;
}

void TaskbarThumbnails::OnTabSelectionChanged(const Tab &tab)
{
	for (const TabProxyInfo &tabProxyInfo : m_TabProxyList)
	{
		if (tabProxyInfo.iTabId == tab.GetId())
		{
			int index = m_tabContainer->GetTabIndex(tab);

			int nTabs = m_tabContainer->GetNumTabs();

			/* Potentially the tab may have swapped position, so
			tell the taskbar to reposition it. */
			if (index == (nTabs - 1))
			{
				m_taskbarList->SetTabOrder(tabProxyInfo.hProxy, nullptr);
			}
			else
			{
				const Tab &nextTab = m_tabContainer->GetTabByIndex(index + 1);

				for (const TabProxyInfo &tabProxyInfoNext : m_TabProxyList)
				{
					if (tabProxyInfoNext.iTabId == nextTab.GetId())
					{
						m_taskbarList->SetTabOrder(tabProxyInfo.hProxy, tabProxyInfoNext.hProxy);
						break;
					}
				}
			}

			m_taskbarList->SetTabActive(tabProxyInfo.hProxy, m_coreInterface->GetMainWindow(), 0);
			break;
		}
	}
}

void TaskbarThumbnails::OnNavigationCommitted(const Tab &tab, const NavigateParams &navigateParams)
{
	UNREFERENCED_PARAMETER(navigateParams);

	InvalidateTaskbarThumbnailBitmap(tab);
	SetTabProxyIcon(tab);
	UpdateTaskbarThumbnailTitle(tab);
}

void TaskbarThumbnails::OnNavigationCompleted(const Tab &tab, const NavigateParams &navigateParams)
{
	UNREFERENCED_PARAMETER(navigateParams);

	InvalidateTaskbarThumbnailBitmap(tab);
}

void TaskbarThumbnails::UpdateTaskbarThumbnailTitle(const Tab &tab)
{
	for (const TabProxyInfo &tabProxyInfo : m_TabProxyList)
	{
		if (tabProxyInfo.iTabId == tab.GetId())
		{
			SetWindowText(tabProxyInfo.hProxy, tab.GetName().c_str());
			m_taskbarList->SetThumbnailTooltip(tabProxyInfo.hProxy, tab.GetName().c_str());
			break;
		}
	}
}

void TaskbarThumbnails::SetTabProxyIcon(const Tab &tab)
{
	for (TabProxyInfo &tabProxyInfo : m_TabProxyList)
	{
		if (tabProxyInfo.iTabId == tab.GetId())
		{
			auto pidlDirectory = tab.GetShellBrowser()->GetDirectoryIdl();

			/* TODO: The proxy icon may also be the lock icon, if
			the tab is locked. */
			SHFILEINFO shfi;
			DWORD_PTR res = SHGetFileInfo((LPCTSTR) pidlDirectory.get(), 0, &shfi, sizeof(shfi),
				SHGFI_PIDL | SHGFI_ICON | SHGFI_SMALLICON);

			if (!res)
			{
				return;
			}

			tabProxyInfo.icon.reset(shfi.hIcon);

			SetClassLongPtr(tabProxyInfo.hProxy, GCLP_HICONSM, (LONG_PTR) tabProxyInfo.icon.get());
			break;
		}
	}
}

void TaskbarThumbnails::OnApplicationShuttingDown()
{
	for (auto &tabProxy : m_TabProxyList)
	{
		DestroyTabProxy(tabProxy);
	}

	m_TabProxyList.clear();
}
