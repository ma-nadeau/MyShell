#!/usr/bin/bash
testdir="$(pwd)/../../A2-2024/test-cases"
myshdir="../../starter-code"

if [ -n "$1" ]; then
  mkdir tmp
  cd tmp
  ${myshdir}/mysh < "$testdir/$1.txt" > ../output
  diff ../output "$testdir/${1}_result.txt" -qw
  cd ..
  rm -r tmp
else

for testname in $(cat test-names-A2); do
  if [ $testname != T_MT[1-3] ]; then
    mkdir tmp
    cd tmp
    ${myshdir}/mysh < "$testdir/$testname.txt" > output
    diff output "$testdir/${testname}_result.txt" -qw
    cd ..
    rm -r tmp
  fi
done

fi

