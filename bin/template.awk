#!/bin/awk -f
# Keep adjacent text and simple variable expansions in one echo invocation.
# Other expressions and rc commands retain their original evaluation order.
function flush() {
    if (output != "") {
        print "echo -n " output
        output = ""
    }
}
function pr(str) {
    if (lastc == "(")
        expr = expr str
    else if (lastc == "{" || lastc == "%")
        printf "%s", str
    else {
        gsub(/'/, "''", str)
        if (output != "") output = output "^"
        output = output "'" str "'"
    }
}
function trans(c, name) {
    if (lastc == "(") {
        if (expr ~ /^\$"?[a-zA-Z_][a-zA-Z_0-9]*$/ && expr !~ /^\$"?status$/) {
            name = expr
            sub(/^\$"?/, "", name)
            if (output != "") output = output "^"
            output = output "$\"" name
        } else {
            flush()
            print "echo -n " expr
        }
    } else if (lastc == "{")
        print ""
    if (c == "{" || c == "%") flush()
    if (c == "(") expr = ""
    lastc = c
}
BEGIN { lastc = "}" }
END { flush(); if (lastc == "{") print "" }
/^%/ && $0 !~ /^%[{()}%]/ && lastc !~ /[({]/ {
    trans("%")
    print substr($0, 2)
    next
}
{
    if (lastc == "%") trans("}")
    n = split($0, a, "%")
    pr(a[1])
    for (i=2; i<=n; i++) {
        c = substr(a[i], 1, 1)
        rest = substr(a[i], 2)
        if ((lastc !~ "[({]" && c ~ "[({]") ||
            (lastc == "{" && c == "}") ||
            (lastc == "(" && c == ")"))
            trans(c)
        else if (c == "%") pr("%")
        else pr("%" c)
        pr(rest)
    }
    pr("\n")
    if (lastc !~ /[({]/ && length(output) > 8192) flush()
}
