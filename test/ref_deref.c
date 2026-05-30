int main()
{
	long a, *b, c = 4;
	a = 5;
	b = &a;
	*b = 1;
	c = a;
	*b = 3;
	return a + *b + c;
}
