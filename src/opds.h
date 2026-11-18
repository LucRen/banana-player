#pragma once
// What the comic servers answer with, read as text (docs/design/manga.md, section 5.1): an OPDS 1.2 Atom feed (Kavita
// and Komga's OPDS, Calibre and the like) and one page of a Komga REST list. Strings in, structs out: no network, no
// threads, no SDL, so core-test can feed these captured answers, and a response we can't read is a false, never a
// crash, as with comicinfo.
#include <string>
#include <vector>

namespace bp
{
	struct OpdsLink
	{
		std::string rel, type, href, title;
		int count = -1;		// thr:count or pse:count carried by the link; -1 when it carries none
		long long length = -1;	// the file's bytes (`length`); -1 when the link carries none
	};

	// An entry's acquisition as a reader should take it: the best of its acquisition links (a comic first, then an
	// EPUB, a PDF, a text file, and any other type last), with that link's type and size. `supported` says whether
	// this app's readers open that kind of file at all.
	struct OpdsAcquisition
	{
		std::string href, type;
		long long length = -1;
		bool supported = false;
	};

	struct OpdsEntry
	{
		std::string id, title, author, summary, updated;
		std::vector<OpdsLink> links;

		// the href of the link an entry carries for each job, "" when it carries none of that kind
		std::string cover() const;	// rel http://opds-spec.org/image
		std::string thumbnail() const;	// rel http://opds-spec.org/image/thumbnail
		std::string acquisition() const;	// rel http://opds-spec.org/acquisition…, the best of them (opdsAcquire)
		std::string pse() const;	// rel http://vaemendis.net/opds-pse/stream, still the {pageNumber} template
		std::string nav() const;	// a link down into another catalogue (atom+xml;profile=opds-catalog)
	};

	// the best acquisition link of an entry (see OpdsAcquisition); href empty when it carries none
	OpdsAcquisition opdsAcquire(const OpdsEntry& e);
	// what a MIME type is called on screen ("EPUB", "PDF", "CBZ", "TXT"), "" for one we can't read
	std::string opdsFormatName(const std::string& type);
	// the extension a type's file is saved with ("epub", "pdf", "cbz", "txt"), "" for one we can't read
	std::string opdsFormatExtension(const std::string& type);
	// an OpenSearch description's query template, made absolute, with its {searchTerms} still in place; "" when it
	// lists none or the text isn't a description
	std::string opdsSearchTemplate(const std::string& descXml, const std::string& descUrl);

	struct OpdsFeed
	{
		std::string id, title;
		std::vector<OpdsEntry> entries;
		std::string next, search, start, up;	// the feed's own paging links, "" when absent
	};

	// an Atom feed the way the OPDS servers write it: namespaces are matched by URI whatever their prefix, entities and
	// CDATA are decoded, and a summary or html content comes out as one line of text. Input over 8 MB is refused, and
	// a feed keeps at most 10 000 entries. false (out unchanged) when the text isn't a feed that closes properly.
	bool parseOpds(const std::string& xml, OpdsFeed& out);

	// a link's href against the URL of the feed it came from: absolute, "//host/x", root-relative "/x", relative "x",
	// "../x" and query-only "?a=1" all resolve
	std::string opdsResolve(const std::string& base, const std::string& href);

	// a PSE template for one page: fills {pageNumber} and {maxWidth}, also where a server wrote them %7B…%7D
	std::string opdsPseUrl(const std::string& templ, int page, int maxWidth);

	struct KomgaItem
	{
		std::string id, name, number;	// a series carries no number; it stays text, Komga writes "10.5" or "Extra"
		int pages = -1;			// a book's media page count
		double numberSort = -1;		// Komga's own ordering value
	};

	// one page of a Komga list answer ({"content": [...], "totalPages": n}) of series or books. Fields we don't use
	// are ignored; malformed JSON is false; items append up to the same 10 000-entry cap as a feed.
	bool parseKomgaPage(const std::string& json, std::vector<KomgaItem>& items, int& totalPages);

	// base + path joined with exactly one slash, whichever way the two happened to be written
	std::string komgaUrl(const std::string& base, const std::string& path);
}
