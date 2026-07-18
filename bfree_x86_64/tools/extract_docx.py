#!/usr/bin/env python3
import zipfile
import xml.etree.ElementTree as ET
import sys

path = sys.argv[1]
z = zipfile.ZipFile(path)
root = ET.fromstring(z.read("word/document.xml"))
paras = []
for p in root.iter("{http://schemas.openxmlformats.org/wordprocessingml/2006/main}p"):
    texts = []
    for t in p.iter("{http://schemas.openxmlformats.org/wordprocessingml/2006/main}t"):
        if t.text:
            texts.append(t.text)
        if t.tail:
            texts.append(t.tail)
    line = "".join(texts).strip()
    if line:
        paras.append(line)
print("\n".join(paras))
