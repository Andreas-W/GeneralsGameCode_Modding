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

// The passes of a PostProcess or PostProcessChain block as an ordered list for the FX Editor: every
// PostProcessPass the INI defines, the ones that are off (Disabled, or not in Passes yet) at their place,
// and the recommended default order to compare with. Used by the Direct3D 12 window and the MCP commands.

#pragma once

#include "Common/AsciiString.h"
#include "Lib/BaseType.h"

#include <string>
#include <vector>

class IniDatabase;

/// What a pass does in the chain, for auto-connect: part of the image chain that ends on the screen, or of a
/// branch whose result a ColorGrade pass reads.
enum PassRole
{
	PASS_ROLE_NONE,								///< left alone: debug views, unknown shaders that read no image
	PASS_ROLE_IMAGE,
	PASS_ROLE_OCCLUSION,
	PASS_ROLE_BLOOM,
	PASS_ROLE_EXPOSURE,

	PASS_ROLE_COUNT
};

struct PassRow
{
	AsciiString name;
	Int block;									///< the PostProcessPass block, -1 for a name in Passes without one
	AsciiString shader;
	std::vector<AsciiString> inputs;			///< the inputs that are other passes
	std::vector<AsciiString> words;				///< the Inputs field as written
	std::vector<AsciiString> wanted;			///< the Inputs auto-connect gives it at this place, empty when it leaves it alone
	Int role;									///< PassRole at this place
	Bool listed;								///< in Passes
	Bool enabled;								///< in Passes and not in Disabled
	Bool debug;									///< a debug view (PP_DepthView, PP_Copy): not part of the order
	Int defaultIndex;							///< place in the default order, -1 for debug views and unknown names
	Bool moved;									///< away from the default order (the fewest rows that explain the order)

	/// Auto-connect would change its Inputs.
	Bool needsConnect() const;
};

struct PassChain
{
	Int block;									///< the PostProcess or PostProcessChain block
	std::vector<PassRow> rows;					///< in the order they run
	std::vector<PassRow> debugRows;				///< debug views; the ones that are on run last
	std::vector<AsciiString> defaultOrder;		///< all passes but the debug views, in the default order
	Int movedCount;

	Bool isCustomOrder() const { return movedCount > 0; }
	Int countNeedsConnect() const;
	Int findRow(const AsciiString &name) const;
	Int findDebugRow(const AsciiString &name) const;
};

namespace PassOrder
{
	/// The words of a list field (Passes, Disabled, Inputs).
	void splitWords(const std::string &text, std::vector<std::string> &words);

	/// The PostProcess block and the PostProcessChain blocks, the ones the game uses (the last of each name).
	void getChainBlocks(IniDatabase &db, std::vector<Int> &blocks);

	/// The list of a chain block from its text and the PostProcessPass blocks. False if it is no chain block.
	Bool build(IniDatabase &db, Int chainBlock, PassChain &chain);

	/// Whether the pass at from can run at to (index after it was taken out): every pass must run after the
	/// passes it reads, the ones that are off too, so switching them on cannot break the chain. With
	/// autoInputs the inputs auto-connect gives the passes at their new places count (see checkReorder).
	Bool canMove(const PassChain &chain, Int from, Int to, AsciiString &reason, Bool autoInputs = FALSE);
	void move(PassChain &chain, Int from, Int to);
	/// The default order; what is on or off stays.
	void resetOrder(PassChain &chain);
	/// The order of the rows is valid.
	Bool checkOrder(const std::vector<PassRow> &rows, AsciiString &reason);
	/// Whether the rows of chain can run in the order of rows. With autoInputs the image chain follows the new
	/// order, but no pass may change its part (image chain or a branch) or lose the result of a branch it reads.
	Bool checkReorder(const PassChain &chain, const std::vector<PassRow> &rows, Bool autoInputs, AsciiString &reason);

	/// Auto-connect: works out the Inputs of every row from its shader and its place (wanted, role), as if
	/// every pass were on; the game skips the ones that are off. The image chain reads the image pass before
	/// it (Scene for the first); a branch starts from Scene or Depth, its blurs read the pass right before
	/// them; ColorGrade reads the last bloom, occlusion and auto exposure result before it in its slots and
	/// keeps its lookup table. Fog and DepthOfField also read Depth, AutoExposure its Previous output.
	void connect(std::vector<PassRow> &rows);
	/// Whether writing the wanted Inputs keeps every other chain that uses these passes in a valid order.
	Bool checkOtherChains(IniDatabase &db, const PassChain &chain, AsciiString &error);
	/// Writes the wanted Inputs of the rows that need it (one undo step per pass block).
	Bool writeInputs(IniDatabase &db, const PassChain &chain, AsciiString &error);
	/// write, and with autoInputs connect and writeInputs first checked and afterwards written.
	Bool apply(IniDatabase &db, PassChain &chain, Bool autoInputs, AsciiString &error);
	const char *getRoleName(Int role);

	/// The passes the renderer runs now when this chain is the one it runs (also without unused ones), else null.
	const std::vector<AsciiString> *getRunningPasses(IniDatabase &db, const PassChain &chain);
	Bool isRunning(const std::vector<AsciiString> *running, const AsciiString &name);

	/// Writes Passes (every row, then the debug views that are on) and Disabled (the rows that are off)
	/// into the chain block, as one undo step.
	Bool write(IniDatabase &db, const PassChain &chain, AsciiString &error);
}
