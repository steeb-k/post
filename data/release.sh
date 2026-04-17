#!/bin/bash

# Create Changelog
python ./generate_changelog.py

# Update metainfo
python ./update_metainfo.py org.tabos.stamp.metainfo.xml.in CHANGELOG.md 0.4.0

mv CHANGELOG.md ..
