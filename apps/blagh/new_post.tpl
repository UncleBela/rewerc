<div>
% notices_handler
<form method="POST">
% csrf_field
<fieldset>
    <legend>Submit a new blog post</legend>
    <textarea cols="94" rows="16" name="body">%(`{echo -n $"post_arg_body | escape_html}%)</textarea><br />
    <label>Title: <input size="64" type="text" name="title" value="%(`{echo -n $"post_arg_title | escape_html}%)" /></label>
    <label>Id: <input size="8" type="text" name="id" value="%(`{echo -n $"post_arg_id | escape_html}%)" /></label>
    <label>Date: <input size="10" maxlength="10" type="text" name="date" value="%(`{echo -n $"post_date | escape_html}%)" /></label>
    <input type="submit" value="Post" />
</fieldset></form>
</div>
