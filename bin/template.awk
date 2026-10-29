#!/bin/awk -f
# Translate templates afresh on every request. Batch text and simple variables
# into one output command; arbitrary rc expressions and code remain boundaries.
function text_piece(quoted) {
    if(literal == "")
        return
    quoted = literal
    gsub(/'/, "''", quoted)
    piece("'" quoted "'")
    literal = ""
}
function piece(expr) {
    output = output (output == "" ? "" : "^") expr
}
function flush_output() {
    text_piece()
    if(output != "")
        print "echo -n " output
    output = ""
}
function expression(expr) {
    # Quoted rc expansion joins lists exactly as echo does, while retaining
    # an empty value instead of erasing neighbouring literal text.
    expr = segment
    if(expr ~ /^[ \t]*\$"?[a-zA-Z_][a-zA-Z0-9_]*[ \t]*$/) {
        sub(/^[ \t]*\$"?/, "", expr)
        sub(/[ \t]*$/, "", expr)
        piece("$\"" expr)
    }
    else {
        flush_output()
        print "echo -n " segment
    }
    segment = ""
}
function pr(str) {
    if(lastc ~ /[({]/)
        segment = segment str
    else
        literal = literal str
}
function trans(c) {
    if(lastc == "(")
        expression()
    else if(lastc == "{") {
        print segment
        segment = ""
    }
    else
        text_piece()

    if(c == "{" || c == "%")
        flush_output()
    lastc = c
}
BEGIN {
    lastc = "}"
}
END {
    if(lastc == "(")
        expression()
    else if(lastc == "{")
        print segment
    flush_output()
}
/^%/ && $0 !~ /^%[{()}%]/ && lastc !~ /[({]/ {
    trans("%")
    print substr($0, 2)
    next
}
{
    if(lastc == "%")
        trans("}")

    n = split($0, a, "%")
    pr(a[1])
    for(i=2; i<=n; i++) {
        c = substr(a[i], 1, 1)
        rest = substr(a[i], 2)
        if((lastc !~ /[({]/ && c ~ /[({]/) ||
           (lastc == "{" && c == "}") ||
           (lastc == "(" && c == ")"))
            trans(c)
        else if(c == "%")
            pr("%")
        else
            pr("%" c)
        pr(rest)
    }
    pr("\n")
}
