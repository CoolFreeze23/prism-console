#!/bin/sh
# Isolated test package: no menu exports and no access to the live Prism data.
set -eu
root=/home/deck/Applications
stage=$root/prism-console-test-package
test ! -e "$stage"
cp -a --reflink=auto "$root/prism-console-build" "$stage"
sed -i 's/name=io.github.CoolFreeze23.PrismConsole$/name=io.github.CoolFreeze23.PrismConsole.Test/' "$stage/metadata"
cp "$root/prism-console-source/console-ui/handheld-check.sh" "$stage/files/bin/prism-console-test"
chmod +x "$stage/files/bin/prism-console-test"
flatpak build-finish --no-exports --command=prism-console-test --share=network --share=ipc --socket=x11 --device=dri --filesystem="$root/prism-console-smoke-data" "$stage"
flatpak build-export "$root/prism-console-test-repository" "$stage"
flatpak --user remote-add --if-not-exists --no-gpg-verify prism-console-local-test "$root/prism-console-test-repository"
flatpak --user install -y prism-console-local-test io.github.CoolFreeze23.PrismConsole.Test
