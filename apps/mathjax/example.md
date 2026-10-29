# MathJax examples

An inline equation: $E = mc^2$.

Subscripts survive Markdown: $x_i + x_j = x_{i+j}$.

The quadratic formula is $x = \frac{-b \pm \sqrt{b^2 - 4ac}}{2a}$.

## Display equations

$$
\int_0^\infty e^{-x^2}\,dx = \frac{\sqrt{\pi}}{2}
$$

$$
\begin{aligned}
x_i &= a_i + b_i \\
y_i &= x_i^2
\end{aligned}
$$

$$
A = \begin{pmatrix}1 & 2 \\ 3 & 4\end{pmatrix}
$$

## Ordinary text and code

**Bold text**, *emphasis*, and [a link](https://example.org) still work.

Costs \$5 and \$10. The expression `$x_i$` here is code.

    $this_is_code$

Fences are left alone by the math app; whether they render as code depends
on the site's configured Markdown formatter:

```tex
$x_i + x_j$
$$\frac{1}{2}$$
```
