<!DOCTYPE HTML>
<html%($html_language_attribute%)>
<head>

    <title>%($pageTitle%)</title>

    <link rel="stylesheet" href="/pub/style/style.css" type="text/css" media="all" title="default">
    <link rel="shortcut icon" href="/favicon.ico" type="image/vnd.microsoft.icon">
% if(test -f $sitedir/_werc/pub/style.css)
%    echo '    <link rel="stylesheet" href="/_werc/pub/style.css" type="text/css" media="screen" title="default">'

    <meta charset="UTF-8">
% # Legacy charset declaration for backards compatibility with non-html5 browsers.
    <meta http-equiv="Content-Type" content="text/html; charset=UTF-8"> 

% if(! ~ $"meta_description '')
%   echo '    <meta name="description" content="'$"meta_description'">'
% if(! ~ $"meta_author '')
%   echo '    <meta name="author" content="'$"meta_author'">'
% if(! ~ $#meta_keywords 0)
%   echo '    <meta name="keywords" content="'$"meta_keywords'">'

% h = `{get_lib_file headers.inc}
% if(! ~ $#h 0)
%   cat $h

    %($"extraHeaders%)

</head>
<body>

