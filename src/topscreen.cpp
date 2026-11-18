// The top panel's own state and what main asks of it (topscreen.h). The bodies here were main's: the reader and text
// viewer, the frame taken from mpv under them, the level card over them, and the panel's drawing and screenshots.
#include "topscreen.h"
#include "epub.h"
#include "javaext.h"
#include "levelcard.h"
#include "manganet.h"
#include "player.h"
#include "supportqr.h"
#include "textsource.h"
#include "theme.h"
#include "ui.h"
#include "ui_docs.h"
#include "ui_manga.h"
#include "ui_reader.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <strings.h>
#include <type_traits>
#include <utility>
#include <vector>

namespace bp
{
	TopScreen::TopScreen(SDL_Window* win, SDL_GLContext ctx, std::string fonts)
		: top(win), gl(ctx), fontsDir(std::move(fonts)), evReader(SDL_RegisterEvents(1))
	{
	}

	bool TopScreen::onTop(const Ui& ui) const
	{
		return (reader.isOpen() || textReader.isOpen() || ui.aboutQrUp()) && !ui.topShowsPlayer();
	}

	void TopScreen::drainUnder(Player& player, Player& photos, bool readerOnTop)
	{
		if ((!photos.playing() && !readerOnTop) || !player.playing() || !player.frameDue()) return;
		SDL_GL_MakeCurrent(top, gl);
		if (!sinkFbo)
		{
			using Gen = void (*)(int, unsigned*);
			using Bind = void (*)(unsigned, unsigned);
			using TexImage = void (*)(unsigned, int, int, int, int, int, unsigned, unsigned, const void*);
			using FbTex = void (*)(unsigned, unsigned, unsigned, unsigned, int);
			auto genFb = (Gen)SDL_GL_GetProcAddress("glGenFramebuffers");
			auto bindFb = (Bind)SDL_GL_GetProcAddress("glBindFramebuffer");
			auto genTex = (Gen)SDL_GL_GetProcAddress("glGenTextures");
			auto bindTex = (Bind)SDL_GL_GetProcAddress("glBindTexture");
			auto texImage = (TexImage)SDL_GL_GetProcAddress("glTexImage2D");
			auto fbTex = (FbTex)SDL_GL_GetProcAddress("glFramebufferTexture2D");
			if (!genFb || !bindFb || !genTex || !bindTex || !texImage || !fbTex) return;
			unsigned tex = 0;
			genTex(1, &tex);
			bindTex(0x0DE1 /* GL_TEXTURE_2D */, tex);
			texImage(0x0DE1, 0, 0x1908 /* GL_RGBA */, 16, 16, 0, 0x1908, 0x1401 /* GL_UNSIGNED_BYTE */, nullptr);
			bindTex(0x0DE1, 0);
			genFb(1, &sinkFbo);
			bindFb(0x8D40 /* GL_FRAMEBUFFER */, sinkFbo);
			fbTex(0x8D40, 0x8CE0 /* GL_COLOR_ATTACHMENT0 */, 0x0DE1, tex, 0);
			bindFb(0x8D40, 0);
		}
		// unseen: taken, not drawn (MPV_RENDER_PARAM_SKIP_RENDERING), and not waited for; a slow take is logged (it
		// would show as a stutter in the reader or a photo's pan while music plays)
		Uint32 t0 = SDL_GetTicks();
		player.render(16, 16, sinkFbo, true);
		player.reportSwap();
		Uint32 took = SDL_GetTicks() - t0;
		if (took > 8 && SDL_GetTicks() - slowLogged > 5000)
		{
			slowLogged = SDL_GetTicks();
			fprintf(stderr, "timing: a frame under the pages took %u ms to take\n", took);
		}
	}

	SDL_Surface* TopScreen::capture(Player& shown, bool readerOnTop, bool qr)
	{
		SDL_GL_MakeCurrent(top, gl);
		int w, h;
		SDL_GL_GetDrawableSize(top, &w, &h);
		using ReadPixels = void (*)(int, int, int, int, unsigned, unsigned, void*);
		auto readPixels = (ReadPixels)SDL_GL_GetProcAddress("glReadPixels");
		if (!readPixels) return nullptr;
		if (qr && drawQr(w, h)) {}	// About's support QR, large
		else if (reader.isOpen() && readerOnTop) reader.draw(w, h);	// a manga chapter's pages
		else if (textReader.isOpen() && readerOnTop) textReader.draw(w, h);	// a document's text
		else if (shown.liteShown()) drawLiteFrame(shown, SDL_GetTicks(), true);	// Lite's own glow and bars
		else shown.render(w, h);	// a video, a photo, or the icon with nothing playing
		std::vector<unsigned char> rgba((size_t)w * h * 4);
		readPixels(0, 0, w, h, 0x1908 /* GL_RGBA */, 0x1401 /* GL_UNSIGNED_BYTE */, rgba.data());
		SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ABGR8888);
		if (s)
			for (int y = 0; y < h; y++)	// GL's rows go up
				memcpy((unsigned char*)s->pixels + (size_t)y * s->pitch, &rgba[(size_t)(h - 1 - y) * w * 4], (size_t)w * 4);
		SDL_GL_SwapWindow(top);
		shown.reportSwap();
		return s;
	}

	bool TopScreen::drawQr(int w, int h)
	{
		SDL_GL_MakeCurrent(top, gl);
		if (!qrReady)
		{
#ifdef BP_GLES
			qrReady = qrRender.init([](const char* n) { return SDL_GL_GetProcAddress(n); }, true);
#else
			qrReady = qrRender.init([](const char* n) { return SDL_GL_GetProcAddress(n); }, false);
#endif
			qrRender.setUploadAll(true);	// (two tiles: up in the frame that shows it)
		}
		if (!qrReady) return false;
		if (!qrUp || qrW != w || qrH != h)
		{
			// a new panel size (or the first time): the picture at its pixels, so the code lands on whole ones
			SupportQrImage img = composeSupportQr(fontsDir, w, h);
			qrW = w;
			qrH = h;	// (a failure is not retried every pass: refresh asks again)
			if (!img.ok()) return false;
			qrRender.addPage(0, img.w, img.h, std::make_shared<const std::vector<uint8_t>>(std::move(img.rgba)));
			qrUp = true;
		}
		qrRender.draw({{0, 0, 0, (float)w, (float)h}}, w, h);
		return true;
	}

	void TopScreen::cardOnReader(const std::string& label, int percent, bool bar, int barTop, int ms)
	{
		int tw = 0, th = 0;
		SDL_GL_GetDrawableSize(top, &tw, &th);
		LevelCardImage c = drawLevelCard(fontsDir, th, label, percent, barTop);
		SDL_GL_MakeCurrent(top, gl);
		// (a reader's page isn't something playing: the card sits low, clear of the text being read)
		float mid = levelCardMiddle(false);
		int64_t until = (int64_t)SDL_GetTicks() + ms;
		if (c.ok() && reader.isOpen()) reader.showOverlay(c.rgba, c.w, c.h, mid, until);
		else if (c.ok()) textReader.showOverlay(c.rgba, c.w, c.h, mid, until);
	}

	TextReader::Style TopScreen::textStyle(const Settings& s) const
	{
		TextReader::Style st;
		st.font = s.txtFont;
		st.size = s.txtSize;
		st.spacing = s.txtSpacing;
		st.margins = s.txtMargins;
		st.theme = s.txtTheme;
		st.filter = s.txtFilter;
		st.dim = s.readDim;
		return st;
	}

	// ---- Lite: two triangles' worth of GL, no texture, no picture from mpv (src/topscreen.h) ----
	namespace
	{
		constexpr unsigned LITE_TRIANGLES = 0x0004, LITE_FLOAT = 0x1406, LITE_ARRAY_BUFFER = 0x8892, LITE_STREAM_DRAW = 0x88E0,
			LITE_BLEND = 0x0BE2, LITE_SRC_ALPHA = 0x0302, LITE_ONE = 0x0001, LITE_COLOR_BUFFER_BIT = 0x4000,
			LITE_DEPTH_TEST = 0x0B71, LITE_SCISSOR_TEST = 0x0C11, LITE_CULL_FACE = 0x0B44, LITE_VERTEX_SHADER = 0x8B31,
			LITE_FRAGMENT_SHADER = 0x8B30, LITE_COMPILE_STATUS = 0x8B81, LITE_LINK_STATUS = 0x8B82;

		struct LiteGl
		{
			bool tried = false, ok = false;
			unsigned prog = 0, vbo = 0, vao = 0;
			int aPos = -1, aCol = -1;
#define LITE_GL(ret, name, ...) ret (*name)(__VA_ARGS__) = nullptr;
			LITE_GL(unsigned, CreateShader, unsigned)
			LITE_GL(void, ShaderSource, unsigned, int, const char* const*, const int*)
			LITE_GL(void, CompileShader, unsigned)
			LITE_GL(void, GetShaderiv, unsigned, unsigned, int*)
			LITE_GL(void, GetShaderInfoLog, unsigned, int, int*, char*)
			LITE_GL(void, DeleteShader, unsigned)
			LITE_GL(unsigned, CreateProgram)
			LITE_GL(void, AttachShader, unsigned, unsigned)
			LITE_GL(void, LinkProgram, unsigned)
			LITE_GL(void, GetProgramiv, unsigned, unsigned, int*)
			LITE_GL(void, UseProgram, unsigned)
			LITE_GL(int, GetAttribLocation, unsigned, const char*)
			LITE_GL(void, GenBuffers, int, unsigned*)
			LITE_GL(void, BindBuffer, unsigned, unsigned)
			LITE_GL(void, BufferData, unsigned, long, const void*, unsigned)
			LITE_GL(void, EnableVertexAttribArray, unsigned)
			LITE_GL(void, DisableVertexAttribArray, unsigned)
			LITE_GL(void, VertexAttribPointer, unsigned, int, unsigned, unsigned char, int, const void*)
			LITE_GL(void, DrawArrays, unsigned, int, int)
			LITE_GL(void, Viewport, int, int, int, int)
			LITE_GL(void, ClearColor, float, float, float, float)
			LITE_GL(void, Clear, unsigned)
			LITE_GL(void, Disable, unsigned)
			LITE_GL(void, Enable, unsigned)
			LITE_GL(void, BlendFunc, unsigned, unsigned)
			LITE_GL(void, GenVertexArrays, int, unsigned*)
			LITE_GL(void, BindVertexArray, unsigned)
#undef LITE_GL
		};

		LiteGl& liteGl(SDL_Window* win)
		{
			static LiteGl g;
			if (g.tried) return g;
			g.tried = true;
			auto load = [&](auto& fn, const char* name, bool need = true) {
				fn = (std::remove_reference_t<decltype(fn)>)SDL_GL_GetProcAddress(name);
				if (!fn && need) fprintf(stderr, "lite: no %s\n", name);
				return fn != nullptr || !need;
			};
			bool ok = true;
#define LITE_LOAD(name, need) ok = load(g.name, "gl" #name, need) && ok;
			LITE_LOAD(CreateShader, true)
			LITE_LOAD(ShaderSource, true)
			LITE_LOAD(CompileShader, true)
			LITE_LOAD(GetShaderiv, true)
			LITE_LOAD(GetShaderInfoLog, true)
			LITE_LOAD(DeleteShader, true)
			LITE_LOAD(CreateProgram, true)
			LITE_LOAD(AttachShader, true)
			LITE_LOAD(LinkProgram, true)
			LITE_LOAD(GetProgramiv, true)
			LITE_LOAD(UseProgram, true)
			LITE_LOAD(GetAttribLocation, true)
			LITE_LOAD(GenBuffers, true)
			LITE_LOAD(BindBuffer, true)
			LITE_LOAD(BufferData, true)
			LITE_LOAD(EnableVertexAttribArray, true)
			LITE_LOAD(DisableVertexAttribArray, true)
			LITE_LOAD(VertexAttribPointer, true)
			LITE_LOAD(DrawArrays, true)
			LITE_LOAD(Viewport, true)
			LITE_LOAD(ClearColor, true)
			LITE_LOAD(Clear, true)
			LITE_LOAD(Disable, true)
			LITE_LOAD(Enable, true)
			LITE_LOAD(BlendFunc, true)
#ifdef BP_GLES
			LITE_LOAD(GenVertexArrays, false)
			LITE_LOAD(BindVertexArray, false)
#else
			LITE_LOAD(GenVertexArrays, true)
			LITE_LOAD(BindVertexArray, true)
#endif
#undef LITE_LOAD
			if (!ok) return g;
#ifdef BP_GLES
			const char* vs = "#version 100\nattribute vec2 pos; attribute vec4 col; varying vec4 c;\n"
							 "void main() { c = col; gl_Position = vec4(pos, 0.0, 1.0); }\n";
			const char* fs = "#version 100\nprecision mediump float; varying vec4 c;\n"
							 "void main() { gl_FragColor = c; }\n";
#else
			const char* vs = "#version 150\nin vec2 pos; in vec4 col; out vec4 c;\n"
							 "void main() { c = col; gl_Position = vec4(pos, 0.0, 1.0); }\n";
			const char* fs = "#version 150\nin vec4 c; out vec4 o;\n"
							 "void main() { o = c; }\n";
#endif
			auto compile = [&](unsigned kind, const char* src) -> unsigned {
				unsigned s = g.CreateShader(kind);
				g.ShaderSource(s, 1, &src, nullptr);
				g.CompileShader(s);
				int done = 0;
				g.GetShaderiv(s, LITE_COMPILE_STATUS, &done);
				if (!done)
				{
					char log[512] = {0};
					g.GetShaderInfoLog(s, sizeof log, nullptr, log);
					fprintf(stderr, "lite: shader: %s\n", log);
					g.DeleteShader(s);
					return 0;
				}
				return s;
			};
			unsigned v = compile(LITE_VERTEX_SHADER, vs), f = compile(LITE_FRAGMENT_SHADER, fs);
			if (!v || !f) return g;
			g.prog = g.CreateProgram();
			g.AttachShader(g.prog, v);
			g.AttachShader(g.prog, f);
			g.LinkProgram(g.prog);
			g.DeleteShader(v);
			g.DeleteShader(f);
			int linked = 0;
			g.GetProgramiv(g.prog, LITE_LINK_STATUS, &linked);
			if (!linked)
			{
				fprintf(stderr, "lite: the shader didn't link\n");
				return g;
			}
			g.aPos = g.GetAttribLocation(g.prog, "pos");
			g.aCol = g.GetAttribLocation(g.prog, "col");
			g.GenBuffers(1, &g.vbo);
			if (g.GenVertexArrays) g.GenVertexArrays(1, &g.vao);
			g.ok = g.aPos >= 0 && g.aCol >= 0;
			fprintf(stderr, "lite: GL ready (%s)\n", g.ok ? "glow and bars" : "no attributes");
			(void)win;
			return g;
		}
	}

	void TopScreen::drawLite(Player& shown, Player& photos, Uint32 now, bool refresh, std::atomic<bool>& renderPending)
	{
		if (photos.playing() || !shown.liteShown())
		{
			liteLoads = -1;
			liteDrew = false;
			return;
		}
		// the frame rate setting (30, 45 or 60), the panel's own draw: no more than that, whatever wakes the loop
		int fps = std::clamp(shown.settings.vizFps, 30, 60);
		Uint32 interval = (Uint32)std::max(16, 1000 / fps);
		bool forced = renderPending.exchange(false) || refresh;
		if (shown.loads() != liteLoads)
		{
			// a new song: its first frame comes at once (or the last song's glow would stay)
			liteLoads = shown.loads();
			for (double& b : liteBars) b = 0;
			litePulse = 0;
			liteBeatsSeen = shown.liteBeats();
			liteTickAt = now;
			liteDrew = false;
			forced = true;
		}
		// (the first frame of an episode is drawn at once, forced: the panel must not keep another style's picture, and
		// the log's first window starts here. Then, once per 10 s, what the tap gave and what was presented: a panel
		// that stays black says with it whether mpv sent af-metadata/bplite at all (the count climbing: the drawing is
		// what's wrong) or nothing came)
		if (!liteLogAt) forced = true;
		if (!liteLogAt || now - liteLogAt >= 10000)
		{
			if (liteLogAt)
				fprintf(stderr, "lite: in %u ms: %llu metadata updates, %llu readings, %llu frames drawn (level %.2f, bass %.2f%s)\n",
					(unsigned)(now - liteLogAt), (unsigned long long)(shown.liteMetaUpdates - liteMetaShown),
					(unsigned long long)(shown.lite.samples - liteReadingsShown), (unsigned long long)(liteFrames - liteFramesShown),
					shown.liteLevel(), shown.liteBassLevel(), shown.liteSilent() ? ", silent" : "");
			liteLogAt = now;
			liteMetaShown = shown.liteMetaUpdates;
			liteReadingsShown = shown.lite.samples;
			liteFramesShown = liteFrames;
		}
		// paused: the picture stays as it was (a key's card may still ask for one draw); nothing moves
		if ((shown.paused || shown.liteSilent()) && liteDrew && !forced) return;
		if (!forced && liteAt && now - liteAt < interval) return;
		if (shown.paused && !forced) return;
		bool drew = drawLiteFrame(shown, now, forced);
		// the frame goes to the panel: drawing alone leaves it in the back buffer, where nobody sees it (the window is
		// double buffered, and the loop's other paths each swap: Lite's has to as well, or the top panel keeps the last
		// frame another path swapped, black or frozen, while the bottom panel moves on)
		if (drew)
		{
			SDL_GL_SwapWindow(top);
			shown.reportSwap();
			liteFrames++;
		}
		renderPending = false;
		if (drew) liteDrew = true;
		liteAt = now;
	}

	// the glow and the bars, into the top panel's framebuffer (the caller swaps it); shared by the loop's own
	// frames and by screenshots, which read the pixels after this
	bool TopScreen::drawLiteFrame(Player& shown, Uint32 now, bool force)
	{
		int fps = std::clamp(shown.settings.vizFps, 30, 60);
		double dt = liteTickAt && now > liteTickAt ? std::min(0.25, (now - liteTickAt) / 1000.0) : 1.0 / fps;
		liteTickAt = now;
		uint64_t beats = shown.liteBeats();
		if (beats != liteBeatsSeen)
		{
			if (beats > liteBeatsSeen) litePulse = 1;
			liteBeatsSeen = beats;
		}
		litePulse *= std::exp(-dt / 0.22);	// a beat's glow fades in about a fifth of a second
		double level = shown.liteLevel();
		double bass = shown.liteBassLevel();
		bool quiet = true;
		for (int i = 0; i < LITE_BARS; i++)
		{
			// the left (bass) bars take the bass reading; the rest the whole sound's, older ones further right, so
			// the row moves as a wave instead of one solid block
			double hist = i < 4 ? shown.liteBassRecent(i) : shown.liteRecent(i - 4);
			double target = std::clamp(hist * (0.92 - 0.025 * i) + level * 0.06, 0.0, 1.0);
			if (target < 0.02) target = 0;
			if (target > liteBars[i]) liteBars[i] = target;	// fast attack
			else liteBars[i] += (target - liteBars[i]) * (1 - std::exp(-dt / 0.2));	// ~200 ms decay
			if (liteBars[i] < 0.005) liteBars[i] = 0;
			if (liteBars[i] > 0.005) quiet = false;
		}
		if (litePulse < 0.01) litePulse = 0;
		// silent or paused and everything has fallen back: leave the panel alone (the last frame is the still one)
		if (quiet && litePulse == 0 && liteDrew && !force) return false;

		SDL_GL_MakeCurrent(top, gl);
		int w = 0, h = 0;
		SDL_GL_GetDrawableSize(top, &w, &h);
		LiteGl& g = liteGl(top);
		if (!g.ok)
		{
			// no GL side (a driver without shaders): the panel still shows mpv's picture or black
			return false;
		}
		// with the art, mpv draws it (and its OSD) first; alone, black first and mpv's OSD over it (a card or the
		// song's title still shows; there is no graph's picture)
		if (!shown.liteOverArt())
		{
			g.ClearColor(0, 0, 0, 1);
			g.Clear(LITE_COLOR_BUFFER_BIT);
		}
		if (shown.render(w, h) < 0)
		{
			g.ClearColor(0, 0, 0, 1);
			g.Clear(LITE_COLOR_BUFFER_BIT);
		}

		std::vector<float> v;
		v.reserve(3000);
		auto nx = [&](double x) { return (float)(x / w * 2 - 1); };
		auto ny = [&](double y) { return (float)(1 - y / h * 2); };
		auto vert = [&](double x, double y, double r, double gg, double b, double a) {
			v.push_back(nx(x));
			v.push_back(ny(y));
			v.push_back((float)r);
			v.push_back((float)gg);
			v.push_back((float)b);
			v.push_back((float)a);
		};
		auto rect = [&](double x0, double y0, double x1, double y1, theme::Rgb c, double a) {
			double r = c.r / 255.0, gg = c.g / 255.0, b = c.b / 255.0;
			vert(x0, y0, r, gg, b, a);
			vert(x1, y0, r, gg, b, a);
			vert(x1, y1, r, gg, b, a);
			vert(x0, y0, r, gg, b, a);
			vert(x1, y1, r, gg, b, a);
			vert(x0, y1, r, gg, b, a);
		};
		auto glow = [&](double cx, double cy, double rad, theme::Rgb c, double a) {
			double r = c.r / 255.0, gg = c.g / 255.0, b = c.b / 255.0;
			const int N = 32;
			for (int i = 0; i < N; i++)
			{
				double a0 = i * 2 * M_PI / N, a1 = (i + 1) * 2 * M_PI / N;
				vert(cx, cy, r, gg, b, a);
				vert(cx + std::cos(a0) * rad, cy + std::sin(a0) * rad, r, gg, b, 0);
				vert(cx + std::cos(a1) * rad, cy + std::sin(a1) * rad, r, gg, b, 0);
			}
		};

		// the glow: a wide faint halo and a brighter core, both growing and brightening with the beat (the bass's)
		double cx = w * 0.5, cy = h * (shown.liteOverArt() ? 0.72 : 0.46);
		double rad = h * (0.09 + 0.15 * bass) * (1 + 0.45 * litePulse);
		if (rad > 1 && (litePulse > 0 || level > 0.02)) glow(cx, cy, rad * 1.9, theme::ACCENT, 0.04 + 0.20 * litePulse);
		if (rad > 1 && (litePulse > 0 || level > 0.02)) glow(cx, cy, rad, theme::ACCENT, 0.06 + 0.45 * litePulse + 0.08 * level);

		// the bars along the foot: 12, the bass side (the newest reading) left
		double bottom = h * 0.94, maxH = h * (shown.liteOverArt() ? 0.26 : 0.34);
		double bw = w * 0.048, gap = w * 0.0125, total = LITE_BARS * bw + (LITE_BARS - 1) * gap;
		double x = (w - total) / 2;
		double pulseLift = litePulse * 0.12;
		for (int i = 0; i < LITE_BARS; i++)
		{
			double bh = maxH * std::min(1.0, liteBars[i] + pulseLift * (i < 4 ? 1.0 : 0.35));
			if (bh > 1)
			{
				rect(x, bottom - bh, x + bw, bottom, theme::ACCENT, 0.92);
				// a cream cap, as the other bars have: it makes the row read as a meter
				rect(x, bottom - bh, x + bw, bottom - bh + std::max(2.0, h * 0.004), theme::TEXT, 0.85);
			}
			x += bw + gap;
		}

		g.Viewport(0, 0, w, h);
		g.Disable(LITE_DEPTH_TEST);
		g.Disable(LITE_SCISSOR_TEST);
		g.Disable(LITE_CULL_FACE);
		g.Enable(LITE_BLEND);
		// additive: the glow builds on the black (or brightens the art) and a beat reads at once; the bars stay solid
		g.BlendFunc(LITE_SRC_ALPHA, LITE_ONE);
		g.UseProgram(g.prog);
		if (g.BindVertexArray) g.BindVertexArray(g.vao);
		g.BindBuffer(LITE_ARRAY_BUFFER, g.vbo);
		g.BufferData(LITE_ARRAY_BUFFER, (long)(v.size() * sizeof(float)), v.data(), LITE_STREAM_DRAW);
		g.EnableVertexAttribArray((unsigned)g.aPos);
		g.EnableVertexAttribArray((unsigned)g.aCol);
		g.VertexAttribPointer((unsigned)g.aPos, 2, LITE_FLOAT, 0, 6 * sizeof(float), nullptr);
		g.VertexAttribPointer((unsigned)g.aCol, 4, LITE_FLOAT, 0, 6 * sizeof(float), (const void*)(2 * sizeof(float)));
		g.DrawArrays(LITE_TRIANGLES, 0, (int)(v.size() / 6));
		g.DisableVertexAttribArray((unsigned)g.aPos);
		g.DisableVertexAttribArray((unsigned)g.aCol);
		g.BindBuffer(LITE_ARRAY_BUFFER, 0);
		if (g.BindVertexArray) g.BindVertexArray(0);
		return true;
	}

	void TopScreen::syncReaders(Ui& ui, Player& player, MangaNet* mangaNet, const std::string& stateDir, Uint32 now, bool woke,
		bool haveUi, const std::function<void(const std::string&, int, int)>& showLevelCard, std::atomic<bool>& renderPending)
	{
		Ui::PageReader::MangaReadRequest rq;
		if (ui.pageReader->takeMangaRead(rq))
		{
			readerFetch.end();
			SDL_GL_MakeCurrent(top, gl);
#ifdef BP_GLES
			if (!readerReady) readerReady = reader.init(true, evReader);
#else
			if (!readerReady) readerReady = reader.init(false, evReader);
#endif
			int w, h;
			SDL_GL_GetDrawableSize(top, &w, &h);
			ui.pageReader->readerStatus = {};
			if (!readerReady) ui.pageReader->readerStatus.error = "The reader couldn't start";
			else if (!rq.pageUrls.empty())
			{
				// a source's or a server's chapter: the pages come through MangaNet into the Ui's own page
				// cache (its Clear cache empties the same folder)
				if (stateDir.empty()) ui.pageReader->readerStatus.error = "No state folder for the chapter's pages";
				else if (!mangaNet || !player.settings.mangaOn)
					ui.pageReader->readerStatus.error = "The manga reader's network side is off";
				else
				{
					mangaNet->start();
					PageCache& cache = ui.manga->mangaPageCacheRef();
					uint64_t limit = mangaCacheBytes(player.settings.mangaCacheSize);
					if (!cache.open(stateDir + "/manga/cache", limit))
						ui.pageReader->readerStatus.error = "The page cache couldn't be opened";
					else if (!reader.open(rq.pageUrls, rq.pageHeaders, cache, rq.path, rq.page, w, h, rq.mode,
								 rq.fit))
						ui.pageReader->readerStatus.error = reader.error();
					else
						readerFetch.begin(mangaNet, &cache, &reader, rq.pageUrls, rq.pageHeaders, limit);
				}
			}
			else if (!reader.open(rq.path, rq.archive, rq.page, w, h, rq.mode, rq.fit))
				ui.pageReader->readerStatus.error = reader.error();
			// the series' own Book (or off), each chapter: it turns the pages as they are decoded
			if (reader.isOpen()) reader.command(MangaReader::Book, rq.book, (int64_t)now);
			// back to the place inside the page it was left at (Vertical scroll: a PDF's tall page)
			if (reader.isOpen() && rq.within > 0) reader.scrollWithin(rq.page, rq.within);
		}
		for (auto& c : ui.pageReader->readerCmds)
		{
			if (c.kind == Ui::PageReader::ReaderCmd::Close)
			{
				readerFetch.end();
				SDL_GL_MakeCurrent(top, gl);	// (close frees the pages' textures in this context)
				reader.close();
			}
			else reader.command((int)c.kind, c.v, c.ms, c.v2, c.v3);
		}
		ui.pageReader->readerCmds.clear();
		// Files' text viewer: a document asked for, the bottom panel's commands, where it is told back
		{
			Ui::Docs& docs = *ui.docs;
			Ui::Docs::TextReadRequest tq;
			if (docs.takeTextRead(tq))
			{
				SDL_GL_MakeCurrent(top, gl);
#ifdef BP_GLES
				if (!textReady) textReady = textReader.init(true, fontsDir);
#else
				if (!textReady) textReady = textReader.init(false, fontsDir);
#endif
				int w, h;
				SDL_GL_GetDrawableSize(top, &w, &h);
				std::string err, title = docs.textStatus.title;
				bool epub = tq.path.size() > 5 && strcasecmp(tq.path.c_str() + tq.path.size() - 5, ".epub") == 0;
				auto src = !textReady ? nullptr : epub ? openEpubText(tq.path, err) : openTextFile(tq.path, err);
				docs.textStatus = {};
				docs.textStatus.title = title;
				docs.textEncoding.clear();
				if (!textReady) docs.textStatus.error = "The text viewer couldn't start";
				else if (!src) docs.textStatus.error = err;
				else if (textReader.setBook(player.settings.txtBook), false) {}
				else if (Uint32 t0 = SDL_GetTicks(); !textReader.open(std::move(src), tq.place, w, h, textStyle(player.settings)) ||
					(fprintf(stderr, "text: open took %u ms\n", SDL_GetTicks() - t0), false))
					docs.textStatus.error = "The text viewer couldn't open it";
				else
				{
					// (the contents once: an EPUB's is read from its navigation document)
					for (auto& c : textReader.contents()) docs.textStatus.contents.push_back({c.title, {c.id, c.depth}});
					if (!textReader.title().empty()) docs.textStatus.title = textReader.title();
					fprintf(stderr, "text: opened %s (%s), %zu in the contents\n", tq.path.c_str(),
						textReader.encoding().empty() ? "book" : textReader.encoding().c_str(), docs.textStatus.contents.size());
				}
			}
			if (docs.textCloseAsked)
			{
				docs.textCloseAsked = false;
				SDL_GL_MakeCurrent(top, gl);	// (its textures are freed in this context)
				textReader.close();
				docs.textStatus = {};
				renderPending = true;	// (the top panel shows what plays, or the icon, again)
				topDark = false;
			}
			if (textReader.isOpen())
			{
				textReader.setStyle(textStyle(player.settings));
				textReader.setBook(player.settings.txtBook);
				textReader.setTurnFold(player.settings.txtTurn == 0);
				textReader.setHighlight(docs.docHighlight());
				if (docs.textEncodingAsked)
				{
					docs.textEncodingAsked = false;
					textReader.setEncoding(docs.textEncoding);
				}
				int w, h;
				SDL_GL_GetDrawableSize(top, &w, &h);
				// (a finger's moves come as shares of the panel: the top's pixels here)
				for (auto& c : docs.textCmds)
				{
					Uint32 t0 = SDL_GetTicks();
					textReader.command(c.kind, c.kind == TextReader::Scroll || c.kind == TextReader::Fling ? c.v * h : c.v, c.ms);
					// (a jump's cost: the text found around its place, nothing else)
					if (c.kind == TextReader::MatchStep) docs.textStatus.matchStep = textReader.matchStepped() ? 1 : -1;
					if (c.kind == TextReader::Jump || c.kind == TextReader::JumpTo || c.kind == TextReader::JumpToLast)
						fprintf(stderr, "text: jump to %.1f%% in %u ms\n", textReader.fraction() * 100, SDL_GetTicks() - t0);
				}
				docs.textStatus.open = true;
				docs.textStatus.fraction = textReader.fraction();
				docs.textStatus.place = textReader.place();
				docs.textStatus.atEnd = textReader.atEnd();
				docs.textStatus.encoding = textReader.encoding();
				docs.textStatus.snippet = textReader.snippet();
				// Book: the bottom panel's page
				docs.textStatus.book = textReader.book();
				docs.textStatus.turnDir = textReader.turnDir();
				docs.textStatus.turnAt = textReader.turnAt();
				docs.textStatus.rightOnTop = textReader.rightOnTop();
				docs.textStatus.turnMs = TextReader::turnMs();
				docs.textStatus.noticeDir = textReader.lastTurnDir();
				docs.textStatus.noticeAt = textReader.lastTurnAt();
				docs.textStatus.bottomOld = textReader.bottomBefore();
				if (textReader.bottomGen() != docs.textStatus.bottomGen)
				{
					docs.textStatus.bottomGen = textReader.bottomGen();
					docs.textStatus.bottom = textReader.bottomPage(docs.textStatus.bottomW, docs.textStatus.bottomH);
				}
				if (!textReader.error().empty()) docs.textStatus.error = textReader.error();
			}
			docs.textCmds.clear();
		}
		// a song's visualizer rests while the pages cover the top panel (nobody sees it; its frames cost CPU)
		player.setTopCovered(onTop(ui));
		// and while the manga engine starts (its ~25 s of Java start-up wants the cores the graph would take)
		player.setEngineLoading(ui.manga->mangaNet && ui.manga->mangaNet->javaExt().state() == JavaExtHost::State::Starting);
		// a URL chapter's pages: requests reordered and the free slots filled (the answers themselves come
		// through MangaNet's fetch owner above)
		if (reader.isOpen()) reader.tick(now);
		if (reader.isOpen()) readerFetch.pass();
		if (reader.isOpen())
		{
			// the row set while a chapter is open reaches the reader too (the fit comes as its own command)
			reader.setPreload(player.settings.mangaPreload);
			// a document of Files: its reading filter, and its pan area's page and box
			{
				int f = ui.docs->docPagesFilter(player);
				// (the Manga reader's own filter; either's Extra dim)
				float r, g, b;
				docFilterTint(f >= 0 ? f : player.settings.mangaFilter, r, g, b, player.settings.readDim);
				reader.setTint(r, g, b);
				Ui::Docs::DocPan dp;
				if (f >= 0 && reader.panBox(dp.page, dp.x0, dp.y0, dp.x1, dp.y1))
				{
					int pw = 0, ph = 0;
					dp.rgba = reader.pixels(dp.page, pw, ph);
					dp.w = pw;
					dp.h = ph;
				}
				ui.docs->docPan = std::move(dp);
			}
			ui.pageReader->readerStatus.open = true;
			ui.pageReader->readerStatus.loading = reader.loading();
			ui.pageReader->readerStatus.error = reader.error();
			ui.pageReader->readerStatus.failedHere = reader.failedInView();
			ui.pageReader->readerStatus.atEnd = reader.atEnd();
			ui.pageReader->readerStatus.page = reader.page();
			ui.pageReader->readerStatus.within = (float)reader.within();
			ui.pageReader->readerStatus.pages = reader.pages();
			ui.pageReader->readerStatus.mode = reader.mode();
			// a zoom changed (keys, the menu, a double-tap): its percent on the pages a moment, fading out
			if (reader.zoomPct() != ui.pageReader->readerStatus.zoomPct && reader.chapterGen() == zoomShownGen && haveUi)
			{
				showLevelCard(std::to_string(reader.zoomPct()) + "%", -1, 1200);
			}
			zoomShownGen = reader.chapterGen();
			ui.pageReader->readerStatus.zoomPct = reader.zoomPct();
			ui.pageReader->readerStatus.fullscreen = reader.fullscreen();
			ui.pageReader->readerStatus.rotation = reader.rotation();
			ui.pageReader->readerStatus.book = reader.book();
			// fullscreen: the bottom panel's share of the view, as fractions of the panel, and the pixels it needs
			Ui::PageReader::ReaderBottom rb;
			rb.on = reader.bothPanels();	// fullscreen, or Book (a page on each panel)
			rb.gen = reader.chapterGen();
			if (rb.on)
			{
				int w, h;
				SDL_GL_GetDrawableSize(top, &w, &h);
				for (auto& b : reader.bottomBoxes())
				{
					rb.boxes.push_back({b.page, b.x / w, b.y / h, b.w / w, b.h / h});
					int pw = 0, ph = 0;
					if (auto px = reader.pixels(b.page, pw, ph)) rb.pages.push_back({b.page, pw, ph, px});
				}
			}
			ui.pageReader->readerBottom = std::move(rb);
			// the pages grid's thumbnails: every page the reader has decoded, ~96 px wide RGBA.
			// always filled while the reader is open (the Ui only uses them when the grid shows);
			// shared_ptr copies are cheap, and absent pages are placeholders in the grid
			Ui::PageReader::ReaderThumbs rt;
			rt.on = true;
			rt.gen = reader.chapterGen();
			for (int pi = 0; pi < reader.pages(); pi++)
			{
				int tw = 0, th = 0;
				if (auto tp = reader.thumbPixels(pi, tw, th))
					rt.pages.push_back({pi, tw, th, tp});
			}
			ui.pageReader->readerThumbs = std::move(rt);
		}
		else
		{
			readerFetch.end();
			ui.pageReader->readerStatus.open = false;
			ui.pageReader->readerBottom = {};
			ui.pageReader->readerThumbs = {};
		}
		if (woke) reader.redrawSoon();
	}

	void TopScreen::draw(Player& player, Player& photos, Player& shown, const Ui& ui, Uint32 now, bool refresh, Uint32 levelUntil,
		std::atomic<bool>& renderPending)
	{
		// About's support QR up: the panel is that picture (down again: its texture goes, in this context)
		bool qrNow = ui.aboutQrUp();
		if (!qrNow && qrUp)
		{
			qrUp = false;
			qrW = qrH = 0;	// (shown again: composed again)
			SDL_GL_MakeCurrent(top, gl);
			qrRender.keepOnly({});
			qrRender.flushDead();
		}
		// Lite on the top panel or off it: one line each way, saying which path draws the panel (a black or frozen top
		// panel is told by these: the player's own GL glow and bars, or mpv's frame, and for what file)
		bool liteNow = player.liteShown() && !photos.playing() && !qrNow && !(reader.isOpen() && onTop(ui)) && !(textReader.isOpen() && onTop(ui));
		if (liteNow != liteOnPanel)
		{
			liteOnPanel = liteNow;
			liteLogAt = 0;
			if (liteNow)
			{
				int w = 0, h = 0;
				SDL_GL_GetDrawableSize(top, &w, &h);
				liteFrames = 0;
				fprintf(stderr, "lite: the top panel %dx%d, %d fps: the player's own GL draws and swaps it (%s), no mpv video frame, no graph\n",
					w, h, std::clamp(shown.settings.vizFps, 30, 60),
					shown.liteOverArt() ? "over mpv's album art" : "on black, mpv's OSD only");
			}
			else
			{
				const char* why = photos.playing() ? "a photo is shown"
					: qrNow ? "About's QR is over it"
					: (reader.isOpen() || textReader.isOpen()) && onTop(ui) ? "a reader is over it"
					: !player.playing() ? "nothing plays" : "another style, or its display shows no visualizer";
				fprintf(stderr, "lite: off the top panel after %llu frames (%llu metadata updates): %s, style %s\n",
					(unsigned long long)liteFrames, (unsigned long long)player.liteMetaUpdates, why,
					visualizerLabel(player.settings.visualizer));
			}
		}
		if (qrNow)
		{
			// About's support QR, large: the panel's own picture, 1:1 (a still: drawn again when it changes, and
			// with the 2 s refresh, so a failed compose gets another try)
			renderPending = false;
			int w, h;
			SDL_GL_GetDrawableSize(top, &w, &h);
			if ((qrW != w || qrH != h || refresh) && drawQr(w, h)) SDL_GL_SwapWindow(top);
		}
		else if (reader.isOpen() && onTop(ui))
		{
			// the pages, not mpv's picture (what plays goes on under them: drainUnder)
			renderPending = false;
			if (reader.frame(now) || refresh)
			{
				SDL_GL_MakeCurrent(top, gl);
				int w, h;
				SDL_GL_GetDrawableSize(top, &w, &h);
				reader.draw(w, h);
				SDL_GL_SwapWindow(top);
			}
		}
		else if (textReader.isOpen() && onTop(ui))
		{
			// a document's text, not mpv's picture (what plays goes on under it: drainUnder)
			renderPending = false;
			if (textReader.frame(now) || refresh)
			{
				SDL_GL_MakeCurrent(top, gl);
				int w, h;
				SDL_GL_GetDrawableSize(top, &w, &h);
				textReader.draw(w, h);
				SDL_GL_SwapWindow(top);
			}
		}
		// (a photo is a still: mpv puts the level card on it later than the key's redraw, and asks for no frame
		// after, so it's drawn again while the card is up, as with nothing playing)
		else if (player.liteShown() && !photos.playing())
		{
			// Lite: the player's own glow and bars, drawn from the af tap (no video frames from mpv for it)
			drawLite(shown, photos, now, refresh, renderPending);
		}
		else if (renderPending.exchange(false) || refresh || (!shown.playing() && (!topDark || levelUntil)) || (photos.playing() && levelUntil))
		{
			SDL_GL_MakeCurrent(top, gl);
			int w, h;
			SDL_GL_GetDrawableSize(top, &w, &h);
			if (!shown.playing())
			{
				// nothing playing: mpv draws its black with the icon (and a level card if one is up)
				int r = player.render(w, h);
				if (r < 0 && topDraws < 5) fprintf(stderr, "top: mpv couldn't draw (%d) at %u ms\n", r, SDL_GetTicks());
				SDL_GL_SwapWindow(top);
				player.reportSwap();
				topDark = true;
				// in the log: the top panel drawn with nothing playing (the first time, and after a level card),
				// to find where a black top panel comes from
				if (topDraws++ == 0 || logNextTop)
					fprintf(stderr, "top: drawn idle %dx%d at %u ms (draw %d)%s\n", w, h, SDL_GetTicks(), topDraws,
						logNextTop ? " with the level card" : "");
				logNextTop = false;
			}
			// a new frame, or the same one again (paused, or a refresh); a photo's over what plays
			else if (shown.frameDue() || refresh)
			{
				shown.render(w, h);
				SDL_GL_SwapWindow(top);
				shown.reportSwap();
			}
		}
	}
}
