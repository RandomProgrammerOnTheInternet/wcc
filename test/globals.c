int a;

int reset_a()
{
	int olda = a;
	a = 0;
	return olda;
}

int increment_a()
{
	int olda = a;
	a = a + 1;
	return olda;
}

int decrement_a()
{
	int olda = a;
	a = a - 1;
	return olda;
}

int main()
{
	int dont_care = reset_a();
	int x = increment_a(); // x=0
	int y = increment_a(); // y=1
	int z = decrement_a(); // z=2, a=1
	return (x + y + z + a); // should be 4
}
