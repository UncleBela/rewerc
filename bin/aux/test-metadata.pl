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
my $dir = tempdir('metadata-test-XXXXXX', DIR => "$root/sites", CLEANUP => 1);
my ($name) = $dir =~ m{/([^/]+)$};
my $page = "$dir/page.md";
make_path("$dir/_werc");
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
sub cgi {
    my ($text, $settings) = @_;
    write_file("$dir/_werc/config", "siteTitle='Site title'\nconf_enable_mathjax\n" . ($settings // ''));
    write_file($page, $text);
    local %ENV = (%ENV, REQUEST_METHOD => 'GET', SERVER_NAME => "$name",
        SERVER_PORT => '8080', REQUEST_URI => '/page', QUERY_STRING => '');
    my ($out, $err, $status) = command('./bin/werc.cgi');
    is $status, 0, 'CGI succeeds';
    is $err, '', 'no CGI errors';
    return $out;
}

subtest 'publishing fields and custom properties work together' => sub {
    my $html = cgi("---\ntitle: Előkészület ZH-ra\ndescription: Formula notes & examples\ndate: 2026-10-26\nupdated: 2026-10-28\nauthor: Árvíztűrő\nlang: hu\ntags: [fizika, zh]\ntargy: Fizika mérnököknek I.\nkod:\n---\n\n# Body heading\n\n| Formula |\n| --- |\n| \$x_i\$ |\n");
    like $html, qr/<title>Előkészület ZH-ra<\/title>/, 'front matter sets browser title';
    like $html, qr/<html lang="hu">/, 'document language set';
    like $html, qr/<meta name="description" content="Formula notes &amp; examples">/, 'description escaped in head';
    like $html, qr/<meta name="author" content="Árvíztűrő">/, 'author metadata emitted';
    like $html, qr/<time datetime="2026-10-26">Published 2026-10-26<\/time>/, 'publication date rendered';
    like $html, qr/<time datetime="2026-10-28">Updated 2026-10-28<\/time>/, 'update date rendered';
    like $html, qr/By Árvíztűrő/, 'compact byline';
    my ($body) = $html =~ m{</head>(.*)}s;
    unlike $body, qr/Előkészület ZH-ra/, 'publishing title is not repeated visibly';
    like $body, qr/<h1 id="body-heading">Body heading(?: <a class="heading-permalink"[^>]*>#<\/a>)?<\/h1>/, 'author controls the visible heading';
    like $body, qr/<dt>targy<\/dt>/, 'custom properties retained';
    like $body, qr/class="property-empty"/, 'custom empty values retained';
    like $body, qr/class="property-tag">fizika/, 'tags retained';
    unlike $body, qr/<dt>(?:title|description|author|date|updated|lang)<\/dt>/, 'publishing fields do not clutter the panel';
    like $body, qr/class="werc-math">\\\(x_i\\\)/, 'table and math rendering still work';
};
subtest 'page values override site defaults; missing values inherit' => sub {
    my $settings = "meta_description='Default description'\nmeta_author='Default Author'\nhtml_lang=en-GB\n";
    my $html = cgi("---\ntitle: Page title\ndescription:\nlang: hu-HU\n---\n\nBody\n", $settings);
    like $html, qr/<title>Page title<\/title>/, 'page title wins';
    like $html, qr/content="Default description"/, 'empty description inherits default';
    like $html, qr/By Default Author/, 'missing author inherits default';
    like $html, qr/<html lang="hu-HU">/, 'page language overrides default';
    $html = cgi("Legacy title\n============\n\nBody\n", $settings);
    like $html, qr/<title>Legacy title<\/title>/, 'legacy opening title convention remains';
    like $html, qr/<html lang="en-GB">/, 'language inherits site default';
    unlike $html, qr/<time /, 'dates are not inferred from filesystem timestamps';
};
subtest 'absent fields preserve old title fallback without empty tags or bylines' => sub {
    my $html = cgi("# Body\n\nText\n");
    like $html, qr/<title>page<\/title>/, 'existing URL title fallback';
    like $html, qr/<html>/, 'unknown language is omitted';
    unlike $html, qr/name="(?:author|description)"/, 'absent metadata does not emit empty tags';
    unlike $html, qr/class="article-metadata"/, 'no empty byline';
};
subtest 'invalid dates and language fall back safely' => sub {
    my $html = cgi("---\ndate: 2025-02-29\nupdated: 2026-13-01\nlang: 'hu\" onload=\"bad'\n---\n\nBody\n", "html_lang=hu\nmeta_date=2024-02-29\n");
    like $html, qr/<html lang="hu">/, 'invalid language uses validated default';
    like $html, qr/datetime="2024-02-29"/, 'valid leap date used as fallback';
    unlike $html, qr/2025-02-29|2026-13-01|onload=/, 'invalid publishing values are not emitted';
};
subtest 'metadata is escaped data, never rc code' => sub {
    my $attack = "O'Brien'; /bin/touch $dir/injected; # <script>bad</script>";
    my $html = cgi("---\ntitle: \"$attack\"\nauthor: '<img src=x onerror=bad>'\ndescription: 'A \"quoted\" & <unsafe> description'\n---\n\nBody\n");
    ok !-e "$dir/injected", 'rc command injection did not execute';
    like $html, qr/O'Brien''?;.*&lt;script&gt;bad&lt;\/script&gt;/, 'title escaped without executing its content';
    unlike $html, qr/<script>bad|<img src=x/, 'author text cannot introduce HTML';
    like $html, qr/&quot;quoted&quot; &amp; &lt;unsafe&gt;/, 'attribute quotes and markup escaped';
};
subtest 'unsupported and oversized front matter have no partial effect' => sub {
    for my $source ("---\ntitle: Ignored\nparent:\n  child: nested\n---\n", "---\ntitle: Ignored\n", "---\ntitle: Ignored\nnote: " . ('x' x 65536) . "\n---\n") {
        write_file($page, $source);
        local $ENV{pageTitle} = 'Fallback';
        my ($out, $err, $status) = command('bin/contrib/md2html', '--page-metadata', $page);
        is $status, 0, 'bounded reader succeeds';
        is $err, '', 'no reader warnings';
        like $out, qr/pageTitle='Fallback'/, 'invalid block cannot partially override the title';
    }
};
subtest 'UTF-8 BOM, CRLF, quoted values and edits are supported' => sub {
    my $html = cgi("\x{FEFF}---\r\ntitle: 'O''Brien'\r\nlang: hu\r\n---\r\nBody\r\n");
    like $html, qr/<title>O'Brien<\/title>/, 'BOM and CRLF agree between early read and rendering';
    $html = cgi("---\ntitle: First\n---\nBody\n");
    like $html, qr/<title>First<\/title>/, 'first metadata value';
    $html = cgi("---\ntitle: Later\n---\nBody\n");
    like $html, qr/<title>Later<\/title>/, 'same-size edit immediately visible';
};
done_testing;
