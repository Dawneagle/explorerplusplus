// Copyright (C) Explorer++ Project
// SPDX-License-Identifier: GPL-3.0-only
// See LICENSE in the top level directory

#pragma once

#include <wil/resource.h>
#include <ShObjIdl.h>
#include <ShlGuid.h>
#include <exdisp.h>
#include <list>
#include <optional>
#include <string>
#include <vector>

#define CONTROL_PANEL_CATEGORY_VIEW _T("::{26EE0668-A00A-44D7-9371-BEB064C98683}")

#define FRIENDLY_NAME_DESKTOP _T("Desktop")
#define FRIENDLY_NAME_PICTURES _T("Pictures")
#define FRIENDLY_NAME_MUSIC _T("Music")
#define FRIENDLY_NAME_VIDEOS _T("Videos")
#define FRIENDLY_NAME_DOCUMENTS _T("Documents")

// The path to the quick access/home folder in Windows 10/11.
static inline const WCHAR QUICK_ACCESS_PATH[] = L"shell:::{679f85cb-0220-4080-b29b-5540cc05aab6}";

// The path to the Linux distributions folder.
static inline const WCHAR WSL_DISTRIBUTIONS_PATH[] = L"\\\\wsl$";

/* See: http://msdn.microsoft.com/en-us/library/bb776902(v=VS.85).aspx#CFSTR_SHELLIDLIST */
#define HIDA_GetPIDLFolder(pida) (PCIDLIST_ABSOLUTE)(((LPBYTE) pida) + (pida)->aoffset[0])
#define HIDA_GetPIDLItem(pida, i) (PCIDLIST_RELATIVE)(((LPBYTE) pida) + (pida)->aoffset[i + 1])

const SHCOLUMNID SCID_ORIGINAL_LOCATION = { PSGUID_DISPLACED, PID_DISPLACED_FROM };
const SHCOLUMNID SCID_DATE_DELETED = { PSGUID_DISPLACED, PID_DISPLACED_DATE };

enum class DefaultIconType
{
	Folder,
	File
};

enum class EnvVarsExpansion
{
	Expand,
	DontExpand
};

enum class ShellItemType
{
	File,
	Folder
};

struct JumpListTaskInformation
{
	const TCHAR *pszName;
	const TCHAR *pszPath;
	const TCHAR *pszArguments;
	const TCHAR *pszIconPath;
	int iIcon;
};

struct ContextMenuHandler
{
	HMODULE hDLL;
	IUnknown *pUnknown;
};

using unique_pidl_absolute = wil::unique_cotaskmem_ptr<std::remove_pointer_t<PIDLIST_ABSOLUTE>>;
using unique_pidl_relative = wil::unique_cotaskmem_ptr<std::remove_pointer_t<PIDLIST_RELATIVE>>;
using unique_pidl_child = wil::unique_cotaskmem_ptr<std::remove_pointer_t<PITEMID_CHILD>>;

using unique_shell_window_cookie = wil::unique_com_token<IShellWindows, long,
	decltype(&IShellWindows::Revoke), &IShellWindows::Revoke>;

HRESULT GetDisplayName(const std::wstring &parsingPath, DWORD flags, std::wstring &output);
HRESULT GetDisplayName(PCIDLIST_ABSOLUTE pidl, DWORD flags, std::wstring &output);
HRESULT GetDisplayName(IShellFolder *shellFolder, PCITEMID_CHILD pidlChild, DWORD flags,
	std::wstring &output);
HRESULT GetCsidlDisplayName(int csidl, DWORD flags, std::wstring &output);
HRESULT GetVirtualParentPath(PCIDLIST_ABSOLUTE pidlDirectory, PIDLIST_ABSOLUTE *pidlParent);
HRESULT GetRootPidl(PIDLIST_ABSOLUTE *pidl);
BOOL IsNamespaceRoot(PCIDLIST_ABSOLUTE pidl);
HRESULT BindToIdl(PCIDLIST_ABSOLUTE pidl, REFIID riid, void **ppv);
HRESULT GetUIObjectOf(IShellFolder *pShellFolder, HWND hwndOwner, UINT cidl,
	PCUITEMID_CHILD_ARRAY apidl, REFIID riid, void **ppv);
HRESULT ConvertVariantToString(const VARIANT *vt, TCHAR *szDetail, size_t cchMax,
	BOOL friendlyDate);
HRESULT ConvertVariantStringArrayToString(SAFEARRAY *array, TCHAR *szDetail, size_t cchMax);
HRESULT ConvertGenericVariantToString(const VARIANT *vt, TCHAR *szDetail, size_t cchMax);
HRESULT ConvertDateVariantToString(DATE date, TCHAR *szDetail, size_t cchMax, BOOL friendlyDate);
BOOL GetBooleanVariant(IShellFolder2 *shellFolder2, PCITEMID_CHILD pidlChild,
	const SHCOLUMNID *column, BOOL defaultValue);
std::wstring ConvertBstrToString(BSTR str);
std::optional<std::wstring> GetFolderPathForDisplay(PCIDLIST_ABSOLUTE pidl);
bool IsPathGUID(const std::wstring &path);
BOOL ArePidlsEquivalent(PCIDLIST_ABSOLUTE pidl1, PCIDLIST_ABSOLUTE pidl2);
HRESULT AddJumpListTasks(const std::list<JumpListTaskInformation> &taskList);
BOOL LoadContextMenuHandlers(const TCHAR *szRegKey,
	std::list<ContextMenuHandler> &contextMenuHandlers,
	const std::vector<std::wstring> &blacklistedCLSIDEntries);
BOOL LoadIUnknownFromCLSID(const TCHAR *szCLSID, ContextMenuHandler *pContextMenuHandler);
HRESULT GetItemAttributes(const TCHAR *szItemParsingPath, SFGAOF *pItemAttributes);
HRESULT GetItemAttributes(PCIDLIST_ABSOLUTE pidl, SFGAOF *pItemAttributes);
BOOL ExecuteFileAction(HWND hwnd, const TCHAR *szVerb, const TCHAR *szParameters,
	const TCHAR *szStartDirectory, LPCITEMIDLIST pidl);
BOOL ExecuteAndShowCurrentProcess(HWND hwnd, const TCHAR *szParameters);
BOOL ExecuteAndShowProcess(HWND hwnd, const TCHAR *szProcess, const TCHAR *szParameters);
HRESULT DecodeFriendlyPath(const std::wstring &friendlyPath, std::wstring &parsingPath);
HRESULT ShowMultipleFileProperties(PCIDLIST_ABSOLUTE pidlDirectory,
	const std::vector<PCITEMID_CHILD> &items, HWND hwnd);
HRESULT ExecuteActionFromContextMenu(PCIDLIST_ABSOLUTE pidlDirectory,
	const std::vector<PCITEMID_CHILD> &items, HWND hwnd, const std::wstring &action, DWORD mask,
	IUnknown *site);
BOOL CompareVirtualFolders(const TCHAR *szDirectory, UINT uFolderCSIDL);
bool IsChildOfLibrariesFolder(PCIDLIST_ABSOLUTE pidl);
HRESULT CreateSimplePidl(const std::wstring &path, PIDLIST_ABSOLUTE *pidl,
	IShellFolder *parent = nullptr, ShellItemType shellItemType = ShellItemType::File);
HRESULT SimplePidlToFullPidl(PCIDLIST_ABSOLUTE simplePidl, PIDLIST_ABSOLUTE *fullPidl);
std::vector<unique_pidl_absolute> DeepCopyPidls(const std::vector<PCIDLIST_ABSOLUTE> &pidls);
std::vector<unique_pidl_absolute> DeepCopyPidls(const std::vector<unique_pidl_absolute> &pidls);
std::vector<PCIDLIST_ABSOLUTE> ShallowCopyPidls(const std::vector<unique_pidl_absolute> &pidls);

std::optional<std::wstring> TransformUserEnteredPathToAbsolutePathAndNormalize(
	const std::wstring &userEnteredPath, const std::wstring &currentDirectory,
	EnvVarsExpansion envVarsExpansionType);
std::optional<std::wstring> MaybeExtractPathFromFileUrl(const std::wstring &url);
std::optional<std::wstring> ExpandEnvironmentStringsWrapper(const std::wstring &sourceString);
std::optional<std::wstring> MaybeTransformRootPathToAbsolutePath(const std::wstring path,
	const std::wstring &currentDirectory);
std::optional<std::wstring> PathCanonicalizeWrapper(const std::wstring &path);
std::optional<std::wstring> PathAppendWrapper(const std::wstring &path,
	const std::wstring &pathToAppend);
std::optional<std::wstring> PathStripToRootWrapper(const std::wstring &path);
std::optional<std::wstring> GetCurrentDirectoryWrapper();

/* Default icon indices. */
int GetDefaultFolderIconIndex();
int GetDefaultFileIconIndex();
int GetDefaultIcon(DefaultIconType defaultIconType);

/* Infotips. */
HRESULT GetItemInfoTip(const std::wstring &itemPath, std::wstring &outputInfoTip);
HRESULT GetItemInfoTip(PCIDLIST_ABSOLUTE pidlComplete, std::wstring &outputInfoTip);

std::size_t hash_value(const IID &iid);
