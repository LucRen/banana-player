// The native visualizers on the top panel (topscreen.h, docs/notes/2026-11-19-viz-native.md): mpv's picture first (the
// theme's ground from the blank graph, the art overlay, the song's lines), the style's shapes over it in plain GL (one
// small shader, coloured triangles in one buffer a frame), then the panel presented. Nothing else swaps the top
// window while the style is up, so this path's own swap is what puts each frame on the glass (KMSDRM: Lite's lesson).
#include "player.h"
#include "settings.h"
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
			GL_VERTEX_SHADER_ = 0x8B31, GL_FRAGMENT_SHADER_ = 0x8B30, GL_COMPILE_STATUS_ = 0x8B81, GL_LINK_STATUS_ = 0x8B82,
			GL_SRC_ALPHA_ = 0x0302, GL_ONE_MINUS_SRC_ALPHA_ = 0x0303;

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
			VIZ_GL(void, Enable, unsigned)
			VIZ_GL(void, BlendFunc, unsigned, unsigned)
			VIZ_GL(void, Scissor, int, int, int, int)
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
			VIZ_LOAD(Enable, true)
			VIZ_LOAD(BlendFunc, true)
			VIZ_LOAD(Scissor, true)
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
			const char* vs = "#version 100\nattribute vec2 pos; attribute vec4 col; varying vec4 c;\n"
							 "void main() { c = col; gl_Position = vec4(pos, 0.0, 1.0); }\n";
			const char* fs = "#version 100\nprecision mediump float; varying vec4 c;\nvoid main() { gl_FragColor = c; }\n";
#else
			const char* vs = "#version 150\nin vec2 pos; in vec4 col; out vec4 c;\nvoid main() { c = col; gl_Position = vec4(pos, 0.0, 1.0); }\n";
			const char* fs = "#version 150\nin vec4 c; out vec4 o;\nvoid main() { o = c; }\n";
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
		// mpv's own news (a 5 fps ground frame, the lines or a card changed) and the tap's (new levels)
		const bool mpvFrame = renderPending.exchange(false) && shown.frameDue();
		float got[VN_MAX_BARS];
		int vis = -1, n = 0;
		VizHint hint;
		bool fresh = shown.nativeTake(got, vis, n, hint);
		const bool first = !nativeUp;
		// another native style chosen: its own state at once (paused too), the tap's levels for it from its next look
		const bool restyled = nativeState.vis != shown.nativeStyle();
		if (first || restyled) nativeState.reset(shown.nativeStyle());
		if (fresh && vis == nativeState.vis && n == nativeState.n)
		{
			std::copy(got, got + n, nativeState.targets);
			nativeState.loud = hint.loud;
			nativeState.beats = hint.beats;
		}
		else fresh = false;
		if (!mpvFrame && !fresh && !refresh && !first && !restyled) return;
		const Uint32 now = SDL_GetTicks();
		const double dt = first ? 0 : (now - nativeAt) / 1000.0;
		nativeAt = now;
		if (first) fprintf(stderr, "viz-native: the top panel is the player's: mpv's ground and lines, the style over them, swapped here\n");
		nativeUp = true;
		// paused: the shapes stay as they were (the graph's picture froze too); mpv may still ask for a frame (a card)
		if (!shown.paused) vizStep(nativeState, dt);
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
		std::vector<VizRect> shapes;
		vizShapes(nativeState, shown.nativeBand(), shapes);
		// the theme's inks, read each frame: a theme change shows at once (Pocket console's four shades too)
		const PocketInks pk = nativeState.vis == VIS_POCKET ? pocketInks() : PocketInks{};
		auto inkOf = [&pk](const VizRect& r, float* c) {
			theme::Rgb k;
			switch (r.ink)
			{
			case VI_P_INK: k = pk.ink; break;
			case VI_P_MID: k = pk.mid; break;
			case VI_P_SCREEN: k = pk.screen; break;
			case VI_P_GRID: k = pk.grid; break;
			case VI_P_LIGHT: k = pk.light; break;
			case VI_P_BRIGHT: k = pk.bright; break;
			case VI_DIM: k = theme::DIM; break;
			case VI_BUTTON: k = theme::BUTTON; break;
			case VI_TEXT: k = theme::TEXT; break;
			case VI_BG: k = theme::BG; break;
			case VI_WHITE: k = {255, 255, 255}; break;
			case VI_RAMP:
			{
				const theme::Rgb a = theme::BUTTON, b = theme::ACCENT;
				c[0] = (a.r + (b.r - a.r) * r.mix) / 255.0f, c[1] = (a.g + (b.g - a.g) * r.mix) / 255.0f, c[2] = (a.b + (b.b - a.b) * r.mix) / 255.0f;
				c[3] = r.alpha;
				return;
			}
			default: k = theme::ACCENT; break;
			}
			c[0] = k.r / 255.0f, c[1] = k.g / 255.0f, c[2] = k.b / 255.0f, c[3] = r.alpha;
		};
		std::vector<float> v;
		v.reserve(shapes.size() * 36);
		const float sx = w / 640.0f, sy = h / 480.0f;
		auto vert = [&](float x, float y, const float* c) {
			v.insert(v.end(), {x / w * 2 - 1, 1 - y / h * 2, c[0], c[1], c[2], c[3]});
		};
		for (const VizRect& r : shapes)
		{
			float c[4];
			inkOf(r, c);
			if (r.kind == 1)
			{
				// a ring (or a disc) round (x0, y0) from radius x1 to y1: a strip of quads, ~3 px a segment
				const float cx = r.x0 * sx, cy = r.y0 * sy, r0 = r.x1 * sy, r1 = r.y1 * sy;
				// (a small one, a spark or a bead: fewer sides, it's a few pixels across)
				const int seg = std::clamp((int)(r1 * 2.1f), r1 < 8 ? 8 : 24, 256);
				float pc = 1, ps = 0;
				for (int i = 1; i <= seg; i++)
				{
					const float a = (float)(2 * M_PI * i / seg), qc = std::cos(a), qs = std::sin(a);
					vert(cx + pc * r1, cy + ps * r1, c);
					vert(cx + qc * r1, cy + qs * r1, c);
					vert(cx + qc * r0, cy + qs * r0, c);
					if (r0 > 0)
					{
						vert(cx + pc * r1, cy + ps * r1, c);
						vert(cx + qc * r0, cy + qs * r0, c);
						vert(cx + pc * r0, cy + ps * r0, c);
					}
					pc = qc, ps = qs;
				}
				continue;
			}
			if (r.kind == 2)
			{
				// a quad: two triangles, its second pair of corners at alpha2
				float c2[4] = {c[0], c[1], c[2], r.alpha2};
				vert(r.x0 * sx, r.y0 * sy, c), vert(r.x1 * sx, r.y1 * sy, c), vert(r.x2 * sx, r.y2 * sy, c2);
				vert(r.x0 * sx, r.y0 * sy, c), vert(r.x2 * sx, r.y2 * sy, c2), vert(r.x3 * sx, r.y3 * sy, c2);
				continue;
			}
			// whole pixels, so each bar keeps its width across the row
			const float x0 = std::round(r.x0 * sx), x1 = std::round(r.x1 * sx), y0 = std::round(r.y0 * sy), y1 = std::round(r.y1 * sy);
			if (x1 <= x0 || y1 <= y0) continue;
			vert(x0, y0, c), vert(x1, y0, c), vert(x1, y1, c), vert(x0, y0, c), vert(x1, y1, c), vert(x0, y1, c);
		}
		// where they may go: the panel less the song's lines (alone) and mpv's cards, which mpv drew under them: a
		// scissor pass for each free box (a few draws of the same buffer, nothing cut shape by shape)
		std::vector<VizRect> free = {{0, 0, 640, 480, 0}};
		for (const VizRect& keep : shown.nativeKeepClear()) vizCutOut(free, keep);
		// in the band, nothing over the art's part (Grid's far ridges, Ribbon's glow reach up); Sparks and Starfield
		// take the whole panel there, kept off the art and the lines by their boxes (nativeKeepClear)
		if (shown.nativeBand() && !vizFullPanel(nativeState.vis)) vizCutOut(free, {0, 0, 640, 280, 0});
		g.Viewport(0, 0, w, h);
		g.Disable(GL_DEPTH_TEST_);
		g.Disable(GL_CULL_FACE_);
		g.Enable(GL_BLEND_);
		g.BlendFunc(GL_SRC_ALPHA_, GL_ONE_MINUS_SRC_ALPHA_);
		g.UseProgram(g.prog);
		if (g.BindVertexArray) g.BindVertexArray(g.vao);
		g.BindBuffer(GL_ARRAY_BUFFER_, g.vbo);
		g.BufferData(GL_ARRAY_BUFFER_, (long)(v.size() * sizeof(float)), v.data(), GL_STREAM_DRAW_);
		g.EnableVertexAttribArray((unsigned)g.aPos);
		g.EnableVertexAttribArray((unsigned)g.aCol);
		g.VertexAttribPointer((unsigned)g.aPos, 2, GL_FLOAT_, 0, 6 * sizeof(float), nullptr);
		g.VertexAttribPointer((unsigned)g.aCol, 4, GL_FLOAT_, 0, 6 * sizeof(float), (const void*)(2 * sizeof(float)));
		const bool whole = free.size() == 1 && free[0].x0 == 0 && free[0].y0 == 0 && free[0].x1 == 640 && free[0].y1 == 480;
		if (whole) g.Disable(GL_SCISSOR_TEST_);
		else g.Enable(GL_SCISSOR_TEST_);
		for (const VizRect& f : free)
		{
			if (!whole)
			{
				// (GL's rows go up)
				const int x0 = (int)std::round(f.x0 * sx), x1 = (int)std::round(f.x1 * sx), y0 = (int)std::round(f.y0 * sy), y1 = (int)std::round(f.y1 * sy);
				if (x1 <= x0 || y1 <= y0) continue;
				g.Scissor(x0, h - y1, x1 - x0, y1 - y0);
			}
			g.DrawArrays(GL_TRIANGLES_, 0, (int)(v.size() / 6));
		}
		// (mpv's next render expects the default state)
		g.Disable(GL_SCISSOR_TEST_);
		g.Disable(GL_BLEND_);
		g.DisableVertexAttribArray((unsigned)g.aPos);
		g.DisableVertexAttribArray((unsigned)g.aCol);
		g.BindBuffer(GL_ARRAY_BUFFER_, 0);
		if (g.BindVertexArray) g.BindVertexArray(0);
		g.UseProgram(0);
	}
}
