/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// The INI text behind the FX Editor. Every ParticleSystem and FXList block is kept as the text it
// has in its file; the text is what gets edited. Each change is parsed by the game's own INI code
// into the live template at once, and saving puts only the changed blocks back into their files,
// so comments, order and formatting of everything else stay as they are.

#pragma once

#include "Common/AsciiString.h"
#include "Lib/BaseType.h"
#include "PreviewScene.h"

#include <string>
#include <vector>

/// One "key = value" line of a block, or a nested block (FXList nugget) header / End.
struct IniLine
{
	enum Type { BLANK, HEADER, FIELD, END, OTHER };

	Type type;
	size_t start;				///< offset of the line in the block text
	size_t end;					///< offset after the line break
	Int depth;					///< 0 for the block header and its End, 1 for fields and nugget headers, 2 inside nuggets
	Int nugget;					///< index of the nugget the line belongs to, -1 outside of nuggets
	std::string key;			///< field name, or the header word (block or nugget type)
	std::string value;		///< field value without comment; for headers the rest of the line
	std::string comment;		///< trailing comment including its marker
	std::string indent;
};

/// Splits block text into lines and finds fields in it. Static helpers, also used by the MCP commands.
namespace IniText
{
	void parseLines(const std::string &text, std::vector<IniLine> &lines);

	/// Number of nested blocks (FXList nuggets) and their type names.
	void getNuggets(const std::string &text, std::vector<std::string> &types);

	/// Value of a field of the block (nugget = -1) or of a nugget; false if the field is not there.
	Bool getField(const std::string &text, Int nugget, const char *key, std::string &value);

	/// Sets a field: the value of the existing line is replaced (its comment stays), or a new line is
	/// added at the end of the block or nugget.
	std::string setField(const std::string &text, Int nugget, const char *key, const std::string &value);
	std::string removeField(const std::string &text, Int nugget, const char *key);
	/// A new field line at the end of the block, even when the key is there already.
	std::string appendField(const std::string &text, const char *key, const std::string &value);

	/// Fields that repeat with a name as their first word (Param = Gloss 24): each name and the rest.
	void getKeyedFields(const std::string &text, const char *key, std::vector<std::string> &names, std::vector<std::string> &values);
	/// Sets the line of that name (its comment stays), or adds one after the last line of the key.
	std::string setKeyedField(const std::string &text, const char *key, const char *name, const std::string &value);
	std::string removeKeyedField(const std::string &text, const char *key, const char *name);

	std::string addNugget(const std::string &text, const char *type, Int insertAt = -1);
	std::string removeNugget(const std::string &text, Int nugget);
	std::string moveNugget(const std::string &text, Int nugget, Int offset);
	std::string duplicateNugget(const std::string &text, Int nugget);

	/// The block's name from its header line ("ParticleSystem Name").
	std::string blockName(const std::string &text);
	/// Same text with another name in the header line.
	std::string renameBlock(const std::string &text, const char *newName);
}

struct IniSourceFile
{
	AsciiString path;			///< as the game opens it, e.g. Data\INI\FXList.ini
	std::string text;			///< the file as it is on disk (or in its archive)
	Bool loose;					///< a file on disk; false if it only exists in a .big archive
	Bool crlf;					///< the file uses CR LF line breaks
	Bool backedUp;				///< a .fxeditor.bak was written this session
};

/// Which INI blocks a database holds.
enum IniDomain
{
	INI_DOMAIN_FX,			///< ParticleSystem and FXList
	INI_DOMAIN_RENDER,	///< PostProcessPass, PostProcess, PostProcessChain, ShadowMap and Material (Direct3D 12)
};

struct IniBlock
{
	PreviewKind kind;			///< INI_DOMAIN_FX only
	std::string type;			///< the header word, e.g. ParticleSystem or PostProcessPass
	AsciiString name;			///< the name in the header; the type for blocks without one (PostProcess, ShadowMap)
	Int file;						///< index into the files, -1 for a block made in the editor and not saved yet
	size_t offset;				///< where the block starts in its file
	size_t length;
	std::string savedText;	///< as on disk, with \n line breaks
	std::string text;			///< as edited
	std::string error;			///< what the game said about the last text that failed to parse
	std::vector<std::string> undo;
	std::vector<std::string> redo;

	Bool isDirty() const { return text != savedText || file < 0; }
};

class IniDatabase
{
public:
	explicit IniDatabase(IniDomain domain = INI_DOMAIN_FX);

	/// Reads all files of the domain the game reads. Edits not saved are lost.
	void load();
	IniDomain getDomain() const { return m_domain; }
	/// The last block of a type and name (name is ignored for the types without one).
	Int findTypedBlock(const char *type, const AsciiString &name) const;
	/// INI_DOMAIN_RENDER: a new PostProcessPass, PostProcessChain or Material, as createBlock.
	Int createTypedBlock(const char *type, const AsciiString &name, Int copyFrom, AsciiString &error);
	Bool isLoaded() const { return m_loaded; }

	Int getBlockCount() const { return (Int)m_blocks.size(); }
	IniBlock *getBlock(Int index) { return (index >= 0 && index < (Int)m_blocks.size()) ? &m_blocks[index] : nullptr; }
	Int findBlock(PreviewKind kind, const AsciiString &name) const;
	const IniSourceFile *getFile(Int index) const { return (index >= 0 && index < (Int)m_files.size()) ? &m_files[index] : nullptr; }
	Int getFileCount() const { return (Int)m_files.size(); }

	/// Replaces the text of a block and parses it into the game. On a parse error the game keeps the
	/// old definition, the error is returned, and the text is kept only when keepOnError is set.
	Bool setText(Int block, const std::string &text, AsciiString &error, Bool keepOnError = FALSE, Bool recordUndo = TRUE);
	Bool setField(Int block, Int nugget, const char *key, const std::string &value, AsciiString &error);
	Bool removeField(Int block, Int nugget, const char *key, AsciiString &error);

	Bool undo(Int block, AsciiString &error);
	Bool redo(Int block, AsciiString &error);
	/// Back to the text on disk.
	Bool revert(Int block, AsciiString &error);
	/// Parses the changed blocks into the game again (after the game read the files from disk).
	void reapplyChanged();

	/// A new block (a copy of another one, or a minimal one) that is added to the end of the file
	/// of the source block, or of the main file of its kind, on save. Returns its index or -1.
	Int createBlock(PreviewKind kind, const AsciiString &name, Int copyFrom, AsciiString &error);

	/// Writes the changed blocks of one block's file (block >= 0) or of all files (block < 0) to disk.
	/// The first save of a file in a session copies it to <file>.fxeditor.bak.
	Bool save(Int block, AsciiString &error, std::vector<AsciiString> *written = nullptr);
	Int getDirtyCount() const;

	/// Full path on disk the file is written to.
	AsciiString getDiskPath(Int file) const;

private:
	void scanFile(const AsciiString &path);
	void addFilesOf(const char *fileDirName);
	Bool parseIntoGame(const IniBlock &block, const std::string &text, AsciiString &error);
	Bool commitText(Int block, const std::string &text, AsciiString &error, Bool keepOnError, Bool recordUndo);

	Bool isBlockType(const std::string &word) const;

	IniDomain m_domain;
	std::vector<IniSourceFile> m_files;
	std::vector<IniBlock> m_blocks;
	Bool m_loaded;
};
