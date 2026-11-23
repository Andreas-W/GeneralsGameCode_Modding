// AssembledShaders.inl
// Generated. The pixel shaders the engine assembles at runtime (Create_Pixel_Shader_From_Asm in
// W3DWater.cpp and W3DProfilerFrameCapture.cpp), assembled once with the 32-bit D3DX8. Each source
// is normalized: comments removed, whitespace collapsed, empty lines dropped, lines joined with \n.
// After changing one of those shaders, assemble it again with D3DX8 and update its entry here.

static const DWORD g_shader0[] = {
	0xffff0101, 0x0009fffe, 0x58443344, 0x68532038, 0x72656461, 0x73734120, 0x6c626d65, 0x56207265,
	0x69737265, 0x30206e6f, 0x0031392e, 0x00000042, 0xb00f0000, 0x00000042, 0xb00f0001, 0x00000042,
	0xb00f0002, 0x00000042, 0xb00f0003, 0x00000005, 0x80070000, 0x90e40000, 0xb0e40000, 0x00000001,
	0x80080000, 0xb0e40000, 0x00000005, 0x800f0001, 0xb0e40001, 0xb0e40002, 0x00000002, 0x80070001,
	0x80e40001, 0xb0e40003, 0x00000005, 0x80070001, 0x80e40001, 0x90ff0000, 0x40000005, 0x80080000,
	0x80e40000, 0xb0e40003, 0x00000002, 0x80070000, 0x80e40000, 0x80e40001, 0x0000ffff
};
static const char g_source0[] = "ps.1.1\ntex t0\ntex t1\ntex t2\ntex t3\nmul r0.rgb, v0, t0\nmov r0.a, t0\nmul r1, t1, t2\nadd r1.rgb, r1, t3\nmul r1.rgb, r1, v0.a\n+mul r0.a, r0, t3\nadd r0.rgb, r0, r1";

static const DWORD g_shader1[] = {
	0xffff0101, 0x0009fffe, 0x58443344, 0x68532038, 0x72656461, 0x73734120, 0x6c626d65, 0x56207265,
	0x69737265, 0x30206e6f, 0x0031392e, 0x00000042, 0xb00f0000, 0x00000042, 0xb00f0001, 0x00000043,
	0xb00f0002, 0xb0e40001, 0x00000005, 0x800f0000, 0x90e40000, 0xb0e40000, 0x00000005, 0x80070001,
	0xb0e40002, 0xa0e40000, 0x00000002, 0x80070000, 0x80e40000, 0x80e40001, 0x0000ffff
};
static const char g_source1[] = "ps.1.1\ntex t0\ntex t1\ntexbem t2, t1\nmul r0,v0,t0\nmul r1.rgb,t2,c0\nadd r0.rgb, r0, r1";

static const DWORD g_shader2[] = {
	0xffff0101, 0x0009fffe, 0x58443344, 0x68532038, 0x72656461, 0x73734120, 0x6c626d65, 0x56207265,
	0x69737265, 0x30206e6f, 0x0031392e, 0x00000042, 0xb00f0000, 0x00000042, 0xb00f0001, 0x00000042,
	0xb00f0002, 0x00000042, 0xb00f0003, 0x00000005, 0x800f0000, 0x90e40000, 0xb0e40000, 0x00000004,
	0x80070000, 0xb0e40001, 0xb0e40002, 0x80e40000, 0x00000005, 0x80070000, 0x80e40000, 0xb0e40003,
	0x0000ffff
};
static const char g_source2[] = "ps.1.1\ntex t0\ntex t1\ntex t2\ntex t3\nmul r0,v0,t0\nmad r0.rgb, t1, t2, r0\nmul r0.rgb, r0, t3";

static const DWORD g_shader3[] = {
	0xffff0104, 0x0009fffe, 0x58443344, 0x68532038, 0x72656461, 0x73734120, 0x6c626d65, 0x56207265,
	0x69737265, 0x30206e6f, 0x0031392e, 0x00000042, 0x800f0000, 0xb0e40000, 0x00000001, 0x80080001,
	0x80000000, 0x00000001, 0x80080002, 0x80550000, 0x00000001, 0x80080003, 0x80aa0000, 0x00000005,
	0x80070000, 0x80ff0003, 0xa0e40000, 0x00000004, 0x80070000, 0x80ff0002, 0xa0e40001, 0x80e40000,
	0x00000004, 0x80070000, 0x80ff0001, 0xa0e40002, 0x80e40000, 0x0000ffff
};
static const char g_source3[] = "ps.1.4\ntexld r0, t0\nmov r1.a, r0.r\nmov r2.a, r0.g\nmov r3.a, r0.b\nmul r0.rgb, r3.a, c0\nmad r0.rgb, r2.a, c1, r0\nmad r0.rgb, r1.a, c2, r0";

static const AssembledShader g_assembledShaders[] = {
	{ g_source0, g_shader0, sizeof(g_shader0) },
	{ g_source1, g_shader1, sizeof(g_shader1) },
	{ g_source2, g_shader2, sizeof(g_shader2) },
	{ g_source3, g_shader3, sizeof(g_shader3) },
};
