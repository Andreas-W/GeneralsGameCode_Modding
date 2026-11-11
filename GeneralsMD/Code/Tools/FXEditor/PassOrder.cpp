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

// The default order: every pass runs after the passes it reads, and among the passes that could run next
// the one of the earliest stage goes first. The stage comes from the built-in shader: occlusion, exposure,
// bloom on the HDR scene, then the color grade that brings it to the screen's range, then fog, depth of
// field, edge smoothing and the final sharpen and vignette. Blurs and unknown shaders belong to the stage
// of the passes they read.

#include "Precompiled/PreRTS.h"

#include "PassOrder.h"
#include "IniDatabase.h"

#include "GameClient/PostProcess.h"
#include "W3DDevice/GameClient/W3DPostProcess.h"

#include <algorithm>

namespace
{

struct ShaderStage
{
	const char *shader;
	Int rank;
};

const Int RANK_DEBUG = 1000;

const ShaderStage SHADER_STAGES[] =
{
	{ "PP_SSAO", 100 },
	{ "PP_Luminance", 200 },
	{ "PP_AutoExposure", 210 },
	{ "PP_BloomExtract", 300 },
	{ "PP_ColorGrade", 400 },
	{ "PP_Fog", 500 },
	{ "PP_DepthOfField", 600 },
	{ "PP_EdgeAA", 700 },
	{ "PP_SharpenVignette", 800 },
	{ "PP_DepthView", RANK_DEBUG },
	{ "PP_Copy", RANK_DEBUG },
};

/// What auto-connect gives a built-in shader.
enum PassKind
{
	KIND_OTHER,
	KIND_IMAGE,					///< the image
	KIND_IMAGE_DEPTH,			///< the image, Depth
	KIND_COLOR_GRADE,			///< the image, bloom, lookup table, occlusion, auto exposure
	KIND_SSAO,					///< Depth: starts an occlusion branch
	KIND_BLOOM_EXTRACT,			///< Scene: starts a bloom branch
	KIND_LUMINANCE,				///< Scene: starts an auto exposure branch
	KIND_AUTO_EXPOSURE,			///< the luminance, Previous
	KIND_BLUR,					///< the pass before it, in the image chain or a branch
	KIND_DEPTH_BLUR,			///< the same, Depth
	KIND_DEBUG,
};

struct ShaderKind
{
	const char *shader;
	PassKind kind;
};

const ShaderKind SHADER_KINDS[] =
{
	{ "PP_ColorGrade", KIND_COLOR_GRADE },
	{ "PP_Fog", KIND_IMAGE_DEPTH },
	{ "PP_DepthOfField", KIND_IMAGE_DEPTH },
	{ "PP_EdgeAA", KIND_IMAGE },
	{ "PP_SharpenVignette", KIND_IMAGE },
	{ "PP_SSAO", KIND_SSAO },
	{ "PP_BloomExtract", KIND_BLOOM_EXTRACT },
	{ "PP_Luminance", KIND_LUMINANCE },
	{ "PP_AutoExposure", KIND_AUTO_EXPOSURE },
	{ "PP_Blur", KIND_BLUR },
	{ "PP_DepthBlur", KIND_DEPTH_BLUR },
	{ "PP_DepthView", KIND_DEBUG },
	{ "PP_Copy", KIND_DEBUG },
};

PassKind shaderKind(const AsciiString &shader)
{
	for (size_t i = 0; i < ARRAY_SIZE(SHADER_KINDS); ++i)
		if (shader.compareNoCase(SHADER_KINDS[i].shader) == 0)
			return SHADER_KINDS[i].kind;
	return KIND_OTHER;
}

Bool isImageKind(PassKind kind)
{
	return kind == KIND_IMAGE || kind == KIND_IMAGE_DEPTH || kind == KIND_COLOR_GRADE;
}

Bool isBranchRole(Int role)
{
	return role == PASS_ROLE_OCCLUSION || role == PASS_ROLE_BLOOM || role == PASS_ROLE_EXPOSURE;
}

Int findRowIn(const std::vector<PassRow> &rows, const AsciiString &name)
{
	for (size_t i = 0; i < rows.size(); ++i)
		if (rows[i].name.compareNoCase(name) == 0)
			return (Int)i;
	return -1;
}

Bool sameWords(const std::vector<AsciiString> &a, const std::vector<AsciiString> &b)
{
	if (a.size() != b.size())
		return FALSE;
	for (size_t i = 0; i < a.size(); ++i)
		if (a[i].compareNoCase(b[i]) != 0)
			return FALSE;
	return TRUE;
}

std::string joinWords(const std::vector<AsciiString> &words)
{
	std::string text;
	for (size_t i = 0; i < words.size(); ++i)
		text += (text.empty() ? "" : " ") + std::string(words[i].str());
	return text;
}

/// A pass block as the order needs it.
struct PassInfo
{
	AsciiString name;
	Int block;
	AsciiString shader;
	std::vector<AsciiString> inputs;		///< every input word
	Int rank;
};

Bool containsName(const std::vector<std::string> &words, const AsciiString &name)
{
	for (size_t i = 0; i < words.size(); ++i)
		if (name.compareNoCase(words[i].c_str()) == 0)
			return TRUE;
	return FALSE;
}

Int findInfo(const std::vector<PassInfo> &passes, const AsciiString &name)
{
	for (size_t i = 0; i < passes.size(); ++i)
		if (passes[i].name.compareNoCase(name) == 0)
			return (Int)i;
	return -1;
}

Int shaderRank(const AsciiString &shader)
{
	for (size_t i = 0; i < ARRAY_SIZE(SHADER_STAGES); ++i)
		if (shader.compareNoCase(SHADER_STAGES[i].shader) == 0)
			return SHADER_STAGES[i].rank;
	return -1;
}

/// The stage of a pass without one of its own: after the passes it reads; without such passes the stage of
/// the pass before it in Passes.
Int computeRank(std::vector<PassInfo> &passes, Int index, const std::vector<std::string> &listed, Int depth)
{
	PassInfo &pass = passes[index];
	if (pass.rank >= 0)
		return pass.rank;
	const Int own = shaderRank(pass.shader);
	if (own >= 0 || depth > 32)
	{
		pass.rank = own >= 0 ? own : 0;
		return pass.rank;
	}

	Int rank = -1;
	for (size_t i = 0; i < pass.inputs.size(); ++i)
	{
		const Int input = findInfo(passes, pass.inputs[i]);
		if (input >= 0 && input != index)
			rank = std::max(rank, computeRank(passes, input, listed, depth + 1) + 1);
	}
	if (rank < 0)
	{
		rank = 0;
		for (size_t i = 0; i < listed.size(); ++i)
		{
			if (pass.name.compareNoCase(listed[i].c_str()) == 0)
				break;
			const Int before = findInfo(passes, AsciiString(listed[i].c_str()));
			if (before >= 0 && before != index && passes[before].rank != RANK_DEBUG)
				rank = computeRank(passes, before, listed, depth + 1);
		}
	}
	pass.rank = rank;
	return rank;
}

PassRow makeRow(const PassInfo *pass, const AsciiString &name)
{
	PassRow row;
	row.name = pass != nullptr ? pass->name : name;
	row.block = pass != nullptr ? pass->block : -1;
	if (pass != nullptr)
	{
		row.shader = pass->shader;
		row.words = pass->inputs;
	}
	row.role = PASS_ROLE_NONE;
	row.listed = FALSE;
	row.enabled = FALSE;
	row.debug = pass != nullptr && pass->rank == RANK_DEBUG;
	row.defaultIndex = -1;
	row.moved = FALSE;
	return row;
}

} // namespace

//-------------------------------------------------------------------------------------------------
Bool PassRow::needsConnect() const
{
	return !wanted.empty() && !sameWords(words, wanted);
}

//-------------------------------------------------------------------------------------------------
Int PassChain::countNeedsConnect() const
{
	Int count = 0;
	for (size_t i = 0; i < rows.size(); ++i)
		if (rows[i].needsConnect())
			++count;
	return count;
}

//-------------------------------------------------------------------------------------------------
Int PassChain::findRow(const AsciiString &name) const
{
	for (size_t i = 0; i < rows.size(); ++i)
		if (rows[i].name.compareNoCase(name) == 0)
			return (Int)i;
	return -1;
}

//-------------------------------------------------------------------------------------------------
Int PassChain::findDebugRow(const AsciiString &name) const
{
	for (size_t i = 0; i < debugRows.size(); ++i)
		if (debugRows[i].name.compareNoCase(name) == 0)
			return (Int)i;
	return -1;
}

//-------------------------------------------------------------------------------------------------
void PassOrder::splitWords(const std::string &text, std::vector<std::string> &words)
{
	words.clear();
	const char *p = text.c_str();
	while (*p)
	{
		while (*p == ' ' || *p == '\t')
			++p;
		const char *start = p;
		while (*p && *p != ' ' && *p != '\t')
			++p;
		if (p > start)
			words.push_back(std::string(start, p - start));
	}
}

//-------------------------------------------------------------------------------------------------
void PassOrder::getChainBlocks(IniDatabase &db, std::vector<Int> &blocks)
{
	blocks.clear();
	const Int main = db.findTypedBlock("PostProcess", AsciiString("PostProcess"));
	if (main >= 0)
		blocks.push_back(main);
	for (Int i = 0; i < db.getBlockCount(); ++i)
	{
		const IniBlock *b = db.getBlock(i);
		if (_stricmp(b->type.c_str(), "PostProcessChain") == 0 && db.findTypedBlock("PostProcessChain", b->name) == i)
			blocks.push_back(i);
	}
}

//-------------------------------------------------------------------------------------------------
Bool PassOrder::build(IniDatabase &db, Int chainBlock, PassChain &chain)
{
	chain.block = chainBlock;
	chain.rows.clear();
	chain.debugRows.clear();
	chain.defaultOrder.clear();
	chain.movedCount = 0;

	const IniBlock *block = db.getBlock(chainBlock);
	if (block == nullptr || (_stricmp(block->type.c_str(), "PostProcess") != 0 && _stricmp(block->type.c_str(), "PostProcessChain") != 0))
		return FALSE;

	std::string text;
	std::vector<std::string> listed;
	std::vector<std::string> disabled;
	if (IniText::getField(block->text, -1, "Passes", text))
		splitWords(text, listed);
	if (IniText::getField(block->text, -1, "Disabled", text))
		splitWords(text, disabled);

	// The passes the game uses: the last block of each name.
	std::vector<PassInfo> passes;
	for (Int i = 0; i < db.getBlockCount(); ++i)
	{
		const IniBlock *b = db.getBlock(i);
		if (_stricmp(b->type.c_str(), "PostProcessPass") != 0 || db.findTypedBlock("PostProcessPass", b->name) != i)
			continue;
		PassInfo pass;
		pass.name = b->name;
		pass.block = i;
		pass.rank = -1;
		if (IniText::getField(b->text, -1, "Shader", text))
			pass.shader = text.c_str();
		std::vector<std::string> inputs;
		if (IniText::getField(b->text, -1, "Inputs", text))
			splitWords(text, inputs);
		for (size_t k = 0; k < inputs.size(); ++k)
			pass.inputs.push_back(AsciiString(inputs[k].c_str()));
		passes.push_back(pass);
	}
	for (size_t i = 0; i < passes.size(); ++i)
		computeRank(passes, (Int)i, listed, 0);

	// The default order: a pass is ready when the passes it reads are placed; the ready pass of the
	// lowest stage goes next, then the one defined first.
	std::vector<Bool> placed(passes.size(), FALSE);
	for (size_t i = 0; i < passes.size(); ++i)
		placed[i] = passes[i].rank == RANK_DEBUG;
	for (;;)
	{
		Int best = -1;
		Int fallback = -1;
		for (size_t i = 0; i < passes.size(); ++i)
		{
			if (placed[i])
				continue;
			if (fallback < 0 || passes[i].rank < passes[fallback].rank)
				fallback = (Int)i;
			Bool ready = TRUE;
			for (size_t k = 0; k < passes[i].inputs.size() && ready; ++k)
			{
				const Int input = findInfo(passes, passes[i].inputs[k]);
				// Which image pass a built-in image pass reads follows the order (auto-connect), so it
				// does not decide the order.
				if (k == 0 && input >= 0 && isImageKind(shaderKind(passes[i].shader)) && isImageKind(shaderKind(passes[input].shader)))
					continue;
				if (input >= 0 && input != (Int)i && !placed[input])
					ready = FALSE;
			}
			if (ready && (best < 0 || passes[i].rank < passes[best].rank))
				best = (Int)i;
		}
		if (fallback < 0)
			break;
		// Passes that read each other in a circle go by their stage.
		const Int next = best >= 0 ? best : fallback;
		placed[next] = TRUE;
		chain.defaultOrder.push_back(passes[next].name);
	}

	// The rows in the order of Passes; debug views apart.
	for (size_t i = 0; i < listed.size(); ++i)
	{
		const AsciiString name(listed[i].c_str());
		if (chain.findRow(name) >= 0 || chain.findDebugRow(name) >= 0)
			continue;
		const Int info = findInfo(passes, name);
		PassRow row = makeRow(info >= 0 ? &passes[info] : nullptr, name);
		row.listed = TRUE;
		row.enabled = !containsName(disabled, name);
		(row.debug ? chain.debugRows : chain.rows).push_back(row);
	}
	for (size_t i = 0; i < passes.size(); ++i)
	{
		if (passes[i].rank == RANK_DEBUG && chain.findDebugRow(passes[i].name) < 0)
			chain.debugRows.push_back(makeRow(&passes[i], passes[i].name));
	}

	for (size_t i = 0; i < chain.defaultOrder.size(); ++i)
	{
		const Int row = chain.findRow(chain.defaultOrder[i]);
		if (row >= 0)
		{
			chain.rows[row].defaultIndex = (Int)i;
			continue;
		}
		// A pass that is not in Passes yet goes after the pass before it in the default order.
		size_t insertAt = 0;
		for (Int before = (Int)i - 1; before >= 0; --before)
		{
			const Int found = chain.findRow(chain.defaultOrder[before]);
			if (found >= 0)
			{
				insertAt = (size_t)found + 1;
				break;
			}
		}
		const Int info = findInfo(passes, chain.defaultOrder[i]);
		PassRow added = makeRow(&passes[info], passes[info].name);
		added.defaultIndex = (Int)i;
		chain.rows.insert(chain.rows.begin() + insertAt, added);
	}

	for (size_t i = 0; i < chain.rows.size(); ++i)
	{
		const Int info = findInfo(passes, chain.rows[i].name);
		if (info < 0)
			continue;
		for (size_t k = 0; k < passes[info].inputs.size(); ++k)
			if (findInfo(passes, passes[info].inputs[k]) >= 0)
				chain.rows[i].inputs.push_back(passes[info].inputs[k]);
	}
	for (size_t i = 0; i < chain.debugRows.size(); ++i)
	{
		const Int info = findInfo(passes, chain.debugRows[i].name);
		if (info >= 0)
			chain.debugRows[i].inputs = passes[info].inputs;
	}

	// Moved: the rows outside the longest run that keeps the default order, so moving one pass marks one row.
	const Int count = (Int)chain.rows.size();
	std::vector<Int> length(count, 0);
	std::vector<Int> previous(count, -1);
	Int last = -1;
	for (Int i = 0; i < count; ++i)
	{
		if (chain.rows[i].defaultIndex < 0)
			continue;
		length[i] = 1;
		for (Int j = 0; j < i; ++j)
		{
			if (chain.rows[j].defaultIndex >= 0 && chain.rows[j].defaultIndex < chain.rows[i].defaultIndex && length[j] + 1 > length[i])
			{
				length[i] = length[j] + 1;
				previous[i] = j;
			}
		}
		if (last < 0 || length[i] >= length[last])
			last = i;
	}
	std::vector<Bool> kept(count, FALSE);
	for (Int i = last; i >= 0; i = previous[i])
		kept[i] = TRUE;
	for (Int i = 0; i < count; ++i)
	{
		chain.rows[i].moved = chain.rows[i].defaultIndex >= 0 && !kept[i];
		if (chain.rows[i].moved)
			++chain.movedCount;
	}
	connect(chain.rows);
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
const char *PassOrder::getRoleName(Int role)
{
	switch (role)
	{
		case PASS_ROLE_IMAGE: return "the image chain";
		case PASS_ROLE_OCCLUSION: return "the occlusion branch";
		case PASS_ROLE_BLOOM: return "the bloom branch";
		case PASS_ROLE_EXPOSURE: return "the auto exposure branch";
		default: return "no part";
	}
}

//-------------------------------------------------------------------------------------------------
void PassOrder::connect(std::vector<PassRow> &rows)
{
	const AsciiString scene("Scene");
	const AsciiString depth("Depth");
	const AsciiString none("None");
	AsciiString image;							// the last pass of the image chain so far
	AsciiString last[PASS_ROLE_COUNT];			// the last pass of each branch so far
	AsciiString result[PASS_ROLE_COUNT];		// what ColorGrade reads of each branch
	Int previousRole = PASS_ROLE_NONE;

	for (size_t i = 0; i < rows.size(); ++i)
	{
		PassRow &row = rows[i];
		row.wanted.clear();
		row.role = PASS_ROLE_NONE;
		const PassKind kind = row.block >= 0 ? shaderKind(row.shader) : KIND_DEBUG;
		const AsciiString source = image.isEmpty() ? scene : image;

		switch (kind)
		{
			case KIND_COLOR_GRADE:
			{
				row.role = PASS_ROLE_IMAGE;
				row.wanted.push_back(source);
				row.wanted.push_back(result[PASS_ROLE_BLOOM].isEmpty() ? none : result[PASS_ROLE_BLOOM]);
				row.wanted.push_back(row.words.size() > 2 ? row.words[2] : none);
				row.wanted.push_back(result[PASS_ROLE_OCCLUSION].isEmpty() ? none : result[PASS_ROLE_OCCLUSION]);
				row.wanted.push_back(result[PASS_ROLE_EXPOSURE].isEmpty() ? none : result[PASS_ROLE_EXPOSURE]);
				while (row.wanted.size() > 1 && row.wanted.back().compareNoCase(none) == 0)
					row.wanted.pop_back();
				break;
			}
			case KIND_IMAGE:
			case KIND_IMAGE_DEPTH:
				row.role = PASS_ROLE_IMAGE;
				row.wanted.push_back(source);
				if (kind == KIND_IMAGE_DEPTH)
					row.wanted.push_back(depth);
				break;
			case KIND_SSAO:
				row.role = PASS_ROLE_OCCLUSION;
				row.wanted.push_back(depth);
				break;
			case KIND_BLOOM_EXTRACT:
				row.role = PASS_ROLE_BLOOM;
				row.wanted.push_back(scene);
				break;
			case KIND_LUMINANCE:
				// Luminance alone is no exposure; the branch has a result only from its AutoExposure on.
				row.role = PASS_ROLE_EXPOSURE;
				row.wanted.push_back(scene);
				result[PASS_ROLE_EXPOSURE].clear();
				break;
			case KIND_AUTO_EXPOSURE:
				if (previousRole == PASS_ROLE_EXPOSURE)
				{
					row.role = PASS_ROLE_EXPOSURE;
					row.wanted.push_back(last[PASS_ROLE_EXPOSURE]);
					row.wanted.push_back(AsciiString("Previous"));
					result[PASS_ROLE_EXPOSURE] = row.name;
				}
				break;
			case KIND_BLUR:
			case KIND_DEPTH_BLUR:
				// A blur continues what runs right before it.
				if (isBranchRole(previousRole))
				{
					row.role = previousRole;
					row.wanted.push_back(last[previousRole]);
				}
				else if (previousRole == PASS_ROLE_IMAGE || i == 0)
				{
					row.role = PASS_ROLE_IMAGE;
					row.wanted.push_back(source);
				}
				if (row.role != PASS_ROLE_NONE && kind == KIND_DEPTH_BLUR)
					row.wanted.push_back(depth);
				break;
			case KIND_OTHER:
			{
				// A shader of the mod's own is part of the image chain when it reads the scene or the image
				// chain; only that input follows the order.
				Bool readsImage = !row.words.empty() && row.words[0].compareNoCase(scene) == 0;
				for (size_t k = 0; k < i && !readsImage && !row.words.empty(); ++k)
					readsImage = rows[k].role == PASS_ROLE_IMAGE && rows[k].name.compareNoCase(row.words[0]) == 0;
				if (readsImage)
				{
					row.role = PASS_ROLE_IMAGE;
					row.wanted.push_back(source);
					for (size_t k = 1; k < row.words.size(); ++k)
						row.wanted.push_back(row.words[k]);
				}
				break;
			}
			default:
				break;
		}

		if (row.role == PASS_ROLE_IMAGE)
			image = row.name;
		else if (isBranchRole(row.role))
		{
			last[row.role] = row.name;
			if (row.role != PASS_ROLE_EXPOSURE)
				result[row.role] = row.name;
		}
		previousRole = row.role;
	}
}

//-------------------------------------------------------------------------------------------------
Bool PassOrder::checkReorder(const PassChain &chain, const std::vector<PassRow> &rows, Bool autoInputs, AsciiString &reason)
{
	if (!autoInputs)
		return checkOrder(rows, reason);

	std::vector<PassRow> before = chain.rows;
	connect(before);
	std::vector<PassRow> after = rows;
	connect(after);

	for (size_t i = 0; i < after.size(); ++i)
	{
		const PassRow &row = after[i];
		const Int old = findRowIn(before, row.name);
		if (old < 0)
			continue;
		const PassRow &was = before[old];
		if (was.role != row.role)
		{
			reason.format("%s would leave %s", row.name.str(), getRoleName(was.role));
			return FALSE;
		}
		// The results of other branches it reads (ColorGrade's bloom, occlusion, exposure).
		for (size_t k = 1; k < was.wanted.size(); ++k)
		{
			if (findRowIn(before, was.wanted[k]) < 0)
				continue;
			if (k >= row.wanted.size() || findRowIn(after, row.wanted[k]) < 0)
			{
				reason.format("%s would lose %s, which must run before it", row.name.str(), was.wanted[k].str());
				return FALSE;
			}
		}
	}

	// What auto-connect gives the passes counts; passes it leaves alone keep what they read.
	for (size_t i = 0; i < after.size(); ++i)
	{
		if (after[i].wanted.empty())
			continue;
		after[i].inputs.clear();
		for (size_t k = 0; k < after[i].wanted.size(); ++k)
			if (findRowIn(after, after[i].wanted[k]) >= 0)
				after[i].inputs.push_back(after[i].wanted[k]);
	}
	return checkOrder(after, reason);
}

//-------------------------------------------------------------------------------------------------
Bool PassOrder::checkOtherChains(IniDatabase &db, const PassChain &chain, AsciiString &error)
{
	std::vector<Int> chains;
	getChainBlocks(db, chains);
	for (size_t c = 0; c < chains.size(); ++c)
	{
		if (chains[c] == chain.block)
			continue;
		const IniBlock *other = db.getBlock(chains[c]);
		std::string text;
		std::vector<std::string> listed;
		if (other == nullptr || !IniText::getField(other->text, -1, "Passes", text))
			continue;
		splitWords(text, listed);
		for (size_t i = 0; i < chain.rows.size(); ++i)
		{
			const PassRow &row = chain.rows[i];
			if (!row.needsConnect())
				continue;
			Int reader = -1;
			for (size_t p = 0; p < listed.size() && reader < 0; ++p)
				if (row.name.compareNoCase(listed[p].c_str()) == 0)
					reader = (Int)p;
			if (reader < 0)
				continue;
			for (size_t k = 0; k < row.wanted.size(); ++k)
			{
				for (size_t p = reader + 1; p < listed.size(); ++p)
				{
					if (row.wanted[k].compareNoCase(listed[p].c_str()) == 0)
					{
						const char *name = _stricmp(other->type.c_str(), "PostProcess") == 0 ? "Default" : other->name.str();
						error.format("Chain %s runs %s before %s, which %s would read: give that chain its own copy of %s",
							name, row.name.str(), row.wanted[k].str(), row.name.str(), row.name.str());
						return FALSE;
					}
				}
			}
		}
	}
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
Bool PassOrder::writeInputs(IniDatabase &db, const PassChain &chain, AsciiString &error)
{
	if (!checkOtherChains(db, chain, error))
		return FALSE;
	for (size_t i = 0; i < chain.rows.size(); ++i)
	{
		const PassRow &row = chain.rows[i];
		IniBlock *block = db.getBlock(row.block);
		if (!row.needsConnect() || block == nullptr)
			continue;
		const std::string text = IniText::setField(block->text, -1, "Inputs", joinWords(row.wanted));
		if (!db.setText(row.block, text, error, FALSE, TRUE))
			return FALSE;
	}
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
Bool PassOrder::apply(IniDatabase &db, PassChain &chain, Bool autoInputs, AsciiString &error)
{
	if (autoInputs)
	{
		connect(chain.rows);
		if (!checkOtherChains(db, chain, error))
			return FALSE;
	}
	if (!write(db, chain, error))
		return FALSE;
	return !autoInputs || writeInputs(db, chain, error);
}

//-------------------------------------------------------------------------------------------------
Bool PassOrder::checkOrder(const std::vector<PassRow> &rows, AsciiString &reason)
{
	for (size_t i = 0; i < rows.size(); ++i)
	{
		for (size_t k = 0; k < rows[i].inputs.size(); ++k)
		{
			for (size_t j = i + 1; j < rows.size(); ++j)
			{
				if (rows[j].name.compareNoCase(rows[i].inputs[k]) == 0)
				{
					reason.format("%s reads %s, which would run after it", rows[i].name.str(), rows[j].name.str());
					return FALSE;
				}
			}
		}
	}
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
Bool PassOrder::canMove(const PassChain &chain, Int from, Int to, AsciiString &reason, Bool autoInputs)
{
	if (from < 0 || from >= (Int)chain.rows.size() || to < 0 || to >= (Int)chain.rows.size())
	{
		reason = "no such place";
		return FALSE;
	}
	if (from == to)
		return TRUE;
	PassChain moved = chain;
	move(moved, from, to);
	return checkReorder(chain, moved.rows, autoInputs, reason);
}

//-------------------------------------------------------------------------------------------------
void PassOrder::move(PassChain &chain, Int from, Int to)
{
	if (from < 0 || from >= (Int)chain.rows.size() || to < 0 || to >= (Int)chain.rows.size() || from == to)
		return;
	const PassRow row = chain.rows[from];
	chain.rows.erase(chain.rows.begin() + from);
	chain.rows.insert(chain.rows.begin() + to, row);
}

//-------------------------------------------------------------------------------------------------
void PassOrder::resetOrder(PassChain &chain)
{
	// Names without a pass block keep their place after the row before them.
	std::vector<PassRow> known;
	std::vector<PassRow> unknown;
	std::vector<Int> unknownAfter;
	for (size_t i = 0; i < chain.rows.size(); ++i)
	{
		if (chain.rows[i].defaultIndex >= 0)
		{
			known.push_back(chain.rows[i]);
		}
		else
		{
			unknown.push_back(chain.rows[i]);
			unknownAfter.push_back(i > 0 ? chain.rows[i - 1].defaultIndex : -1);
		}
	}
	struct ByDefault
	{
		bool operator()(const PassRow &a, const PassRow &b) const { return a.defaultIndex < b.defaultIndex; }
	};
	std::stable_sort(known.begin(), known.end(), ByDefault());

	chain.rows.clear();
	for (size_t u = 0; u < unknown.size(); ++u)
		if (unknownAfter[u] < 0)
			chain.rows.push_back(unknown[u]);
	for (size_t i = 0; i < known.size(); ++i)
	{
		chain.rows.push_back(known[i]);
		for (size_t u = 0; u < unknown.size(); ++u)
			if (unknownAfter[u] == known[i].defaultIndex)
				chain.rows.push_back(unknown[u]);
	}
}

//-------------------------------------------------------------------------------------------------
const std::vector<AsciiString> *PassOrder::getRunningPasses(IniDatabase &db, const PassChain &chain)
{
	if (TheW3DPostProcess == nullptr || !TheW3DPostProcess->isRunning())
		return nullptr;
	const IniBlock *block = db.getBlock(chain.block);
	if (block == nullptr)
		return nullptr;
	const AsciiString &running = ThePostProcessControl.getChainName();
	const Bool same = running.isEmpty() ? _stricmp(block->type.c_str(), "PostProcess") == 0
		: _stricmp(block->type.c_str(), "PostProcessChain") == 0 && block->name.compareNoCase(running) == 0;
	return same ? &TheW3DPostProcess->getPasses() : nullptr;
}

//-------------------------------------------------------------------------------------------------
Bool PassOrder::isRunning(const std::vector<AsciiString> *running, const AsciiString &name)
{
	if (running == nullptr)
		return FALSE;
	for (size_t i = 0; i < running->size(); ++i)
		if ((*running)[i].compareNoCase(name) == 0)
			return TRUE;
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
Bool PassOrder::write(IniDatabase &db, const PassChain &chain, AsciiString &error)
{
	IniBlock *block = db.getBlock(chain.block);
	if (block == nullptr)
	{
		error = "no chain block";
		return FALSE;
	}
	std::string passes;
	std::string disabled;
	for (size_t i = 0; i < chain.rows.size(); ++i)
	{
		passes += (passes.empty() ? "" : " ") + std::string(chain.rows[i].name.str());
		if (!chain.rows[i].enabled)
			disabled += (disabled.empty() ? "" : " ") + std::string(chain.rows[i].name.str());
	}
	for (size_t i = 0; i < chain.debugRows.size(); ++i)
	{
		if (chain.debugRows[i].enabled)
			passes += (passes.empty() ? "" : " ") + std::string(chain.debugRows[i].name.str());
	}

	// Passes clears Disabled when the game reads it, so Disabled goes after it: removed and added again.
	std::string text = IniText::setField(block->text, -1, "Passes", passes);
	text = IniText::removeField(text, -1, "Disabled");
	if (!disabled.empty())
		text = IniText::setField(text, -1, "Disabled", disabled);
	if (text == block->text)
		return TRUE;
	return db.setText(chain.block, text, error, FALSE, TRUE);
}
