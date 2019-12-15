// Copyright (C) Explorer++ Project
// SPDX-License-Identifier: GPL-3.0-only
// See LICENSE in the top level directory

#pragma once

#include "BookmarkHelper.h"
#include "CoreInterface.h"
#include "Navigation.h"
#include "ResourceHelper.h"
#include "../Helper/Bookmark.h"
#include "../Helper/DpiCompatibility.h"
#include "../Helper/WindowSubclassWrapper.h"
#include <boost/optional.hpp>
#include <wil/resource.h>
#include <unordered_map>

class CBookmarksToolbarDropHandler : public IDropTarget
{
public:

	CBookmarksToolbarDropHandler(HWND hToolbar, CBookmarkFolder &AllBookmarks, const std::wstring &guidBookmarksToolbar);
	~CBookmarksToolbarDropHandler();

	/* IUnknown methods. */
	HRESULT __stdcall	QueryInterface(REFIID iid,void **ppvObject);
	ULONG __stdcall		AddRef(void);
	ULONG __stdcall		Release(void);

private:

	CBookmarksToolbarDropHandler & operator = (const CBookmarksToolbarDropHandler &btdh);

	/* IDropTarget methods. */
	HRESULT __stdcall	DragEnter(IDataObject *pDataObject,DWORD grfKeyState,POINTL pt,DWORD *pdwEffect);
	HRESULT __stdcall	DragOver(DWORD grfKeyState,POINTL pt,DWORD *pdwEffect);
	HRESULT __stdcall	DragLeave(void);
	HRESULT __stdcall	Drop(IDataObject *pDataObject,DWORD grfKeyState,POINTL pt,DWORD *pdwEffect);

	int					GetToolbarPositionIndex(const POINTL &pt,bool &bAfter);
	void				RemoveInsertionMark();

	ULONG m_ulRefCount;

	HWND m_hToolbar;
	CBookmarkFolder &m_AllBookmarks;
	std::wstring m_guidBookmarksToolbar;

	IDragSourceHelper *m_pDragSourceHelper;
	IDropTargetHelper *m_pDropTargetHelper;
	bool m_bAcceptData;
};

class CBookmarksToolbar
{
public:

	CBookmarksToolbar(HWND hToolbar, HINSTANCE instance, IExplorerplusplus *pexpp,
		Navigation *navigation, CBookmarkFolder &AllBookmarks, const std::wstring &guidBookmarksToolbar,
		UINT uIDStart, UINT uIDEnd);
	~CBookmarksToolbar();

	/* IBookmarkItemNotification methods. */
	void	OnBookmarkAdded(const CBookmarkFolder &ParentBookmarkFolder,const CBookmark &Bookmark,std::size_t Position);
	void	OnBookmarkFolderAdded(const CBookmarkFolder &ParentBookmarkFolder,const CBookmarkFolder &BookmarkFolder,std::size_t Position);
	void	OnBookmarkModified(const std::wstring &guid);
	void	OnBookmarkFolderModified(const std::wstring &guid);
	void	OnBookmarkRemoved(const std::wstring &guid);
	void	OnBookmarkFolderRemoved(const std::wstring &guid);

private:

	CBookmarksToolbar & operator = (const CBookmarksToolbar &bt);

	static const UINT_PTR SUBCLASS_ID = 0;
	static const UINT_PTR PARENT_SUBCLASS_ID = 0;

	static LRESULT CALLBACK	BookmarksToolbarProcStub(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
	LRESULT CALLBACK	BookmarksToolbarProc(HWND hwnd,UINT uMsg,WPARAM wParam,LPARAM lParam);

	static LRESULT CALLBACK	BookmarksToolbarParentProcStub(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
	LRESULT CALLBACK	BookmarksToolbarParentProc(HWND hwnd,UINT uMsg,WPARAM wParam,LPARAM lParam);

	void	InitializeToolbar();

	void	InsertBookmarkItems();
	void	InsertBookmark(const CBookmark &Bookmark);
	void	InsertBookmark(const CBookmark &Bookmark,std::size_t Position);
	void	InsertBookmarkFolder(const CBookmarkFolder &BookmarkFolder);
	void	InsertBookmarkFolder(const CBookmarkFolder &BookmarkFolder,std::size_t Position);
	void	InsertBookmarkItem(const std::wstring &strName, const std::wstring &guid, bool bFolder, std::size_t Position);

	void	ModifyBookmarkItem(const std::wstring &guid, bool bFolder);

	void	RemoveBookmarkItem(const std::wstring &guid);

	void	OpenBookmarkItemInNewTab(const VariantBookmark &variantBookmarkItem);

	bool	OnCommand(WPARAM wParam, LPARAM lParam);
	bool	OnButtonClick(int command);
	BOOL	OnRightClick(const NMMOUSE *nmm);
	void	OnRightClickMenuItemSelected(int menuItemId, const VariantBookmark &variantBookmark);
	void	ShowBookmarkFolderMenu(const CBookmarkFolder &bookmarkFolder, int command, int index);
	void	OnBookmarkMenuItemClicked(const CBookmark &bookmark);
	void	OnNewBookmark();
	bool	OnGetInfoTip(NMTBGETINFOTIP *infoTip);

	void	OnToolbarContextMenuPreShow(HMENU menu, HWND sourceWindow);

	int		GetBookmarkItemIndex(const std::wstring &guid);

	VariantBookmark	*GetBookmarkItemFromToolbarIndex(int index);

	HWND m_hToolbar;
	DpiCompatibility m_dpiCompat;
	wil::unique_himagelist m_imageList;
	IconImageListMapping m_imageListMappings;

	HINSTANCE m_instance;

	IExplorerplusplus *m_pexpp;
	Navigation *m_navigation;

	CBookmarkFolder &m_AllBookmarks;
	std::wstring m_guidBookmarksToolbar;

	std::unordered_map<UINT, std::wstring> m_mapID;
	UINT m_uIDStart;
	UINT m_uIDEnd;
	UINT m_uIDCounter;

	CBookmarksToolbarDropHandler *m_pbtdh;

	std::vector<WindowSubclassWrapper> m_windowSubclasses;
	std::vector<boost::signals2::scoped_connection> m_connections;
};