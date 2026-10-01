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

// McpCmdImage.cpp
// MCP bridge commands that exchange terrain data with image files: heightmaps and cell masks.
//
// Image conventions: row 0 is the north edge (+y), so images look like the top-down view.
// Heightmap images have one pixel per heightmap vertex; mask images have one pixel per cell.

#include "StdAfx.h"
#include "mcp/McpCommands.h"

#include "WHeightMapEdit.h"
#include "Common/MapData.h"
#include "WorldBuilderDoc.h"
#include "GameLogic/PolygonTrigger.h"

#include <math.h>
#include <vector>

// Only the stb writer is compiled into the engine; the reader is only needed here.
#pragma warning(push, 0)
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#include <stb_image.h>
#pragma warning(pop)
#include <stb_image_write.h>

namespace
{

/// A rectangle of heightmap vertices that an image is mapped onto.
struct VertexRegion
{
	Int x0, y0, w, h;
};

VertexRegion getRegion(WorldHeightMapEdit *map, bool includeBorder)
{
	VertexRegion r;
	const Int border = includeBorder ? 0 : map->getBorderSize();
	r.x0 = border;
	r.y0 = border;
	r.w = map->getXExtent() - 2 * border;
	r.h = map->getYExtent() - 2 * border;
	if (r.w < 2 || r.h < 2) {
		mcpFail("map area is too small");
	}
	return r;
}

/// An RGBA image with 16 bits per channel; 8-bit files are expanded on load.
class Image
{
public:
	Image() : m_pixels(nullptr), m_width(0), m_height(0), m_channel(-1) {}
	~Image() { if (m_pixels) stbi_image_free(m_pixels); }

	void load(const std::string &path, const std::string &channel)
	{
		if (GetFileAttributes(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
			mcpFail("file not found: %s", path.c_str());
		}
		int comp = 0;
		m_pixels = stbi_load_16(path.c_str(), &m_width, &m_height, &comp, 4);
		if (m_pixels == nullptr) {
			mcpFail("could not read image %s (%s); PNG, BMP and TGA are supported", path.c_str(), stbi_failure_reason());
		}
		if (channel == "luma") m_channel = -1;
		else if (channel == "r") m_channel = 0;
		else if (channel == "g") m_channel = 1;
		else if (channel == "b") m_channel = 2;
		else if (channel == "a") m_channel = 3;
		else mcpFail("channel must be luma, r, g, b or a");
	}

	int width() const { return m_width; }
	int height() const { return m_height; }

	/// Pixel value in 0..1 for the selected channel.
	double at(int x, int y) const
	{
		if (x < 0) x = 0;
		if (y < 0) y = 0;
		if (x >= m_width) x = m_width - 1;
		if (y >= m_height) y = m_height - 1;
		const unsigned short *p = m_pixels + 4 * (y * m_width + x);
		if (m_channel >= 0) {
			return p[m_channel] / 65535.0;
		}
		return (0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2]) / 65535.0;
	}

	double bilinear(double u, double v) const
	{
		const int x = (int)floor(u);
		const int y = (int)floor(v);
		const double fx = u - x;
		const double fy = v - y;
		const double top = at(x, y) * (1 - fx) + at(x + 1, y) * fx;
		const double bottom = at(x, y + 1) * (1 - fx) + at(x + 1, y + 1) * fx;
		return top * (1 - fy) + bottom * fy;
	}

private:
	unsigned short *m_pixels;
	int m_width;
	int m_height;
	int m_channel;
};

enum FitMode
{
	FIT_STRETCH,
	FIT_EXACT,
	FIT_CENTER
};

FitMode parseFit(const McpJson &args)
{
	const std::string fit = mcpArgString(args, "fit", "stretch");
	if (fit == "stretch") return FIT_STRETCH;
	if (fit == "exact") return FIT_EXACT;
	if (fit == "center") return FIT_CENTER;
	mcpFail("fit must be stretch, exact or center");
	return FIT_STRETCH;
}

/// Maps a grid position (0..count-1 along one axis, image row order already applied) to an image
/// coordinate. Returns false when the position falls outside the image (center fit only).
bool mapAxis(FitMode fit, double pos, int count, int imageSize, bool samplesAreCells, double *out)
{
	switch (fit) {
		case FIT_STRETCH:
			if (samplesAreCells) {
				*out = (pos + 0.5) * imageSize / count - 0.5;
			} else {
				*out = count > 1 ? pos * (imageSize - 1) / (double)(count - 1) : 0;
			}
			return true;
		case FIT_EXACT:
			*out = pos;
			return true;
		case FIT_CENTER:
		default:
			*out = pos - (count - imageSize) / 2;
			return *out > -0.5 && *out < imageSize - 0.5;
	}
}

void requireExactSize(FitMode fit, const Image &img, int w, int h, const char *what)
{
	if (fit == FIT_EXACT && (img.width() != w || img.height() != h)) {
		mcpFail("fit=exact needs a %dx%d image (one pixel per %s), got %dx%d", w, h, what, img.width(), img.height());
	}
}

std::string defaultImagePath(const char *kind)
{
	static Int s_counter = 0;
	char tempDir[MAX_PATH];
	::GetTempPath(MAX_PATH, tempDir);
	char name[MAX_PATH];
	sprintf(name, "%swb_mcp_%s_%u_%d.png", tempDir, kind, (unsigned)::GetCurrentProcessId(), ++s_counter);
	return name;
}

void writeGrayPng(const std::string &path, int w, int h, const std::vector<unsigned char> &pixels)
{
	if (!stbi_write_png(path.c_str(), w, h, 1, &pixels[0], w)) {
		mcpFail("could not write %s", path.c_str());
	}
}

//-------------------------------------------------------------------------------------------------
// Heightmaps
//-------------------------------------------------------------------------------------------------

McpJson cmdExportHeightmap(const McpJson &args)
{
	WorldHeightMapEdit *map = mcpHeightMap();
	const bool includeBorder = mcpArgBool(args, "include_border", true);
	const bool normalize = mcpArgBool(args, "normalize", false);
	const VertexRegion r = getRegion(map, includeBorder);
	std::string path = mcpArgString(args, "path", "");
	if (path.empty()) {
		path = defaultImagePath("height");
	}

	Int minH = WorldHeightMap::getMaxHeightValue(), maxH = 0;
	for (Int y = 0; y < r.h; y++) {
		for (Int x = 0; x < r.w; x++) {
			const Int h = map->getHeight(r.x0 + x, r.y0 + y);
			if (h < minH) minH = h;
			if (h > maxH) maxH = h;
		}
	}
	const Int heightLimit = WorldHeightMap::getMaxHeightValue();
	const double heightScale = TheMapData->m_HeightmapScale > 0 ? TheMapData->m_HeightmapScale : 1.0;
	std::vector<unsigned char> pixels(r.w * r.h);
	for (Int y = 0; y < r.h; y++) {
		for (Int x = 0; x < r.w; x++) {
			Int h = map->getHeight(r.x0 + x, r.y0 + y);
			if (normalize) {
				h = maxH > minH ? (h - minH) * 255 / (maxH - minH) : 0;
			} else {
				// An 8-bit image holds what the map file holds: the height divided by the HeightMapScale.
				h = (Int)floor(h / heightScale + 0.5);
				if (h > 255) h = 255;
			}
			// Row 0 is the north edge.
			pixels[(r.h - 1 - y) * r.w + x] = (unsigned char)h;
		}
	}
	writeGrayPng(path, r.w, r.h, pixels);

	McpJson j = McpJson::makeObject();
	j.set("path", path).set("width", r.w).set("height", r.h);
	j.set("height_raw_min", minH).set("height_raw_max", maxH).set("normalized", normalize);
	j.set("include_border", includeBorder);
	j.set("white_is_raw_height", normalize ? maxH : heightLimit);
	return j;
}

McpJson cmdImportHeightmap(const McpJson &args)
{
	WorldHeightMapEdit *map = mcpHeightMap();
	const std::string mode = mcpArgString(args, "mode", "set");
	if (mode != "set" && mode != "add") {
		mcpFail("mode must be set or add");
	}
	double lo = mode == "set" ? 0 : -64;
	// By default a white pixel is the highest height the map can store.
	double hi = mode == "set" ? 255.0 * (TheMapData->m_HeightmapScale > 0 ? TheMapData->m_HeightmapScale : 1.0) : 64;
	if (args.has("range")) {
		const McpJson &range = mcpArgArray(args, "range");
		if (range.size() != 2 || !range.at(0).isNumber() || !range.at(1).isNumber()) {
			mcpFail("range must be [low, high] in raw height units");
		}
		lo = range.at(0).asNumber();
		hi = range.at(1).asNumber();
	}
	const FitMode fit = parseFit(args);
	const Int smooth = mcpArgInt(args, "smooth", 0);
	if (smooth < 0 || smooth > 20) {
		mcpFail("smooth must be 0..20");
	}
	const VertexRegion r = getRegion(map, mcpArgBool(args, "include_border", true));

	Image img;
	img.load(mcpArgString(args, "path"), mcpArgString(args, "channel", "luma"));
	requireExactSize(fit, img, r.w, r.h, "heightmap vertex");

	// Compute the new heights in a float buffer so smoothing sees unclamped values.
	std::vector<double> heights(r.w * r.h);
	std::vector<unsigned char> touched(r.w * r.h, 0);
	Int numTouched = 0;
	for (Int y = 0; y < r.h; y++) {
		for (Int x = 0; x < r.w; x++) {
			const Int ndx = y * r.w + x;
			heights[ndx] = map->getHeight(r.x0 + x, r.y0 + y);
			double u, v;
			if (!mapAxis(fit, x, r.w, img.width(), false, &u) || !mapAxis(fit, r.h - 1 - y, r.h, img.height(), false, &v)) {
				continue;
			}
			const double value = lo + img.bilinear(u, v) * (hi - lo);
			heights[ndx] = mode == "set" ? value : heights[ndx] + value;
			touched[ndx] = 1;
			numTouched++;
		}
	}
	if (numTouched == 0) {
		mcpFail("the image does not overlap the map");
	}
	for (Int pass = 0; pass < smooth; pass++) {
		std::vector<double> prev = heights;
		for (Int y = 0; y < r.h; y++) {
			for (Int x = 0; x < r.w; x++) {
				if (!touched[y * r.w + x]) continue;
				double sum = 0;
				Int count = 0;
				for (Int dy = -1; dy <= 1; dy++) {
					for (Int dx = -1; dx <= 1; dx++) {
						const Int sx = x + dx, sy = y + dy;
						if (sx < 0 || sy < 0 || sx >= r.w || sy >= r.h) continue;
						sum += prev[sy * r.w + sx];
						count++;
					}
				}
				heights[y * r.w + x] = sum / count;
			}
		}
	}

	WorldHeightMapEdit *copy = map->duplicate();
	for (Int y = 0; y < r.h; y++) {
		for (Int x = 0; x < r.w; x++) {
			if (!touched[y * r.w + x]) continue;
			double h = floor(heights[y * r.w + x] + 0.5);
			if (h < 0) h = 0;
			if (h > WorldHeightMap::getMaxHeightValue()) h = WorldHeightMap::getMaxHeightValue();
			copy->setHeight(r.x0 + x, r.y0 + y, (Int)h);
		}
	}
	McpJson j = mcpCommitHeightMapEdit(copy, false);
	j.set("image_width", img.width()).set("image_height", img.height()).set("vertices_changed", numTouched);
	return j;
}

//-------------------------------------------------------------------------------------------------
// Cell masks
//-------------------------------------------------------------------------------------------------

} // namespace

/// True if the cell is covered by a water polygon whose surface is above the terrain.
bool mcpIsCellUnderWater(WorldHeightMapEdit *map, Int cx, Int cy)
{
	const Int border = map->getBorderSize();
	ICoord3D pt;
	pt.x = (Int)((cx - border + 0.5) * MAP_XY_FACTOR);
	pt.y = (Int)((cy - border + 0.5) * MAP_XY_FACTOR);
	pt.z = 0;
	const double ground = (map->getHeight(cx, cy) + map->getHeight(cx + 1, cy) + map->getHeight(cx, cy + 1) + map->getHeight(cx + 1, cy + 1)) / 4.0 * MAP_HEIGHT_SCALE;
	for (PolygonTrigger *trig = PolygonTrigger::getFirstPolygonTrigger(); trig; trig = trig->getNext()) {
		if (!trig->isWaterArea() || trig->getNumPoints() < 3) continue;
		if (trig->getPoint(0)->z > ground && trig->pointInTrigger(pt)) {
			return true;
		}
	}
	return false;
}

namespace
{

McpJson cmdExportMask(const McpJson &args)
{
	WorldHeightMapEdit *map = mcpHeightMap();
	const std::string kind = mcpArgString(args, "kind");
	Int texClass = -1;
	if (kind == "texture") {
		texClass = mcpFindTextureClass(args, "texture");
	} else if (kind != "passability" && kind != "water") {
		mcpFail("kind must be passability, texture or water");
	}
	const VertexRegion r = getRegion(map, mcpArgBool(args, "include_border", true));
	const Int cw = r.w - 1, ch = r.h - 1;
	std::string path = mcpArgString(args, "path", "");
	if (path.empty()) {
		path = defaultImagePath("mask");
	}

	std::vector<unsigned char> pixels(cw * ch);
	Int numSet = 0;
	for (Int y = 0; y < ch; y++) {
		for (Int x = 0; x < cw; x++) {
			const Int cx = r.x0 + x, cy = r.y0 + y;
			bool set;
			if (kind == "passability") set = map->getCliffState(cx, cy) != 0;
			else if (kind == "texture") set = map->getTextureClass(cx, cy, true) == texClass;
			else set = mcpIsCellUnderWater(map, cx, cy);
			pixels[(ch - 1 - y) * cw + x] = set ? 255 : 0;
			if (set) numSet++;
		}
	}
	writeGrayPng(path, cw, ch, pixels);
	return McpJson::makeObject().set("path", path).set("width", cw).set("height", ch).set("cells_set", numSet);
}

McpJson cmdImportMask(const McpJson &args)
{
	WorldHeightMapEdit *map = mcpHeightMap();
	const std::string target = mcpArgString(args, "target");
	Int texClass = -1;
	if (target == "texture") {
		texClass = mcpFindTextureClass(args, "texture");
		if (!map->isTexClassUsed(texClass) && !map->canFitTexture(texClass)) {
			mcpFail("the map has no room for another texture; paint over or replace an existing one first");
		}
	} else if (target != "passability") {
		mcpFail("target must be passability or texture");
	}
	const Int threshold = mcpArgInt(args, "threshold", 128);
	const bool invert = mcpArgBool(args, "invert", false);
	const bool impassable = mcpArgBool(args, "impassable", true);
	const FitMode fit = parseFit(args);
	const VertexRegion r = getRegion(map, mcpArgBool(args, "include_border", true));
	const Int cw = r.w - 1, ch = r.h - 1;

	Image img;
	img.load(mcpArgString(args, "path"), mcpArgString(args, "channel", "luma"));
	requireExactSize(fit, img, cw, ch, "cell");

	WorldHeightMapEdit *copy = map->duplicate();
	Int numSet = 0;
	bool tilesAdded = false;
	for (Int y = 0; y < ch; y++) {
		for (Int x = 0; x < cw; x++) {
			double u, v;
			if (!mapAxis(fit, x, cw, img.width(), true, &u) || !mapAxis(fit, ch - 1 - y, ch, img.height(), true, &v)) {
				continue;
			}
			// Masks are sampled nearest so edges stay crisp.
			bool set = img.at((int)floor(u + 0.5), (int)floor(v + 0.5)) * 255.0 >= threshold;
			if (invert) set = !set;
			if (!set) continue;
			const Int cx = r.x0 + x, cy = r.y0 + y;
			if (target == "passability") {
				copy->setCliff(cx, cy, impassable);
			} else if (copy->setTileNdx(cx, cy, texClass, false)) {
				tilesAdded = true;
			}
			numSet++;
		}
	}
	if (numSet == 0) {
		REF_PTR_RELEASE(copy);
		return McpJson::makeObject().set("changed", false).set("reason", "no mask pixels passed the threshold");
	}
	McpJson j = mcpCommitHeightMapEdit(copy, target == "texture" || tilesAdded);
	j.set("cells_set", numSet).set("image_width", img.width()).set("image_height", img.height());
	return j;
}

} // namespace

void mcpRegisterImageCommands()
{
	mcpRegisterCommand("terrain.export_heightmap", cmdExportHeightmap, "{path?,normalize?,include_border?} Writes the heightmap as an 8-bit grayscale PNG (north up, one pixel per vertex).");
	mcpRegisterCommand("terrain.import_heightmap", cmdImportHeightmap, "{path,mode?:set|add,range?:[lo,hi],channel?,fit?:stretch|exact|center,include_border?,smooth?} Loads heights from an image.");
	mcpRegisterCommand("terrain.export_mask", cmdExportMask, "{kind:passability|texture|water,texture?,path?,include_border?} Writes a black/white PNG, one pixel per cell.");
	mcpRegisterCommand("terrain.import_mask", cmdImportMask, "{path,target:passability|texture,texture?,threshold?,invert?,impassable?,channel?,fit?,include_border?} Applies a mask image to cells.");
}
