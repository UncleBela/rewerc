<hr>
<h2>Comments</h2>

% for(c in `{ls $comments_dir/ | sed '/\/\./d'}) {
%    if(test -s $c/body) {
        <div class="comment">
            <h5>By: <i>%(`{cat $c/user | escape_html}%)</i></b> (%(`{cat $c/posted | escape_html}%))
        </h5>
%       cat $c/body | escape_html | sed 's,$,<br>,'
        <hr></div>
%    }
% }

