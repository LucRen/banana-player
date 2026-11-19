// The native visualizers on the top panel (topscreen.h, docs/notes/2026-11-19-viz-native.md): mpv's picture first (the
// theme's ground from the blank graph, the art overlay, the song's lines), the style's shapes over it in plain GL (one
// small shader, coloured triangles: Mirror bars is 41 quads), then the panel presented. Nothing else swaps the top
// window while the style is up, so this path's own swap is what puts each frame on the glass (KMSDRM: Lite's lesson).
#include "player.h"
#include "theme.h"
#include "topscreen.h"
#include "viznative.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <type_traits>
#include <vector>

namespace bp
{
	namespace
	{
		constexpr unsigned GL_TRIANGLES_ = 0x0004, GL_FLOAT_ = 0x1406, GL_ARRAY_BUFFER_ = 0x8892, GL_STREAM_DRAW_ = 0x88E0,
			GL_BLEND_ = 0x0BE2, GL_DEPTH_TEST_ = 0x0B71, GL_SCISSOR_TEST_ = 0x0C11, GL_CULL_FACE_ = 0x0B44,
			GL_VERTEX_SHADER_ = 0x8B31, GL_FRAGMENT_SHADER_ = 0x8B30, GL_COMPILE_STATUS_ = 0x8B81, GL_LINK_STATUS_ = 0x8B82;

		struct VizGl
		{
			bool tried = false, ok = false;
			unsigned prog = 0, vbo = 0, vao = 0;
			int aPos = -1, aCol = -1;
#define VIZ_GL(ret, name, ...) ret (*name)(__VA_ARGS__) = nullptr;
			VIZ_GL(unsigned, CreateShader, unsigned)
			VIZ_GL(void, ShaderSource, unsigned, int, const char* const*, const int*)
			VIZ_GL(void, CompileShader, unsigned)
			VIZ_GL(void, GetShaderiv, unsigned, unsigned, int*)
			VIZ_GL(void, GetShaderInfoLog, unsigned, int, int*, char*)
			VIZ_GL(void, DeleteShader, unsigned)
			VIZ_GL(unsigned, CreateProgram)
			VIZ_GL(void, AttachShader, unsigned, unsigned)
			VIZ_GL(void, LinkProgram, unsigned)
			VIZ_GL(void, GetProgramiv, unsigned, unsigned, int*)
			VIZ_GL(void, UseProgram, unsigned)
			VIZ_GL(int, GetAttribLocation, unsigned, const char*)
			VIZ_GL(void, GenBuffers, int, unsigned*)
			VIZ_GL(void, BindBuffer, unsigned, unsigned)
			VIZ_GL(void, BufferData, unsigned, long, const void*, unsigned)
			VIZ_GL(void, EnableVertexAttribArray, unsigned)
			VIZ_GL(void, DisableVertexAttribArray, unsigned)
			VIZ_GL(void, VertexAttribPointer, unsigned, int, unsigned, unsigned char, int, const void*)
			VIZ_GL(void, DrawArrays, unsigned, int, int)
			VIZ_GL(void, Viewport, int, int, int, int)
			VIZ_GL(void, Disable, unsigned)
			VIZ_GL(void, GenVertexArrays, int, unsigned*)
			VIZ_GL(void, BindVertexArray, unsigned)
#undef VIZ_GL
		};

		// made once, in the top panel's context (current when called)
		VizGl& vizGl()
		{
			static VizGl g;
			if (g.tried) return g;
			g.tried = true;
			bool ok = true;
			auto load = [&](auto& fn, const char* name, bool need) {
				fn = (std::remove_reference_t<decltype(fn)>)SDL_GL_GetProcAddress(name);
				if (!fn && need)
				{
					fprintf(stderr, "viz-native: no %s: the bars can't be drawn\n", name);
					ok = false;
				}
			};
#define VIZ_LOAD(name, need) load(g.name, "gl" #name, need);
			VIZ_LOAD(CreateShader, true)
			VIZ_LOAD(ShaderSource, true)
			VIZ_LOAD(CompileShader, true)
			VIZ_LOAD(GetShaderiv, true)
			VIZ_LOAD(GetShaderInfoLog, true)
			VIZ_LOAD(DeleteShader, true)
			VIZ_LOAD(CreateProgram, true)
			VIZ_LOAD(AttachShader, true)
			VIZ_LOAD(LinkProgram, true)
			VIZ_LOAD(GetProgramiv, true)
			VIZ_LOAD(UseProgram, true)
			VIZ_LOAD(GetAttribLocation, true)
			VIZ_LOAD(GenBuffers, true)
			VIZ_LOAD(BindBuffer, true)
			VIZ_LOAD(BufferData, true)
			VIZ_LOAD(EnableVertexAttribArray, true)
			VIZ_LOAD(DisableVertexAttribArray, true)
			VIZ_LOAD(VertexAttribPointer, true)
			VIZ_LOAD(DrawArrays, true)
			VIZ_LOAD(Viewport, true)
			VIZ_LOAD(Disable, true)
#ifdef BP_GLES
			VIZ_LOAD(GenVertexArrays, false)
			VIZ_LOAD(BindVertexArray, false)
#else
			VIZ_LOAD(GenVertexArrays, true)	// (a core profile draws nothing without one)
			VIZ_LOAD(BindVertexArray, true)
#endif
#undef VIZ_LOAD
			if (!ok) return g;
#ifdef BP_GLES
			const char* vs = "#version 100\nattribute vec2 pos; attribute vec3 col; varying vec3 c;\n"
							 "void main() { c = col; gl_Position = vec4(pos, 0.0, 1.0); }\n";
			const char* fs = "#version 100\nprecision mediump float; varying vec3 c;\nvoid main() { gl_FragColor = vec4(c, 1.0); }\n";
#else
			const char* vs = "#version 150\nin vec2 pos; in vec3 col; out vec3 c;\nvoid main() { c = col; gl_Position = vec4(pos, 0.0, 1.0); }\n";
			const char* fs = "#version 150\nin vec3 c; out vec4 o;\nvoid main() { o = vec4(c, 1.0); }\n";
#endif
			auto compile = [&](unsigned kind, const char* src) -> unsigned {
				unsigned s = g.CreateShader(kind);
				g.ShaderSource(s, 1, &src, nullptr);
				g.CompileShader(s);
				int done = 0;
				g.GetShaderiv(s, GL_COMPILE_STATUS_, &done);
				if (!done)
				{
					char log[512] = {0};
					g.GetShaderInfoLog(s, sizeof log, nullptr, log);
					fprintf(stderr, "viz-native: shader: %s\n", log);
					g.DeleteShader(s);
					return 0;
				}
				return s;
			};
			unsigned v = compile(GL_VERTEX_SHADER_, vs), f = compile(GL_FRAGMENT_SHADER_, fs);
			if (!v || !f) return g;
			g.prog = g.CreateProgram();
			g.AttachShader(g.prog, v);
			g.AttachShader(g.prog, f);
			g.LinkProgram(g.prog);
			g.DeleteShader(v);
			g.DeleteShader(f);
			int linked = 0;
			g.GetProgramiv(g.prog, GL_LINK_STATUS_, &linked);
			if (!linked)
			{
				fprintf(stderr, "viz-native: the shader didn't link\n");
				return g;
			}
			g.aPos = g.GetAttribLocation(g.prog, "pos");
			g.aCol = g.GetAttribLocation(g.prog, "col");
			g.GenBuffers(1, &g.vbo);
			if (g.GenVertexArrays) g.GenVertexArrays(1, &g.vao);
			g.ok = g.aPos >= 0 && g.aCol >= 0;
			fprintf(stderr, "viz-native: GL ready%s\n", g.ok ? "" : " but the shader has no attributes: no bars");
			return g;
		}
	}

	void TopScreen::drawNative(Player& shown, std::atomic<bool>& renderPending, bool refresh)
	{
		// mpv's own news (a 5 fps ground frame, the lines or a card changed) and the tap's (new bars)
		const bool mpvFrame = renderPending.exchange(false) && shown.frameDue();
		const bool fresh = shown.nativeTake(nativeTargets);
		const bool first = !nativeUp;
		if (!mpvFrame && !fresh && !refresh && !first) return;
		const Uint32 now = SDL_GetTicks();
		const double dt = first ? 0 : (now - nativeAt) / 1000.0;
		nativeAt = now;
		if (first)
		{
			std::fill(nativeLevels, nativeLevels + VN_BARS, 0.0f);
			std::fill(nativeTargets, nativeTargets + VN_BARS, 0.0f);
			fprintf(stderr, "viz-native: the top panel is the player's: mpv's ground and lines, the bars over them, swapped here\n");
		}
		nativeUp = true;
		// paused: the bars stay as they were (the graph's picture froze too); mpv may still ask for a frame (a card)
		if (!shown.paused)
			for (int i = 0; i < VN_BARS; i++) nativeLevels[i] = vizEase(nativeLevels[i], nativeTargets[i], dt);
		SDL_GL_MakeCurrent(top, gl);
		int w = 0, h = 0;
		SDL_GL_GetDrawableSize(top, &w, &h);
		nativeFrame(shown, w, h);
		SDL_GL_SwapWindow(top);
		shown.reportSwap();
		shown.nativeDrew();
	}

	void TopScreen::nativeFrame(Player& shown, int w, int h)
	{
		shown.render(w, h, 0, false, true);	// the ground, the art, the lines (mpv's last frame again when it has no new one)
		VizGl& g = vizGl();
		if (!g.ok) return;
		std::vector<VizRect> rects;
		mirrorBarsRects(nativeLevels, VN_BARS, shown.nativeBand(), rects);
		// a card up (volume, a state, a note): mpv drew it with its picture, under the bars; they go round it instead
		for (const VizRect& card : shown.cardsUp()) vizCutOut(rects, card);
		// the theme's inks, read each frame: a theme change shows at once
		const theme::Rgb inks[2] = {theme::ACCENT, theme::DIM};
		std::vector<float> v;
		v.reserve(rects.size() * 30);
		const float sx = w / 640.0f, sy = h / 480.0f;
		for (const VizRect& r : rects)
		{
			// whole pixels, so each bar keeps its width across the row
			const float x0 = std::round(r.x0 * sx) / w * 2 - 1, x1 = std::round(r.x1 * sx) / w * 2 - 1;
			const float y0 = 1 - std::round(r.y0 * sy) / h * 2, y1 = 1 - std::round(r.y1 * sy) / h * 2;
			const theme::Rgb c = inks[r.ink];
			const float cr = c.r / 255.0f, cg = c.g / 255.0f, cb = c.b / 255.0f;
			const float q[6][2] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y0}, {x1, y1}, {x0, y1}};
			for (auto& p : q) v.insert(v.end(), {p[0], p[1], cr, cg, cb});
		}
		g.Viewport(0, 0, w, h);
		g.Disable(GL_DEPTH_TEST_);
		g.Disable(GL_SCISSOR_TEST_);
		g.Disable(GL_CULL_FACE_);
		g.Disable(GL_BLEND_);
		g.UseProgram(g.prog);
		if (g.BindVertexArray) g.BindVertexArray(g.vao);
		g.BindBuffer(GL_ARRAY_BUFFER_, g.vbo);
		g.BufferData(GL_ARRAY_BUFFER_, (long)(v.size() * sizeof(float)), v.data(), GL_STREAM_DRAW_);
		g.EnableVertexAttribArray((unsigned)g.aPos);
		g.EnableVertexAttribArray((unsigned)g.aCol);
		g.VertexAttribPointer((unsigned)g.aPos, 2, GL_FLOAT_, 0, 5 * sizeof(float), nullptr);
		g.VertexAttribPointer((unsigned)g.aCol, 3, GL_FLOAT_, 0, 5 * sizeof(float), (const void*)(2 * sizeof(float)));
		g.DrawArrays(GL_TRIANGLES_, 0, (int)(v.size() / 5));
		// (mpv's next render expects the default bindings)
		g.DisableVertexAttribArray((unsigned)g.aPos);
		g.DisableVertexAttribArray((unsigned)g.aCol);
		g.BindBuffer(GL_ARRAY_BUFFER_, 0);
		if (g.BindVertexArray) g.BindVertexArray(0);
		g.UseProgram(0);
	}
}
