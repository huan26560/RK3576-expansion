#!/bin/bash
git add .
git commit -m "自动提交 $(date '+%Y-%m-%d %H:%M')"
git push
