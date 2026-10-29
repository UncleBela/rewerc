#!/usr/bin/env perl
use strict;
use warnings;
use Test::More;
use FindBin;
use Cwd qw(abs_path getcwd);
use File::Temp qw(tempdir);
use File::Path qw(make_path);
use File::Copy qw(copy);
use File::Find;
use IPC::Open3;
use IO::Select;
use Symbol qw(gensym);
use Time::HiRes qw(time);
use POSIX qw(mkfifo);

my $root = abs_path("$FindBin::Bin/../..");
chdir $root or die $!;
require "$root/bin/aux/TestAuth.pm";
TestAuth::setup($root);
make_path("$root/var");
my $scratch = tempdir('readonly-test-XXXXXX', DIR => "$root/var", CLEANUP => 1);
my $site = tempdir('readonly-site-XXXXXX', DIR => "$root/sites", CLEANUP => 1);
(my $name = $site) =~ s!.*/!!;
sub write_file {
    my ($path, $text) = @_;
    open my $file, '>:raw', $path or die "$path: $!";
    print {$file} $text;
    close $file or die $!;
}
sub read_file {
    open my $file, '<:raw', $_[0] or die $!;
    local $/;
    return <$file>;
}
sub command {
    my ($input, @args) = @_;
    my $err = gensym;
    local $SIG{ALRM} = sub { die "command timed out: @args\n" };
    local $SIG{PIPE} = 'IGNORE';
    alarm 10;
    my $pid = open3(my $in, my $out, $err, @args);
    print {$in} $input if length $input;
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
    my $status = $?;
    alarm 0;
    return ($stdout, $stderr, $status);
}
sub cgi {
    my ($uri, $method, $extra) = @_;
    local %ENV = (%ENV, PLAN9 => '/opt/plan9', SERVER_NAME => 'localhost',
        SERVER_PORT => '8080', WERC_SITE => $name, REQUEST_URI => $uri,
        QUERY_STRING => '', REQUEST_METHOD => $method // 'GET', %{ $extra // {} });
    TestAuth::authorize() unless exists($extra->{HTTP_COOKIE});
    return command('', './bin/werc.cgi');
}
sub snapshot {
    my %files;
    find(sub { $files{$File::Find::name} = read_file($File::Find::name) if -f $_ }, $site);
    return \%files;
}
make_path("$site/_werc");
write_file("$site/_werc/config", "siteTitle='Read-only fixture'\nconf_enable_mathjax\n");
write_file("$site/index.md", "# Public page\n\nEquation \$x_i\$.\n");

subtest 'GET renders and HEAD has no body or cookies' => sub {
    my ($out, $err, $status) = cgi('/');
    is $status, 0, 'GET succeeds';
    is $err, '', 'no diagnostics';
    like $out, qr/Public page/, 'article rendered';
    like $out, qr/werc-math/, 'native math integration retained';
    unlike $out, qr/Set-Cookie:/i, 'no session cookie created';
    my ($head, $head_err, $head_status) = cgi('/', 'HEAD');
    is $head_status, 0, 'HEAD succeeds';
    like $head, qr/Content-Type: text\/html/, 'HEAD retains headers';
    is((split /\r?\n\r?\n/, $head, 2)[1] // '', '', 'HEAD has no body');
    is $head_err, '', 'HEAD has no diagnostics';
    write_file("$site/asset.txt", 'static body');
    my ($static) = cgi('/asset.txt', 'HEAD');
    is((split /\r?\n\r?\n/, $static, 2)[1] // '', '', 'CGI static fallback also suppresses HEAD body');
};
subtest 'host and site are separate and redirects retain public hostname' => sub {
    make_path("$site/section");
    write_file("$site/section/index.md", "# Section\n");
    my ($out, $err, $status) = cgi('/section');
    is $status, 0, 'redirect succeeds';
    like $out, qr!Location: http://localhost:8080/section/!, 'hostname is not the site directory';
    my ($head) = cgi('/section', 'HEAD');
    like $head, qr!Location: http://localhost:8080/section/!, 'HEAD redirect retains target';
    is((split /\r?\n\r?\n/, $head, 2)[1] // '', '', 'HEAD redirect has no body');
    for my $bad ('../site', '/site', 'site/other', '', '.hidden') {
        my ($rejected, undef, $code) = cgi('/', 'GET', { WERC_SITE => $bad });
        isnt $code, 0, 'invalid site selector rejected';
        like $rejected, qr/Status: 400/, 'selector is checked before rc';
    }
    my ($fallback, undef, $code) = cgi('/', 'GET', { SERVER_NAME => $name, WERC_SITE => $name });
    is $code, 0, 'non-local public hostname also works without account state';
};
subtest 'all write methods rejected before body parsing or rc' => sub {
    for my $method ('POST', 'PUT', 'PATCH', 'DELETE', 'OPTIONS', '') {
        my ($out, $err, $status) = cgi('/', $method, {
            CONTENT_LENGTH => '999999999', CONTENT_TYPE => 'anything',
            security_ready => 'yes', security_user => 'admin', security_post => 'yes',
            werc_readonly => 'yes', post_arg_dirdir_save => 'Save',
        });
        isnt $status, 0, "$method fails";
        like $out, qr/Status: 405 Method Not Allowed/, 'method rejected, not parsed as a form';
        like $out, qr/Allow: GET, HEAD/, 'supported methods advertised';
        unlike $err, qr/admin|999999999/, 'no supplied credentials or values logged';
    }
};
subtest 'private, source, account and traversal paths are unavailable' => sub {
    for my $uri ('/../secret', '/%2e%2e/secret', '/_werc/config', '/%5fwerc/config',
                 '/.git/config', '/index.md', '/index.MD', '/secret.sqlite3',
                 '/_users/register', '/_users/private', '/%255fwerc/config',
                 '/x%00', '/x%ZZ', '/x%2', '/x\\y', '/pub/../_werc/config') {
        my ($out, undef, $status) = cgi($uri);
        isnt $status, 0, "$uri rejected";
        like $out, qr/^Status: (?:400|403|404)/, 'rejected before page rendering';
        unlike $out, qr/<!DOCTYPE/, 'no page template executed';
    }
    my ($head) = cgi('/_users/login', 'HEAD');
    is((split /\r?\n\r?\n/, $head, 2)[1] // '', '', 'rejected HEAD has no body either');
    my ($long) = cgi('/' . ('a' x 8192));
    like $long, qr/Status: 414/, 'oversized URI rejected';
    my ($encoded, $err, $status) = cgi('/%73ection/');
    is $status, 0, 'ordinary percent-encoded path supported';
    like $encoded, qr/Section/, 'decoded path selects the page';
    is $err, '', 'valid encoded path has no diagnostics';
};
subtest 'symlinks and special files in the selected site fail closed' => sub {
    write_file("$scratch/secret", 'private marker');
    symlink "$scratch/secret", "$site/leak.md" or die $!;
    my ($out, $err, $status) = cgi('/leak');
    isnt $status, 0, 'linked file rejected';
    unlike $out, qr/private marker/, 'target not disclosed';
    like $err, qr/symlink or special file/, 'fixed diagnostic identifies the category';
    unlink "$site/leak.md" or die $!;
    symlink $scratch, "$site/linked-dir" or die $!;
    my (undef, undef, $dir_status) = cgi('/');
    isnt $dir_status, 0, 'linked directory rejected';
    unlink "$site/linked-dir" or die $!;
    mkfifo("$site/pipe", 0600) == 0 or die $!;
    my (undef, undef, $fifo_status) = cgi('/');
    isnt $fifo_status, 0, 'FIFO rejected without blocking';
    unlink "$site/pipe" or die $!;
    symlink $site, "$site-alias" or die $!;
    my (undef, undef, $alias_status) = cgi('/', 'GET', { WERC_SITE => "$name-alias" });
    isnt $alias_status, 0, 'selected site root cannot be a symlink';
    unlink "$site-alias" or die $!;
};
subtest 'forged cookies and inherited identity cannot authenticate or write' => sub {
    my ($out, $err, $status) = cgi('/', 'GET', {
        HTTP_COOKIE => 'werc_user=admin:0:secret; werc_session=forged',
        security_user => 'admin', logged_user => 'admin', security_ready => 'yes',
        post_arg_dirdir_save => 'Save', post_arg_edit_text => 'forged replacement',
    });
    is $status, 0, 'forged session receives a controlled response';
    like $out,qr/Location: \/_users\/login/,'forged session cannot read protected content';
    unlike $out,qr/Public page/,'protected body withheld';
    is $err, '', 'no credentials in diagnostics';
    unlike $out, qr/Set-Cookie|forged replacement/, 'no session or submitted content used';
    like read_file("$site/index.md"), qr/Public page/, 'content unchanged';
};
subtest 'host and port validation protects generated URLs' => sub {
    for my $extra ({ SERVER_NAME => '../bad' }, { SERVER_NAME => "host\r\nInjected" },
                   { SERVER_PORT => '0' }, { SERVER_PORT => '65536' },
                   { SERVER_PORT => '8080\nHeader' }) {
        my ($out, undef, $status) = cgi('/', 'GET', $extra);
        isnt $status, 0, 'invalid URL environment rejected';
        like $out, qr/^Status: 400/, 'controlled error';
    }
};
subtest 'retained apps render content without submission controls' => sub {
    my %configs = (wiki => 'conf_enable_wiki admin', blog => 'conf_enable_blog',
                   comments => 'conf_enable_comments', paste => 'conf_enable_wercpaste');
    for my $app (sort keys %configs) {
        make_path("$site/$app/_werc");
        write_file("$site/$app/_werc/config", "$configs{$app}\n");
        write_file("$site/$app/index.md", "# Existing $app page\n");
    }
    make_path("$site/comments/_werc/comments/record");
    for my $entry ([user => 'author'], [posted => '2026-10-29'], [body => '<script>not executable</script>']) {
        write_file("$site/comments/_werc/comments/record/$entry->[0]", $entry->[1]);
    }
    my $before = snapshot();
    for my $app (sort keys %configs) {
        my ($out, $err, $status) = cgi("/$app/");
        is $status, 0, "$app read view succeeds";
        is $err, '', "$app has no runtime dependency on Python";
        unlike $out, qr/<form\b|Sign in|login<\/a>/i, 'no disabled write/login controls';
        if ($app eq 'comments') {
            like $out, qr/&lt;script&gt;not executable/, 'existing comments remain escaped';
        }
        my ($rejected, undef, $rejected_status) = cgi("/$app/", 'POST');
        isnt $rejected_status, 0, "$app POST rejected";
        like $rejected, qr/Status: 405/, 'no write app initialized';
    }
    is_deeply snapshot(), $before, 'GET/POST checks did not change any site files';
};
subtest 'direct rc entry and legacy write hooks cannot bypass read-only mode' => sub {
    local %ENV = (%ENV, REQUEST_METHOD => 'POST', PLAN9 => '/opt/plan9',
                   SERVER_NAME => 'localhost', security_ready => 'yes', security_user => 'admin');
    my ($out, $err, $status) = command('', '/opt/plan9/bin/rc', '-I', '-c', "cd bin\n. ./werc.rc\n");
    like $out, qr/Status: 405/, 'direct rc POST rejected';
    unlike $out, qr/<!DOCTYPE/, 'direct entry produces no page';
    my ($hooks, $hooks_err) = command('', '/opt/plan9/bin/rc', '-I', '-c', <<'RC');
. ./bin/werclib.rc
if(check_user admin) echo AUTH-BYPASS
if(secure_write wiki) echo WRITE-BYPASS
if(auth_user admin secret) echo PASSWORD-BYPASS
status=()
RC
    is $hooks, '', 'compatibility hooks always deny authentication and writes';
    is $hooks_err, '', 'hooks need no external helper';
    my $cwd = getcwd(); chdir $scratch or die $!;
    my ($legacy, $legacy_err, $legacy_status) = command('', '/opt/plan9/bin/rc', '-I', "$root/bin/aux/addwuser.rc", 'alice', 'do-not-log-me');
    chdir $cwd or die $!;
    isnt $legacy_status, 0, 'legacy plaintext account creation disabled';
    unlike $legacy . $legacy_err, qr/do-not-log-me/, 'password argument never displayed';
    ok !-e "$scratch/etc/users/alice/password", 'no credential file written';
};
subtest 'portable launcher uses authenticated native backend and initializes scratch storage' => sub {
    my $install = "$scratch/fresh-checkout";
    make_path("$install/bin/contrib", "$install/sites/example/a", "$install/plan9/bin", "$install/etc");
    copy('bin/werc.cgi', "$install/bin/werc.cgi") or die $!;
    copy('bin/contrib/werc-guard', "$install/bin/contrib/werc-guard") or die $!;
    chmod 0755, "$install/bin/werc.cgi", "$install/bin/contrib/werc-guard";
    write_file("$install/plan9/bin/rc", <<'SH');
#!/bin/sh
printf 'Content-Type: text/plain\n\n%s|%s|%s|%s|%s|%s\n' "$werc_readonly" "$werc_site" "$REQUEST_URI" "${security_user-unset}" "${HTTP_COOKIE-unset}" "${post_arg_edit_text-unset}"
SH
    chmod 0755, "$install/plan9/bin/rc";
    local %ENV = (%ENV, PLAN9 => "$install/plan9", SERVER_NAME => 'example',
        SERVER_PORT => '8080', WERC_SITE => 'example', REQUEST_URI => '/%61//b?x=1',
        REQUEST_METHOD => 'GET', security_user => 'admin', HTTP_COOKIE => 'credential',
        post_arg_edit_text => 'injected');
    TestAuth::authorize('example','example');
    ok !-e "$install/var", 'fresh install starts without runtime state';
    my ($out, $err, $status) = command('', "$install/bin/werc.cgi");
    is $status, 0, 'launcher and native guard execute from another directory';
    is $err, '', 'no Python/interpreter or database error';
    like $out, qr!yes\|example\|/a/b\?x=1\|unset\|unset\|unset!, 'guard normalizes paths and scrubs identity before rc';
    ok -d "$install/var/tmp", 'scratch directory initialized';
    ok !-e "$install/var/security", 'obsolete security directory is not recreated';
    my ($rejected, undef, $code) = cgi('/', 'POST');
    like $rejected, qr/Status: 405/, 'write rejection remains explicit';
};
subtest 'untrusted renderer emits escaped text only' => sub {
    my ($out, $err, $status) = command('<script>alert(1)</script> [x](javascript:alert(1)) &copy;',
        'bin/contrib/md2html', '--untrusted', '--math');
    is $status, 0, 'restricted rendering succeeds';
    is $err, '', 'no diagnostics';
    unlike $out, qr/<script|<a /, 'no authored HTML or generated links';
    like $out, qr/&lt;script&gt;/, 'HTML escaped';
    like $out, qr/&amp;copy;/, 'entities remain literal';
};
subtest 'parser limits reject without partial output' => sub {
    for my $case ([('[a](' x 8192), 'parsing work limit', []],
                  [('x' x (4*1024*1024+1)), '4 MiB input limit', ['--untrusted']]) {
        my ($out, $err, $status) = command($case->[0], 'bin/contrib/md2html', @{$case->[2]});
        isnt $status, 0, 'pathological/oversized input rejected';
        is $out, '', 'no partial HTML';
        like $err, qr/\Q$case->[1]\E/, 'limit explained';
    }
};
subtest 'adversarial parser scaling stays within the regression envelope' => sub {
    for my $case ([brackets => '['], [emphasis => '*a '], [ampersands => '& '], [dollars => '$a '], [html => 'a <a ']) {
        my @medians;
        for my $n (8192,32768) {
            my @times;
            for (1..3) {
                my $start = time;
                my (undef, $err, $status) = command($case->[1] x $n, 'bin/contrib/md2html', '--math');
                is $status, 0, "$case->[0] parser succeeds";
                push @times, time - $start;
            }
            @times = sort {$a <=> $b} @times;
            push @medians, $times[1];
        }
        my $limit = $medians[0]*8 > .15 ? $medians[0]*8 : .15;
        cmp_ok $medians[1], '<', $limit, "$case->[0] avoids old quadratic regression";
        diag sprintf '%s: 3-run parser medians %.2f/%.2f ms (%d/%d bytes)',
            $case->[0], $medians[0]*1000, $medians[1]*1000, length($case->[1])*8192, length($case->[1])*32768;
    }
};
done_testing;
