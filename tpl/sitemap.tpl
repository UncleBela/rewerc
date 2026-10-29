<h1>Site map</h1>

%{
tmpfile=$TMPDIR/werc_sitemap_$pid.txt
tmpfilex=$TMPDIR/werc_sitemapx_$pid.txt
saveddf=$dirfilter
sitemap_output=$TMPDIR/sitemaps/$site
mkdir -p $sitemap_output

MON2NUM='s/Jan/01/; s/Feb/02/; s/Mar/03/; s/Apr/04/; s/May/05/; s/Jun/06/; s/Jul/07/; s/Aug/08/; s/Sep/09/; s/Oct/10/; s/Nov/11/; s/Dec/12/;'

fn get_mdate {
    t=`{$werc_root/bin/contrib/werc-guard --mtime $1}
    t=`{date $t(1) | sed -e $MON2NUM -e 's/  ([0-9]) / 0\1 /g'} # Make sure day of the month is two digits.
    echo $t(6)^'-'^$t(2)^'-'^$t(3)
}

fn listDir {
    d=$1
    dirfilter=$saveddf
    load_config $d/_werc/config

    if(~ $#perm_redir_to 0) {
        echo '<ul class="sitemap-list">'

        ifs=$NEW_LINE { entries=`{list_content $d >[2]/dev/null | sed $dirfilter'/\/[^_.\/][^\/]*(\.(md|txt|html)|\/)$/!d' | path_lines | cut -f2} }
        for(i in $entries) {
            if(! ~ $i */ && test -d $i/.)
                i=$i^'/'
            desc=`{get_file_title $i}
            ifs=$NEW_LINE { u=`{echo $i|sed 's!'$sitedir'!!; '$dirclean's!/index$!/!; '} }
            if(! ~ $#desc 0 && ! ~ $desc '')
                desc=' - '$"desc
            n=`{echo /$u|sed 's/[\-_]/ /g; s,.*/([^/]+)/?$,\1,'}
            public_url=`{url_path $u}
            echo '<li><a href="'$public_url'">'^$"n^'</a>' $"desc '</li>'
            echo $base_url^$public_url >> $tmpfile
            echo '<url><loc>'$base_url^$public_url'</loc><lastmod>'^`{get_mdate $i}^'</lastmod></url>' >> $tmpfilex
            if(test -d $i/.)
                @{ listDir $i }
        }
        echo '</ul>'
    }
}


listDir $sitedir/

if(test -s $tmpfile) {
    mv $tmpfile $sitemap_output/sitemap.txt &
}
if not if(test -f $tmpfile)
    rm $tmpfile

if(test -s $tmpfilex) {
    {
        echo '<?xml version="1.0" encoding="UTF-8"?>
<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">'

        cat $tmpfilex
        rm $tmpfilex &
        echo '</urlset>'
        # TODO Enable automaic search engine update notification.
        #hget 'http://google.com/ping?sitemap='^`{url_encode $base_url'/sitemap.gz'} > /dev/null

    } | gzip > $sitemap_output/sitemap.gz &
    #} > $sitedir/sitemap.xml &
}
if not if(test -f $tmpfilex)
    rm $tmpfilex

%}
