#!/bin/bash
set -eu
NVML_PKG_NAME=$(pkg-config --list-all | grep NVML | grep -v '^nvml ' | head -n 1 | cut -d\  -f1)
if [[ -z "$NVML_PKG_NAME" ]]; then
  pkg-config nvml
  if [[ $? == 0 ]]; then
    MSG='No NVML package other than nvml.pc found'
  else
    MSG='No NVML package found'
  fi
  echo "$MSG. Please check your PKG_CONFIG_PATH environment variable."
  exit 1
fi;
FINDPKG_RESULT=$(while read -d: PATHENTRY; do ls "$PATHENTRY/$NVML_PKG_NAME.pc"; done <<< $PKG_CONFIG_PATH 2>/dev/null)
PKG_SELECTED=$(head -n 1 <<< $FINDPKG_RESULT)
echo "Using $PKG_SELECTED"
if [[ -f "nvml.pc" ]]; then
  echo 'nvml.pc already exists. Remove it first';
  exit 1
fi
cp "$PKG_SELECTED" nvml.pc
sed "s&cudaroot=.*&cudaroot=$(dirname $FINDPKG_RESULT)/../../../../&" nvml.pc -i
echo "Remember to append this into PKG_CONFIG_PATH by running"
echo 'export PKG_CONFIG_PATH=$(pwd):$PKG_CONFIG_PATH'
