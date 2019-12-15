// Copyright (C) Explorer++ Project
// SPDX-License-Identifier: GPL-3.0-only
// See LICENSE in the top level directory

#pragma once

#include <boost/variant.hpp>
#include <list>
#include <optional>
#include <vector>

class CBookmark;
class CBookmarkFolder;

typedef boost::variant<CBookmarkFolder, CBookmark> VariantBookmark;

class CBookmarkFolder
{
public:

	static CBookmarkFolder	Create(const std::wstring &strName, std::optional<std::wstring> guid = std::nullopt);
	static CBookmarkFolder	*CreateNew(const std::wstring &strName, std::optional<std::wstring> guid = std::nullopt);
	static CBookmarkFolder	UnserializeFromRegistry(const std::wstring &strKey);

	void			SerializeToRegistry(const std::wstring &strKey);

	std::wstring GetGUID() const;

	std::wstring	GetName() const;
	void			SetName(const std::wstring &strName);

	FILETIME		GetDateCreated() const;
	FILETIME		GetDateModified() const;

	bool			HasChildren() const;

	/* Returns true if this folder has *at least*
	one child folder. */
	bool			HasChildFolder() const;

	std::list<VariantBookmark>::iterator	begin();
	std::list<VariantBookmark>::iterator	end();

	std::list<VariantBookmark>::const_iterator	begin() const;
	std::list<VariantBookmark>::const_iterator	end() const;

	void			InsertBookmark(const CBookmark &Bookmark);
	void			InsertBookmark(const CBookmark &Bookmark,std::size_t Position);
	void			InsertBookmarkFolder(const CBookmarkFolder &BookmarkFolder);
	void			InsertBookmarkFolder(const CBookmarkFolder &BookmarkFolder,std::size_t Position);

	void			RemoveBookmark();
	void			RemoveBookmarkFolder();

private:

	enum InitializationType_t
	{
		INITIALIZATION_TYPE_NORMAL,
		INITIALIZATION_TYPE_UNSERIALIZE,
		INITIALIZATION_TYPE_REGISTRY
	};

	CBookmarkFolder(const std::wstring &str, InitializationType_t InitializationType, std::optional<std::wstring> guid);

	void			Initialize(const std::wstring &name, std::optional<std::wstring> guid);
	void			InitializeFromRegistry(const std::wstring &strKey);

	void			UpdateModificationTime();

	std::wstring	m_guid;

	std::wstring	m_strName;

	/* Keeps track of the number of child
	folders that are added. Used purely as
	an optimization for the HasChildFolder()
	method above. */
	int				m_nChildFolders;

	FILETIME		m_ftCreated;
	FILETIME		m_ftModified;

	/* List of child folders and bookmarks. Note that
	the ordering within this list defines the ordering
	between child items (i.e. there is no explicit
	ordering). */
	std::list<VariantBookmark>	m_ChildList;
};

class CBookmark
{
public:

	static CBookmark Create(const std::wstring &strName, const std::wstring &strLocation, const std::wstring &strDescription);
	static CBookmark UnserializeFromRegistry(const std::wstring &strKey);

	void			SerializeToRegistry(const std::wstring &strKey);

	std::wstring	GetGUID() const;

	std::wstring	GetName() const;
	std::wstring	GetLocation() const;
	std::wstring	GetDescription() const;

	void			SetName(const std::wstring &strName);
	void			SetLocation(const std::wstring &strLocation);
	void			SetDescription(const std::wstring &strDescription);

	int				GetVisitCount() const;
	FILETIME		GetDateLastVisited() const;

	void			UpdateVisitCount();

	FILETIME		GetDateCreated() const;
	FILETIME		GetDateModified() const;

private:

	CBookmark(const std::wstring &strKey);
	CBookmark(const std::wstring &strName, const std::wstring &strLocation, const std::wstring &strDescription);

	void			InitializeFromRegistry(const std::wstring &strKey);

	void			UpdateModificationTime();

	std::wstring	m_guid;

	std::wstring	m_strName;
	std::wstring	m_strLocation;
	std::wstring	m_strDescription;

	int				m_iVisitCount;
	FILETIME		m_ftLastVisited;

	FILETIME		m_ftCreated;
	FILETIME		m_ftModified;
};