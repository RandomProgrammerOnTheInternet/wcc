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

assert 0 "{ ;; return 0; }"
assert 0 "{ return 0; }"
assert 1 '{ return 2 - 1; }'
assert 46 '{ return 12 + 34; }'
assert 108 '{ return 12345678901 - 294317281 + 238724816749 - 1389141; }'
assert 19 '{ return 9 + 10; }'

# proper tokenizer
assert 1 "{ return                     2           -       1; }"
assert 108 "{ return 12345678901                  - 294317281              + 238724816749-1389141; }"

# parser
assert 0 '{ return (2 + 2) - 4; }'
assert 1 '{ return ((((5 + 2) * (1 + 5)) - 3) + 1) / 40; }'

# unary operations
assert 0 '{ return -11 + 11; }'
assert 41 '{ return (((((5 + 2) * (1 + 5)) - 3) + 1) / 40) - -40; }'

# comparisions
assert 1 '{ return 4 > 2; }'
assert 1 '{ return 2 >= 2; }'
assert 0 '{ return 4 < 2; }'
assert 1 '{ return 1 <= 2; }'
assert 0 '{ return 4 == 2; }'
assert 1 '{ return 4 != 2; }'

# program statements
assert 5 '{ 1; 2; 3; 4; return 5; }'

# variables
assert 42 '{ b = 42; return b; }'
assert 5 '{ a = 1; b = a + 1; c = b + 1; d = c + 1; e = d + 1; return e; }'
assert 1 '{ a = 1; b = 2; return b > a; }'

# multiple-char varaibles
assert 1 '{ foo = 2; bar = 2; return foo + bar == 4; }'
assert 2 '{ doo = 4; scooby = doo; thing = 12; result = (scooby * doo) - thing; return result / 2; }'

# (early) return
assert 2 '{ a = 1; b = 2; return b; c = 3; d = 4; }'

# blocks
assert 4 '{ a = 1; { a = 2; { b = 3; } } return (b + a) - 1; }'

# if statements
assert 42 '{ if(42 >= 24) { return 42; } else { return 24; } }'
assert 24 '{ if(42 < 24) { return 42;} else {return 24; } }'

# register allocation test
assert 11 "{ a = 1; b = a + 1; c = b + 1; d = c + 1; e = d + 1; f = e + 1; g = f + 1; h = g + 1; i = h + 1; j = i + 1; return j + 1; }"

# for loop
assert 3 "{ a = 0; for(b = 0; b < 3; b = b + 1) { a = a + 1; } return a; }"

# while loop
assert 42 "{ a = 0; b = 42; while(a != 42) { a = a + 1; b = b - 1; } return a + b; }"

if [ "$passing" = "1" ]; then
  echo "all tests passed"
else
  echo "some tests failed"
fi
