<hr>

% notices_handler
<form action="" method="post">
% csrf_field
    <textarea name="comment_text" id="comment_text" cols="80" rows="16">%(`{echo -n $"saved_comment_text | escape_html}%)</textarea>
    <br>
    <input type="submit" name="bridge_post" value="Post a comment">

</form>
