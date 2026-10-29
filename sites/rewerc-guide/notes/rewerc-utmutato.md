---
title: Útmutató a rewerc használatához
course: Ide írhatod a tantárgy nevét
code: Ide kerülhet például a tantárgy kódja
teacher: Vagy akár az oktatód neve.
description: Mindez az oldal tetején jelenik meg a megjelenített nézetben.
lang: hu
lang_info: Nagyon fontos, hogy megadd a nyelvet. Alapértelmezés szerint angolra van állítva.
---

# Metaadatok
Minden jegyzet metaadatai közé írhatsz tetszőleges, kettősponttal (`:`) elválasztott mezőket. Ahogy a fenti példában is látható, megadhatod a `course`, a `code`, a `teacher` mezőt és még sok más „címkét”. Ez akkor hasznos, ha szeretnéd rendezett formában nyilvántartani a jegyzeteidet. Az itt meg nem jelenő metaadatmezők a `title` és a `lang`.

A `lang` mezőhöz sokféle értéket megadhatsz, de érdemes a HTML-ben használt valódi nyelvkódok közül választani: például `en` az angolhoz, `hu` a magyarhoz, vagy a választott nyelv megfelelő kódja. Az oldal HTML-forrását megvizsgálva magad is ellenőrizheted az eredményt!

## Az oldal címének megjelenítése
Az oldal címeként az „Útmutató a rewerc használatához” szöveget láthatod. Ez azért van így, mert a Markdown-fájl metaadatai között pontosan ezt az értéket adtuk meg címként. A metaadatok címmezője nem jelenik meg a megjelenített nézetben.

## Tartalomjegyzék (TOC)
A tartalomjegyzék alapértelmezés szerint minden jegyzetben megjelenik. Ha szeretnéd kikapcsolni, az adott jegyzet metaadatai közé írd be a `toc: false` sort. Másik lehetőségként módosíthatod az `md2html` C++-forrásfájlt. Jelenleg a tartalomjegyzék magyar és angol fordítása érhető el. Magyar nyelv esetén a „Contents” helyett a „Tartalomjegyzék” felirat jelenik meg.

# Címsorok (1. szintű címsor)
A címsorokra hivatkozni is lehet. Ha a megjelenített oldalon a címsor fölé viszed az egérmutatót, egy kettőskereszt (#) jelenik meg mellette. Kattints rá, és megváltozik a böngésző címsorában látható URL. Ezt a hivatkozást már elküldheted az ismerőseidnek: ha megnyitják, az oldal közvetlenül az adott címsorhoz ugrik!

Egészen a 6. szintű címsorig elmehetsz, ahogy az alábbi példa is mutatja:

## 2. szintű címsor
### 3. szintű címsor
#### 4. szintű címsor
##### 5. szintű címsor
###### 6. szintű címsor

# Táblázat
| Szintaxis   | Leírás      | Próbaszöveg   |
| :---        |    :----:   |          ---: |
| Fejléc      | Cím         | Íme egy példa |
| Bekezdés    | Szöveg      | És még több   |

# Matematikai egyenletek és mértékegységek MathJax segítségével

**Sebesség:**

$$
v\,[\mathrm{m/s}]=\frac{v\,[\mathrm{km/h}]}{3{,}6}
$$

**Súlyerő:**

$$
G=mg,\qquad m=\frac{G}{g}
$$

# Kiemelt blokkok (calloutok)
> [!warning] Figyelmeztetés
> A kiemelt blokkokat a megfelelő szintaxissal kell megadni.

> [!tip] Tipp
> Érdemes figyelmesen elolvasnod ezt az egész fejezetet.

> [!note] Megjegyzés
> A kiemelt blokkok szintaxisa pontosan megegyezik az Obsidianban használttal.

> [!info] Információ
> Kiegészítő információ egy adathoz.

> [!abstract] Összefoglaló
> Ezt akár egy tanulmány rövid összefoglalójaként is megfogalmazhatod.

> [!todo] Teendő
> Bevásárolni a kutyámnak.

> [!question] Kérdés
> Mennyi $2 + 2$?

> [!failure] Hiba
> Valami nagyon félrement!

> [!danger] Veszély
> Ennek komoly következményei lehetnek!

> [!bug] Programhiba
> Erre érdemes odafigyelned, mert a rendszer hibájáról van szó.

> [!example] Példa
> Például: a 4 négyzetgyöke 2.

> [!quote] Idézet
> Gondolkodom, tehát vagyok.

> [!success] Siker!
> Sikeresen elolvastad a kiemelt blokkokról szóló fejezetet!

# Lábjegyzetek
Ebben a fejezetben megnézzük, hogyan működnek a lábjegyzetek.

Itt például egy „reference” nevű lábjegyzetet helyezek el.[^reference]

Ez pedig egy másik lábjegyzet.[^yetanother]

[^reference]: Igen, ez valóban a fenti szövegre vonatkozó megjegyzés.
[^yetanother]: A lábjegyzeteket nem kell megszámoznod. Elég, ha mindegyikhez egyedi azonosítót rendelsz.

## Egy másik, teljesen független alfejezet a lábjegyzetekhez
Ez egy alfejezet, amelyen jól látható, hogy a lábjegyzetek meghatározását akár ugyanabban a fejezetben is elhelyezheted: a megjelenített oldalon automatikusan alulra kerülnek. Éppen ezért, ha használsz lábjegyzeteket a jegyzetedben, érdemes a legutolsó fejezetet „Lábjegyzetek”-nek nevezned.
