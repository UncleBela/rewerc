#!/usr/bin/env perl
use strict;
use warnings;
use Test::More;
use FindBin;
use IPC::Open3;
use Symbol qw(gensym);
# Load the formatter as a library without invoking its CLI.
{ no warnings 'once'; $blosxom::version = 1; }
require "$FindBin::Bin/../contrib/markdown.pl";
require "$FindBin::Bin/../../apps/mathjax/filter.pl";

sub render {
    return Markdown::Markdown($_[0]) if ($ENV{MARKDOWN_FORMATTER} // '') eq 'markdown.pl';
    my $err = gensym;
    my $pid = open3(my $in, my $out, $err, "$FindBin::Bin/../contrib/md2html", @_[1 .. $#_]);
    print {$in} $_[0]; close $in;
    local $/;
    my $html = <$out> // ''; my $errors = <$err> // '';
    waitpid($pid, 0);
    die "md2html failed: $errors" if $? || length $errors;
    return $html;
}

my $native = ($ENV{MARKDOWN_FORMATTER} // '') ne 'markdown.pl';
my $html = render("| Name | Value | Note |\n| :--- | ---: | :---: |\n| **bold** | 42 | [link](/about) |\n");
like $html, qr/<thead>.*<th scope="col" style="text-align:left">Name<\/th>/s, 'semantic header and left alignment';
like $html, qr/<td style="text-align:right">42<\/td>/, 'right alignment';
like $html, qr/<td style="text-align:center"><a href="\/about">link<\/a><\/td>/, 'center alignment and inline links';
like $html, qr/<strong>bold<\/strong>/, 'inline formatting';
unlike $html, qr/<p>\s*<div/, 'table is a block';

$html = render("A | B\n--- | ---\nx\\|y | `a|b`\nshort |\nextra | cells | ignored\n\nAfter\n");
like $html, qr/<td>x&#124;y<\/td><td><code>a\|b<\/code><\/td>/, 'escaped and code pipes stay in cells';
like $html, qr/<td>short<\/td><td><\/td>/, 'missing cells padded';
unlike $html, qr/ignored/, 'excess cells ignored';
like $html, qr/<p>After<\/p>/, 'following paragraph preserved';

for my $source (
    "A | B\n--- | nope\nx | y\n",
    "A | B\n| --- |\nx | y\n",
    "    A | B\n    --- | ---\n    x | y\n",
    "```\nA | B\n--- | ---\nx | y\n```\n",
    "~~~\nA | B\n--- | ---\nx | y\n~~~\n",
    "<pre>\nA | B\n--- | ---\nx | y\n</pre>\n",
    "Ordinary text\n\nHeading\n-------\n\n`a|b`\n",
) {
    unlike render($source), qr/class="markdown-table"/, 'non-table or code preserved';
}
$html = render("| Only |\n| --- |\n| cell |\n");
like $html, qr/<td>cell<\/td>/, 'single column with outer pipes';
$html = render("> A | B\n> --- | ---\n> x | y\n");
like $html, qr/<blockquote>.*class="markdown-table"/s, 'table inside blockquote';
$html = render("A | B\r\n--- | ---\r\nx | y\r\n");
like $html, qr/<td>x<\/td><td>y<\/td>/, 'CRLF input';
my $source = '| Math | Text |'."\n| --- | --- |\n".'| $x_i + x_j$ | **bold** |'."\n";
$html = ($native ? render($source, '--math') : WercMathJax::restore(render(WercMathJax::protect($source))));
like $html, qr/<td><span class="werc-math">\\\(x_i \+ x_j\\\)<\/span><\/td>/, 'MathJax survives inside a table cell';
unlike $html, qr/werc-math-token/, 'math placeholders restored';

$html = render("---\ntitle: 007.0. Előkészület ZH-ra\ntargy: Fizika mérnököknek I. gy. (FN1604GA-1)\nkod:\noktato:\nidopont:\nkapcsolodo_link:\n---\n\nBody **bold**.\n");
like $html, qr/class="markdown-properties"/, 'front matter becomes properties';
if ($native) { unlike $html, qr/Előkészület ZH-ra/, 'publishing title is not a visible property'; }
else { like $html, qr/<dt>title<\/dt><dd>.*007\.0\. Előkészület ZH-ra/, 'legacy title property preserved'; }
is scalar(() = $html =~ /class="markdown-property"/g), ($native ? 5 : 6), 'custom properties including empty fields';
is scalar(() = $html =~ /class="property-empty"/g), 4, 'empty values visible';
like $html, qr/<p>Body <strong>bold<\/strong>\.<\/p>/, 'body Markdown still formats';
unlike $html, qr/<hr/, 'front matter delimiters removed';
$html = render("---\ntags: [physics, 'exam, preparation']\nlinks:\n  - https://example.org/?a=1&b=2\n  - 'Another value'\n---\n");
is scalar(() = $html =~ /class="property-tag"/g), 2, 'inline tags including quoted comma';
like $html, qr/exam, preparation/, 'quoted scalar unwrapped';
like $html, qr/href="https:\/\/example.org\/\?a=1&amp;b=2"/, 'HTTP property becomes escaped link';
like $html, qr/Another value/, 'block list supported';
$html = render("---\nnote: '<script>alert(1)</script>'\nlink: javascript:alert(1)\n---\n");
unlike $html, qr/<script>|href="javascript:/, 'property values cannot inject HTML or script URLs';
like $html, qr/&lt;script&gt;/, 'literal HTML displayed as text';
for my $source ("---\ntitle: Missing end\n", "Text\n\n---\ntitle: Later\n---\n", "---\nparent:\n  child: nested\n---\n") {
    unlike render($source), qr/class="markdown-properties"/, 'unclosed, non-leading, or unsupported metadata stays ordinary Markdown';
}
$source = "---\nprice: 'Price \$5'\ntags:\n  - physics\n  - exam\n---\n\n| Formula |\n| --- |\n| \$x_i\$ |\n";
$html = ($native ? render($source, '--math') : WercMathJax::restore(render(WercMathJax::protect($source))));
like $html, qr/Price \$5/, 'metadata dollars stay literal through MathJax';
like $html, qr/class="markdown-table"/, 'table following properties renders';
like $html, qr/class="werc-math"/, 'body math following properties renders';
unlike $html, qr/werc-math-token/, 'no leaked placeholders';

$html = render("# One\n\n## Two\n\nSetext\n======\n\nSmall\n-----\n");
like $html, qr/<h1(?: id="one")?>One(?: <a class="heading-permalink"[^>]*>#<\/a>)?<\/h1>.*<h2(?: id="two")?>Two(?: <a class="heading-permalink"[^>]*>#<\/a>)?<\/h2>.*<h1(?: id="setext")?>Setext(?: <a class="heading-permalink"[^>]*>#<\/a>)?<\/h1>.*<h2(?: id="small")?>Small(?: <a class="heading-permalink"[^>]*>#<\/a>)?<\/h2>/s, 'ATX and setext headings';
$html = render('***both*** **bold** _em_ \\*literal* &amp; & < 3');
like $html, qr/<strong><em>both<\/em><\/strong>/, 'nested emphasis';
like $html, qr/\*literal\*/, 'backslash punctuation escapes';
like $html, qr/&amp; &amp; &lt; 3/, 'entities and literal angle bracket';
$html = render("[A](https://example.org \"Title\") ![alt](x.png \"Picture\")\n\n[ref][id]\n\n[id]: /about \"About\"\n");
like $html, qr/<a href="https:\/\/example.org" title="Title">A<\/a>/, 'inline link and title';
like $html, qr/<img src="x.png" alt="alt" title="Picture"\s*\/>/, 'image and title';
like $html, qr/<a href="\/about" title="About">ref<\/a>/, 'reference link';
$html = render("[one][] and [two]\n\n[one]: /1\n[two]: /2\n");
like $html, (($ENV{MARKDOWN_FORMATTER} // '') eq 'markdown.pl' ? qr/href="\/1">one<\/a> and \[two\]/ : qr/href="\/1">one<\/a> and <a href="\/2">two<\/a>/), 'collapsed references and parser-specific shortcut behavior';
$html = render("- one\n- two\n  - child\n- three\n\n1. first\n2. second\n");
like $html, ($native ? qr/<ul>.*<li>one<\/li>.*<li>two.*<ul>.*<li>child<\/li>.*<\/ul>.*<li>three<\/li>.*<ol>.*<li>first<\/li>.*<li>second<\/li>/s : qr/<ul>.*<li>one<\/li>.*<li>two.*<ul>.*<li>child<\/li>.*<\/ul>.*<li><p>three<\/p><\/li>.*<li><p>first<\/p><\/li>/s), 'nested lists and parser-specific marker transitions';
$html = render("- first\n\n- second\n\n  paragraph\n");
like $html, qr/<li><p>first<\/p>/, 'loose list paragraph';
like $html, qr/<p>paragraph<\/p>/, 'continuation paragraph';
$html = render("> quote\n>\n> - one\n> - two\n");
like $html, qr/<blockquote>.*<p>quote<\/p>.*<ul>/s, 'blockquote with nested blocks';
$html = render("    x < 2 && y > 1\n\n`x < 2`\n");
like $html, qr/<pre><code>x &lt; 2 &amp;&amp; y &gt; 1\n<\/code><\/pre>/, 'indented code escaping';
like $html, qr/<code>x &lt; 2<\/code>/, 'inline code escaping';
$html = render("line  \nbreak\n\n---\n\n<https://example.org>\n");
like $html, qr/line <br\s*\/>\nbreak/, 'hard line break';
like $html, qr/<hr\s*\/>/, 'horizontal rule';
like $html, qr/<a href="https:\/\/example.org">https:\/\/example.org<\/a>/, 'automatic link';
$html = render("<div>\n*literal*\n</div>\n\nAfter\n");
like $html, qr/<div>\n\*literal\*\n<\/div>/, 'raw HTML blocks preserved';
like $html, qr/<p>After<\/p>/, 'paragraph following HTML';

$html = render("<div>\n[id]: /hidden\n</div>\n\n[id][id]\n");
like $html, qr/\[id\]: \/hidden/, 'raw HTML contents are not parsed as reference definitions';
unlike $html, qr/href="\/hidden"/, 'definitions inside raw HTML do not leak';
$html = render("<hr />\n\nAfter\n");
like $html, qr/<hr \/>\s+<p>After<\/p>/, 'standalone HTML rule does not swallow following content';
SKIP: {
    skip 'native parser improvements', 3 unless $native;
    $html = render("```cpp\nx < 2\n```\n");
    like $html, qr/<pre><code class="language-cpp">x &lt; 2\n<\/code><\/pre>/, 'native fenced code with language';
    $html = render('[**bold**](https://example.org/a(b))');
    like $html, qr/href="https:\/\/example.org\/a\(b\)"><strong>bold<\/strong><\/a>/, 'balanced URL parentheses';
    $html = render("- item\n\n1. ordered\n");
    like $html, qr/<\/ul>\s+<ol>/, 'list types stay separate';
}
done_testing;
