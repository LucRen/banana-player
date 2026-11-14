# The overlay stack: dialogs and sheets on the bottom panel (2026-11-14)

## The problem

A dialog or a sheet over a page was wired into `Ui` by hand in four places: `Ui::press` (the keys), `Ui::pointer`
(the touches), `Ui::renderPage` (its drawing) and `Ui::signature` (the redraw rule: the panel is drawn again only when
the signature changes). Its state and a dozen `xyz*` methods also sat in `ui.h`. Forgetting one of the four is a bug
that shows late: a dialog that draws but lets the keys through to the page, or one whose cursor moves without the panel
being drawn again.

## The pattern

`Ui::Overlay` (declared in `src/ui.h`, the stack in `src/ui_overlay.cpp`) is a small interface:

```cpp
struct Overlay
{
	Ui& ui;
	bool done = false;
	virtual const char* name() const = 0;	// "light", "alert": one of a kind open at a time
	virtual bool key(Button b, Player& p) = 0;	// as Ui::press (true leaves the player)
	virtual bool pointer(const Pointer& pt, int x, int y, Player& p) = 0;	// x, y in 640x480 units
	virtual void draw(const Player& p) = 0;
	virtual std::string signature(const Player& p) const = 0;
	void close() { done = true; }
};
```

`Ui` owns `std::vector<std::unique_ptr<Overlay>> overlays` (the last is on top) and asks it in the four places, once:

- `press`: `if (topOverlay()) return overlayKey(b, p);` first of all. Only the topmost gets the key: an overlay is modal.
- `pointer`: `if (topOverlay()) return overlayPointer(pt, x, y, p);` (after the manga list's drag is reset).
- `renderPage`: `drawOverlays(p)`, the oldest first, over the page and the older dialogs, under the file job's card.
- `signature`: `overlaySignature(p)`: `|ov`, then each one's `|name:signature`, in the stack's order.

An overlay is a struct **nested in `Ui`**, forward-declared in `ui.h` (`struct LightSheet;`) and defined in its own
`.cpp`. A nested struct may use `Ui`'s private members, so it draws with `ui.drawDialog`, `ui.drawFocus`,
`ui.drawTextMid`, `ui.fillRound`, `ui.drawHints`, reads `ui.backlight`, `ui.page`, and gets the `Player&` (and so
`p.settings`) with each call, as every `Ui` method does (it isn't kept: main hands the photos' player in while a photo
shows). No friend lists, no context struct, no templates. The theme's colours come from `ui_internal.h` as before.

Closing: the overlay calls `close()`; the stack takes it off once the call that closed it has returned
(`dropClosedOverlays`), so an overlay may close itself, or push another, from inside `key()`/`pointer()`. A closed one
is skipped by everything meanwhile. `pushOverlay` of a name that's already open marks the old one closed and puts the
new one on top (a second message replaces the first, as before).

What goes in `signature()`: everything `draw()` shows that isn't already in `Ui::signature` (which has all of
`Settings`): the cursor, a pressed button, a level read from elsewhere. The Brightness sheet's is its cursor and both
screens' `backlight.permille()` (the backlight isn't a setting). Something that animates puts a clock step in
(`SDL_GetTicks() / 110`, as the loader does).

## Adding a dialog

One line in `ui.h` for the struct, one for what opens it; the rest in a `.cpp`:

```cpp
// ui.h (private)
struct ConfirmBox;
void confirm(const std::string& question, std::function<void(Player&)> yes);

// ui_confirm.cpp
struct Ui::ConfirmBox : Ui::Overlay
{
	std::string question;
	std::function<void(Player&)> yes;
	int cursor = 0;	// 0 Yes, 1 No

	ConfirmBox(Ui& u, std::string q, std::function<void(Player&)> y) : Overlay(u), question(std::move(q)), yes(std::move(y)) {}
	const char* name() const override { return "confirm"; }
	Rect box() const { return {96, 150, 448, 170}; }
	Rect button(int i) const { return {box().x + 24 + i * 208, box().y + 100, 192, 46}; }

	void choose(int i, Player& p)
	{
		close();
		if (i == 0) yes(p);	// (may open a message: it goes on top, this one is taken off after)
	}
	bool key(Button b, Player& p) override
	{
		if (b == Button::Left || b == Button::Right) cursor ^= 1;
		else if (b == Button::A) choose(cursor, p);
		else if (b == Button::B) close();
		return false;
	}
	bool pointer(const Pointer& pt, int x, int y, Player& p) override
	{
		if (pt.kind != Pointer::Up) return false;
		for (int i = 0; i < 2; i++)
			if (button(i).hit(x, y)) choose(i, p);
		return false;
	}
	void draw(const Player&) override
	{
		ui.drawDialog(box(), question);
		for (int i = 0; i < 2; i++)
		{
			Rect r = button(i);
			ui.drawBox(r, i == cursor ? ACCENT : BUTTON, R_BUTTON);
			ui.drawTextMid(i ? "No" : "Yes", 1, i == cursor ? ON_ACCENT : TEXT, r.x + r.w / 2, r.y + r.h / 2, r.w - 16, 2);
		}
		ui.drawHints({{"A", "Choose"}, {"B", "Close"}});
	}
	std::string signature(const Player&) const override { return question + std::to_string(cursor); }
};

void Ui::confirm(const std::string& q, std::function<void(Player&)> yes)
{
	pushOverlay(std::make_unique<ConfirmBox>(*this, q, std::move(yes)));
}
```

and the new file's name in `CMakeLists.txt`. Nothing in `press`, `pointer`, `renderPage` or `signature`.

## What moved (the proof)

- **The readers' Brightness sheet** (`src/ui_light.cpp`): `Ui::LightSheet`. `lightOpen/lightCursor/lightDrag` and
  eleven `light*` methods left `ui.h`; `openLight()` (the readers' callers are untouched) pushes it. `lightFilter` and
  `lightNote` stay `Ui`'s: the readers' buttons use them while the sheet is closed.
- **The message dialog** (`Ui::alert`, now in `src/ui_overlay.cpp`): `Ui::AlertBox`. `alert(title, text)` is as it
  was for its callers.

Checked with the scripted harness against a build of the commit before (`build-before`): 60 screenshots (the text
reader's sheet closed, open, after each of Up/Down/Left/Right/A/B/Start, a drag on a bar, a tap inside, a tap outside,
with both backlights and with none; the "Can't open this file" message, a key it ignores, A, B and a tap closing
it), byte for byte the same, and the harness's logs the same.

**One difference, on purpose:** before, the sheet always had the keys and the message was always drawn on top, so a
message that came up while the sheet was open (none does today from inside the sheet; a background error could) was
drawn over a sheet that still took the input. Now the one on top takes it.

## Not verified

- The handheld itself (the desktop build only), and the Manga reader's and the PDF viewer's way into the sheet (the
  same `openLight()`; only the text reader's was driven).
- A message over an open sheet (the difference above): reasoned from the code, not driven.

## The other dialogs: how each would move

In `press` and `pointer` the old `if (xyzOpen)` chain is checked after the stack, so a converted dialog is above every
unconverted one for input, and drawn above them too (`drawOverlays` is near the end of `renderPage`). That is right
only for dialogs that are already on top of everything, so the order to move them in is **from the top of the old
chain down**; moving a lower one first would lift it over the ones still by hand.

1. **`mangaErrOpen`** (the browser-check message): the next in both chains, and it is a message: no class of its own,
   `alert("…", mangaErrText)` with the title it has, or an `AlertBox` with another name. Its `mangaErrFor[]`
   bookkeeping stays where it is.
2. **`infoOpen`** (the file's Info card, `drawFileInfo`): any key or tap closes it. A ten-line overlay.
3. **Quick settings** (`quickOpen`, `quickKey/quickPointer/drawQuick`, `quickDrag`): the same shape as the Brightness
   sheet (rows, bars, a drag); `main.cpp` or the hotkey that toggles it would ask `findOverlay("quick")`. Check the
   dark-panel rule in `press` first (`!quickOpen` there) and the level card.
4. **The manga dialogs that are plain modals**: `mstModal` (source details/options/repo/remove), `mstJavaModal`,
   `mstAvMenu`, `mangaServerModal`, `mangaSrvMenu`, `mlibMenu`, `mhisMenu`, the Download sheet. Each already has its
   own `…Key/…Pointer/draw…/…Box/…RowRect` set and a slice of `mangaNetSignature()`/`mangaDlSignature()`; they are
   dispatched inside the Manga pages' own key and pointer handlers, so each can move by itself without the ordering
   concern above. The biggest win in lines of `ui.h`.
5. **`docMenu` and `mangaMenu`** (the readers' Options sheets): modal over their page and closed when the page is
   left. As overlays, leaving the page must close them: the reader's close path calls `findOverlay(...)->close()`, or
   see `alive()` below.
6. **`ctxOpen`** (the browser's Options menu, and the mini bar's hold menu) and **`pickerOpen`** (Add to playlist):
   fit, but each is checked at two places in `pointer` today (the picker before and after the page's own handling,
   by page), and `ctxArmed`/`ctxStatus`/`pressedBtn` are shared with the page. Move after the simpler ones, with
   screenshots of each entry point.
7. **The on-screen keyboard** (`kbOpen`): modal, but it works together with what is under it (the form's field, the
   EQ preset's name, the manga search), blinks its caret (already a signature term) and has a `panel.on()` condition
   in `pointer`. It fits as an overlay that takes a "where the text goes" callback; last of the modals.

**Don't fit as they are**

- **Shown by a condition, not opened**: the update popup (`popupShown(p)`), the Display popup (`displayOpen &&
  displayShown(p)`), Play on (`playOnOpen && inMenu && page == Page::Playing && …`). Their "open" is recomputed from the
  player's state on every press. The stack would need one more hook, `virtual bool alive(const Player&) const { return
  true; }`, asked before input, drawing and the signature (false: dropped). Small, but not added now: nothing
  converted needs it. The update popup is better left a condition.
- **The file job's card** (`fileJob`): driven by `tick` and a worker, drawn over everything (after the overlays) and
  holds the panel until the job ends. It is above the stack by design; leave it.
- **Not modal**: the extension notice (`drawExtNotice`), the clip bar (`clipPointer` takes only its own touches), the
  mini bar, the level card. An overlay takes all input; these are parts of the page.
- **Key repeat**: `Ui::repeats` decides by page which held buttons repeat, and knows nothing of overlays (it didn't
  know of the sheet before either: unchanged). A dialog that must not repeat (as the Display popup, "a held key would
  spin the chips round") needs `repeats` to ask the top overlay: one more optional hook when the first such dialog
  moves.
