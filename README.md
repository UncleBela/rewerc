# rewerc

rewerc is a file-based publishing system for notes, documentation and small
websites. It extends [werc](http://werc.cat-v.org/) with a native C++ Markdown
renderer, document metadata, mathematical notation (MathJax), tables, callouts, footnotes
and a table of contents.

Write a Markdown file, point your browser to it in the correct directory, and
rewerc renders it inside the site's layout. Pages are rendered on request rather
than built into a static site. Edit documents on disk: HTTP requests can read
content after signing in, but cannot change authored files through the web.

## How the rendering works

```text
Page request
  -> bin/werc.cgi                 sets the working directory and environment
  -> bin/contrib/werc-guard       authenticates, validates paths and bounds filesystem work
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
- **A C++ request guard** handles sessions/login, rejects content writes and checks filesystem access before rc.
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
| A C++17 compiler, Linux 5.6+ headers and `make` | Building the renderer and POSIX request guard |
| OpenSSL and SQLite development libraries | Native password hashing and session storage |
| GNU `timeout` | Per-request process-tree deadline |
| Bubblewrap and fcgiwrap | Dedicated local worker isolation |
| A CGI-capable server or CGI bridge | Invoking the application for HTTP requests |
| Perl and its core modules | Regression tests and the optional legacy formatter |
| Python 3 standard library | Security tests and authenticated test fixtures only |

The Markdown renderer uses the C++ standard library; the request guard also uses
Linux filesystem/process calls. The account backend links OpenSSL libcrypto and SQLite. MathJax is a
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
sitemap files. Every protected request also checks the private account/session database.

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

Content accepts GET/HEAD only. The login/logout endpoints additionally accept
POST through the native account backend; other methods and authoring POSTs are
rejected before rc.

Pass the usual CGI environment: `REQUEST_METHOD`, `REQUEST_URI`, `SERVER_NAME`,
`SERVER_PORT` and `QUERY_STRING`. WERC_SITE is an optional trusted selector, not
an HTTP header. Cookies, inherited identity and legacy POST variables are
removed before rc starts. Authenticated identity is supplied only by the native
backend, and does not enable legacy authoring apps.

Serve only deliberately public shared assets directly; the supplied allowlist
contains the shared CSS and shared favicon. Site attachments require CGI.
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

UTF-8 filenames, including Hungarian accents, spaces and parentheses, work in
nested directories. Werc keeps filesystem names
unchanged and percent-encodes generated browser links and redirects. The sidebar
is generated from the site's directory structure. Paths beginning with `.` or
`_`, source/configuration files, traversal, control characters and unsafe URL
characters remain restricted. No renaming is needed.
Markdown headings and front matter do not rename the underlying file.

The renderer supports paragraphs, six heading levels, links, images, emphasis,
lists, blockquotes, code spans, fenced/indented code, horizontal rules and authored
HTML. It is not a complete implementation of CommonMark or every Obsidian feature.

### Content directories

Published content must live inside the selected real site directory. Symlink
publishing is unsupported: the native reader rejects linked path components,
and directory listings reject links rather than following them. No external
approval file is used. Move or copy reviewed content deliberately; do not point
the site at a directory containing private material.

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

## Accounts and local service

Pages, directory listings and site attachments require a valid session. Only the
shared `/pub/style/style.css` and `/pub/favicon.ico` are public in the supplied
server configuration. Your webserver must send every other content/attachment
request to the fixed `bin/werc.cgi` script, including PDFs, images and text files.
Serving them directly would bypass authentication. Webservers such as lighttpd
or nginx invoke this CGI boundary through their CGI/FastCGI integration.

The native backend uses OpenSSL scrypt (`N=131072`, `r=8`, `p=1`) with random
salts. It stores account hashes and hashed random session identifiers in private
SQLite state. Cookies are HttpOnly and SameSite=Strict, and acquire Secure on
actual HTTPS requests. The supplied instance remains HTTP on loopback. Never
expose this HTTP setup remotely or trust arbitrary forwarding headers for HTTPS.
Account names are ASCII identifiers; passwords may contain UTF-8 and must be
12–1024 bytes. All provisioned accounts can read the whole selected site; there
are no per-directory account permissions or web authoring.

`/_users/login` and `/_users/logout` serve ordinary HTML forms. Login rotates the
session; logout requires POST. Both POST operations require a session CSRF token,
an exact matching browser Origin and a bounded uniquely named UTF-8 form
(64 KiB, 32 fields). Sessions expire after eight hours or 30 minutes idle; login
challenges expire after ten minutes. Password changes and account disabling
revoke that user's sessions. Failed login attempts are throttled before hashing,
with per-user, per-address and global budgets. Session/rate tables are bounded.
No public registration or password reset endpoint is provided.

### Prepare the dedicated worker

Run these commands from the checkout on the local Artix machine after reviewing
the two scripts. They require administrator privileges for account creation;
the coding agent has not executed them:

```sh
sudo ./bin/aux/service-setup.sh anon
sudo -u rewerc env WERC_AUTH_DIR="$PWD/var/service/auth" ./bin/aux/addwuser.rc YOUR_USERNAME
sudo -u rewerc ./bin/aux/service-run.sh
```

`anon` is the current webserver account; change it for another local account.
The account command prompts for a password without echoing it and asks for
confirmation. Repeating it resets that account's password and revokes sessions.
To disable an account:

```sh
sudo -u rewerc env WERC_AUTH_DIR="$PWD/var/service/auth" ./bin/contrib/werc-guard --disable-user YOUR_USERNAME
```

Preparation creates non-login `rewerc`, a local ignored `etc/service.uid` marker,
and private `var/service` runtime directories. It does not recursively change
authored content or code ownership. Keep code, configuration and content owned
by the publisher/administrator, readable but unwritable by the serving account.
The serving account must be able to traverse the checkout's parent directories.

The foreground worker is intended for your preferred supervisor. Bubblewrap
mounts code/content read-only, exposes only private scratch/auth/socket storage
as writable host directories, hides unrelated home files, drops capabilities
and isolates networking. It refuses to start if isolation is unavailable. The
socket is `var/service/socket/werc.sock`; it inherits the webserver's group
without granting the worker membership in the publisher's group. Stop and wait
for the previous worker before removing a stale socket. Manage the complete
worker process tree through the supervisor.

The sibling local webserver's `start.sh` now expects this worker to be running;
it no longer launches a worker as your normal account. Activation is a deliberate
host operation. No service was restarted during implementation. Once prepared,
visit `http://localhost:8080/_users/login`.

By default CGI refuses to run without the prepared service UID. Explicit
`WERC_DEVELOPMENT=yes` permits a normal-user development instance, but does not
bypass authentication. It is used by disposable regression fixtures and is not
set in the supplied webserver configuration. Do not enable it for the isolated
service. Native provisioning/testing uses `WERC_AUTH_DIR` only from trusted
process configuration; never derive it from request data. Fresh installs have
no default password and remain closed until an account is provisioned.

## Request and filesystem boundary

`werc-guard` validates the hostname, numeric port, site and once-decoded UTF-8
URL before rc. GET/HEAD are allowed for protected content; POST is limited to
the two account routes. Private paths, source/state files, traversal, unsafe
characters and unsupported methods are rejected. The old authoring/account
hooks remain disabled, so enabling a retained app cannot restore HTTP writes.

Content, metadata, templates, configuration and directory listings use pinned
root descriptors with Linux `openat2` no-follow resolution. No symlink target is
accepted. Linux 5.6+, suitable headers and mounted `/proc` are required; there is
no unsafe fallback. Built-in navigation enumerates only the requested ancestors,
not the whole site. Limits are 32 path components, 2,048 entries per listing and
a shared 16,384-operation budget per request, including helper subprocesses.
Sitemaps use the same bounded listing API and keep generated exports in private
`var/tmp/sitemaps/SITE/`; they no longer write into the site directory.

CGI processes have CPU (5 seconds), memory (512 MiB), open-file (256), regular
output-file (32 MiB) and wall-clock limits. The launcher uses a 25-second process
tree timeout with a two-second termination grace period. The supplied server
also limits request rate and concurrent connections. These are explicit ceilings,
not a guarantee against every denial-of-service scenario.

Executable configuration/template owners remain trusted. Custom templates must
use `content_read`, `local_read` and `list_content` rather than arbitrary shell
reads. Authored Markdown/HTML behavior is unchanged.

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
prove bin/aux/test-readonly.pl
python3 -B bin/aux/test-security.py
```

For legacy formatter compatibility:

```sh
MARKDOWN_FORMATTER=markdown.pl prove bin/aux/test-markdown.pl apps/mathjax/test_mathjax.pl
```

Tests use disposable authenticated sites/state; Python is a test dependency, not a serving dependency. They cover native rendering, rc templates,
metadata, math, callouts/footnotes/TOC, request validation, rejection of writes,
portable startup, symlink checks and adversarial parser inputs. They do not prove browser MathJax layout,
print pagination or a production deployment's access controls.

You can inspect a CGI response without HTTP after completing local setup:

```sh
env REQUEST_METHOD=GET SERVER_NAME=localhost SERVER_PORT=8080 \
    WERC_SITE=rewerc-guide REQUEST_URI=/ QUERY_STRING= ./bin/werc.cgi
```

Without a session this returns a login redirect (or a service setup error). It writes CGI headers to stdout and diagnostics to stderr; it does
not run browser JavaScript. A “Request rejected” response can indicate a missing
selected site, unsafe paths/symlinks, invalid CGI environment or an unsupported
HTTP method. Check the selected directory and runtime setup before weakening validation.

For math problems, check that the site enables MathJax and the rc file ends in a newline. Inspect generated HTML for `werc-math` wrappers, `window.MathJax` and one
deferred loader. Intact wrappers and headers with unrendered equations point to a
browser/network problem rather than missing server-side TeX software.

## Upstream and acknowledgments

rewerc builds on [werc](http://werc.cat-v.org/). The original werc project is in the public domain.

For rewerc development and issues, use the [rewerc repository](https://github.com/UncleBela/rewerc).
