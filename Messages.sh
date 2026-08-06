#!/bin/sh
BASEDIR=$(dirname "$0")
cd "$BASEDIR"

find . -name "*.cpp" -o -name "*.h" | sort | xargs xgettext \
    --from-code=UTF-8 \
    --keyword=i18n:1 \
    --keyword=i18nc:1c,2 \
    --keyword=i18np:1,2 \
    --keyword=i18ncp:1c,2,3 \
    --keyword=ki18n:1 \
    --keyword=ki18nc:1c,2 \
    --keyword=ki18np:1,2 \
    --keyword=ki18ncp:1c,2,3 \
    --package-name="nvidia-driver-manager" \
    --package-version="1.0" \
    --msgid-bugs-address="your@email.com" \
    -o po/nvidia-driver-manager.pot