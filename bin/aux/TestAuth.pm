# Test-only account/session fixtures. No runtime bypass flag is provided.
package TestAuth;
use strict;
use warnings;
use File::Temp qw(tempdir);
use IPC::Open3;
use Symbol qw(gensym);
my ($root,$directory,%cookies);
sub setup {
    $root=shift;
    $directory=tempdir('test-auth-XXXXXX',DIR=>"$root/var",CLEANUP=>1);
    chmod 0700,$directory or die $!;
    $ENV{WERC_AUTH_DIR}=$directory;
    $ENV{WERC_DEVELOPMENT}='yes';
    local $ENV{REQUEST_METHOD};delete $ENV{REQUEST_METHOD};
    system("$root/bin/contrib/werc-guard",'--disable-user','fixture')==0 or die 'auth fixture initialization failed';
}
sub authorize {
    my ($host,$site)=@_;
    $host //= $ENV{SERVER_NAME} // 'localhost';
    $site //= $ENV{WERC_SITE} // ($host eq 'localhost'?'rewerc-guide':$host);
    my $scope="$host:".($ENV{SERVER_PORT}//'8080')."/$site/".($ENV{HTTPS}//'');
    unless($cookies{$scope}) {
        open my $random,'<:raw','/dev/urandom' or die $!;
        read($random,my $bytes,32)==32 or die $!;close $random;
        my $token=unpack('H*',$bytes);
        my $script=<<'PY';
import sqlite3,sys,time,hashlib
p,scope,token=sys.argv[1:]
db=sqlite3.connect(p+'/auth.sqlite')
now=int(time.time())
db.execute("INSERT OR IGNORE INTO users VALUES('fixture',?, ?,1)",('0'*64,'0'*64))
db.execute('INSERT INTO sessions VALUES(?,?,?,?,?,?)',(hashlib.sha256(token.encode()).hexdigest(),scope,'fixture','a'*64,now+3600,now))
db.commit()
PY
        system('python3','-c',$script,$directory,$scope,$token)==0 or die 'auth fixture insert failed';
        $cookies{$scope}="werc_session=$token";
    }
    $ENV{HTTP_COOKIE}=$cookies{$scope};
}
1;
