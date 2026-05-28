{
	a = 5;
	b = &a;
	*b = 1;
	c = a;
	*(&b) = 3;
	*(&b + 1) = 2;
	return a + b + c;
}
