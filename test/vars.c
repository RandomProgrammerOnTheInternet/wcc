{
	b0 = 42;
	q = (b0 == 42);
	a = 1;
	b = a + 1;
	c = b + 1;
	d = c + 1;
	e = d + 1;
	f = (e == 5);
	a0 = 1;
	b1 = 2;
	c1 = b1 > a0;
	g = (c1 == 1);
	foo = 2;
	bar = 2;
	foobarres = (foo + bar == 4);
	doo = 4;
	scooby = doo;
	thing = 12;
	result = (scooby * doo) - thing;
	resultres = (result / 2) == 2;

	return (q + f + g + foobarres + resultres) == 5;
}
