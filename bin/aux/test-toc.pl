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
my $dir = tempdir('toc-test-XXXXXX', DIR => "$root/sites", CLEANUP => 1);
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
    write_file($file, $_[0]);
    my ($out, $err, $status) = command('bin/contrib/md2html', '--math', $file);
    die "Rendering failed: $err" if $status || length $err;
    return $out;
}
sub nav {
    my ($html) = @_;
    my ($nav) = $html =~ m{(<nav class="table-of-contents".*?</nav>)}s;
    return $nav // '';
}

subtest 'outline defaults to visible and follows actual heading hierarchy' => sub {
    my $html = render("# One\n\n### Deep\n\n## Middle\n\n#### Deeper\n\n# Last\n");
    like $html, qr/class="table-of-contents"/, 'contents visible by default';
    like $html, qr/<h1 id="one">One(?: <a class="heading-permalink"[^>]*>#<\/a>)?<\/h1>/, 'heading gets fragment ID';
    like nav($html), qr{href="#one">One</a>\s*<ol>\s*<li><a href="#deep">Deep</a></li>\s*<li><a href="#middle">Middle</a>\s*<ol>}s, 'skipped heading levels still form a valid nested hierarchy';
    is scalar(() = nav($html) =~ /<li>/g), 5, 'one list item per article heading';
    is scalar(() = $html =~ /class="table-of-contents"/g), 1, 'one outline per document';
    unlike render('Ordinary text without headings.'), qr/table-of-contents/, 'no empty outline';
};
subtest 'TOC labels follow the note language' => sub {
    for my $lang ('hu', 'hu-HU', 'HU') {
        my $html = render("---\nlang: $lang\n---\n# Heading\n");
        like nav($html), qr/<div class="toc-title">Tartalomjegyzék<\/div>/, "$lang uses Hungarian title";
        like nav($html), qr/aria-label="Tartalomjegyzék"/, 'accessible label also translated';
    }
    for my $lang ('en', 'en-GB', 'de', 'hux', '') {
        like nav(render("---\nlang: $lang\n---\n# Heading\n")), qr/<div class="toc-title">Contents<\/div>/, "$lang falls back to English";
    }
    unlike render("---\nlang: hu\ntoc: false\n---\n# Heading\n"), qr/table-of-contents/, 'Hungarian respects opt-out';
};
subtest 'heading permalinks reuse unique IDs and stay out of the outline' => sub {
    my $html = render("---\nlang: hu\n---\n# Árvíz\n\n## Árvíz\n\nTitle\n=====\n");
    my @ids = $html =~ /<h[1-6] id="([^"]+)"/g;
    my @links = $html =~ /class="heading-permalink" href="#([^"]+)"/g;
    is_deeply \@links, \@ids, 'each heading links to its own unique ID';
    like $html, qr/aria-label="Hivatkozás erre a szakaszra"/, 'Hungarian accessible link label';
    unlike nav($html), qr/heading-permalink|>#<\/a>/, 'permalink symbols do not enter TOC';
    like render("---\ntoc: false\n---\n# Heading\n"), qr/class="heading-permalink" href="#heading" aria-label="Link to this section"/, 'English permalink remains when TOC is hidden';
    unlike render("```\n# Code\n```\n"), qr/heading-permalink/, 'code does not receive links';
    like render('# [Original](https://example.test)'), qr{</a> <a class="heading-permalink"}, 'authored links and permalinks are siblings';
};
subtest 'toc false hides only the outline, not heading links' => sub {
    for my $value ('false', 'False', '"false"', 'false # hide the outline') {
        my $html = render("---\ntoc: $value\ncustom: retained\n---\n\n# Heading\n");
        unlike $html, qr/class="table-of-contents"/, "$value disables the outline";
        like $html, qr/<h1 id="heading">/, 'section link remains available';
        unlike $html, qr/<dt>toc<\/dt>/, 'control flag does not clutter properties';
        like $html, qr/<dt>custom<\/dt>/, 'custom properties remain';
    }
    for my $value ('true', '', 'no', '[false, true]') {
        like render("---\ntoc: $value\n---\n# Heading\n"), qr/class="table-of-contents"/, 'only false opts out';
    }
};
subtest 'ATX and Setext headings get links at every level' => sub {
    my $html = render("Title\n=====\n\nSubtitle\n--------\n\n### Three\n\n#### Four\n\n##### Five\n\n###### Six\n");
    like $html, qr/<h1 id="title">Title(?: <a class="heading-permalink"[^>]*>#<\/a>)?<\/h1>/, 'Setext H1';
    like $html, qr/<h2 id="subtitle">Subtitle(?: <a class="heading-permalink"[^>]*>#<\/a>)?<\/h2>/, 'Setext H2';
    like $html, qr/<h6 id="six">Six(?: <a class="heading-permalink"[^>]*>#<\/a>)?<\/h6>/, 'ATX H6';
    is scalar(() = nav($html) =~ /<li>/g), 6, 'all six heading levels included';
};
subtest 'Hungarian accents, decomposed accents, Unicode and entities' => sub {
    my $html = render("# Előkészület ZH-ra\n\n## ŐRÜLT TŰZ\n\n## O\x{30B}ru\x{308}lt tu\x{30B}z\n\n## 日本語\n\n## &#337; &amp; text\n");
    like $html, qr/id="elokeszulet-zh-ra"/, 'Hungarian transliteration';
    like $html, qr/id="orult-tuz"/, 'uppercase accented letters';
    like $html, qr/id="orult-tuz-2"/, 'combining accents produce the same slug base';
    like $html, qr/id="日本語"/, 'other scripts retained as UTF-8';
    like $html, qr/id="o-text"/, 'numeric/common entities decoded for IDs';
    like nav($html), qr/Előkészület ZH-ra/, 'visible label keeps original accents';
    like nav($html), qr/&#337; &amp; text/, 'label entities are not double-escaped';
};
subtest 'duplicate headings and footnote namespaces never collide' => sub {
    my $html = render("# A\n\n# A\n\n# A-2\n\n# A\n\n# !!!\n\n# ???\n\n# Fn 1\n\nText[^x].\n\n[^x]: Note.\n");
    my @ids = $html =~ /\bid="([^"]+)"/g;
    my %seen;
    ok !grep($seen{$_}++, @ids), 'every generated HTML ID is unique';
    like $html, qr/<h1 id="a-2-2">/, 'literal suffixed heading does not collide';
    like $html, qr/<h1 id="a-3">/, 'later duplicate keeps a unique suffix';
    like $html, qr/<h1 id="section-2">/, 'empty slugs have deterministic fallbacks';
    like $html, qr/<h1 id="section-fn-1">/, 'footnote namespace is reserved';
    like $html, qr/<li id="fn-1">/, 'footnote target retained';
    $html = render("# Header\n\n# Footer\n\n# Main copy\n\n# Side bar\n");
    for my $id ('header', 'footer', 'main-copy', 'side-bar') {
        like $html, qr/<h1 id="section-\Q$id\E">/, 'werc layout ID stays reserved';
    }
};
subtest 'labels strip interactive markup without rerendering headings' => sub {
    my $html = render('# **Bold** [link](/about) $x_i$[^x]' . "\n\n[^x]: Note.\n");
    my $nav = nav($html);
    unlike $nav, qr/<strong>|<sup|werc-math|href="\/about"/, 'outline labels are text, not copied interactive/math markup';
    like $nav, qr/Bold link/, 'label text preserved';
    is scalar(() = $html =~ /id="fnref-1"/g), 1, 'heading footnote rendered exactly once';
    is scalar(() = $html =~ /class="werc-math"/g), 1, 'heading equation rendered exactly once';
    $html = render('# A <span title=">">B</span> &amp; C' . "\n\n# <script>bad()</script>Safe\n");
    like nav($html), qr/>A B &amp; C<\/a>/, 'quoted tag attributes do not enter labels';
    unlike nav($html), qr/<script>|bad\(\)/, 'raw script content is not duplicated into navigation';
    like nav($html), qr/>Safe<\/a>/, 'text following script remains';
};
subtest 'code, raw HTML, math, containers and footnotes do not clutter the outline' => sub {
    my $html = render("---\ntitle: Metadata title\n---\n\n# Article\n\n```\n# Code heading\n```\n\n    # Indented heading\n\n<div>\n<h2>Raw heading</h2>\n</div>\n\n\$\$\n# Math heading\n\$\$\n\n> [!note] Callout\n> ## Callout heading\n\n> ## Quote heading\n\n- List item\n\n  ## List heading\n\nText[^x].\n\n[^x]: ## Footnote heading\n");
    my $nav = nav($html);
    is scalar(() = $nav =~ /<li>/g), 1, 'only the article heading is listed';
    like $nav, qr/href="#article"/, 'article target listed';
    unlike $nav, qr/Metadata title|Code heading|Indented heading|Raw heading|Math heading|Callout heading|Quote heading|List heading|Footnote heading/, 'non-article headings excluded';
    like $html, qr/<h2 id="callout-heading">/, 'excluded container heading still has an anchor';
    like $html, qr/<h2 id="footnote-heading">/, 'footnote heading still has an anchor';
};
subtest 'CGI defaults on; metadata edits turn the outline off immediately' => sub {
    my ($name) = $dir =~ m{/([^/]+)$};
    make_path("$dir/_werc");
    write_file("$dir/_werc/config", "siteTitle='TOC test'\nconf_enable_mathjax\n");
    local %ENV = (%ENV, REQUEST_METHOD => 'GET', SERVER_NAME => "$name",
        SERVER_PORT => '8080', REQUEST_URI => '/page', QUERY_STRING => '');
    for my $setting ('true ', 'false') {
        write_file($file, "---\ntitle: Browser only\nlang: hu\ntoc: $setting\n---\n\n# Látható cím\n\nFormula \$x_i\$.\n");
        my ($out, $err, $status) = command('./bin/werc.cgi');
        is $status, 0, 'CGI succeeds';
        is $err, '', 'no CGI errors';
        like $out, qr/<title>Browser only<\/title>/, 'browser title retained';
        like $out, qr/<html lang="hu">/, 'language retained';
        like $out, qr/<h1 id="lathato-cim">Látható cím(?: <a class="heading-permalink"[^>]*>#<\/a>)?<\/h1>/, 'visible title belongs to Markdown';
        if ($setting eq 'false') { unlike $out, qr/class="table-of-contents"/, 'same-size opt-out edit is visible immediately'; }
        else { like $out, qr/class="table-of-contents"/, 'TOC enabled normally'; }
        is scalar(() = $out =~ /<script defer/g), 1, 'one MathJax loader';
    }
};
done_testing;
