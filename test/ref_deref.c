{
	a = 5;
	b = &a;
	*b = 1;
	*(&b) = 3;
	return a + b;
}
