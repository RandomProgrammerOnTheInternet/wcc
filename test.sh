#!/bin/sh

passing=1

assert() {
  
  expect="$1"
  actual="$2"

  ./bin/wcc "$actual" prog.s
  compilerstatus="$?"

  if [ "$compilerstatus" = "1" ]; then
    echo "compile error $actual"
    passing=0
    return
  fi
  
  cc -o prog prog.s && ./prog
  status="$?"
  rm prog prog.s
  
  if [ "$status" = "$expect" ]; then
    echo "ok $actual => $status"
  else
    echo "not ok $actual => $status"
    passing=0
  fi
}

# basics
assert 0 "0;"
assert 1 '2-1;'
assert 46 '12+34;'
assert 108 '12345678901-294317281+238724816749-1389141;'
assert 19 '9+10;'

# proper tokenizer
assert 1 "                     2           -       1;"
assert 108 "12345678901                  -294317281              +238724816749-1389141;"

# parser
assert 0 '(2+2)-4;'
assert 1 '((((5+2)*(1+5))-3)+1)/40;'

# unary operations
assert 0 '-11+11;'
assert 41 '(((((5+2)*(1+5))-3)+1)/40)--40;'

# comparisions
assert 1 '4>2;'
assert 1 '2>=2;'
assert 0 '4<2;'
assert 1 '1<=2;'
assert 0 '4==2;'
assert 1 '4!=2;'

# program statements
assert 5 '1; 2; 3; 4; 5;'

# variables
assert 42 'b=42; b;'
assert 5 'a=1; b=a+1; c=b+1; d=c+1; e=d+1; e;'
assert 1 'a=1; b=2; b>a;'

if [ "$passing" = "1" ]; then
  echo "all tests passed"
else
  echo "some tests failed"
fi
