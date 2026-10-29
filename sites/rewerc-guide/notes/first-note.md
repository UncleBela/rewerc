---
title: Guide to using rewerc
course: You could write a course name here
code: Maybe a course code here
teacher: Or even your professor's name.
description: All of this is displayed at top, in rendered view.
lang: en
lang_info: It is very important you specify a language. By default, it is set English.
---

# Metadata
In the metadata section of every note, you may write anything with a ':'. This means that as shown above, I could write `course`, `code`, `teacher` and many other "tags". It is useful if you want to keep track of your notes in a nice way. The only metadata properties that will not be visible there, are: `title` and `lang`.

`lang` can take as many values as you imagine. But it's practical to type values of actual language codes accordingly to HTML, such as `en` for English, `hu` for Hungarian, or your choice of language. You can inspect the HTML code of this site and see for yourself!

## How the title of the page is served
You may see "Guide to using rewerc" as the page's title, in rendered view. That is because we set the title to that exact string, in the metadata section in the markdown file itself. The title section of the metadata is not visible in rendered view.

## TOC (Table of Contents)
TOC will be visible by default on all notes. If you'd like to disable it, you'll have to edit the metadata on each note by inputting `toc: false`. Otherwise, rewrite the `md2html` CPP file. Currently, correct TOC translations are implemented only for the Hungarian and English language. For Hungarian, instead of "Contents", it would say "Tartalomjegyzék".

# Headings (Heading level-1)
Headings can be referenced. This means that if you were to hover your mouse over the heading in the rendered view, you will see a hashmark (#) pop up next to it. Click it, and your browser's url will change. You can copy that path now to your friends, and and if they click it, that specific heading will show up first on their end!

You can go as low as heading level-6, as demonstrated below:

## Heading level-2
### Heading level-3
#### Heading level-4
##### Heading level-5
###### Heading level-6

# Table
| Syntax      | Description | Test Text     |
| :---        |    :----:   |          ---: |
| Header      | Title       | Here's this   |
| Paragraph   | Text        | And more      |

# Mathematical equations & units with MathJax

**Velocity:**

$$
v\,[\mathrm{m/s}]=\frac{v\,[\mathrm{km/h}]}{3{,}6}
$$

**Weight force:**

$$
G=mg,\qquad m=\frac{G}{g}
$$

# Callouts
> [!warning] Warning
> Callouts must be called with appropriate syntax.

> [!tip] Tip
> It is recommended that you pay attention to this whole chapter.

> [!note] Note
> Callouts follow the exact same syntax as in Obsidian.
 
> [!info] Info
> Additional information for a datum.

> [!abstract] Abstract
> You could format this as if it was a study abstract.

> [!todo] To-Do
> Buy groceries for my dog.

> [!question] Question
> What is $2 + 2$?

> [!failure] Failure
> Something has gone terribly wrong!

> [!danger] Danger
> This might be very bad!

> [!bug] Bug
> You should look out for this, as this is a bug in the system.

> [!example] Example
> For example: 2 is the square root of 4.

> [!quote] Quote
> I think, therefore I am.

> [!success] Success!
> You've successfully read the Callouts chapter!

# Footnotes
For this chapter, we'll look at how footnotes work.

Here, you'll see that I'm placing a footnote with a name of "reference"[^reference]

This is yet another footnote.[^yetanother]

[^reference]: Yes, this really is a reference to that text above.
[^yetanother]: It is not needed to number footnotes. Just address them a unique ID.

## Some other, completely unrelated chapter for footnotes
This is a sub-chapter, so you can see clearly how footnotes can actually be put in the very same chapter, and they will be displayed at the bottom automatically. It is recommended to name the very last chapter "Footnotes" for this exact reason, if you intend on using footnotes in your note at all.
