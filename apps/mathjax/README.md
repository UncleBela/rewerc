# MathJax app for werc

Enable dollar-delimited math in a site's or directory's `_werc/config`:

```rc
conf_enable_mathjax
```

End the command with a newline: the installed Plan 9 rc rejects a final
command without one. Merely installing the app does not enable it.
Changes apply on the next request; no server restart is needed.

## Native rendering

With the local `formatter=(md2html)` setting, the app enables `md2html --math`.
The C++ parser recognizes equations directly and emits escaped TeX inside
`werc-math` spans or divs. There are no protect/restore processes, placeholders,
or Perl calls on this path. Header configuration and URL escaping also use
C++, through `md2html --mathjax-headers URL`.

`app.rc` follows werc's normal app lifecycle, preserves existing extra headers,
and avoids duplicate initialization. File input and stdin both work.
`formatter=(fltr_cache md2html)` also works; math-enabled cache entries use
a distinct command name so they cannot collide with plain rendering.

Build the native executable from the werc root:

```sh
make -C bin/contrib md2html
```

Requirements: werc, the compiled C++17 formatter, and JavaScript in the browser.
The default loader is pinned to MathJax 4.0.0 on jsDelivr, requiring browser
network access. MathJax still performs typesetting in the browser; C++ emits
TeX and HTML, not equation images.

## Write math

```text
Inline: $x_i + x_j = \alpha$.

$$
\begin{aligned}
x_i &= a_i + b_i \\
y_i &= x_i^2
\end{aligned}
$$
```

Use single backslashes in Markdown. Prefer a separate paragraph for display
math. Inline math stays on one line, with no whitespace immediately inside
its dollar delimiters. A closing inline dollar followed by a decimal digit
is not accepted. Odd preceding backslashes escape a dollar; unmatched
delimiters remain text. Escape literal currency as `\$5`.

Math works inside headings, emphasis, links, lists, blockquotes and table
cells. Pipes inside an equation do not split table cells. Code spans,
fenced/indented code, HTML attributes/comments and raw `pre`, `code`, `script`,
`style` and `textarea` elements are excluded. Front matter stays plain metadata.
TeX punctuation, Unicode text and HTML-like expressions survive unchanged as
escaped text. Automatic MathJax scanning is restricted to `werc-math` elements.

This is dollar-delimited math support, not full Obsidian syntax. Bare TeX
environments and direct `\(...\)` / `\[...\]` author input are not enabled.
HTML/template handlers that bypass the formatter are not converted.

## Loader configuration

Set a compatible MathJax v4 URL before enabling the app:

```rc
mathjax_url='https://cdn.jsdelivr.net/npm/mathjax@4.0.0/tex-chtml.js'
conf_enable_mathjax
```

A self-hosted installation needs the supporting components and fonts too.
The app adds an inline configuration followed by a deferred loader through
`extraHeaders`. Custom header templates must emit that variable. Browser
script restrictions must allow the configuration and MathJax assets.
If JavaScript fails to load, the TeX remains visible.

## Legacy formatter compatibility

`filter.pl` is retained for `markdown.pl` and other non-native formatters.
Only that fallback uses the old Perl protect/format/restore pipeline.
Its stable SHA-256 markers and UTF-8 hex payloads preserve cache reuse.
Switching to `formatter=(markdown.pl)` therefore still works, but loses the
native math speed improvement. Header generation continues to use `md2html`.

## Verify

```sh
perl bin/aux/test-markdown.pl
perl apps/mathjax/test_mathjax.pl
MARKDOWN_FORMATTER=markdown.pl perl apps/mathjax/test_mathjax.pl
```

Tests need Perl's core modules. They cover delimiter rules, exclusions,
Unicode, escaping, Markdown interactions, caching, disabled/idempotent app
behavior, and real werc CGI requests. Temporary fixtures stay inside the app
and are removed afterward. Native rc tests reject any attempted Perl call.
Browser layout and font loading are separate from server-side checks.
`example.md` contains copyable equations.
