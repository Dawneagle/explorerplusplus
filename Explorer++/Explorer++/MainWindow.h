// Copyright (C) Explorer++ Project
// SPDX-License-Identifier: GPL-3.0-only
// See LICENSE in the top level directory

#pragma once

#include "Tab.h"
#include "../Helper/DropTargetWindow.h"
#include "../Helper/WinRTBaseWrapper.h"

struct Config;
class CoreInterface;
class WindowSubclassWrapper;

class MainWindow : private DropTargetInternal
{
public:
	static MainWindow *Create(HWND hwnd, std::shared_ptr<Config> config, HINSTANCE resourceInstance,
		CoreInterface *coreInterface);

private:
	MainWindow(HWND hwnd, std::shared_ptr<Config> config, HINSTANCE resourceInstance,
		CoreInterface *coreInterface);
	~MainWindow() = default;

	LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

	void OnNavigationCommitted(const Tab &tab, PCIDLIST_ABSOLUTE pidl, bool addHistoryEntry);
	void OnTabSelected(const Tab &tab);

	void OnShowFullTitlePathUpdated(BOOL newValue);
	void OnShowUserNameInTitleBarUpdated(BOOL newValue);
	void OnShowPrivilegeLevelInTitleBarUpdated(BOOL newValue);

	void UpdateWindowText();

	// DropTargetInternal
	DWORD DragEnter(IDataObject *dataObject, DWORD keyState, POINT pt, DWORD effect) override;
	DWORD DragOver(DWORD keyState, POINT pt, DWORD effect) override;
	void DragLeave() override;
	DWORD Drop(IDataObject *dataObject, DWORD keyState, POINT pt, DWORD effect) override;

	void OnNcDestroy();

	HWND m_hwnd;
	std::shared_ptr<Config> m_config;
	HINSTANCE m_resourceInstance;
	CoreInterface *m_coreInterface;

	std::vector<std::unique_ptr<WindowSubclassWrapper>> m_windowSubclasses;
	std::vector<boost::signals2::scoped_connection> m_connections;

	winrt::com_ptr<DropTargetWindow> m_dropTargetWindow;
};
