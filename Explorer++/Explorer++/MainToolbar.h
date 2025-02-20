// Copyright (C) Explorer++ Project
// SPDX-License-Identifier: GPL-3.0-only
// See LICENSE in the top level directory

#pragma once

#include "CoreInterface.h"
#include "DefaultToolbarButtons.h"
#include "IconResourceLoader.h"
#include "Tab.h"
#include "../Helper/BaseWindow.h"
#include "../Helper/WindowSubclassWrapper.h"
#include <wil/com.h>
#include <wil/resource.h>
#include <optional>
#include <unordered_map>

struct Config;
class MainToolbar;

class MainToolbarPersistentSettings
{
public:
	static MainToolbarPersistentSettings &GetInstance();

	void LoadXMLSettings(IXMLDOMNode *pNode);
	void SaveXMLSettings(IXMLDOMDocument *pXMLDom, IXMLDOMElement *pe);

private:
	friend MainToolbar;

	MainToolbarPersistentSettings();

	MainToolbarPersistentSettings(const MainToolbarPersistentSettings &);
	MainToolbarPersistentSettings &operator=(const MainToolbarPersistentSettings &);

	// The current set of toolbar buttons.
	std::vector<MainToolbarButton> m_toolbarButtons;
};

class MainToolbar : public BaseWindow
{
public:
	static MainToolbar *Create(HWND parent, HINSTANCE resourceInstance,
		CoreInterface *coreInterface, std::shared_ptr<Config> config);

	void UpdateConfigDependentButtonStates();
	void UpdateToolbarButtonStates();

private:
	enum class HistoryType
	{
		Back,
		Forward
	};

	MainToolbar(HWND parent, HINSTANCE resourceInstance, CoreInterface *coreInterface,
		std::shared_ptr<Config> config);
	~MainToolbar();

	static HWND CreateMainToolbar(HWND parent);

	LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

	static LRESULT CALLBACK ParentWndProcStub(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
		UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
	LRESULT CALLBACK ParentWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

	void Initialize(HWND parent);
	void SetTooolbarImageList();
	static std::unordered_map<int, int> SetUpToolbarImageList(HIMAGELIST imageList,
		IconResourceLoader *iconResourceLoader, int iconSize, UINT dpi);
	void AddButtonsToToolbar(const std::vector<MainToolbarButton> &buttons);
	void AddButtonToToolbar(MainToolbarButton button);
	TBBUTTON GetToolbarButtonDetails(MainToolbarButton button) const;
	void AddStringsToToolbar();
	void AddStringToToolbar(MainToolbarButton button);
	void GetToolbarButtonText(MainToolbarButton button, TCHAR *szText, int bufSize) const;
	BYTE LookupToolbarButtonExtraStyles(MainToolbarButton button) const;
	int LookupToolbarButtonTextID(MainToolbarButton button) const;

	BOOL OnTBQueryInsert();
	BOOL OnTBQueryDelete();
	BOOL OnTBRestore();
	BOOL OnTBGetButtonInfo(LPARAM lParam);
	void OnTBReset();
	void OnTBChange();
	void OnTBGetInfoTip(LPARAM lParam);
	LRESULT OnTbnDropDown(const NMTOOLBAR *nmtb);
	void ShowHistoryMenu(HistoryType historyType, const POINT &pt);
	void ShowToolbarViewsDropdown();
	void CreateViewsMenu(POINT *ptOrigin);

	void OnTabSelected(const Tab &tab);
	void OnNavigationCommitted(const Tab &tab, PCIDLIST_ABSOLUTE pidl, bool addHistoryEntry);
	void OnFocusChanged(WindowFocusSource windowFocusSource);

	void UpdateToolbarButtonImageIndexes();

	void OnUseLargeToolbarIconsUpdated(BOOL newValue);

	void OnClipboardUpdate();
	void OnMButtonDown(HWND hwnd, BOOL doubleClick, int x, int y, UINT keysDown);
	void OnMButtonUp(HWND hwnd, int x, int y, UINT keysDown);

	MainToolbarPersistentSettings *m_persistentSettings;

	HINSTANCE m_resourceInstance;
	CoreInterface *m_coreInterface;
	std::shared_ptr<Config> m_config;

	wil::com_ptr_nothrow<IImageList> m_systemImageList;
	wil::unique_hbitmap m_defaultFolderIconBitmap;
	wil::unique_himagelist m_imageListSmall;
	wil::unique_himagelist m_imageListLarge;
	std::unordered_map<int, int> m_toolbarImageMapSmall;
	std::unordered_map<int, int> m_toolbarImageMapLarge;
	std::unordered_map<int, int> m_toolbarStringMap;

	std::vector<std::unique_ptr<WindowSubclassWrapper>> m_windowSubclasses;
	std::vector<boost::signals2::scoped_connection> m_connections;

	std::optional<int> m_middleButtonItem;
};
