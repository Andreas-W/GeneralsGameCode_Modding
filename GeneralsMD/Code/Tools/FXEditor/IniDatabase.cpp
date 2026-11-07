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

#include "Precompiled/PreRTS.h"

#include "IniDatabase.h"

#include "Common/File.h"
#include "Common/FileSystem.h"
#include "Common/GlobalData.h"
#include "Common/INI.h"
#include "Common/INIException.h"
#include "Common/LocalFileSystem.h"
#include "GameClient/FXList.h"
#include "GameClient/ParticleSys.h"
#include "GameClient/PostProcess.h"
#include "W3DDevice/GameClient/W3DMaterials.h"

#include <algorithm>
#include <stdio.h>

namespace
{

const size_t MAX_UNDO = 200;

Bool isSpace(char c)
{
	return c == ' ' || c == '\t' || c == '\r';
}

std::string trim(const std::string &s)
{
	size_t a = 0;
	size_t b = s.size();
	while (a < b && isSpace(s[a]))
		++a;
	while (b > a && isSpace(s[b - 1]))
		--b;
	return s.substr(a, b - a);
}

/// Where a comment starts in a line (";" or "//"), or npos.
size_t commentStart(const std::string &line)
{
	for (size_t i = 0; i < line.size(); ++i)
	{
		if (line[i] == ';')
			return i;
		if (line[i] == '/' && i + 1 < line.size() && line[i + 1] == '/')
			return i;
	}
	return std::string::npos;
}

std::string toLF(const std::string &text)
{
	std::string out;
	out.reserve(text.size());
	for (size_t i = 0; i < text.size(); ++i)
	{
		if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n')
			continue;
		out += text[i];
	}
	return out;
}

std::string toCRLF(const std::string &text)
{
	std::string out;
	out.reserve(text.size() + text.size() / 20);
	for (size_t i = 0; i < text.size(); ++i)
	{
		if (text[i] == '\n')
			out += '\r';
		out += text[i];
	}
	return out;
}

Bool sameNoCase(const std::string &a, const char *b)
{
	return _stricmp(a.c_str(), b) == 0;
}

/// One line split into indent, key/value or header word, and comment.
IniLine splitLine(const std::string &text, size_t start, size_t end)
{
	IniLine line;
	line.start = start;
	line.end = end;
	line.depth = 0;
	line.nugget = -1;
	line.type = IniLine::BLANK;

	std::string raw = text.substr(start, end - start);
	while (!raw.empty() && (raw[raw.size() - 1] == '\n' || raw[raw.size() - 1] == '\r'))
		raw.erase(raw.size() - 1);

	size_t i = 0;
	while (i < raw.size() && isSpace(raw[i]))
		++i;
	line.indent = raw.substr(0, i);

	const size_t c = commentStart(raw);
	std::string body = raw.substr(0, c == std::string::npos ? raw.size() : c);
	if (c != std::string::npos)
		line.comment = raw.substr(c);
	body = trim(body);
	if (body.empty())
	{
		line.type = line.comment.empty() ? IniLine::BLANK : IniLine::OTHER;
		return line;
	}

	const size_t eq = body.find('=');
	if (eq != std::string::npos)
	{
		line.type = IniLine::FIELD;
		line.key = trim(body.substr(0, eq));
		line.value = trim(body.substr(eq + 1));
		return line;
	}

	size_t sp = 0;
	while (sp < body.size() && !isSpace(body[sp]))
		++sp;
	line.key = body.substr(0, sp);
	line.value = trim(body.substr(sp));
	line.type = sameNoCase(line.key, "End") ? IniLine::END : IniLine::HEADER;
	return line;
}

/// Index of the End line that closes the scope (block: nugget = -1, or a nugget).
Int scopeEnd(const std::vector<IniLine> &lines, Int nugget)
{
	for (size_t i = 0; i < lines.size(); ++i)
	{
		const IniLine &l = lines[i];
		if (l.type != IniLine::END)
			continue;
		if (nugget < 0 && l.depth == 0)
			return (Int)i;
		if (nugget >= 0 && l.depth == 1 && l.nugget == nugget)
			return (Int)i;
	}
	return -1;
}

/// Index of the header line of a nugget.
Int nuggetHeader(const std::vector<IniLine> &lines, Int nugget)
{
	for (size_t i = 0; i < lines.size(); ++i)
	{
		if (lines[i].type == IniLine::HEADER && lines[i].depth == 1 && lines[i].nugget == nugget)
			return (Int)i;
	}
	return -1;
}

Int findFieldLine(const std::vector<IniLine> &lines, Int nugget, const char *key)
{
	const Int depth = nugget < 0 ? 1 : 2;
	for (size_t i = 0; i < lines.size(); ++i)
	{
		const IniLine &l = lines[i];
		if (l.type == IniLine::FIELD && l.depth == depth && l.nugget == nugget && sameNoCase(l.key, key))
			return (Int)i;
	}
	return -1;
}

Bool readWholeFile(const AsciiString &path, std::string &text)
{
	File *file = TheFileSystem->openFile(path.str(), File::READ | File::BINARY);
	if (file == nullptr)
		return FALSE;
	const Int size = file->size();
	text.assign(size > 0 ? size : 0, '\0');
	if (size > 0)
		file->read(&text[0], size);
	file->close();
	return TRUE;
}

Bool ensureDirectoryOf(const std::string &path)
{
	for (size_t i = 0; i < path.size(); ++i)
	{
		if ((path[i] == '\\' || path[i] == '/') && i > 2)
		{
			const std::string dir = path.substr(0, i);
			CreateDirectoryA(dir.c_str(), nullptr);
		}
	}
	return TRUE;
}

} // namespace

//=================================================================================================
// IniText
//=================================================================================================

void IniText::parseLines(const std::string &text, std::vector<IniLine> &lines)
{
	lines.clear();
	Int depth = 0;
	Int nugget = -1;
	Int nuggetCount = 0;
	size_t pos = 0;
	while (pos < text.size())
	{
		size_t end = text.find('\n', pos);
		end = (end == std::string::npos) ? text.size() : end + 1;
		IniLine line = splitLine(text, pos, end);

		switch (line.type)
		{
			case IniLine::HEADER:
				line.depth = depth;
				if (depth == 1)
				{
					nugget = nuggetCount++;
				}
				line.nugget = depth >= 1 ? nugget : -1;
				++depth;
				break;
			case IniLine::END:
				if (depth > 0)
					--depth;
				line.depth = depth;
				line.nugget = depth >= 1 ? nugget : -1;
				if (depth == 1)
				{
					// closes the nugget; lines after it are back on block level
					lines.push_back(line);
					nugget = -1;
					pos = end;
					continue;
				}
				break;
			default:
				line.depth = depth;
				line.nugget = depth >= 2 ? nugget : -1;
				break;
		}
		lines.push_back(line);
		pos = end;
	}
}

void IniText::getNuggets(const std::string &text, std::vector<std::string> &types)
{
	types.clear();
	std::vector<IniLine> lines;
	parseLines(text, lines);
	for (size_t i = 0; i < lines.size(); ++i)
	{
		if (lines[i].type == IniLine::HEADER && lines[i].depth == 1)
			types.push_back(lines[i].key);
	}
}

Bool IniText::getField(const std::string &text, Int nugget, const char *key, std::string &value)
{
	std::vector<IniLine> lines;
	parseLines(text, lines);
	const Int i = findFieldLine(lines, nugget, key);
	if (i < 0)
		return FALSE;
	value = lines[i].value;
	return TRUE;
}

namespace
{

/// The text with the value of one field line replaced; the key, its alignment and the comment stay.
std::string rewriteValue(const std::string &text, const IniLine &l, const std::string &value)
{
	std::string raw = text.substr(l.start, l.end - l.start);
	std::string lineBreak;
	while (!raw.empty() && (raw[raw.size() - 1] == '\n' || raw[raw.size() - 1] == '\r'))
	{
		lineBreak.insert(lineBreak.begin(), raw[raw.size() - 1]);
		raw.erase(raw.size() - 1);
	}
	size_t eq = raw.find('=');
	size_t valueStart = eq + 1;
	while (valueStart < raw.size() && isSpace(raw[valueStart]))
		++valueStart;
	const size_t c = commentStart(raw);
	std::string tail;
	if (c != std::string::npos)
	{
		// the spacing in front of the comment stays
		size_t valueEnd = c;
		while (valueEnd > valueStart && isSpace(raw[valueEnd - 1]))
			--valueEnd;
		tail = raw.substr(valueEnd);
	}
	std::string prefix = raw.substr(0, valueStart);
	if (valueStart == eq + 1)
		prefix += " ";
	return text.substr(0, l.start) + prefix + value + tail + lineBreak + text.substr(l.end);
}

std::string firstWord(const std::string &value, std::string *rest = nullptr)
{
	size_t sp = 0;
	while (sp < value.size() && !isSpace(value[sp]))
		++sp;
	if (rest)
		*rest = trim(value.substr(sp));
	return value.substr(0, sp);
}

/// Line of a repeating field (Param = <name> ...) of the block, or -1.
Int findKeyedLine(const std::vector<IniLine> &lines, const char *key, const char *name)
{
	for (size_t i = 0; i < lines.size(); ++i)
	{
		const IniLine &l = lines[i];
		if (l.type == IniLine::FIELD && l.depth == 1 && l.nugget < 0 && sameNoCase(l.key, key) && sameNoCase(firstWord(l.value), name))
			return (Int)i;
	}
	return -1;
}

} // namespace

std::string IniText::setField(const std::string &text, Int nugget, const char *key, const std::string &value)
{
	std::vector<IniLine> lines;
	parseLines(text, lines);
	const Int i = findFieldLine(lines, nugget, key);
	if (i >= 0)
		return rewriteValue(text, lines[i], value);

	// A new line at the end of the scope, indented like the other fields there.
	const Int endLine = scopeEnd(lines, nugget);
	if (endLine < 0)
		return text;
	std::string indent = nugget < 0 ? "  " : "    ";
	for (Int j = endLine - 1; j >= 0; --j)
	{
		if (lines[j].type == IniLine::FIELD && lines[j].nugget == nugget && lines[j].depth == (nugget < 0 ? 1 : 2))
		{
			indent = lines[j].indent;
			break;
		}
		if (lines[j].type == IniLine::HEADER && lines[j].nugget == nugget)
		{
			indent = lines[j].indent + "  ";
			break;
		}
	}
	const size_t at = lines[endLine].start;
	return text.substr(0, at) + indent + key + " = " + value + "\n" + text.substr(at);
}

void IniText::getKeyedFields(const std::string &text, const char *key, std::vector<std::string> &names, std::vector<std::string> &values)
{
	names.clear();
	values.clear();
	std::vector<IniLine> lines;
	parseLines(text, lines);
	for (size_t i = 0; i < lines.size(); ++i)
	{
		const IniLine &l = lines[i];
		if (l.type == IniLine::FIELD && l.depth == 1 && l.nugget < 0 && sameNoCase(l.key, key))
		{
			std::string rest;
			names.push_back(firstWord(l.value, &rest));
			values.push_back(rest);
		}
	}
}

std::string IniText::setKeyedField(const std::string &text, const char *key, const char *name, const std::string &value)
{
	std::vector<IniLine> lines;
	parseLines(text, lines);
	const Int i = findKeyedLine(lines, key, name);
	if (i >= 0)
		return rewriteValue(text, lines[i], std::string(name) + " " + value);

	// After the last line of the same key, else at the end of the block.
	Int after = -1;
	for (size_t j = 0; j < lines.size(); ++j)
	{
		if (lines[j].type == IniLine::FIELD && lines[j].depth == 1 && lines[j].nugget < 0 && sameNoCase(lines[j].key, key))
			after = (Int)j;
	}
	if (after < 0)
		return appendField(text, key, std::string(name) + " " + value);
	// A field line is never the last line of a block (End follows), so it ends with a line break.
	const IniLine &l = lines[after];
	return text.substr(0, l.end) + l.indent + l.key + " = " + name + " " + value + "\n" + text.substr(l.end);
}

std::string IniText::removeKeyedField(const std::string &text, const char *key, const char *name)
{
	std::vector<IniLine> lines;
	parseLines(text, lines);
	const Int i = findKeyedLine(lines, key, name);
	if (i < 0)
		return text;
	return text.substr(0, lines[i].start) + text.substr(lines[i].end);
}

std::string IniText::appendField(const std::string &text, const char *key, const std::string &value)
{
	std::vector<IniLine> lines;
	parseLines(text, lines);
	const Int endLine = scopeEnd(lines, -1);
	if (endLine < 0)
		return text;
	std::string indent = "  ";
	for (Int j = endLine - 1; j >= 0; --j)
	{
		if (lines[j].type == IniLine::FIELD && lines[j].depth == 1)
		{
			indent = lines[j].indent;
			break;
		}
	}
	const size_t at = lines[endLine].start;
	return text.substr(0, at) + indent + key + " = " + value + "\n" + text.substr(at);
}

std::string IniText::removeField(const std::string &text, Int nugget, const char *key)
{
	std::vector<IniLine> lines;
	parseLines(text, lines);
	const Int i = findFieldLine(lines, nugget, key);
	if (i < 0)
		return text;
	return text.substr(0, lines[i].start) + text.substr(lines[i].end);
}

std::string IniText::addNugget(const std::string &text, const char *type, Int insertAt)
{
	std::vector<IniLine> lines;
	parseLines(text, lines);
	Int at = -1;
	if (insertAt >= 0)
		at = nuggetHeader(lines, insertAt);
	if (at < 0)
		at = scopeEnd(lines, -1);
	if (at < 0)
		return text;
	const std::string nuggetText = std::string("  ") + type + "\n  End\n";
	return text.substr(0, lines[at].start) + nuggetText + text.substr(lines[at].start);
}

std::string IniText::removeNugget(const std::string &text, Int nugget)
{
	std::vector<IniLine> lines;
	parseLines(text, lines);
	const Int a = nuggetHeader(lines, nugget);
	const Int b = scopeEnd(lines, nugget);
	if (a < 0 || b < 0)
		return text;
	return text.substr(0, lines[a].start) + text.substr(lines[b].end);
}

std::string IniText::moveNugget(const std::string &text, Int nugget, Int offset)
{
	std::vector<std::string> types;
	getNuggets(text, types);
	const Int target = nugget + offset;
	if (nugget < 0 || nugget >= (Int)types.size() || target < 0 || target >= (Int)types.size() || offset == 0)
		return text;

	std::vector<IniLine> lines;
	parseLines(text, lines);
	// Swap the nugget with its neighbour, one step at a time.
	const Int first = offset < 0 ? target : nugget;
	const Int second = first + 1;
	const Int a0 = nuggetHeader(lines, first);
	const Int a1 = scopeEnd(lines, first);
	const Int b0 = nuggetHeader(lines, second);
	const Int b1 = scopeEnd(lines, second);
	if (a0 < 0 || a1 < 0 || b0 < 0 || b1 < 0)
		return text;
	const std::string partA = text.substr(lines[a0].start, lines[a1].end - lines[a0].start);
	const std::string between = text.substr(lines[a1].end, lines[b0].start - lines[a1].end);
	const std::string partB = text.substr(lines[b0].start, lines[b1].end - lines[b0].start);
	std::string result = text.substr(0, lines[a0].start) + partB + between + partA + text.substr(lines[b1].end);
	if (offset > 1 || offset < -1)
		return moveNugget(result, offset < 0 ? nugget - 1 : nugget + 1, offset < 0 ? offset + 1 : offset - 1);
	return result;
}

std::string IniText::duplicateNugget(const std::string &text, Int nugget)
{
	std::vector<IniLine> lines;
	parseLines(text, lines);
	const Int a = nuggetHeader(lines, nugget);
	const Int b = scopeEnd(lines, nugget);
	if (a < 0 || b < 0)
		return text;
	std::string copy = text.substr(lines[a].start, lines[b].end - lines[a].start);
	if (!copy.empty() && copy[copy.size() - 1] != '\n')
		copy += "\n";
	return text.substr(0, lines[b].end) + copy + text.substr(lines[b].end);
}

std::string IniText::blockName(const std::string &text)
{
	std::vector<IniLine> lines;
	parseLines(text, lines);
	for (size_t i = 0; i < lines.size(); ++i)
	{
		if (lines[i].type == IniLine::HEADER && lines[i].depth == 0)
		{
			std::string name = lines[i].value;
			const size_t sp = name.find_first_of(" \t");
			return sp == std::string::npos ? name : name.substr(0, sp);
		}
	}
	return std::string();
}

std::string IniText::renameBlock(const std::string &text, const char *newName)
{
	std::vector<IniLine> lines;
	parseLines(text, lines);
	for (size_t i = 0; i < lines.size(); ++i)
	{
		const IniLine &l = lines[i];
		if (l.type == IniLine::HEADER && l.depth == 0)
		{
			std::string newLine = l.indent + l.key + " " + newName;
			if (!l.comment.empty())
				newLine += " " + l.comment;
			newLine += "\n";
			return text.substr(0, l.start) + newLine + text.substr(l.end);
		}
	}
	return text;
}

//=================================================================================================
// IniDatabase
//=================================================================================================

IniDatabase::IniDatabase(IniDomain domain) :
	m_domain(domain),
	m_loaded(FALSE)
{
}

//-------------------------------------------------------------------------------------------------
void IniDatabase::load()
{
	m_files.clear();
	m_blocks.clear();
	if (m_domain == INI_DOMAIN_RENDER)
	{
		// As W3DPostProcess::loadSettings and W3DMaterials::loadSettings read them.
		addFilesOf("Data\\INI\\Default\\PostProcess");
		addFilesOf("Data\\INI\\PostProcess");
		addFilesOf("Data\\INI\\Default\\Material");
		addFilesOf("Data\\INI\\Material");
	}
	else
	{
		// The same files the game reads, in the same order (ParticleSystemManager::init, GameEngine::init).
		addFilesOf("Data\\INI\\ParticleSystem");
		addFilesOf("Data\\INI\\Default\\FXList");
		addFilesOf("Data\\INI\\FXList");
	}
	m_loaded = TRUE;
}

//-------------------------------------------------------------------------------------------------
Bool IniDatabase::isBlockType(const std::string &word) const
{
	if (m_domain == INI_DOMAIN_RENDER)
	{
		return sameNoCase(word, "PostProcessPass") || sameNoCase(word, "PostProcess") || sameNoCase(word, "PostProcessChain")
			|| sameNoCase(word, "ShadowMap") || sameNoCase(word, "Material");
	}
	return sameNoCase(word, "ParticleSystem") || sameNoCase(word, "FXList");
}

//-------------------------------------------------------------------------------------------------
Int IniDatabase::findTypedBlock(const char *type, const AsciiString &name) const
{
	const Bool unnamed = _stricmp(type, "PostProcess") == 0 || _stricmp(type, "ShadowMap") == 0;
	for (Int i = (Int)m_blocks.size() - 1; i >= 0; --i)
	{
		if (sameNoCase(m_blocks[i].type, type) && (unnamed || m_blocks[i].name.compareNoCase(name) == 0))
			return i;
	}
	return -1;
}

//-------------------------------------------------------------------------------------------------
void IniDatabase::addFilesOf(const char *fileDirName)
{
	AsciiString file = fileDirName;
	file.concat(".ini");
	if (TheFileSystem->doesFileExist(file.str()))
		scanFile(file);

	AsciiString dir = fileDirName;
	dir.concat('\\');
	FilenameList list;
	TheFileSystem->getFileListInDirectory(dir, "*.ini", list, TRUE);
	for (FilenameList::const_iterator it = list.begin(); it != list.end(); ++it)
		scanFile(*it);
}

//-------------------------------------------------------------------------------------------------
/** Finds the ParticleSystem and FXList blocks of a file. */
//-------------------------------------------------------------------------------------------------
void IniDatabase::scanFile(const AsciiString &path)
{
	for (size_t i = 0; i < m_files.size(); ++i)
	{
		if (m_files[i].path.compareNoCase(path) == 0)
			return;
	}

	IniSourceFile source;
	source.path = path;
	if (!readWholeFile(path, source.text))
		return;
	source.loose = TheLocalFileSystem->doesFileExist(path.str());
	source.crlf = source.text.find("\r\n") != std::string::npos;
	source.backedUp = FALSE;
	const Int fileIndex = (Int)m_files.size();
	m_files.push_back(source);
	const std::string &text = m_files.back().text;

	Int depth = 0;
	size_t blockStart = 0;
	Bool inBlock = FALSE;
	PreviewKind kind = PREVIEW_PARTICLE_SYSTEM;
	std::string type;
	std::string name;
	size_t pos = 0;
	while (pos < text.size())
	{
		size_t end = text.find('\n', pos);
		end = (end == std::string::npos) ? text.size() : end + 1;
		const IniLine line = splitLine(text, pos, end);

		if (line.type == IniLine::HEADER)
		{
			if (depth == 0)
			{
				inBlock = FALSE;
				if (isBlockType(line.key))
				{
					inBlock = TRUE;
					kind = sameNoCase(line.key, "FXList") ? PREVIEW_FXLIST : PREVIEW_PARTICLE_SYSTEM;
					type = line.key;
					blockStart = pos;
					name = line.value;
					const size_t sp = name.find_first_of(" \t");
					if (sp != std::string::npos)
						name.erase(sp);
					if (name.empty())
						name = type;
				}
			}
			++depth;
		}
		else if (line.type == IniLine::END && depth > 0)
		{
			--depth;
			if (depth == 0 && inBlock)
			{
				IniBlock block;
				block.kind = kind;
				block.type = type;
				block.name = name.c_str();
				block.file = fileIndex;
				block.offset = blockStart;
				block.length = end - blockStart;
				block.savedText = toLF(text.substr(blockStart, end - blockStart));
				block.text = block.savedText;
				m_blocks.push_back(block);
				inBlock = FALSE;
			}
		}
		pos = end;
	}
}

//-------------------------------------------------------------------------------------------------
Int IniDatabase::findBlock(PreviewKind kind, const AsciiString &name) const
{
	// The game uses the last definition of a name.
	for (Int i = (Int)m_blocks.size() - 1; i >= 0; --i)
	{
		if (m_blocks[i].kind == kind && m_blocks[i].name.compareNoCase(name) == 0)
			return i;
	}
	return -1;
}

//-------------------------------------------------------------------------------------------------
/** Runs the text through the game's INI parser, into the live template. */
//-------------------------------------------------------------------------------------------------
Bool IniDatabase::parseIntoGame(const IniBlock &block, const std::string &text, AsciiString &error)
{
	if (text.find_first_not_of(" \t\r\n") == std::string::npos)
	{
		error = "the block is empty";
		return FALSE;
	}
	const Bool render = m_domain == INI_DOMAIN_RENDER;
	std::string name = IniText::blockName(text);
	if (render)
	{
		// The header must stay of the block's type; PostProcess and ShadowMap have no name.
		std::vector<IniLine> lines;
		IniText::parseLines(text, lines);
		Bool headerOk = FALSE;
		for (size_t i = 0; i < lines.size(); ++i)
		{
			if (lines[i].type == IniLine::HEADER && lines[i].depth == 0)
			{
				headerOk = sameNoCase(lines[i].key, block.type.c_str());
				break;
			}
		}
		if (!headerOk)
		{
			error.format("the first line must be the block header '%s ...'", block.type.c_str());
			return FALSE;
		}
		if (name.empty())
			name = block.type;
	}
	else if (name.empty())
	{
		error = "the first line must be the block header, e.g. 'ParticleSystem Name'";
		return FALSE;
	}

	AsciiString tempPath = TheGlobalData->getPath_UserData();
	tempPath.concat("FXEditorReload.ini");
	FILE *fp = fopen(tempPath.str(), "wb");
	if (fp == nullptr)
	{
		error.format("cannot write '%s'", tempPath.str());
		return FALSE;
	}
	fwrite(text.data(), 1, text.size(), fp);
	fwrite("\n", 1, 1, fp);
	fclose(fp);

	if (render)
	{
		// Passes, chains and materials are replaced as a whole when they are parsed again; these two
		// blocks are parsed into what is there, so fields the block no longer has go back first.
		if (sameNoCase(block.type, "ShadowMap") && TheShadowMapSettings)
			*TheShadowMapSettings = ShadowMapSettings();
		if (sameNoCase(block.type, "PostProcess") && ThePostProcessSettings)
		{
			ThePostProcessSettings->m_chain.clear();
			ThePostProcessSettings->m_hdrScene = FALSE;
		}
	}
	else if (block.kind == PREVIEW_PARTICLE_SYSTEM)
	{
		// Fields the block no longer has go back to their defaults.
		if (ParticleSystemTemplate *tmpl = TheParticleSystemManager->findTemplate(AsciiString(name.c_str())))
			tmpl->resetToDefaults();
	}

	Bool ok = TRUE;
	try
	{
		INI ini;
		ini.load(tempPath, INI_LOAD_OVERWRITE, nullptr);
	}
	catch (INIException &e)
	{
		error = e.mFailureMessage ? e.mFailureMessage : "the block could not be parsed";
		ok = FALSE;
	}
	catch (...)
	{
		error = "the block could not be parsed (unknown field or value)";
		ok = FALSE;
	}
	DeleteFileA(tempPath.str());

	if (render)
	{
		// The renderer builds the chain again when the generation changes, and the materials on next
		// use. The changed settings are the mod's now, so ending the game keeps them.
		ThePostProcessControl.settingsReloaded();
		if (TheW3DMaterials && sameNoCase(block.type, "Material"))
			TheW3DMaterials->settingsChanged();
	}

	// The message names the temporary file; name the block instead.
	if (!ok)
	{
		AsciiString msg;
		msg.format("%s %s: %s", block.type.c_str(), name.c_str(), error.str());
		error = msg;
	}
	return ok;
}

//-------------------------------------------------------------------------------------------------
Bool IniDatabase::commitText(Int index, const std::string &text, AsciiString &error, Bool keepOnError, Bool recordUndo)
{
	IniBlock *block = getBlock(index);
	if (block == nullptr)
	{
		error = "no such block";
		return FALSE;
	}
	if (text == block->text && block->error.empty())
		return TRUE;

	const std::string oldText = block->text;
	if (!parseIntoGame(*block, text, error))
	{
		// The game keeps working with the last text that parsed.
		AsciiString ignored;
		if (block->error.empty())
			parseIntoGame(*block, oldText, ignored);
		if (keepOnError)
		{
			if (recordUndo && text != oldText)
			{
				block->undo.push_back(oldText);
				block->redo.clear();
			}
			block->text = text;
			block->error = error.str();
		}
		return FALSE;
	}

	if (recordUndo && text != oldText)
	{
		block->undo.push_back(oldText);
		if (block->undo.size() > MAX_UNDO)
			block->undo.erase(block->undo.begin());
		block->redo.clear();
	}
	block->text = text;
	block->error.clear();
	const std::string newName = IniText::blockName(text);
	if (!newName.empty())
		block->name = newName.c_str();
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
Bool IniDatabase::setText(Int block, const std::string &text, AsciiString &error, Bool keepOnError, Bool recordUndo)
{
	return commitText(block, toLF(text), error, keepOnError, recordUndo);
}

//-------------------------------------------------------------------------------------------------
Bool IniDatabase::setField(Int index, Int nugget, const char *key, const std::string &value, AsciiString &error)
{
	IniBlock *block = getBlock(index);
	if (block == nullptr)
	{
		error = "no such block";
		return FALSE;
	}
	return commitText(index, IniText::setField(block->text, nugget, key, value), error, FALSE, TRUE);
}

//-------------------------------------------------------------------------------------------------
Bool IniDatabase::removeField(Int index, Int nugget, const char *key, AsciiString &error)
{
	IniBlock *block = getBlock(index);
	if (block == nullptr)
	{
		error = "no such block";
		return FALSE;
	}
	return commitText(index, IniText::removeField(block->text, nugget, key), error, FALSE, TRUE);
}

//-------------------------------------------------------------------------------------------------
Bool IniDatabase::undo(Int index, AsciiString &error)
{
	IniBlock *block = getBlock(index);
	if (block == nullptr || block->undo.empty())
	{
		error = "nothing to undo";
		return FALSE;
	}
	const std::string current = block->text;
	const std::string previous = block->undo.back();
	block->undo.pop_back();
	block->error.clear();
	if (!commitText(index, previous, error, TRUE, FALSE))
	{
		block->redo.push_back(current);
		return FALSE;
	}
	block->redo.push_back(current);
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
Bool IniDatabase::redo(Int index, AsciiString &error)
{
	IniBlock *block = getBlock(index);
	if (block == nullptr || block->redo.empty())
	{
		error = "nothing to redo";
		return FALSE;
	}
	const std::string current = block->text;
	const std::string next = block->redo.back();
	block->redo.pop_back();
	block->error.clear();
	const Bool ok = commitText(index, next, error, TRUE, FALSE);
	block->undo.push_back(current);
	return ok;
}

//-------------------------------------------------------------------------------------------------
Bool IniDatabase::revert(Int index, AsciiString &error)
{
	IniBlock *block = getBlock(index);
	if (block == nullptr)
	{
		error = "no such block";
		return FALSE;
	}
	block->error.clear();
	return commitText(index, block->savedText, error, TRUE, TRUE);
}

//-------------------------------------------------------------------------------------------------
Int IniDatabase::createBlock(PreviewKind kind, const AsciiString &name, Int copyFrom, AsciiString &error)
{
	if (name.isEmpty() || name.find(' ') || name.find('\t'))
	{
		error = "the name must not be empty or contain spaces";
		return -1;
	}
	if (findBlock(kind, name) >= 0
		|| (kind == PREVIEW_PARTICLE_SYSTEM && TheParticleSystemManager->findTemplate(name))
		|| (kind == PREVIEW_FXLIST && TheFXListStore->findFXList(name.str())))
	{
		error.format("'%s' exists already", name.str());
		return -1;
	}

	std::string text;
	const IniBlock *source = getBlock(copyFrom);
	if (source && source->kind == kind)
	{
		text = IniText::renameBlock(source->text, name.str());
	}
	else if (kind == PREVIEW_PARTICLE_SYSTEM)
	{
		text = std::string("ParticleSystem ") + name.str() + "\n"
			"  Priority = WEAPON_EXPLOSION\n"
			"  IsOneShot = No\n"
			"  Shader = ADDITIVE\n"
			"  Type = PARTICLE\n"
			"  ParticleName = EXSmokNew1.tga\n"
			"  Lifetime = 30.00 30.00\n"
			"  SystemLifetime = 0\n"
			"  Size = 10.00 10.00\n"
			"  Alpha1 = 1.00 1.00 0\n"
			"  Color1 = R:255 G:255 B:255 0\n"
			"  BurstDelay = 5.00 5.00\n"
			"  BurstCount = 1.00 1.00\n"
			"  VelocityType = SPHERICAL\n"
			"  VelSpherical = 1.00 1.00\n"
			"  VolumeType = POINT\n"
			"End\n";
	}
	else
	{
		text = std::string("FXList ") + name.str() + "\nEnd\n";
	}

	IniBlock block;
	block.kind = kind;
	block.type = kind == PREVIEW_FXLIST ? "FXList" : "ParticleSystem";
	block.name = name;
	block.file = -1;
	block.offset = 0;
	block.length = 0;
	block.text = text;
	// The file it goes to on save: that of the copied block, or the main file of its kind.
	if (source)
	{
		block.offset = source->file;
	}
	else
	{
		block.offset = (size_t)-1;
		for (size_t i = 0; i < m_files.size(); ++i)
		{
			const char *main = kind == PREVIEW_FXLIST ? "Data\\INI\\FXList.ini" : "Data\\INI\\ParticleSystem.ini";
			if (m_files[i].path.compareNoCase(main) == 0)
				block.offset = i;
		}
	}
	if (!parseIntoGame(block, text, error))
		return -1;
	m_blocks.push_back(block);
	return (Int)m_blocks.size() - 1;
}

//-------------------------------------------------------------------------------------------------
void IniDatabase::reapplyChanged()
{
	for (size_t i = 0; i < m_blocks.size(); ++i)
	{
		const IniBlock &b = m_blocks[i];
		if (b.isDirty() && b.error.empty())
		{
			AsciiString ignored;
			parseIntoGame(b, b.text, ignored);
		}
	}
}

//-------------------------------------------------------------------------------------------------
Int IniDatabase::createTypedBlock(const char *type, const AsciiString &name, Int copyFrom, AsciiString &error)
{
	const Bool pass = _stricmp(type, "PostProcessPass") == 0;
	const Bool chain = _stricmp(type, "PostProcessChain") == 0;
	const Bool material = _stricmp(type, "Material") == 0;
	if (m_domain != INI_DOMAIN_RENDER || !(pass || chain || material))
	{
		error = "only PostProcessPass, PostProcessChain and Material blocks can be made";
		return -1;
	}
	if (name.isEmpty() || name.find(' ') || name.find('\t'))
	{
		error = "the name must not be empty or contain spaces";
		return -1;
	}
	if (findTypedBlock(type, name) >= 0)
	{
		error.format("%s %s exists already", type, name.str());
		return -1;
	}

	std::string text;
	const IniBlock *source = getBlock(copyFrom);
	if (source && sameNoCase(source->type, type))
		text = IniText::renameBlock(source->text, name.str());
	else if (pass)
		text = std::string("PostProcessPass ") + name.str() + "\n  Shader = PP_Copy\n  Inputs = Scene\nEnd\n";
	else if (chain)
		text = std::string("PostProcessChain ") + name.str() + "\n  Passes =\nEnd\n";
	else
		text = std::string("Material ") + name.str() + "\n  Textures =\nEnd\n";

	IniBlock block;
	block.kind = PREVIEW_PARTICLE_SYSTEM;
	block.type = pass ? "PostProcessPass" : (chain ? "PostProcessChain" : "Material");
	block.name = name;
	block.file = -1;
	block.length = 0;
	block.text = text;
	// The file it goes to on save: that of the copied block, else the mod's main file of its kind.
	block.offset = (size_t)-1;
	if (source && source->file >= 0)
	{
		block.offset = source->file;
	}
	else
	{
		const char *main = material ? "Data\\INI\\Material.ini" : "Data\\INI\\PostProcess.ini";
		for (size_t i = 0; i < m_files.size(); ++i)
		{
			if (m_files[i].path.compareNoCase(main) == 0)
				block.offset = i;
		}
	}
	if (!parseIntoGame(block, text, error))
		return -1;
	m_blocks.push_back(block);
	return (Int)m_blocks.size() - 1;
}

//-------------------------------------------------------------------------------------------------
Int IniDatabase::getDirtyCount() const
{
	Int count = 0;
	for (size_t i = 0; i < m_blocks.size(); ++i)
	{
		if (m_blocks[i].isDirty())
			++count;
	}
	return count;
}

//-------------------------------------------------------------------------------------------------
AsciiString IniDatabase::getDiskPath(Int file) const
{
	char dir[MAX_PATH];
	dir[0] = 0;
	GetCurrentDirectoryA(MAX_PATH, dir);
	AsciiString path = dir;
	path.concat('\\');
	if (const IniSourceFile *f = getFile(file))
		path.concat(f->path);
	return path;
}

//-------------------------------------------------------------------------------------------------
Bool IniDatabase::save(Int only, AsciiString &error, std::vector<AsciiString> *written)
{
	// New blocks get the file they belong to (kept in offset until then).
	for (size_t i = 0; i < m_blocks.size(); ++i)
	{
		IniBlock &b = m_blocks[i];
		if (b.file < 0 && (only < 0 || (Int)i == only) && b.offset != (size_t)-1 && b.offset < m_files.size())
		{
			b.file = (Int)b.offset;
			b.offset = m_files[b.file].text.size();
			b.length = 0;
			b.savedText.clear();
		}
	}

	std::vector<Int> files;
	if (only >= 0)
	{
		const IniBlock *b = getBlock(only);
		if (b == nullptr || b->file < 0)
		{
			error = "the block has no file to go to";
			return FALSE;
		}
		files.push_back(b->file);
	}
	else
	{
		for (size_t i = 0; i < m_blocks.size(); ++i)
		{
			if (m_blocks[i].isDirty() && m_blocks[i].file >= 0
				&& std::find(files.begin(), files.end(), m_blocks[i].file) == files.end())
				files.push_back(m_blocks[i].file);
		}
	}

	for (size_t f = 0; f < files.size(); ++f)
	{
		const Int fileIndex = files[f];
		IniSourceFile &source = m_files[fileIndex];

		// Text the game could not parse is not written.
		for (size_t i = 0; i < m_blocks.size(); ++i)
		{
			if (m_blocks[i].file == fileIndex && !m_blocks[i].error.empty())
			{
				error.format("%s has an error; fix or revert it before saving: %s", m_blocks[i].name.str(), m_blocks[i].error.c_str());
				return FALSE;
			}
		}

		// The blocks of the file in file order; the text between them stays as it is.
		std::vector<Int> order;
		for (size_t i = 0; i < m_blocks.size(); ++i)
		{
			if (m_blocks[i].file == fileIndex)
				order.push_back((Int)i);
		}
		struct ByOffset
		{
			const std::vector<IniBlock> *blocks;
			bool operator()(Int a, Int b) const { return (*blocks)[a].offset < (*blocks)[b].offset; }
		};
		ByOffset cmp;
		cmp.blocks = &m_blocks;
		std::stable_sort(order.begin(), order.end(), cmp);

		std::string out;
		size_t pos = 0;
		std::vector<size_t> newOffsets(order.size());
		std::vector<size_t> newLengths(order.size());
		for (size_t k = 0; k < order.size(); ++k)
		{
			IniBlock &b = m_blocks[order[k]];
			if (b.offset > pos)
				out += source.text.substr(pos, b.offset - pos);
			if (b.length == 0 && b.offset >= source.text.size())
			{
				// a new block at the end of the file
				if (!out.empty() && out[out.size() - 1] != '\n')
					out += source.crlf ? "\r\n" : "\n";
				out += source.crlf ? "\r\n" : "\n";
			}
			std::string blockText = b.text;
			if (!blockText.empty() && blockText[blockText.size() - 1] != '\n')
				blockText += "\n";
			if (source.crlf)
				blockText = toCRLF(blockText);
			newOffsets[k] = out.size();
			out += blockText;
			newLengths[k] = blockText.size();
			pos = b.offset + b.length;
		}
		if (pos < source.text.size())
			out += source.text.substr(pos);

		const AsciiString diskPath = getDiskPath(fileIndex);
		if (source.loose && !source.backedUp)
		{
			AsciiString backup = diskPath;
			backup.concat(".fxeditor.bak");
			if (!CopyFileA(diskPath.str(), backup.str(), FALSE))
			{
				error.format("cannot back up '%s' to '%s'", diskPath.str(), backup.str());
				return FALSE;
			}
			source.backedUp = TRUE;
		}
		ensureDirectoryOf(diskPath.str());
		AsciiString tempPath = diskPath;
		tempPath.concat(".fxeditor.tmp");
		FILE *fp = fopen(tempPath.str(), "wb");
		if (fp == nullptr)
		{
			error.format("cannot write '%s'", tempPath.str());
			return FALSE;
		}
		const size_t wrote = fwrite(out.data(), 1, out.size(), fp);
		fclose(fp);
		if (wrote != out.size() || !MoveFileExA(tempPath.str(), diskPath.str(), MOVEFILE_REPLACE_EXISTING))
		{
			DeleteFileA(tempPath.str());
			error.format("cannot replace '%s'", diskPath.str());
			return FALSE;
		}

		source.text = out;
		source.loose = TRUE;
		for (size_t k = 0; k < order.size(); ++k)
		{
			IniBlock &b = m_blocks[order[k]];
			b.offset = newOffsets[k];
			b.length = newLengths[k];
			b.savedText = toLF(out.substr(b.offset, b.length));
			b.text = b.savedText;
		}
		if (written)
			written->push_back(diskPath);
	}
	return TRUE;
}
