#!/usr/bin/env perl
# Run from the werc root: perl apps/mathjax/test_mathjax.pl
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
use Encode qw(encode decode);

my $formatter = $ENV{MARKDOWN_FORMATTER} // 'md2html';
die 'Unknown test formatter' unless $formatter eq 'md2html' || $formatter eq 'markdown.pl';
my $app = $FindBin::Bin;
my $root = abs_path("$app/../..");
require "$root/bin/aux/TestAuth.pm";
TestAuth::setup($root);
require "$app/filter.pl";
chdir $root or die "chdir $root: $!";

# Read both output pipes concurrently so CGI stderr cannot block stdout.
sub command {
    my ($input, @args) = @_;
    my $err = gensym;
    my $pid = open3(my $in, my $out, $err, @args);
    binmode $in, ':raw';
    print {$in} encode('UTF-8', $input);
    close $in;
    my $select = IO::Select->new($out, $err);
    my $out_fd = fileno($out);
    my ($stdout, $stderr) = ('', '');
    while (my @ready = $select->can_read) {
        for my $handle (@ready) {
            my $count = sysread($handle, my $buffer, 8192);
            die "read child: $!" unless defined $count;
            if (!$count) {
                $select->remove($handle);
                close $handle;
            } elsif (fileno($handle) == $out_fd) {
                $stdout .= $buffer;
            } else {
                $stderr .= $buffer;
            }
        }
    }
    waitpid($pid, 0);
    return (decode('UTF-8', $stdout), decode('UTF-8', $stderr), $?);
}

sub markdown {
    my ($out, $err, $status) = command($_[0], $formatter eq 'markdown.pl' ? ($^X, "$root/bin/contrib/markdown.pl") : ("$root/bin/contrib/md2html"));
    die "Markdown failed ($status): $err" if $status || length $err;
    return $out;
}
sub render {
    return WercMathJax::restore(markdown(WercMathJax::protect($_[0]))) if $formatter eq 'markdown.pl';
    my ($out, $err, $status) = command($_[0], "$root/bin/contrib/md2html", '--math');
    die "Native math failed ($status): $err" if $status || length $err;
    return $out;
}
sub write_file {
    my ($path, $text) = @_;
    open my $file, '>:encoding(UTF-8)', $path or die "write $path: $!";
    print {$file} $text;
    close $file or die "close $path: $!";
}
$ENV{MARKDOWN_FORMATTER} = $formatter;
my $rc_setup = <<'RC';
path=(/opt/plan9/bin . bin/contrib /usr/bin /bin)
. ./apps/mathjax/app.rc
RC
$rc_setup .= "fn perl { echo unexpected-perl >[1=2]; exit unexpected-perl }\n" if $formatter eq 'md2html';

subtest 'inline math and surrounding Markdown' => sub {
    my $html = render('**Hello** $x_i + x_j = \alpha & y < z$!');
    like $html, qr{<strong>Hello</strong>}, 'ordinary Markdown still formats';
    like $html, qr/\Q\(x_i + x_j = \alpha &amp; y &lt; z\)\E/, 'TeX remains intact and escaped';
    unlike $html, qr/<em>/, 'subscripts do not become emphasis';
};
subtest 'multiline display math' => sub {
    my $html = render("Before\n\n\$\$\n\\begin{aligned}\nx_i &= 1 \\\\\ny_j &= 2\n\\end{aligned}\n\$\$\n\nAfter");
    like $html, qr/\Q<div class="werc-math">\[\E/, 'display wrapper';
    like $html, qr/x_i &amp;= 1/, 'alignment character escaped';
    unlike $html, qr/<p><div/, 'block is outside a paragraph';
    like $html, qr/<p>After<\/p>/, 'following paragraph preserved';
};
subtest 'code and HTML exclusions' => sub {
    my @cases = ('`$x_i$`', '``a `$x$` b``', "```tex\n\$x\$\n```\n",
        "~~~\n\$\$x\$\$\n~~~\n", "> ```\n> \$x\$\n> ```\n",
        "- ```\n  \$x\$\n  ```\n", "    \$x\$\n", "\t\$x\$\n",
        '<pre>$x$</pre>', '<code>$x$</code>', '<!-- $x$ -->',
        '<script>let a = "$x$";</script>', '<style>$x$</style>',
        '<textarea>$x$</textarea>', '<a title="$x$">link</a>');
    for my $text (@cases) {
        is WercMathJax::protect($text), $text, 'legacy scanner also excludes this region';
        unlike render($text), qr/class="werc-math"/, 'no math wrapper added';
    }
};
subtest 'currency and unmatched delimiters' => sub {
    for my $text ('Price: \$5 and \$10.', 'Costs $5 and $10.', '$ x $',
        '$x $', "\$x\ny\$", '$unclosed', '$$unclosed', '$$') {
        unlike render($text), qr/class="werc-math"/, 'literal source is not recognized as math';
    }
};
subtest 'escaped dollars inside math' => sub {
    like render('$x + \$5$'), qr/\Q\(x + \$5\)\E/, 'escaped dollar preserved';
};
subtest 'HTML escaping' => sub {
    my $html = render('$<script>alert(1)</script>$');
    unlike $html, qr/<script>/, 'equation cannot become a script element';
    like $html, qr/&lt;script&gt;/, 'literal tag survives as text';
};
subtest 'Unicode and multiple equations' => sub {
    my $html = render("\$α_1\$ and \$β_2\$\n\n\$\$γ_3\$\$");
    my $count = () = $html =~ /class="werc-math"/g;
    is $count, 3, 'all three equations marked';
    like $html, qr/α_1/, 'Unicode preserved';
    unlike $html, qr/werc-math-token/, 'no placeholders remain';
};
subtest 'stable rendering for cache reuse' => sub {
    is render('$x$'), render('$x$'), 'same input, same output';
};
subtest 'native math boundaries and Markdown interactions' => sub {
    plan skip_all => 'native parser checks' if $formatter eq 'markdown.pl';
    my $html = render("---\nprice: 'Price \$5'\ntags: [physics]\n---\n\n| Formula | Note |\n| --- | --- |\n| \$a|b\$ | **bold** |\n");
    like $html, qr/Price \$5/, 'property dollars stay literal';
    like $html, qr/<td><span class="werc-math">\\\(a\|b\\\)<\/span><\/td>/, 'equation pipe is not a table separator';
    like $html, qr/<td><strong>bold<\/strong><\/td>/, 'following table cell stays intact';
    $html = render("\$\$\nx_i &= 1\n\n[x]: /not-a-reference\n\$\$\n");
    like $html, qr/x_i &amp;= 1\n\n\[x\]: \/not-a-reference/, 'display math preserves blank lines and reference-looking text';
    $html = render('Before $$x_i$$ after.');
    like $html, qr/<p>Before <\/p><div class="werc-math">.*<\/div><p> after\.<\/p>/, 'display math splits surrounding prose into paragraphs';
    unlike $html, qr/<p><div/, 'display math is never inside a paragraph';
    $html = render('<div title="$attribute$">$x_i$ <code>$code$</code></div>');
    like $html, qr/title="\$attribute\$"/, 'HTML attributes remain literal';
    like $html, qr/class="werc-math">\\\(x_i\\\)/, 'math in ordinary raw HTML is rendered';
    like $html, qr/<code>\$code\$<\/code>/, 'raw code inside HTML is excluded';
    unlike render('$x$٢'), qr/werc-math/, 'Unicode decimal digit prevents a currency-like closing delimiter';
    unlike render("\$\x{A0}x\$"), qr/werc-math/, 'Unicode opening whitespace is rejected';
    unlike render("\$x\x{2003}\$"), qr/werc-math/, 'Unicode closing whitespace is rejected';
    unlike render('\$x$'), qr/werc-math/, 'odd backslash escapes opening dollar';
    like render('\\\\$x$'), qr/werc-math/, 'even backslashes permit an opening dollar';
    like render('**Math $x_i$**'), qr/<strong>Math <span class="werc-math">/, 'math inside emphasis';
    like render('# Math $x_i$'), qr/<h1 id="[^"]+">Math <span class="werc-math">/, 'math inside headings';
    like render('- $x_i$'), qr/<li><span class="werc-math">/, 'math inside list items';
    like render('> $x_i$'), qr/<blockquote>.*werc-math/s, 'math inside blockquotes';
    like render('_Before $x_i$ after_'), qr/<em>Before <span class="werc-math">\\\(x_i\\\)<\/span> after<\/em>/, 'outer emphasis ignores math underscores';
    like render('**Before $a**b$ after**'), qr/<strong>Before <span class="werc-math">\\\(a\*\*b\\\)<\/span> after<\/strong>/, 'outer strong ignores math asterisks';
    like render('[Math $x]$](/about)'), qr/<a href="\/about">Math <span class="werc-math">\\\(x\]\\\)<\/span><\/a>/, 'link labels ignore TeX brackets';
};
subtest 'real rc enablement and idempotent initialization' => sub {
    my $script = $rc_setup . <<'RC';
. ./bin/cgilib.rc
. ./bin/wercconf.rc
formatter=($MARKDOWN_FORMATTER)
extraHeaders=('<meta name="existing">')
conf_enable_mathjax
conf_enable_mathjax
mathjax_init
mathjax_init
if(! ~ $enabled_apps mathjax) exit bad-enable
if(! ~ $formatter mathjax_format) exit bad-formatter
echo $"extraHeaders
$formatter
RC
    my ($out, $err, $status) = command('Text $x_i + x_j$ and **bold**.', '/opt/plan9/bin/rc', '-c', $script);
    is $status, 0, 'rc succeeded';
    is $err, '', 'no rc errors';
    my $loaders = () = $out =~ /<script defer/g;
    is $loaders, 1, 'one MathJax loader';
    like $out, qr/<meta name="existing">/, 'existing headers preserved';
    like $out, qr/\Q\(x_i + x_j\)\E/, 'math formatted';
    like $out, qr/<strong>bold<\/strong>/, 'ordinary Markdown formatted';
    unlike $out, qr/werc-math-token/, 'placeholders removed';
};
subtest 'disabled app leaves output unchanged' => sub {
    my $script = $rc_setup . <<'RC';
formatter=($MARKDOWN_FORMATTER)
if(! ~ $#extraHeaders 0) exit unexpected-headers
$formatter
RC
    my $source = '**Normal** $x$';
    my ($out, $err, $status) = command($source, '/opt/plan9/bin/rc', '-c', $script);
    is $status, 0, 'rc succeeded';
    is $err, '', 'no errors';
    is $out, markdown($source), 'matches unwrapped formatter';
};
subtest 'file input with cached formatter' => sub {
    my $dir = tempdir('test-XXXXXX', DIR => $app, CLEANUP => 1);
    write_file("$dir/page.md", 'File $x_i + x_j$.');
    my $script = $rc_setup . <<'RC';
. ./bin/fltr_cache.rc
formatter=(fltr_cache $MARKDOWN_FORMATTER)
TMPDIR=$1
mathjax_init
$formatter $1/page.md
RC
    my @outputs;
    for (1..2) {
        my ($out, $err, $status) = command('', '/opt/plan9/bin/rc', '-c', $script, $dir);
        is $status, 0, 'cached rc run succeeded';
        is $err, '', 'no cached formatter errors';
        push @outputs, $out;
    }
    is $outputs[0], $outputs[1], 'repeat rendering is stable';
    like $outputs[0], qr/\Q\(x_i + x_j\)\E/, 'file equation preserved';
};
subtest 'full werc CGI request' => sub {
    my $dir = tempdir('test-XXXXXX', DIR => "$root/sites", CLEANUP => 1);
    make_path("$dir/_werc");
    write_file("$dir/_werc/config", "siteTitle='Math test'\nconf_enable_mathjax\n");
    write_file("$dir/index.md", "# Math test\n\nInline \$x_i + x_j\$.\n\n\$\$\\frac{1}{2}\$\$\n");
    write_file("$dir/table.md", "---\ntitle: Table properties\ntags: [physics, exam]\nkod:\n---\n\n| Formula | Description |\n| --- | --- |\n| \$x_i + x_j\$ | **Sum** |\n");
    my ($name) = $dir =~ m{/([^/]+)$};
    local %ENV = (%ENV, PLAN9 => '/opt/plan9', REQUEST_METHOD => 'GET',
        SERVER_NAME => "$name", SERVER_PORT => '8080', REQUEST_URI => '/', QUERY_STRING => '');
    TestAuth::authorize();
    chdir "$root/bin" or die $!;
    my ($out, $err, $status) = command('', './werc.cgi');
    chdir $root or die $!;
    is $status, 0, 'CGI succeeded';
    is $err, '', 'no CGI errors';
    like $out, qr/Content-Type: text\/html/, 'HTML response';
    like $out, qr/window.MathJax/, 'MathJax configured';
    my $loaders = () = $out =~ /<script defer/g;
    is $loaders, 1, 'one loader';
    like $out, qr/\Q\(x_i + x_j\)\E/, 'inline equation';
    like $out, qr/\Q\[\frac{1}{2}\]\E/, 'display equation';
    unlike $out, qr/werc-math-token/, 'no unprocessed tokens';
    $ENV{REQUEST_URI} = '/table';
    TestAuth::authorize();
    chdir "$root/bin" or die $!;
    my ($table, $table_err, $table_status) = command('', './werc.cgi');
    chdir $root or die $!;
    is $table_status, 0, 'table CGI launcher succeeded';
    is $table_err, '', 'no table CGI errors';
    like $table, qr/<th scope="col">Formula<\/th>/, 'CGI renders table headers';
    like $table, qr/class="markdown-properties"/, 'CGI renders front matter';
    like $table, qr/Table properties/, 'CGI preserves metadata title';
    like $table, qr/class="property-tag">physics/, 'CGI renders tag chips';
    like $table, qr/<td><span class="werc-math">\\\(x_i \+ x_j\\\)<\/span><\/td>/, 'CGI renders math in table cells';
    like $table, qr/<td><strong>Sum<\/strong><\/td>/, 'CGI renders inline table formatting';
};
subtest 'CLI UTF-8 and header escaping' => sub {
    my $source = 'Árvíztűrő tükörfúrógép: $α_i$';
    my $html = render($source);
    like $html, qr/Árvíztűrő tükörfúrógép:.*α_i/, 'Hungarian and Greek preserved';
    my ($headers, $err, $status) = command('', "$root/bin/contrib/md2html", '--mathjax-headers', q{https://example.org/"x'?a=1&b=2});
    is $status, 0, 'header CLI succeeded';
    is $err, '', 'no header warnings';
    like $headers, qr/&quot;x'\?a=1&amp;b=2/, 'URL safely escaped in a double-quoted attribute';
    like $headers, qr/inlineMath: \[\['\\\\\(', '\\\\\)'\]\]/, 'JavaScript delimiters keep their escaped backslashes';
};
done_testing;
