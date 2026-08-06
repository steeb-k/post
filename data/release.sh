#!/bin/bash

# Create Changelog
python ./generate_changelog.py

# Update metainfo
python ./update_metainfo.py io.github.steeb_k.Post.metainfo.xml.in CHANGELOG.md 0.4.0

mv CHANGELOG.md ..
