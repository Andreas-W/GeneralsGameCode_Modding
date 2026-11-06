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

// Dear ImGui renderer backend for a Direct3D 8 device; see imgui_impl_dx8.h.
// Follows the structure of ImGui's own imgui_impl_dx9.cpp.

#include "imgui_impl_dx8.h"

#include <windows.h>
#include <d3d8.h>
#include <string.h>

namespace
{

struct BackendData
{
	IDirect3DDevice8 *device = nullptr;
	IDirect3DVertexBuffer8 *vb = nullptr;
	IDirect3DIndexBuffer8 *ib = nullptr;
	IDirect3DTexture8 *fontTexture = nullptr;
	int vertexBufferSize = 5000;
	int indexBufferSize = 10000;
};

struct Vertex
{
	float pos[3];
	D3DCOLOR col;
	float uv[2];
};

const DWORD VERTEX_FVF = D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1;

BackendData *getBackendData()
{
	return ImGui::GetCurrentContext() ? (BackendData *)ImGui::GetIO().BackendRendererUserData : nullptr;
}

// ImGui packs colors as RGBA in memory (ABGR as a 32 bit value); D3D wants ARGB.
D3DCOLOR toD3DColor(ImU32 col)
{
	return (col & 0xFF00FF00) | ((col & 0xFF0000) >> 16) | ((col & 0xFF) << 16);
}

void setIdentity(D3DMATRIX &m)
{
	memset(&m, 0, sizeof(m));
	m._11 = m._22 = m._33 = m._44 = 1.0f;
}

// Orthographic projection that maps the rectangle (l, t)-(r, b) in display pixels onto the viewport.
// The half pixel offset makes texels line up with pixels in D3D8.
void setProjection(IDirect3DDevice8 *device, float l, float t, float r, float b)
{
	l += 0.5f;
	r += 0.5f;
	t += 0.5f;
	b += 0.5f;
	D3DMATRIX proj = {
		2.0f / (r - l),    0.0f,              0.0f, 0.0f,
		0.0f,              2.0f / (t - b),    0.0f, 0.0f,
		0.0f,              0.0f,              0.5f, 0.0f,
		(l + r) / (l - r), (t + b) / (b - t), 0.5f, 1.0f
	};
	device->SetTransform(D3DTS_PROJECTION, &proj);
}

void setupRenderState(BackendData *bd)
{
	IDirect3DDevice8 *device = bd->device;

	device->SetPixelShader(0);
	device->SetVertexShader(VERTEX_FVF);
	device->SetStreamSource(0, bd->vb, sizeof(Vertex));
	device->SetIndices(bd->ib, 0);

	device->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
	device->SetRenderState(D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
	device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
	device->SetRenderState(D3DRS_ZENABLE, FALSE);
	device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
	device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
	device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	device->SetRenderState(D3DRS_FOGENABLE, FALSE);
	device->SetRenderState(D3DRS_RANGEFOGENABLE, FALSE);
	device->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
	device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
	device->SetRenderState(D3DRS_CLIPPING, TRUE);
	device->SetRenderState(D3DRS_LIGHTING, FALSE);
	device->SetRenderState(D3DRS_COLORVERTEX, TRUE);
	device->SetRenderState(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_COLOR1);
	device->SetRenderState(D3DRS_TEXTUREFACTOR, 0xFFFFFFFF);
	device->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);

	device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
	device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
	device->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
	device->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	device->SetTextureStageState(0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
	device->SetTextureStageState(0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
	device->SetTextureStageState(0, D3DTSS_MIPFILTER, D3DTEXF_NONE);
	device->SetTextureStageState(0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
	device->SetTextureStageState(0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
	device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
	device->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	device->SetTexture(1, nullptr);

	D3DMATRIX identity;
	setIdentity(identity);
	device->SetTransform(D3DTS_WORLD, &identity);
	device->SetTransform(D3DTS_VIEW, &identity);
}

bool createFontsTexture(BackendData *bd)
{
	ImGuiIO &io = ImGui::GetIO();
	unsigned char *pixels = nullptr;
	int width = 0;
	int height = 0;
	io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

	if (bd->fontTexture) {
		bd->fontTexture->Release();
		bd->fontTexture = nullptr;
	}
	if (FAILED(bd->device->CreateTexture(width, height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &bd->fontTexture)))
		return false;

	D3DLOCKED_RECT locked;
	if (FAILED(bd->fontTexture->LockRect(0, &locked, nullptr, 0)))
		return false;
	for (int y = 0; y < height; ++y) {
		const ImU32 *src = (const ImU32 *)(pixels + y * width * 4);
		D3DCOLOR *dst = (D3DCOLOR *)((unsigned char *)locked.pBits + y * locked.Pitch);
		for (int x = 0; x < width; ++x)
			dst[x] = toD3DColor(src[x]);
	}
	bd->fontTexture->UnlockRect(0);

	io.Fonts->SetTexID((ImTextureID)(intptr_t)bd->fontTexture);
	return true;
}

bool ensureBuffers(BackendData *bd, int vertexCount, int indexCount)
{
	if (!bd->vb || bd->vertexBufferSize < vertexCount) {
		if (bd->vb) {
			bd->vb->Release();
			bd->vb = nullptr;
		}
		bd->vertexBufferSize = vertexCount + 5000;
		if (FAILED(bd->device->CreateVertexBuffer(bd->vertexBufferSize * sizeof(Vertex), D3DUSAGE_WRITEONLY, VERTEX_FVF, D3DPOOL_MANAGED, &bd->vb)))
			return false;
	}
	if (!bd->ib || bd->indexBufferSize < indexCount) {
		if (bd->ib) {
			bd->ib->Release();
			bd->ib = nullptr;
		}
		bd->indexBufferSize = indexCount + 10000;
		const D3DFORMAT format = sizeof(ImDrawIdx) == 2 ? D3DFMT_INDEX16 : D3DFMT_INDEX32;
		if (FAILED(bd->device->CreateIndexBuffer(bd->indexBufferSize * sizeof(ImDrawIdx), D3DUSAGE_WRITEONLY, format, D3DPOOL_MANAGED, &bd->ib)))
			return false;
	}
	return true;
}

} // namespace

bool ImGui_ImplDX8_Init(IDirect3DDevice8 *device)
{
	ImGuiIO &io = ImGui::GetIO();
	IM_ASSERT(io.BackendRendererUserData == nullptr && "Already initialized a renderer backend!");

	BackendData *bd = IM_NEW(BackendData)();
	io.BackendRendererUserData = (void *)bd;
	io.BackendRendererName = "imgui_impl_dx8";
	// Vertex offsets are applied through SetIndices, so large meshes can be split into several draws.
	io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;

	bd->device = device;
	bd->device->AddRef();
	return true;
}

void ImGui_ImplDX8_Shutdown()
{
	BackendData *bd = getBackendData();
	IM_ASSERT(bd != nullptr && "No renderer backend to shutdown, or already shutdown?");
	ImGuiIO &io = ImGui::GetIO();

	ImGui_ImplDX8_InvalidateDeviceObjects();
	if (bd->device)
		bd->device->Release();
	io.BackendRendererName = nullptr;
	io.BackendRendererUserData = nullptr;
	io.BackendFlags &= ~ImGuiBackendFlags_RendererHasVtxOffset;
	IM_DELETE(bd);
}

bool ImGui_ImplDX8_CreateDeviceObjects()
{
	BackendData *bd = getBackendData();
	if (!bd || !bd->device)
		return false;
	return createFontsTexture(bd);
}

void ImGui_ImplDX8_InvalidateDeviceObjects()
{
	BackendData *bd = getBackendData();
	if (!bd || !bd->device)
		return;
	if (bd->vb) {
		bd->vb->Release();
		bd->vb = nullptr;
	}
	if (bd->ib) {
		bd->ib->Release();
		bd->ib = nullptr;
	}
	if (bd->fontTexture) {
		bd->fontTexture->Release();
		bd->fontTexture = nullptr;
		ImGui::GetIO().Fonts->SetTexID(0);
	}
}

void ImGui_ImplDX8_NewFrame()
{
	BackendData *bd = getBackendData();
	IM_ASSERT(bd != nullptr && "Context or backend not initialized! Did you call ImGui_ImplDX8_Init()?");
	if (!bd->fontTexture)
		ImGui_ImplDX8_CreateDeviceObjects();
}

void ImGui_ImplDX8_RenderDrawData(ImDrawData *drawData)
{
	// Avoid rendering when minimized.
	if (drawData->DisplaySize.x <= 0.0f || drawData->DisplaySize.y <= 0.0f)
		return;

	BackendData *bd = getBackendData();
	if (!bd || !ensureBuffers(bd, drawData->TotalVtxCount, drawData->TotalIdxCount))
		return;
	IDirect3DDevice8 *device = bd->device;

	// Save the whole device state; the engine caches its own states and must find them unchanged.
	DWORD stateBlock = 0;
	if (FAILED(device->CreateStateBlock(D3DSBT_ALL, &stateBlock)))
		return;
	D3DVIEWPORT8 lastViewport;
	device->GetViewport(&lastViewport);
	D3DMATRIX lastWorld;
	D3DMATRIX lastView;
	D3DMATRIX lastProjection;
	device->GetTransform(D3DTS_WORLD, &lastWorld);
	device->GetTransform(D3DTS_VIEW, &lastView);
	device->GetTransform(D3DTS_PROJECTION, &lastProjection);

	// Copy all vertices and indices into the single vertex and index buffer.
	Vertex *vtxDst = nullptr;
	ImDrawIdx *idxDst = nullptr;
	if (FAILED(bd->vb->Lock(0, (UINT)(drawData->TotalVtxCount * sizeof(Vertex)), (BYTE **)&vtxDst, 0))) {
		device->DeleteStateBlock(stateBlock);
		return;
	}
	if (FAILED(bd->ib->Lock(0, (UINT)(drawData->TotalIdxCount * sizeof(ImDrawIdx)), (BYTE **)&idxDst, 0))) {
		bd->vb->Unlock();
		device->DeleteStateBlock(stateBlock);
		return;
	}
	for (int n = 0; n < drawData->CmdListsCount; n++) {
		const ImDrawList *drawList = drawData->CmdLists[n];
		const ImDrawVert *vtxSrc = drawList->VtxBuffer.Data;
		for (int i = 0; i < drawList->VtxBuffer.Size; i++) {
			vtxDst->pos[0] = vtxSrc->pos.x;
			vtxDst->pos[1] = vtxSrc->pos.y;
			vtxDst->pos[2] = 0.0f;
			vtxDst->col = toD3DColor(vtxSrc->col);
			vtxDst->uv[0] = vtxSrc->uv.x;
			vtxDst->uv[1] = vtxSrc->uv.y;
			vtxDst++;
			vtxSrc++;
		}
		memcpy(idxDst, drawList->IdxBuffer.Data, drawList->IdxBuffer.Size * sizeof(ImDrawIdx));
		idxDst += drawList->IdxBuffer.Size;
	}
	bd->vb->Unlock();
	bd->ib->Unlock();

	setupRenderState(bd);

	const ImVec2 clipOff = drawData->DisplayPos;
	const float displayW = drawData->DisplaySize.x;
	const float displayH = drawData->DisplaySize.y;
	int globalVtxOffset = 0;
	int globalIdxOffset = 0;
	for (int n = 0; n < drawData->CmdListsCount; n++) {
		const ImDrawList *drawList = drawData->CmdLists[n];
		for (int cmdIndex = 0; cmdIndex < drawList->CmdBuffer.Size; cmdIndex++) {
			const ImDrawCmd *cmd = &drawList->CmdBuffer[cmdIndex];
			if (cmd->UserCallback != nullptr) {
				if (cmd->UserCallback == ImDrawCallback_ResetRenderState)
					setupRenderState(bd);
				else
					cmd->UserCallback(drawList, cmd);
				continue;
			}

			// Clip rectangle in framebuffer pixels, limited to the display.
			float l = cmd->ClipRect.x - clipOff.x;
			float t = cmd->ClipRect.y - clipOff.y;
			float r = cmd->ClipRect.z - clipOff.x;
			float b = cmd->ClipRect.w - clipOff.y;
			if (l < 0.0f) l = 0.0f;
			if (t < 0.0f) t = 0.0f;
			if (r > displayW) r = displayW;
			if (b > displayH) b = displayH;
			const DWORD x = (DWORD)l;
			const DWORD y = (DWORD)t;
			const DWORD w = (DWORD)r > x ? (DWORD)r - x : 0;
			const DWORD h = (DWORD)b > y ? (DWORD)b - y : 0;
			if (w == 0 || h == 0)
				continue;

			// No scissor test in D3D8: draw into a viewport of the clip rectangle with a projection of the same rectangle.
			D3DVIEWPORT8 vp = { x, y, w, h, 0.0f, 1.0f };
			device->SetViewport(&vp);
			setProjection(device, (float)x + clipOff.x, (float)y + clipOff.y, (float)(x + w) + clipOff.x, (float)(y + h) + clipOff.y);

			device->SetTexture(0, (IDirect3DTexture8 *)(intptr_t)cmd->GetTexID());
			device->SetIndices(bd->ib, cmd->VtxOffset + globalVtxOffset);
			device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, (UINT)drawList->VtxBuffer.Size - cmd->VtxOffset,
				cmd->IdxOffset + globalIdxOffset, cmd->ElemCount / 3);
		}
		globalIdxOffset += drawList->IdxBuffer.Size;
		globalVtxOffset += drawList->VtxBuffer.Size;
	}

	// Restore the engine's state.
	device->SetTexture(0, nullptr);
	device->ApplyStateBlock(stateBlock);
	device->DeleteStateBlock(stateBlock);
	device->SetViewport(&lastViewport);
	device->SetTransform(D3DTS_WORLD, &lastWorld);
	device->SetTransform(D3DTS_VIEW, &lastView);
	device->SetTransform(D3DTS_PROJECTION, &lastProjection);
}
