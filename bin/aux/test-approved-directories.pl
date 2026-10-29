#!/usr/bin/env perl
use strict;
use warnings;
use Test::More;
use FindBin;
use Cwd qw(abs_path);
use File::Temp qw(tempdir);
use File::Path qw(make_path remove_tree);
use File::Find;
use File::Copy qw(copy);
use IPC::Open3;
use IO::Select;
use Symbol qw(gensym);
use POSIX qw(mkfifo);

my $root = abs_path("$FindBin::Bin/../..");
chdir $root or die $!;
my $scratch = tempdir('approved-test-XXXXXX', DIR => "$root/var", CLEANUP => 1);
my $install = "$scratch/checkout";
make_path($install);
# Isolate the policy file and external notes from the working installation.
for my $folder (qw(bin lib tpl apps pub)) {
    find({no_chdir => 1, wanted => sub {
        my $source = $File::Find::name;
        (my $relative = $source) =~ s!^\Q$root\E/!!;
        my $target = "$install/$relative";
        if (-d $source) { make_path($target); }
        elsif (-f $source && !-l $source) {
            copy($source, $target) or die "$source: $!";
            chmod((stat($source))[2] & 0777, $target) or die $!;
        }
    }}, "$root/$folder");
}
make_path("$install/etc", "$install/sites/fixture/_werc", "$scratch/notes one", "$scratch/notes-two", "$scratch/notes one-sibling");
copy('etc/initrc', "$install/etc/initrc") or die $!;
sub write_file {
    my ($path, $data) = @_;
    open my $fh, '>:raw', $path or die "$path: $!";
    print {$fh} $data;
    close $fh or die $!;
}
write_file("$install/etc/initrc.local", <<'RC');
PLAN9=/opt/plan9
plan9port=$PLAN9
path=($plan9port/bin . ./bin ./bin/contrib /bin /usr/bin)
TMPDIR=$werc_root/var/tmp
formatter=(md2html)
rcargs=-I
debug=()
enabled_apps=()
RC
write_file("$install/sites/fixture/_werc/config", "siteTitle='Approved fixture'\nconf_enable_mathjax\n");
write_file("$install/sites/fixture/index.md", "# Home\n");
write_file("$scratch/notes one/article.md", "# Approved note\n\nEquation \$x_i\$.\n");
write_file("$scratch/notes one/index.md", "# Linked directory\n");
write_file("$scratch/notes-two/second.md", "# Second approved directory\n");
write_file("$scratch/notes one-sibling/secret.md", "# Must not escape\n");
my $policy = "$install/etc/APPROVED_DIRECTORIES";
sub policy {
    write_file($policy, $_[0]);
    chmod 0600, $policy or die $!;
}
sub request {
    my ($uri, $method) = @_;
    local %ENV = (%ENV, REQUEST_METHOD => $method // 'GET', SERVER_NAME => 'localhost',
        SERVER_PORT => '8080', REQUEST_URI => $uri, WERC_SITE => 'fixture',
        QUERY_STRING => '', PLAN9 => '/opt/plan9');
    local $SIG{ALRM} = sub { die "CGI timed out\n" };
    alarm 10;
    my $err = gensym;
    my $pid = open3(my $in, my $out, $err, "$install/bin/werc.cgi");
    close $in;
    my $stdout_fd = fileno $out;
    my $select = IO::Select->new($out, $err);
    my ($stdout, $stderr) = ('','');
    while (my @ready = $select->can_read) {
        for my $fh (@ready) {
            my $n = sysread($fh, my $buffer, 8192);
            die $! unless defined $n;
            if (!$n) { $select->remove($fh); close $fh; }
            elsif (fileno($fh) == $stdout_fd) { $stdout .= $buffer; }
            else { $stderr .= $buffer; }
        }
    }
    waitpid($pid,0);
    my $status = $?;
    alarm 0;
    return ($stdout,$stderr,$status);
}
sub rejected {
    my ($uri, $label, $pattern) = @_;
    my ($out,$err,$status) = request($uri);
    isnt $status, 0, $label;
    like $out, qr/Status: (?:403|404)/, 'controlled denial';
    unlike $out, qr/\Q$scratch\E/, 'physical paths not disclosed to clients';
    like $err, $pattern, 'fixed diagnostic' if $pattern;
}
my $linked = "$install/sites/fixture/article.md";
subtest 'missing and empty policy keep ordinary reading but reject links' => sub {
    my ($out,$err,$status) = request('/');
    is $status, 0, 'no policy required for normal pages';
    is $err, '', 'no diagnostic';
    symlink "$scratch/notes one/article.md", $linked or die $!;
    rejected('/article','external link not implicitly approved');
    policy('');
    rejected('/article','empty policy approves nothing');
    unlink $linked or die $!;
};
subtest 'one absolute directory per line supports spaces comments and CRLF' => sub {
    policy("# Public notes only\r\n\r\n$scratch/notes one\r\n$scratch/notes-two\r\n");
    symlink "$scratch/notes one/article.md", $linked or die $!;
    symlink "$scratch/notes-two/second.md", "$install/sites/fixture/second.md" or die $!;
    for my $case (['/article','Approved note'],['/second','Second approved directory']) {
        my ($out,$err,$status) = request($case->[0]);
        is $status, 0, 'approved link rendered through actual CGI';
        is $err, '', 'no diagnostic';
        like $out, qr/\Q$case->[1]\E/, 'correct target content';
        unlike $out, qr/\Q$scratch\E/, 'approval paths do not enter HTML';
    }
    my ($out) = request('/article');
    like $out, qr/werc-math/, 'linked note retains native math';
    like $out, qr!href="/article"!, 'file link joins navigation';
    unlink "$install/sites/fixture/second.md" or die $!;
};
subtest 'directory links include navigation and nested Markdown pages' => sub {
    symlink "$scratch/notes one", "$install/sites/fixture/vault" or die $!;
    my ($home,$err,$status) = request('/');
    is $status, 0, 'site remains readable with an approved directory link';
    is $err, '', 'no diagnostic';
    like $home, qr!href="/vault/"!, 'linked directory appears in sidebar';
    my ($index) = request('/vault/');
    like $index, qr/Linked directory/, 'directory index rendered';
    my ($page,undef,$code) = request('/vault/article');
    is $code, 0, 'nested note rendered';
    like $page, qr/Approved note/, 'nested body correct';
    my ($redirect) = request('/vault');
    like $redirect, qr!Location: http://localhost:8080/vault/!, 'canonical public URL kept';
    my ($sitemap, $sitemap_err, $sitemap_status) = request('/sitemap');
    is $sitemap_status, 0, 'sitemap supports approved directory links';
    is $sitemap_err, '', 'sitemap has no diagnostics';
    like $sitemap, qr!href="/vault/article"!, 'sitemap traverses linked notes';
};
subtest 'nested links cannot escape by sibling prefix or another directory' => sub {
    symlink "$scratch/notes one-sibling/secret.md", "$scratch/notes one/escape.md" or die $!;
    rejected('/vault/escape','component boundary rejects a similarly named sibling');
    unlink "$scratch/notes one/escape.md" or die $!;
    symlink 'article.md', "$scratch/notes one/nested.md" or die $!;
    my ($out,$err,$status) = request('/vault/nested');
    is $status, 0, 'nested link within approved roots works';
    like $out, qr/Approved note/, 'nested target resolved';
    is $err, '', 'no diagnostic';
    unlink "$scratch/notes one/nested.md" or die $!;
};
subtest 'cycles broken links and special files fail closed' => sub {
    symlink '.', "$scratch/notes one/loop" or die $!;
    rejected('/vault/','directory cycle rejected',qr/cycle/);
    unlink "$scratch/notes one/loop" or die $!;
    symlink 'missing.md', "$scratch/notes one/missing.md" or die $!;
    rejected('/vault/','broken nested link rejected');
    unlink "$scratch/notes one/missing.md" or die $!;
    mkfifo("$scratch/notes one/pipe",0600)==0 or die $!;
    rejected('/vault/','special file rejected without reading');
    unlink "$scratch/notes one/pipe" or die $!;
};
subtest 'approved content cannot supply executable werc configuration or templates' => sub {
    make_path("$scratch/notes one/_werc");
    write_file("$scratch/notes one/_werc/config", "echo EXECUTED-INJECTION\n");
    rejected('/vault/', 'external _werc directory is not executed',qr/configuration/);
    remove_tree("$scratch/notes one/_werc");
    write_file("$scratch/notes one/evil.tpl", "% echo EXECUTED-INJECTION\n");
    rejected('/vault/evil','external template cannot execute',qr/configuration/);
    unlink "$scratch/notes one/evil.tpl" or die $!;
    write_file("$scratch/notes-two/config", "echo EXECUTED-INJECTION\n");
    symlink "$scratch/notes-two/config", "$install/sites/fixture/_werc/linked" or die $!;
    rejected('/','local configuration directory cannot contain links',qr/configuration and template symlinks/);
    unlink "$install/sites/fixture/_werc/linked" or die $!;
};
subtest 'application-private files cannot be approved into public aliases' => sub {
    unlink $linked or die $!;
    symlink $policy,$linked or die $!;
    policy("$install/etc\n$scratch/notes one\n");
    rejected('/article','private policy cannot be disguised as Markdown',qr/publication policy/);
    unlink $linked or die $!;
    write_file("$install/etc/private.md", "PRIVATE DATA\n");
    symlink "$install/etc/private.md",$linked or die $!;
    rejected('/article','project-private configuration cannot be disguised as notes',qr/application-private/);
    unlink $linked or die $!;
    policy("$scratch/notes one\n$scratch/notes-two\n");
};
subtest 'the policy is not an HTTP endpoint at any depth' => sub {
    for my $uri ('/APPROVED_DIRECTORIES','/etc/APPROVED_DIRECTORIES','/vault/APPROVED_DIRECTORIES','/%41PPROVED_DIRECTORIES') {
        my ($out,undef,$status) = request($uri);
        isnt $status, 0, 'policy URL blocked';
        like $out, qr/Status: 404/, 'not found to clients';
    }
    my ($head) = request('/APPROVED_DIRECTORIES','HEAD');
    is((split /\r?\n\r?\n/,$head,2)[1] // '', '', 'denied HEAD has no body');
};
subtest 'policy must be private owned regular data not a symlink or FIFO' => sub {
    chmod 0644,$policy or die $!;
    rejected('/','world-readable approval file rejected',qr/owner-only/);
    chmod 0600,$policy or die $!;
    rename $policy,"$policy.saved" or die $!;
    symlink "$policy.saved",$policy or die $!;
    rejected('/','symlinked approval file rejected',qr/cannot open private/);
    unlink $policy or die $!;
    mkfifo($policy,0600)==0 or die $!;
    rejected('/','FIFO policy rejected without blocking',qr/owner-only/);
    unlink $policy or die $!;
    rename "$policy.saved",$policy or die $!;
};
subtest 'invalid entries fail rather than being interpreted as code' => sub {
    for my $line ('relative/path', '~/.notes', '$HOME/notes', '/', "$scratch/notes one/../notes-two", "$scratch/missing", 'echo injected', "bad\0data") {
        policy("$line\n");
        rejected('/','invalid entry rejected');
    }
    symlink "$scratch/notes one","$scratch/root-alias" or die $!;
    policy("$scratch/root-alias\n");
    rejected('/','approved root must not itself traverse symlinks');
    unlink "$scratch/root-alias" or die $!;
    policy('x' x 65537);
    rejected('/','oversized policy rejected',qr/size limit/);
    policy("$scratch/notes one\n");
};
subtest 'policy changes take effect on the next request and writes remain forbidden' => sub {
    policy("$scratch/notes one\n");
    my (undef,undef,$ok) = request('/vault/article');
    is $ok, 0, 'allowed before removal';
    policy('');
    rejected('/vault/article','removing an entry revokes access immediately');
    my ($out,undef,$status) = request('/vault/article','POST');
    isnt $status, 0, 'POST still rejected';
    like $out, qr/Status: 405/, 'read-only method check precedes policy';
};
done_testing;
