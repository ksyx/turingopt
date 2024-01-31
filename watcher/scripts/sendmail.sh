#!/bin/bash
set -eu
MAIL_INTERVAL_AT_LEAST_SECONDS=60
MAIL_INTERVAL_RANDOM_EXTRA_SECONDS=30
for file in *.mail
do
  cat "$file.empty" 2>/dev/null \
    || (mail -t <<< $(cat "$file.header" "$file") \
        && echo "$file $(date)" \
        && sleep $(( "$MAIL_INTERVAL_AT_LEAST_SECONDS"
                    + $RANDOM%"$MAIL_INTERVAL_RANDOM_EXTRA_SECONDS" \
                  )) \
       )
done
