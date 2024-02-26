mkdir html
for file in *.mail
do
  cat "$file.empty" 2>/dev/null || cat "$file.header" "$file" \
  | tail -n +7 > html/"$file.html"
  NAME=$(head -c -6 <<< $file)
  sed -i "s/:$NAME/:html/" "html/$file.html" 2>/dev/null
done
