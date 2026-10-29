#!/usr/bin/env perl
use strict;
use warnings;
use utf8;
use Test::More;
use FindBin;
use Cwd qw(abs_path);
use File::Temp qw(tempdir);
use File::Path qw(make_path remove_tree);
use IPC::Open3;
use IO::Select;
use Symbol qw(gensym);
use Encode qw(decode);

my $root = abs_path("$FindBin::Bin/../..");
chdir $root or die $!;
require "$root/bin/aux/TestAuth.pm";
TestAuth::setup($root);
my $dir = tempdir('template-test-XXXXXX', DIR => "$root/sites", CLEANUP => 1);
my $file = "$dir/page.tpl";
my $setup = <<'RC';
path=(/opt/plan9/bin . ./bin ./bin/contrib /usr/bin /bin)
. ./bin/cgilib.rc
. ./bin/werclib.rc
rcargs=-I
RC

sub write_file {
    my ($path, $text) = @_;
    open my $fh, '>:encoding(UTF-8)', $path or die $!;
    print {$fh} $text;
    close $fh or die $!;
}
sub run_rc {
    my ($script) = @_;
    my $err = gensym;
    my $pid = open3(my $in, my $out, $err, '/opt/plan9/bin/rc', '-I', '-c', $setup . $script);
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
sub check_template {
    my ($name, $source, $variables, $expected) = @_;
    subtest $name => sub {
        write_file($file, $source);
        for my $mode ('rcargs=-I', 'rcargs=()') {
            my ($out, $err, $status) = run_rc("$mode\n$variables\ntemplate $file\n");
            is $status, 0, "$mode succeeds";
            is $err, '', "$mode has no errors";
            is $out, $expected, "$mode preserves output";
        }
    };
}

check_template('literals and percent characters', "Árvíztűrő: it's 100%% ready.\n", '', "Árvíztűrő: it's 100%% ready.\n");
check_template('adjacent lists and empty variables', 'A%($value%)B%($empty%)C'."\n", "value=(one two)\nempty=()", "Aone twoBC\n");
check_template('quoted scalar values', 'X%($"value%)Y'."\n", "value=('one two' '&<>')", "Xone two &<>Y\n");
check_template('arbitrary argument lists', 'A%($a $b%)B'."\n", "a=one\nb=(two three)", "Aone two threeB\n");
check_template('indexed expansions', 'A%($value(2)%)B'."\n", 'value=(one two)', "AtwoB\n");
check_template('command substitution', 'A%(`{echo -n $value}%)B'."\n", "value='from command'", "Afrom commandB\n");
check_template('code blocks', "Before\n%{\nvalue='set by code'\n%}\nAfter %(\$value%)\n", '', "Before\n\nAfter set by code\n");
check_template('loops and conditionals', "% for(v in one two) {\n<%(\$v%)>\n% }\n% if(~ yes yes) {\nDone\n% }\n", '', "<one>\n<two>\nDone\n");

check_template('interpreter positional context', 'zero=%($0%) count=%($#*%) args=%($"*%)'."\n", '', "zero=/dev/stdin count=0 args=\n");

subtest 'template state and exit are isolated' => sub {
    write_file($file, "% value=inner\n% cd /\nInner %(\$value%)\n% exit 7\n");
    my ($out, $err, $status) = run_rc("value=outer\ntemplate $file\necho \$value\npwd\n");
    is $status, 0, 'caller continues after child exit';
    is $err, '', 'no errors';
    is $out, "Inner inner\nouter\n$root\n", 'variables and working directory do not leak';
};
subtest 'edits are visible in the same request' => sub {
    write_file($file, "First\n");
    my ($out, $err, $status) = run_rc("template $file\necho Later > $file\ntemplate $file\n");
    is $status, 0, 'render-edit-render succeeds';
    is $err, '', 'no errors';
    is $out, "First\nLater\n", 'same-size edits are visible without a cache or restart';
};
subtest 'custom interpreter flags retain the interpreter path' => sub {
    write_file($file, "Flag check\n");
    my ($out, $err, $status) = run_rc("rcargs=(-I -x)\ntemplate $file\n");
    is $status, 0, 'custom flags succeed';
    is $out, "Flag check\n", 'output unchanged';
    like $err, qr/echo -n/, 'requested rc tracing is honored';
};
subtest 'title extraction retains the opening-title convention' => sub {
    my @cases = (
        ["Title\n=====\n\nBody\n", "Title\n"],
        ["# Heading\n\nBody\n", ''],
        ["Title\n=====\nNo blank line\n", ''],
        ["Title\n=====\n", ''],
        ["First\n=====\n\nSecond\n=====\n\n", "First\n"],
        ["\nTitle\n=====\n\n", ''],
    );
    for my $case (@cases) {
        write_file("$dir/title.md", $case->[0]);
        my ($out, $err, $status) = run_rc("get_md_title $dir/title.md\n");
        is $status, 0, 'title lookup succeeds';
        is $err, '', 'title lookup has no errors';
        is $out, $case->[1], 'opening title behavior preserved';
    }
};

subtest 'sitemap template retains functions, nested calls and generated files' => sub {
    my ($name) = $dir =~ m{/([^/]+)$};
    make_path("$dir/_werc", "$dir/_runtime");
    write_file("$dir/_werc/config", "siteTitle='Template test'\nsitedir=sites/$name\nTMPDIR='$dir/_runtime'\n");
    write_file("$dir/example.md", "Fixture title\n=============\n\nBody.\n");
    TestAuth::authorize($name,$name);
    my ($out, $err, $status) = run_rc("PLAN9=/opt/plan9\nREQUEST_METHOD=GET\nSERVER_NAME='$name'\nSERVER_PORT=8080\nREQUEST_URI=/sitemap\nQUERY_STRING=''\n./bin/werc.cgi\n");
    is $status, 0, 'full sitemap CGI succeeds';
    is $err, '', 'no sitemap errors';
    like $out, qr/<h1>Site map<\/h1>/, 'global template renders';
    like $out, qr/Fixture title/, 'nested title extraction works';
    ok -s "$dir/_runtime/sitemaps/$name/sitemap.txt", 'text sitemap generated in private runtime';
    ok -s "$dir/_runtime/sitemaps/$name/sitemap.gz", 'compressed XML sitemap generated in private runtime';
    unlink "$dir/_runtime/sitemaps/$name/sitemap.txt", "$dir/_runtime/sitemaps/$name/sitemap.gz" or die $!;
    remove_tree("$dir/_runtime/fltr_cache");
    ($out, $err, $status) = run_rc("PLAN9=/opt/plan9\nWERC_HARDENED=yes\nREQUEST_METHOD=GET\nSERVER_NAME='$name'\nSERVER_PORT=8080\nREQUEST_URI=/sitemap\nQUERY_STRING=''\n./bin/werc.cgi\n");
    is $status, 0, 'hardened sitemap CGI succeeds';
    is $err, '', 'hardened sitemap has no errors';
    ok -s "$dir/_runtime/sitemaps/$name/sitemap.txt", 'hardened text export stays in scratch storage';
    ok -s "$dir/_runtime/sitemaps/$name/sitemap.gz", 'hardened compressed export stays in scratch storage';
    ok !-e "$dir/sitemap.txt" && !-e "$dir/sitemap.gz", 'hardened generation never writes exports into the site';
};
done_testing;
