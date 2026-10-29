#!/usr/bin/env perl
use strict;
use warnings;
use utf8;
use Test::More;
use FindBin;
use Cwd qw(abs_path);
use File::Temp qw(tempdir);
use File::Path qw(make_path);
use IPC::Open3;
use IO::Select;
use Symbol qw(gensym);
use Encode qw(decode);

my $root = abs_path("$FindBin::Bin/../..");
chdir $root or die $!;
require "$root/bin/aux/TestAuth.pm";
TestAuth::setup($root);
my $dir = tempdir('notes-test-XXXXXX', DIR => "$root/sites", CLEANUP => 1);
my $file = "$dir/page.md";
sub write_file {
    my ($path, $text) = @_;
    open my $fh, '>:encoding(UTF-8)', $path or die $!;
    print {$fh} $text;
    close $fh or die $!;
}
sub command {
    my @args = @_;
    my $err = gensym;
    my $pid = open3(my $in, my $out, $err, @args);
    close $in;
    my $stdout_fd = fileno $out;
    my $select = IO::Select->new($out, $err);
    my ($stdout, $stderr) = ('', '');
    while (my @ready = $select->can_read) {
        for my $fh (@ready) {
            my $n = sysread($fh, my $buffer, 8192);
            die $! unless defined $n;
            if (!$n) { $select->remove($fh); close $fh; }
            elsif (fileno($fh) == $stdout_fd) { $stdout .= $buffer; }
            else { $stderr .= $buffer; }
        }
    }
    waitpid($pid, 0);
    return (decode('UTF-8', $stdout), decode('UTF-8', $stderr), $?);
}
sub render {
    my ($text, @flags) = @_;
    write_file($file, $text);
    my ($out, $err, $status) = command('bin/contrib/md2html', '--math', @flags, $file);
    die "Renderer failed: $err" if $status || length $err;
    return $out;
}

subtest 'callout types, aliases, titles and folding' => sub {
    my $html = render("> [!note] A **title**\n> Text \$x_i\$.\n");
    like $html, qr/<aside class="callout" data-callout="note">/, 'note uses structured callout markup';
    like $html, qr/class="callout-title">A <strong>title<\/strong>/, 'title renders inline Markdown';
    like $html, qr/class="werc-math">\\\(x_i\\\)/, 'body renders math';
    $html = render("> [!CAUTION]+\n> Read this.\n");
    like $html, qr/<details class="callout" data-callout="warning" open>/, 'case-insensitive alias and expanded fold';
    like $html, qr/<summary class="callout-title">Warning<\/summary>/, 'default title';
    $html = render("> [!faq]- Question\n> Answer.\n");
    like $html, qr/<details class="callout" data-callout="question">/, 'collapsed fold';
    unlike $html, qr/ open>/, 'collapsed callout has no open attribute';
    $html = render("> [!custom-type]\n");
    like $html, qr/data-callout="note"/, 'unknown types fall back to note';
    unlike $html, qr/class="callout-content"/, 'title-only callout has no empty body';
};
subtest 'nested and adjacent callouts preserve ordinary blockquotes' => sub {
    my $html = render("> [!note] Outer\n> Text.\n>\n> > [!tip] Inner\n> > Nested.\n\n> [!example] Next\n> Another.\n");
    is scalar(() = $html =~ /class="callout"/g), 3, 'nested and adjacent callouts are distinct';
    like $html, qr/data-callout="note".*data-callout="tip".*data-callout="example"/s, 'callout order preserved';
    $html = render("> Ordinary **quote**.\n");
    like $html, qr/<blockquote>.*<strong>quote<\/strong>.*<\/blockquote>/s, 'normal quote unchanged';
    unlike $html, qr/class="callout"/, 'no accidental callout';
    $html = render("> [!note] Note\n> Body.\n\n> Ordinary quote.\n");
    like $html, qr{</aside>\s*<blockquote>.*Ordinary quote}s, 'unquoted blank line ends a callout before an ordinary quote';
    $html = render("> [!bad\"type] Plain\n> Text.\n");
    unlike $html, qr/class="callout"/, 'invalid type is not interpolated into HTML attributes';
};
subtest 'callouts share full body formatting and protect code' => sub {
    my $html = render("> [!example] Example\n> | A | B |\n> | --- | --- |\n> | \$a|b\$ | **bold** |\n>\n> - Item\n>\n> ```\n> [!warning] literal\n> \$not_math\$\n> ```\n");
    like $html, qr/class="markdown-table"/, 'table inside callout';
    like $html, qr/\\\(a\|b\\\)/, 'table equation stays intact';
    like $html, qr/<ul>/, 'list inside callout';
    like $html, qr/<pre><code>\[!warning\] literal\n\$not_math\$/, 'code stays literal';
    is scalar(() = $html =~ /class="callout"/g), 1, 'code marker is not another callout';
};
subtest 'first-reference order, repeats, forward definitions and unused notes' => sub {
    my $html = render("B first[^b], A second[^a], B again[^b].\n\n[^a]: Alpha.\n[^b]: Beta.\n[^unused]: Not shown.\n");
    like $html, qr/id="fnref-1".*id="fnref-2".*id="fnref-1-2"/s, 'reference order and unique repeated IDs';
    like $html, qr/<li id="fn-1">\s*<p>Beta\.<\/p>.*<li id="fn-2">\s*<p>Alpha\.<\/p>/s, 'notes ordered by first use';
    like $html, qr/href="#fnref-1"/, 'first occurrence has a return link';
    like $html, qr/href="#fnref-1-2"/, 'repeated occurrence has a return link';
    unlike $html, qr/Not shown|\[\^unused\]/, 'unused definitions do not render';
    is scalar(() = $html =~ /<section class="footnotes"/g), 1, 'one document-level footnote section';
    $html = render("Unknown[^missing].\n");
    like $html, qr/Unknown\[\^missing\]/, 'undefined reference stays literal';
    unlike $html, qr/class="footnotes"/, 'undefined reference creates no empty section';
};
subtest 'footnote bodies use the main renderer' => sub {
    my $html = render("Text[^a].\n\n[^a]: **Bold** and [link](/about) with \$x_i\$.\n\n    Another paragraph.\n\n    - Item\n\n    | A |\n    | --- |\n    | Cell |\n");
    like $html, qr/<strong>Bold<\/strong>.*href="\/about".*werc-math/s, 'inline formatting, links and math';
    like $html, qr/<p>Another paragraph\.<\/p>/, 'indented continuation supports paragraphs';
    like $html, qr/<ul>/, 'list in a footnote';
    like $html, qr/class="markdown-table"/, 'table in a footnote';
    $html = render("Text[^a].\n\n[^a]: First definition.\n[^a]: Second definition.\n");
    like $html, qr/First definition/, 'first duplicate definition wins';
    unlike $html, qr/Second definition/, 'duplicate definition does not leak into the article';
};
subtest 'math, tables, emphasis and callout references interact correctly' => sub {
    my $html = render("_Emphasis[^a_b]_.\n\n| Reference |\n| --- |\n| Cell[^a|b] |\n\n> [!note] Title[^title]\n> Body[^body]\n\n[^a_b]: One.\n[^a|b]: Two.\n[^title]: Three.\n[^body]: Four.\n");
    like $html, qr/<em>Emphasis<sup/, 'footnote identifier cannot end surrounding emphasis';
    like $html, qr/<td>Cell<sup.*<\/td>/, 'identifier pipe cannot split a table cell';
    like $html, qr/class="callout-title">Title<sup[^>]+id="fnref-3"/, 'callout title numbered before its body';
    like $html, qr/<p>Body<sup[^>]+id="fnref-4"/, 'callout body reference follows title';
};
subtest 'code, HTML, metadata and math exclude footnote syntax' => sub {
    for my $source (
        '`[^a]`', "```\n[^a]\n```\n", "~~~\n[^a]\n~~~\n", "    [^a]\n",
        '<code>[^a]</code>', '<pre>[^a]</pre>', '<!-- [^a] -->',
        '<a title="[^a]">link</a>', '\\[^a]', '$x[^a]$', '![^a]', '[Label[^a]](/about)',
        "---\ncustom: '[^a]'\n---\n",
    ) {
        my $html = render($source . "\n\n[^a]: Unused.\n");
        unlike $html, qr/class="footnote-ref"/, 'excluded context has no reference link';
        unlike $html, qr/class="footnotes"/, 'excluded context does not activate a note';
    }
    my $html = render("```\n[^a]: Not a definition.\n```\n\nText[^a].\n");
    like $html, qr/Not a definition/, 'code definition stays visible';
    unlike $html, qr/class="footnotes"/, 'code cannot define a footnote';
};
subtest 'definition collection respects multiline code and fence boundaries' => sub {
    for my $source ("`Code\n[^a]: Not a definition.\nend`\n\nText[^a].\n",
                    "<code>\n[^a]: Not a definition.\n</code>\n\nText[^a].\n",
                    "```\n```not-a-closer\n[^a]: Not a definition.\n```\n\nText[^a].\n") {
        my $html = render($source);
        like $html, qr/Not a definition/, 'definition-like code stays visible';
        unlike $html, qr/class="footnotes"/, 'code cannot register a definition';
    }
    my $html = render("    ```\n    Indented code, not a fence.\n\nText[^a].\n\n[^a]: A real note.\n");
    like $html, qr/class="footnotes"/, 'indented fence text does not hide a later definition';
};
subtest 'cross-references and cycles terminate with complete backlinks' => sub {
    my $html = render("Start[^a].\n\n[^a]: See B[^b].\n[^b]: See A[^a].\n");
    is scalar(() = $html =~ /<li id="fn-/g), 2, 'each body rendered once despite a cycle';
    like $html, qr/href="#fnref-1-2"/, 'late cross-reference has a backlink';
};
subtest 'identifiers are data and HTML IDs are generated numbers' => sub {
    my $html = render("Text[^a\"onload=x].\n\n[^a\"onload=x]: Safe.\n");
    like $html, qr/id="fnref-1"/, 'unusual identifier maps to a numeric ID';
    unlike $html, qr/onload=/, 'identifier never enters HTML attributes';
};
subtest 'CRLF, Unicode labels and HTML4 output work' => sub {
    my $html = render("Text[^árvíz].\r\n\r\n[^árvíz]: Tükörfúrógép.\r\n", '--html4tags');
    like $html, qr/Tükörfúrógép/, 'Unicode label and body preserved';
    like $html, qr/<hr>/, 'HTML4 void-tag option honored';
    unlike $html, qr/<hr \/>/, 'no XHTML rule in HTML4 mode';
};
subtest 'full CGI combines publishing metadata, callouts, footnotes and math' => sub {
    my ($name) = $dir =~ m{/([^/]+)$};
    make_path("$dir/_werc");
    write_file("$dir/_werc/config", "siteTitle='Notes test'\nconf_enable_mathjax\n");
    write_file($file, "---\ntitle: Browser title\nlang: hu\n---\n\n> [!tip]+ A **tip**\n> Formula \$x_i\$.[^source]\n\n[^source]: A source with \$y_i\$.\n");
    local %ENV = (%ENV, REQUEST_METHOD => 'GET', SERVER_NAME => "$name",
        SERVER_PORT => '8080', REQUEST_URI => '/page', QUERY_STRING => '');
    TestAuth::authorize();
    my ($out, $err, $status) = command('./bin/werc.cgi');
    is $status, 0, 'CGI succeeds';
    is $err, '', 'no CGI errors';
    like $out, qr/<title>Browser title<\/title>/, 'publishing title retained';
    like $out, qr/<html lang="hu">/, 'language retained';
    like $out, qr/<details class="callout" data-callout="tip" open>/, 'foldable callout rendered';
    like $out, qr/class="footnotes"/, 'footnote section rendered';
    is scalar(() = $out =~ /class="werc-math"/g), 2, 'callout and footnote equations marked';
    is scalar(() = $out =~ /<script defer/g), 1, 'one MathJax loader';
};
done_testing;
