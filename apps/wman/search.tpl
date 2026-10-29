% if(~ $werc_readonly yes) {
<p>Browse the manual pages directly; POST-based search is unavailable in read-only mode.</p>
% }
% if not {
<form action="" method="POST">
% csrf_field
<fieldset>
    <input type="text" name="wman_search" value="%($"s%)" />
    <input type="submit" name="go" value="Feel Lucky" />
    <input type="submit" value="Search" />

% if(! ~ $"post_arg_wman_search '') {
%   if(~ $"wman_search_results '') {
        No matches found for <i>'%(`{echo -n $"post_arg_wman_search | escape_html}%)'</i>.
%   }
%   if not {
        <ul>
%       echo $wman_search_results|awk -F/ '$(NF-1) ~ "^[0-9]+$" {printf "<li><a href=\"'$wman_base_uri'%s/%s\" />%s(%s)</a></li>", $(NF-1),$NF, $NF, $(NF-1)}'
        </ul>
%   }
% }

</fieldset>
</form>

% }
