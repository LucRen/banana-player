# Book catalogs: the OPDS extension

**Question.** The reader's OPDS client lists a Calibre shelf's books as chapters, and opening one said "This chapter
is a whole file: reading it here isn't supported yet": it could read page streams and CBZ chapters, not the EPUB and
PDF files an OPDS book shelf offers. Books needed their own way in.

**Answer.** A second extension, **Catalogs** (`catalogsOn`, off by default), with its own area (`Ui::Catalog`,
`ui_catalog.h` / `ui_catalog.cpp`, `Page::Catalogs`). It reads OPDS 1.2 Atom feeds through the reader's `MangaNet`
fetcher and server store (so an address saved in one place is saved in both), and every whole file it downloads goes
into `<the card>/Books` and opens in Files' own readers. The manga reader is untouched: its servers, its pages and its
half-file rule stay as they were.

**The pieces.**

- `opds.h` / `opds.cpp`: `OpdsAcquisition` and `opdsAcquire()` (the best acquisition link: CBZ, then EPUB, PDF, TXT,
  any other; with its `length`), `opdsFormatName()` / `opdsFormatExtension()`, and `opdsSearchTemplate()` (moved out of
  `mangaremote.cpp`, so the reader's search and the catalogs' share it). `OpdsLink` now carries `length`.
- `MangaNet::booksRoot()`: `<the card>/Books`, an existing Books folder first, the main card's `/data` otherwise
  (`manganet.cpp`).
- The area: the catalog list (add / edit / test / delete), a feed as a folder stack, the feed's own `next` page and
  `search` description, and a download that shows a card until the file is in place.
- Extensions: the drawer and Manage extensions now list two (the reader, the catalogs), `UP/DOWN` moves between the
  rows, and `main.cpp` keeps `MangaNet` up while either extension is on.
- `Ui::kbClose` takes the catalogs' fields (name, address, user, password, search) and hands them back to the area.

**Limits, on purpose.** OPDS 1.2 only (no OPDS 2.0 JSON); PSE streams are still the manga reader's (a catalog row says
"Comic"); no cover thumbnails on the rows; a format the app has no reader for (MOBI, HTML) is shown and refused, never
downloaded. The server store is the manga reader's (`<state>/manga/servers.tsv`), so the two apps share their addresses
and sealed passwords — the same server added once.

**Checked.** `./build/core-test` (the OPDS tests: the book acquisition ranking, `length`, the format names, the
OpenSearch template), a headless run through the drawer into **Catalogs** and its Add form (`--press`, screenshots),
and a network build against a real OPDS catalogue for a download.
