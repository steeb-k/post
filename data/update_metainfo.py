#!/usr/bin/env python3
import sys
import xml.etree.ElementTree as ET
from pathlib import Path
from datetime import date
import re


def indent(elem, level=0):
    i = "\n" + "  " * level
    if len(elem):
        if not elem.text or not elem.text.strip():
            elem.text = i + "  "
        if not elem.tail or not elem.tail.strip():
            elem.tail = i
        for child in elem:
            indent(child, level + 1)
        if not child.tail or not child.tail.strip():
            child.tail = i
    else:
        if level and (not elem.tail or not elem.tail.strip()):
            elem.tail = i


def update_metainfo(metainfo_path: str, changelog_path: str, version: str, release_date: str = None) -> None:
    changelog_content = Path(changelog_path).read_text(encoding='utf-8').strip()
    
    ET.register_namespace('', 'http://www.freedesktop.org/standards/appstream/1.0')
    tree = ET.parse(metainfo_path)
    root = tree.getroot()
    
    ns = {'as': 'http://www.freedesktop.org/standards/appstream/1.0'}
    
    releases_elem = root.find('as:releases', ns)
    if releases_elem is None:
        releases_elem = root.find('releases')
    if releases_elem is None:
        releases_elem = ET.SubElement(root, 'releases')
    
    releases_to_remove = []
    for release in releases_elem.iter('release'):
        if release.get('version') == version:
            releases_to_remove.append(release)
    
    for release in releases_to_remove:
        releases_elem.remove(release)
    
    release = ET.SubElement(releases_elem, 'release')
    release.set('version', version)
    if release_date:
        release.set('date', release_date)
    else:
        release.set('date', date.today().isoformat())
    
    desc = ET.SubElement(release, 'description')
    ul = None
    for line in changelog_content.split('\n'):
        line = line.strip()
        if not line:
            continue
        if line.startswith('- ') or line.startswith('* '):
            if ul is None:
                ul = ET.SubElement(desc, 'ul')
            li = ET.SubElement(ul, 'li')
            li.text = line[2:]
        else:
            ul = None
            p = ET.SubElement(desc, 'p')
            p.text = line
    
    indent(root)
    xml_str = ET.tostring(root, encoding='unicode')
    xml_str = re.sub(r'\n\s*\n', '\n', xml_str)
    Path(metainfo_path).write_text(xml_str, encoding='utf-8')


if __name__ == '__main__':
    if len(sys.argv) < 4:
        print(f"Usage: {sys.argv[0]} <metainfo.xml> <CHANGELOG.md> <version> [date]")
        sys.exit(1)
    
    version = sys.argv[3]
    release_date = sys.argv[4] if len(sys.argv) > 4 else None
    
    update_metainfo(sys.argv[1], sys.argv[2], version, release_date)