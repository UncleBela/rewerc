# rewerc

rewerc is a file-based publishing system for notes, documentation and small
websites. Its extends [werc](http://werc.cat-v.org/) with a native C++ Markdown
renderer, document metadata, mathematical notation (MathJax), tables, callouts, footnotes
and a table of contents.

Write a Markdown file, point your browser to it in the correct directory, and
rewerc renders it inside the site's layout. Pages are rendered on request rather
than built into a static site. Edit documents on disk: HTTP requests can read
content but cannot log in or change it, and no account/session database is used.

## How the rendering works

```text
Page request
  -> bin/werc.cgi                 sets the working directory and environment
  -> bin/contrib/werc-guard       accepts GET/HEAD and validates paths/site files
  -> bin/werc.rc                  loads configuration and selects a page handler
  -> bin/contrib/md2html          renders Markdown, metadata and optional math
  -> rc templates                 assemble navigation, headers and the page body
  -> HTML response
```

rewerc is a CGI application, not a webserver. A server such as lighttpd or nginx
handles HTTP and static assets, then invokes `bin/werc.cgi` for page requests;
a FastCGI-only arrangement needs a CGI bridge such as fcgiwrap. Choose and
configure the server yourself. rewerc does not listen on a port or implement a
FastCGI service.

The main parts have distinct jobs:

- **Plan 9 tools and rc** provide configuration, routing, app hooks and templates.
- **A small C++ request guard** rejects write requests and unsafe paths before rc.
- **C++** parses Markdown, extracts publishing metadata, recognizes equations and
  generates the MathJax header configuration.
- **MathJax** typesets marked equations in the browser when the app is enabled.

Page bodies are rendered on request. Ordinary Markdown pages and translated
templates are not cached by default, so edits appear on the next request.
The optional formatter cache and sitemap generation still use runtime storage.
Read-only refers to authored content: derived sitemap files and caches may still
be generated, but there are no HTTP authoring or account features.

## Requirements

The current implementation targets a Unix-like environment with:

| Component | Purpose |
| --- | --- |
| Plan 9 from User Space | The `rc` shell and utilities used by werc |
| POSIX shell and common Unix utilities | CGI launcher and supporting commands |
| A C++17 compiler and `make` | Building the renderer and POSIX request guard |
| A CGI-capable server or CGI bridge | Invoking the application for HTTP requests |
| Perl and its core modules | Regression tests and the optional legacy formatter |

The Markdown renderer uses the C++ standard library; the request guard also uses
POSIX filesystem/process calls. Neither adds a third-party library. MathJax is a
browser dependency only when math is enabled; the default loader uses a CDN.

## Setup

Run the following commands from this repository's root, the directory containing
`bin/`, `etc/`, `sites/` and this README.

### Build the renderer and create runtime directories

```sh
make -C bin/contrib
mkdir -p var/tmp
```

The generated `bin/contrib/md2html` and `bin/contrib/werc-guard` executables are
ignored by Git and must be built after a fresh checkout. Builds compile to
temporary executables and replace each previous binary atomically. A failed
build leaves its previous executable available; no webserver restart is needed.

The launcher creates `var/tmp` automatically if it is absent; creating it here
also supports standalone tests and tools. Runtime output is not supplied by Git.
The serving user needs write access for scratch/cache output and any generated
sitemap files, but ordinary page reads need no account/session database.

### Check the local environment

The launcher finds the checkout relative to its own location. It defaults PLAN9
to `/opt/plan9` and executes `$PLAN9/bin/rc` through the native guard. Set PLAN9
in the trusted CGI environment for another installation, and keep the tool path
in `etc/initrc.local` consistent. Check the launcher's `WERC_SITE` default if you
use a different local site directory.

`etc/initrc.local` is installation-specific; the current development copy still
contains an absolute TMPDIR path. Adapt it for another checkout, or use the
root-relative example below. Integration tests currently expect Plan 9 tools
under `/opt/plan9`.

Use `etc/initrc.local` for installation overrides rather than editing the defaults
in `etc/initrc`. For example, with Plan 9 tools installed under `/opt/plan9`:

```rc
PLAN9=/opt/plan9
plan9port=$PLAN9
path=($plan9port/bin . ./bin ./bin/contrib /bin /usr/bin)
TMPDIR=$werc_root/var/tmp
formatter=(md2html)
rcargs=-I
debug=()
enabled_apps=()
```

The native formatter must be selected explicitly: the retained upstream defaults
still select the legacy Perl formatter through a cache. Keep a final newline in
rc configuration files; the installed rc can reject a final command without one.

### Choose the site directory

The included example site is `sites/rewerc-guide/`, with an English guide at
`notes/first-note.md` and a Hungarian version at `notes/rewerc-utmutato.md`.

`SERVER_NAME` identifies the public hostname. `WERC_SITE`, when supplied by the
trusted launcher or CGI environment, selects a directory under `sites/`
independently of that hostname. Without it, the hostname is used as the directory
name. The supplied launcher defaults localhost requests to `rewerc-guide`.

Keep the selected directory and the server's static document root aligned. Both
host and site names are validated; WERC_SITE must be a single safe directory
name, never a path or a value taken from a client HTTP header. Site directories
must be real directories, not symlink aliases.

For a new site, create a directory such as `sites/example.com/` with an
`_werc/config` file and an `index.md`. An example site configuration is:

```rc
siteTitle='My notes'
siteSubTitle='Documents and examples'
sideBarNavTitle='Pages'
meta_description='A collection of notes'
html_lang=en
conf_enable_mathjax
```

Configuration files are executable rc code, not user-submitted data. Only trusted
administrators should be able to edit them. Directory-level `_werc/config` files
can override or extend site configuration for their subtree.

### Connect the CGI entry point

Configure your chosen server to invoke **`bin/werc.cgi`**, not `bin/werc.rc`.
Direct rc entry fails closed because it bypasses the security boundary.

Only GET and HEAD are accepted. Other methods return `405 Method Not Allowed`
with `Allow: GET, HEAD`, without reading a request body or invoking rc. Account
endpoints such as `/_users/login` return 404.

Pass the usual CGI environment: `REQUEST_METHOD`, `REQUEST_URI`, `SERVER_NAME`,
`SERVER_PORT` and `QUERY_STRING`. WERC_SITE is an optional trusted selector, not
an HTTP header. Cookies, inherited identity and legacy POST variables are
removed before rc starts; they cannot enable accounts or write features.

Serve public assets directly, including the shared `pub/` directory at `/pub/`.
Do not expose the entire checkout as a static document root. Block configuration,
source files, hidden paths and runtime state, including `_werc/`, `.git/`, `etc/`
and `var/`. Static serving bypasses CGI checks, so access restrictions must also
be enforced at the server boundary.

## Writing and organizing pages

For the selected site, paths map to files like this:

| Source file | Page URL |
| --- | --- |
| `index.md` | `/` |
| `about.md` | `/about` |
| `notes/index.md` | `/notes/` |
| `notes/example.md` | `/notes/example` |

Use ASCII filenames for page URLs; Unicode document text and headings are
supported. The sidebar is generated from the site's directory structure.
Markdown headings and front matter do not rename the underlying file.

The renderer supports paragraphs, six heading levels, links, images, emphasis,
lists, blockquotes, code spans, fenced/indented code, horizontal rules and authored
HTML. It is not a complete implementation of CommonMark or every Obsidian feature.

### Link notes from another directory

Use the private **`etc/APPROVED_DIRECTORIES`** file to approve external content
roots. Each nonblank line names one existing absolute directory:

```text
/home/you/Notes/public
/home/you/Documents/course-notes
```

The file is optional: missing or empty approves no links. On a new checkout,
copy `etc/APPROVED_DIRECTORIES.example` only if the private file does not already
exist, then set its permissions:

```sh
chmod 600 etc/APPROVED_DIRECTORIES
```

The actual file must be owned by the Unix user running rewerc and have no group
or other-user permissions. It cannot be a symlink, special file or hardlinked
file. It is ignored by Git; the example is safe to publish. HTTP requests for
APPROVED_DIRECTORIES are rejected at any URL depth, and physical approved paths
are not exported to rc or HTML. Your webserver must also block this filename
and keep the checkout's `etc/` outside public static roots. Unix owners can still
read their own file; these permissions protect it from other local accounts,
not from the process that must read it.

Blank lines and full-line `#` comments are ignored. Paths may contain spaces,
but are literal: no quotes, `~`, environment variables or shell commands are
expanded. Use real directory paths without symlink components or `..`. The file
is limited to 64 KiB and 256 entries; invalid entries fail closed. Changes take
effect on the next CGI request, without rebuilding or restarting rewerc.

Once a directory is approved, create a link **inside the selected site**, for
example:

```sh
ln -s /home/you/Notes/public sites/rewerc-guide/linked-notes
```

An external `example.md` then appears at `/linked-notes/example`. Individual
Markdown-file links work too. Directory links participate in navigation and
sitemap generation; the selected site root itself must remain a real directory.

Approving a directory is a publishing decision: use a directory containing only
material intended for readers. Resolved targets must stay inside an approved
root, including nested links; similarly named sibling directories do not qualify.
Broken links, cycles, special files and escapes fail closed. Linked directories
cannot contain `_werc` configuration or `.tpl` templates, and links inside local
configuration/template paths are forbidden. Application-private areas and the
policy file cannot be exposed through aliases. Approving notes does not grant
permission to execute new rc code.

This assumes trusted filesystem/content owners and is not protection against a
local attacker who can change files during a request. Ordinary Markdown still
permits authored HTML. The approval applies to CGI-rendered content; static
assets served directly by your webserver retain that server's symlink rules.
Do not turn off static-file protections globally to enable note links.

### Front matter and publishing metadata

Put a flat metadata block at the start of a Markdown document:

```markdown
---
title: Notes on motion
description: Equations and worked examples.
author: Your name
date: 2026-10-29
lang: en
tags: [physics, examples]
course: Mechanics
---

# Motion

The article begins here.
```

| Field | Effect |
| --- | --- |
| `title` | Browser-tab title; does not insert a visible article heading or change navigation labels |
| `description` | HTML description metadata |
| `author` | HTML author metadata and a visible byline |
| `date`, `updated` | Explicit publication/update dates in the byline |
| `lang` | HTML language attribute and supported navigation-label language |
| `toc` | Controls the table of contents |
| Other fields | Visible properties panel, preserving field order and spelling |

The publishing fields and `toc` are omitted from the properties panel. Custom
values are escaped plain text; empty fields show a dash, tags become chips, and
HTTP/HTTPS values can become links. Dollar signs in metadata remain literal.

The parser supports flat fields, quoted strings, bracket lists and indented dash
lists. It is not a full YAML parser: nested mappings, anchors and block scalars
are unsupported. Unsupported or unclosed blocks remain ordinary Markdown.

Page fields override site defaults such as `meta_description`, `meta_author`,
`meta_date`, `meta_updated` and `html_lang`; empty fields inherit defaults.
Dates must be valid `YYYY-MM-DD` calendar dates. Invalid language/date values
fall back or are omitted, and publication dates are never inferred from files.

### Table of contents and heading links

A table of contents appears by default when a native Markdown page has article
headings. Hide it with `toc: false` in front matter. Heading IDs and their `#`
permalinks remain available even when the outline is hidden.

Only article-level headings join the outline; headings inside lists, blockquotes,
callouts and endnotes still get anchors. Duplicate IDs receive numeric suffixes.
Renaming headings or adding earlier duplicates can change fragment URLs.

For `lang: hu` or a Hungarian regional tag, navigation uses Hungarian labels such
as “Tartalomjegyzék”; otherwise it uses English. Visible accents are preserved,
while Hungarian/common Latin accents are transliterated in generated IDs.

### Tables

```markdown
| Formula     | Description | Value |
| :---------- | :---------: | ----: |
| $x_i + x_j$ | **Sum**     |    42 |
```

Separator cells need at least three hyphens. Colons control alignment. Cells
support inline Markdown and enabled math; escaped pipes and pipes inside code or
math do not split cells. Missing cells are padded and excess cells ignored.
Each source row occupies one line.

### Mathematics

Enable the MathJax app in the relevant `_werc/config`:

```rc
conf_enable_mathjax
```

Then write inline or display math:

```markdown
Inline: $v = s/t$.

Display:
$$
F = ma
$$
```

The app enables the native renderer's `--math` flag and adds MathJax headers.
C++ recognizes delimiters and emits escaped TeX; the browser performs typesetting.
Without app enablement, ordinary serving leaves math recognition disabled.

Code, metadata, HTML attributes and code-like HTML regions are excluded.
Inline math cannot cross a newline or contain whitespace immediately inside its
delimiters. Use `\$` for a literal dollar sign. Display math can span lines and
blank lines, and math can be used in table cells.

The default loader is pinned to MathJax 4.0.0 on jsDelivr. Set `mathjax_url` before
`conf_enable_mathjax` to use another compatible source; self-hosting also needs
its components and fonts. Custom head templates must preserve `extraHeaders`.
See the [MathJax app guide](apps/mathjax/README.md) for further examples.

### Callouts

```markdown
> [!note] Assumptions
> We neglect air resistance.

> [!example]+ Worked example
> Apply $F = ma$.

> [!warning]- Expand to read
> A folded warning.
```

Callouts use the first line of a blockquote. Types include note, info, tip,
warning, example, question, abstract, todo, success, failure, danger, bug and
quote, with common aliases. Types are case-insensitive; unknown valid types use
note styling. Titles and bodies support Markdown and enabled math.

`+` creates an initially expanded foldable callout; `-` creates a collapsed one.
Folding uses native `details`/`summary`, without JavaScript. Nest callouts with
another `>` level and separate adjacent callouts with an unquoted blank line.

### Footnotes

```markdown
A statement with a source.[^paper]

[^paper]: **Author**, [paper](https://example.org/paper), 2026.

    A continuation paragraph, indented by four spaces.
```

Definitions belong at document level and may precede or follow references.
Numbering follows first use. Repeated references share a note and receive
individual return links. Unused definitions are hidden, undefined references stay
literal, and the first duplicate definition wins. Labels are case-sensitive.

Footnote bodies support Markdown, tables, callouts and math. Code, raw HTML,
metadata, equations and link/image labels do not create footnote references.
Anonymous `^[inline notes]` are not supported. State and generated IDs are local
to each rendered document; combining fragments requires distinct ID namespaces.

## Layout, templates and printing

The active stylesheet is `pub/style/style.css`. There is no CSS framework or CSS
build step. `lib/headers.tpl` loads it with `media="all"` so print rules apply.
Print styling hides site chrome, retains article metadata/TOC/endnotes, wraps
code and uses 18 mm margins. Paper size and orientation remain browser choices.
Wait for MathJax before printing; folded callouts may need to be opened manually.
Visual print/PDF pagination has not yet been verified.

Templates live under `lib/` and `tpl/`. Put site-specific library overrides in
`sites/SITE/_werc/lib/`, or configure shared defaults with `masterSite`.
Templates use rc expressions and code, translated by `bin/template.awk` on each
call and executed in an isolated rc child. Template edits do not require a
compiled-template cache or a rebuild. Treat template/configuration authors as
trusted code authors.

## Read-only request boundary

`bin/contrib/werc-guard` runs before rc. It accepts only GET/HEAD, validates the
hostname, port, site selector and decoded URL, and rejects traversal, private
components, source/state extensions and oversized URIs. Site directories are
opened without following symlinks. A descriptor-based tree inspection rejects
unapproved links, executable linked configuration, special files and inspection
errors. Approved note links are described below.

There are no logins, accounts, password hashes, sessions, cookies, CSRF tokens or
web-based writes. Authentication and write compatibility hooks always fail,
regardless of cookies or inherited account variables. The retained
`bin/aux/addwuser.rc` refuses account creation rather than writing a password file.
Do not expose content as private on the assumption that rewerc authenticates it;
use separately reviewed server-side access controls if private publication is
required. Hiding a page from navigation does not make it private.

The unused wiki/blog/comments/paste apps remain in the tree. Their presence or
configuration cannot enable HTTP writes. Existing blog pages, escaped comments
and text files can still be read; submission/login controls are disabled. A
future editing feature would require a separate security design rather than
turning the old account helpers back on.

The request guard assumes trusted content/configuration owners; it is not a
sandbox against a local user who can replace files or edit executable rc code.
Static-file serving also needs its own restrictions. These checks do not replace
review of a public deployment, HTTPS behavior or private-file access rules.

### Trusted and untrusted documents

Ordinary Markdown permits raw HTML and authored URL schemes. Only trusted authors
should edit published Markdown files. The ordinary formatter is not an HTML
sanitizer.

For untrusted text, use the native renderer's `--untrusted` flag. This emits
escaped plain text in a `pre` element without Markdown links, raw HTML, metadata
processing or math. It is not a rich sanitized-Markdown mode.

Native document input is limited to 4 MiB, and instrumented expensive scans have
a work budget. Limit failures return a nonzero CLI status without partial HTML.
These safeguards are not a proof of linear complexity for every Markdown input.

## Renderer commands and legacy compatibility

```text
md2html [--untrusted] [--math] [--html4tags] [--] [FILE ...]
md2html --page-metadata [FILE]
md2html --mathjax-headers URL
md2html --help
md2html --version
md2html --shortversion
```

No file means stdin; `-` also reads stdin. Multiple files are concatenated into
one document. `--html4tags` changes void-tag output. The early metadata mode
reads at most 64 KiB of the opening header and emits fixed-name, safely quoted
assignments for rc, using the same metadata parser as body rendering.

`bin/contrib/markdown.pl` remains available as a legacy formatter. Selecting it
restores the compatibility math protect/format/restore pipeline, using Perl.
The native binary is still needed for publishing metadata and MathJax headers.
Callouts, footnotes and the automatic TOC are native-only extensions.

## Source layout

| Path | Responsibility |
| --- | --- |
| `bin/werc.cgi` | CGI launcher |
| `bin/contrib/werc-guard.cpp` | Small native read-only request/path guard |
| `bin/werc.rc`, `bin/corehandlers.rc` | Routing, page handlers and response assembly |
| `bin/werclib.rc`, `bin/cgilib.rc`, `bin/wercconf.rc` | Templates, CGI helpers and app/configuration hooks |
| `bin/template.awk` | Fresh rc template translation |
| `bin/contrib/md2html.cpp` | Main native Markdown parser and renderer CLI |
| `bin/contrib/metadata.*` | Shared front matter and publishing metadata |
| `bin/contrib/callouts.*` | Callout syntax and outer markup |
| `bin/contrib/footnotes.*` | Definition collection, numbering and backlinks |
| `bin/contrib/toc.*` | Heading IDs, outline labels and permalinks |
| `etc/` | Defaults and local installation overrides |
| `apps/` | Optional applications, including MathJax |
| `sites/` | Site content and per-directory configuration |
| `lib/`, `tpl/` | Layout fragments and page templates |
| `pub/` | Shared public assets and styles |
| `var/tmp/` | Scratch files and optional formatter cache |

Keep runtime databases, caches, local secrets, machine configuration, generated
sitemaps, editor backups and compiled artifacts out of public commits. Review the
actual ignore rules before staging; not all of these categories are currently
covered automatically. Preserve authored site content separately from generated
output.

## Verification and debugging

From the repository root:

```sh
make -C bin/contrib
prove bin/aux/test-toc.pl bin/aux/test-callouts-footnotes.pl bin/aux/test-metadata.pl bin/aux/test-templates.pl bin/aux/test-markdown.pl apps/mathjax/test_mathjax.pl
prove bin/aux/test-readonly.pl bin/aux/test-approved-directories.pl
```

For legacy formatter compatibility:

```sh
MARKDOWN_FORMATTER=markdown.pl prove bin/aux/test-markdown.pl apps/mathjax/test_mathjax.pl
```

Tests use disposable sites/state. They cover native rendering, rc templates,
metadata, math, callouts/footnotes/TOC, request validation, rejection of writes,
portable startup, symlink checks and adversarial parser inputs. They do not prove browser MathJax layout,
print pagination or a production deployment's access controls.

You can inspect a CGI response without HTTP after completing local setup:

```sh
env REQUEST_METHOD=GET SERVER_NAME=localhost SERVER_PORT=8080 \
    WERC_SITE=rewerc-guide REQUEST_URI=/ QUERY_STRING= ./bin/werc.cgi
```

This writes CGI headers and HTML to stdout and diagnostics to stderr; it does
not run browser JavaScript. A “Request rejected” response can indicate a missing
selected site, unsafe paths/symlinks, invalid CGI environment or an unsupported
HTTP method. Check the selected directory and runtime setup before weakening validation.

For math problems, check that the site enables MathJax and the rc file ends in a newline. Inspect generated HTML for `werc-math` wrappers, `window.MathJax` and one
deferred loader. Intact wrappers and headers with unrendered equations point to a
browser/network problem rather than missing server-side TeX software.

## Upstream and acknowledgments

rewerc builds on [werc](http://werc.cat-v.org/). The original werc project is in the public domain.

For rewerc development and issues, use the [rewerc repository](https://github.com/UncleBela/rewerc).
