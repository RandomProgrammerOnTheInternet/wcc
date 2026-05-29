{
	long b0 = 42;
	long q = (b0 == 42);
	long a = 1;
	long b = a + 1;
	long c = b + 1;
	long d = c + 1;
	long e = d + 1;
	long f = (e == 5);
	long a0 = 1;
	long b1 = 2;
	long c1 = b1 > a0;
	long g = (c1 == 1);
	long foo = 2;
	long bar = 2;
	long foobarres = (foo + bar == 4);
	long doo = 4;
	long scooby = doo;
	long thing = 12;
	long result = (scooby * doo) - thing;
	long resultres = (result / 2) == 2;

	return (q + f + g + foobarres + resultres) == 5;
}
