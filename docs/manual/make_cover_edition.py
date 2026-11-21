#!/usr/bin/env python3
"""Make zine_cover_src.html, the print-with-a-cover edition, from zine_src.html.

The cover edition is 20 pages: the inside front and inside back covers are blank, so
the outer sheet (front, back and the two blanks) can be printed on card. To keep the
count a multiple of 4 it also gives "Using it without USB" its own page and makes the
"My carts" log two pages, enough for all 48 carts.

  python3 make_cover_edition.py && build_zine.py zine_cover_src.html ...
"""
import re

ROWS = 24
s = open("zine_src.html").read()

def once(old, new):
    global s
    assert s.count(old) == 1, old
    s = s.replace(old, new)

once("</style>", ".blankpg{page:cover;break-before:page;height:1in}\n.log td{height:16pt!important;padding-top:0!important;padding-bottom:0!important}\n</style>")
once('<section class="inside">', '<div class="blankpg"></div>\n\n<section class="inside">')
once('<div class="back" id="back">', '<div class="blankpg"></div>\n\n<div class="back" id="back">')
once('<section id="nousb" class="cont">', '<section id="nousb">')

row = "<tr><td></td><td></td><td></td></tr>"
head = "<tr><th>File name</th><th>Cart</th><th>Notes</th></tr>"
log = re.search(r'<section id="log">.*?</section>', s, re.S).group(0)
pages = "\n\n".join(
    f'<section{" id=\"log\"" if i == 0 else ""}>\n  <h2>My carts{"" if i == 0 else " (continued)"}</h2>\n'
    f'  <table class="log">\n    {head}\n    {row * ROWS}\n  </table>\n</section>' for i in range(2))
once(log, pages)
open("zine_cover_src.html", "w").write(s)
print("wrote zine_cover_src.html")
