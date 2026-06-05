#!/bin/sh

passing=1

cc test/one.c -c -o one.o -O3

assert() {
  
  expect="$1"
  actual=$2

  ./bin/wcc test/$actual -o prog.s -O3
  compilerstatus="$?"

  if [ "$compilerstatus" = "1" ]; then
    echo "compile error $actual"
    passing=0
    return
  fi
  
  cc -o prog prog.s one.o && ./prog
  status="$?"
  rm prog prog.s
  
  if [ "$status" = "$expect" ]; then
    echo "ok $actual => $status"
  else
    echo "not ok $actual => $status"
    passing=0
  fi
}
assert 0 "null_stmt.c"
assert 0 "ret0.c"
assert 1 "sub.c"
assert 46 "add.c"
assert 108 "crazy.c"
assert 108 "whitespace.c"
assert 1 "parse.c"
assert 41 "unary.c"
assert 1 "comparisons.c"
assert 5 "prog_stmts.c"
assert 1 "vars.c"
assert 2 "early_ret.c"
assert 4 "blocks.c"
assert 42 "if.c"
assert 11 "regalloc_test.c"
assert 3 "for.c"
assert 42 "while.c"
assert 123 "do_while.c"
assert 7 "ref_deref.c"
assert 1 "call.c"
assert 156 "call_many.c"
assert 2 "types.c"
assert 3 "funs.c"
assert 251 "triangle.c"
assert 229 "primes.c"
assert 229 "primes_short.c"
assert 1 "arrays.c"
#todo proper alignment support (need to switch to sp-rel addressing)
#assert 6 "align.c"
assert 164 "sizeof_alignof.c"

if [ "$passing" = "1" ]; then
  echo "all tests passed"
else
  echo "some tests failed"
fi
