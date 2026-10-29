<hr>
<h2>Comments</h2>

% for(c in `{ls $comments_dir/ | sed '/\/\./d'}) {
%    if(test -s $c/body) {
        <div class="comment">
            <h5>By: <i>%(`{local_read $c/user | escape_html}%)</i></b> (%(`{local_read $c/posted | escape_html}%))
        </h5>
%       local_read $c/body | escape_html | sed 's,$,<br>,'
        <hr></div>
%    }
% }

