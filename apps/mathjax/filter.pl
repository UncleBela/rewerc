#!/usr/bin/env perl
package WercMathJax;
use strict;
use warnings;
use utf8;
use Digest::SHA qw(sha256_hex);
use Encode qw(encode decode FB_CROAK);

# All helpers operate on Unicode text; only the CLI and payload use UTF-8 bytes.
sub escaped {
    my ($text, $pos) = @_;
    my $start = $pos;
    --$start while $start && substr($text, $start - 1, 1) eq '\\';
    return ($pos - $start) % 2;
}

sub protect {
    my ($text) = @_;
    # Stable placeholders preserve reuse of werc's optional formatter cache.
    my $nonce = substr(sha256_hex(encode('UTF-8', $text)), 0, 32);
    my ($out, $i, $length) = ('', 0, length($text));
    # Properties are plain metadata; leave dollar signs for the front matter reader.
    if ($text =~ /\A(?:\x{FEFF}|\xEF\xBB\xBF)?---[ \t]*\r?\n.*?\r?\n---[ \t]*(?:\r?\n|\z)/s) {
        $i = $+[0];
        $out = substr($text, 0, $i);
    }

    while ($i < $length) {
        pos($text) = $i;
        if ($i == 0 || substr($text, $i - 1, 1) eq "\n") {
            # Skip code fences, including common blockquote and list forms.
            if ($text =~ /\G(?: {0,3}>[ \t]?)* {0,3}(?:[-+*] |\d+[.)] )?(`{3,}|~{3,})[^\n]*(?:\n|\z)/gc) {
                my ($marker, $after) = ($1, pos($text));
                my $char = substr($marker, 0, 1);
                my $count = length($marker);
                my $rest = substr($text, $after);
                my $stop = $length;
                if ($rest =~ /^(?: {0,3}>[ \t]?)*[ \t]*\Q$char\E{$count,}[ \t]*(?:\n|\z)/m) {
                    $stop = $after + $+[0];
                }
                $out .= substr($text, $i, $stop - $i);
                $i = $stop;
                next;
            }
            if (substr($text, $i, 4) eq '    ' || substr($text, $i, 1) eq "\t") {
                my $stop = index($text, "\n", $i);
                $stop = $stop < 0 ? $length : $stop + 1;
                $out .= substr($text, $i, $stop - $i);
                $i = $stop;
                next;
            }
        }
        # Tags/attributes and raw HTML code-like regions remain literal.
        pos($text) = $i;
        if ($text =~ /\G(?:<!--.*?-->|<(pre|code|script|style|textarea)\b[^>]*>.*?<\/\1\s*>|<\/?[A-Za-z][^>]*>)/gcis) {
            my $stop = pos($text);
            $out .= substr($text, $i, $stop - $i);
            $i = $stop;
            next;
        }
        if (substr($text, $i, 1) eq '`' && !escaped($text, $i)) {
            pos($text) = $i;
            $text =~ /\G(`+)/gc;
            my $run = $1;
            my $after = $i + length($run);
            my $rest = substr($text, $after);
            if ($rest =~ /(?<!`)\Q$run\E(?!`)/) {
                my $stop = $after + $+[0];
                $out .= substr($text, $i, $stop - $i);
                $i = $stop;
                next;
            }
        }
        if (substr($text, $i, 1) ne '$' || escaped($text, $i)) {
            $out .= substr($text, $i++, 1);
            next;
        }
        my $display = substr($text, $i, 2) eq '$$';
        my $delimiter = $display ? '$$' : '$';
        my $start = $i + length($delimiter);
        my ($end, $found) = ($start, 0);
        if ($display || ($start < $length && substr($text, $start, 1) !~ /\s/)) {
            while ($end < $length) {
                last if !$display && substr($text, $end, 1) eq "\n";
                if (substr($text, $end, length($delimiter)) eq $delimiter && !escaped($text, $end)) {
                    if ($display || ($end > $start && substr($text, $end - 1, 1) !~ /\s/
                        && ($end + 1 == $length || substr($text, $end + 1, 1) !~ /\p{Digit}/))) {
                        $found = 1;
                        last;
                    }
                }
                ++$end;
            }
        }
        if ($found && $end > $start) {
            my $tag = $display ? 'div' : 'span';
            my $payload = unpack('H*', encode('UTF-8', substr($text, $start, $end - $start)));
            my $token = qq{<$tag class="werc-math-token">WERCMA${nonce}Z${payload}END</$tag>};
            $out .= $display ? "\n\n$token\n\n" : $token;
            $i = $end + length($delimiter);
        } else {
            $out .= $delimiter;
            $i = $start;
        }
    }
    return $out;
}

sub html_escape {
    my ($text, $attribute) = @_;
    $text =~ s/&/&amp;/g;
    $text =~ s/</&lt;/g;
    $text =~ s/>/&gt;/g;
    if ($attribute) {
        $text =~ s/"/&quot;/g;
        $text =~ s/'/&#x27;/g;
    }
    return $text;
}

sub restored_token {
    my ($tag, $payload) = @_;
    my $tex = html_escape(decode('UTF-8', pack('H*', $payload), FB_CROAK));
    my ($left, $right) = $tag eq 'div' ? ('\\[', '\\]') : ('\\(', '\\)');
    return qq{<$tag class="werc-math">$left$tex$right</$tag>};
}

sub restore {
    my ($text) = @_;
    $text =~ s{<(span|div) class="werc-math-token">WERCMA([a-f0-9]+)Z([a-f0-9]*)END</\1>}{restored_token($1, $3)}ge;
    return $text;
}

sub headers {
    my ($url) = @_;
    my $config = <<'JS';
<script>
window.MathJax = {
  tex: {inlineMath: [['\\(', '\\)']], displayMath: [['\\[', '\\]']],
        processEscapes: true, processEnvironments: false, processRefs: false},
  options: {ignoreHtmlClass: '.*', processHtmlClass: 'werc-math'}
};
</script>
JS
    return $config . '<script defer src="' . html_escape($url, 1) . '"></script>';
}

sub main {
    my $mode = shift @ARGV // '';
    binmode STDOUT, ':encoding(UTF-8)';
    if ($mode eq 'headers' && @ARGV == 1) {
        print headers(decode('UTF-8', $ARGV[0], FB_CROAK)), "\n";
    } elsif (($mode eq 'protect' || $mode eq 'restore') && !@ARGV) {
        binmode STDIN, ':raw';
        local $/;
        my $text = decode('UTF-8', scalar(<STDIN>) // '', FB_CROAK);
        print $mode eq 'protect' ? protect($text) : restore($text);
    } else {
        die "Usage: perl filter.pl protect|restore|headers URL\n";
    }
}

main() unless caller;
1;
